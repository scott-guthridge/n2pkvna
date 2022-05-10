/*
 * N2PK Vector Network Analyzer
 * Copyright © 2021-2022 D Scott Guthridge <scott_guthridge@rompromity.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "archdep.h"

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <n2pkvna.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vnacal.h>
#include <vnadata.h>

#include "main.h"
#include "measure.h"
#include "message.h"

//TODO: Make the vnacommon_* functions officially public so we don't have
//      to do use this clandestine method.
extern double complex _vnacommon_mldivide(complex double *x, complex double *a,
        const double complex *b, int m, int n);
extern int _vnacommon_qrsolve(complex double *x, complex double *a,
        complex double *b, int m, int n, int o);

/*
 * measurement_vector_t: a measured vector
 */
typedef struct measurement_vector {
    vector_code_t		mv_code;
    double complex	       *mv_vector;
    struct measurement_vector  *mv_next;
} measurement_vector_t;

/*
 * measurement_cell_t: collection of measurement vectors for a matrix cell
 */
typedef struct measurement_cell {
    measurement_mask_t		mc_mask;
    int				mc_count;
    measurement_vector_t       *mc_vectors;
} measurement_cell_t;

/*
 * measurement_matrix_t: collection of all measurement vectors
 */
typedef struct measurement_matrix {
    measurement_cell_t	       *mm_matrix;
    const measurement_args_t   *mm_map;
} measurement_matrix_t;

/*
 * vector_names: table of measurement names in enum order
 */
static const char vector_names[][4] = {
    "a11",
    "b11",
    "v11",
    "i11",
    "a12",
    "b12",
    "v12",
    "i12",
    "a21",
    "b21",
    "v21",
    "i21",
    "a22",
    "b22",
    "v22",
    "i22",
};

/*
 * DEFAULT_RB_MASK: measurement mask for default reflection bridge
 */
#define DEFAULT_RB_MASK		(VC_MASK(VC_B11) | VC_MASK(VC_B21))

/*
 * Forward declarations
 */
extern mstep_t default_RB_mstep;
extern setup_t default_RB_setup;

/*
 * default_RB_measurement: default setup if none in calibration file
 */
measurement_t default_RB_measurement = {
    .m_switch = -1,
    .m_detectors = { VC_B11, VC_B21 },
    .m_mask = DEFAULT_RB_MASK,
    .m_used = false,
    .m_mstep = &default_RB_mstep,
    .m_next = NULL
};

/*
 * default_RB_mstep: default step if none in calibration file
 */
mstep_t default_RB_mstep = {
    .ms_name = NULL,
    .ms_text = NULL,
    .ms_measurements = &default_RB_measurement,
    .ms_mask = DEFAULT_RB_MASK,
    .ms_setup = &default_RB_setup,
    .ms_next = NULL,
};

/*
 * default_RB_setup: default setup if none in calibration file
 */
setup_t default_RB_setup = {
    .su_name = "RB",
    .su_rows = 2,
    .su_columns = 1,
    .su_enabled = true,
    .su_fmin = 50.0e+3,
    .su_fmax = 60.0e+3,
    .su_fosc = 0.0,
    .su_steps = &default_RB_mstep,
    .su_mask = DEFAULT_RB_MASK,
    .su_next = NULL
};

/*
 * vector_code_to_name: convert measurement enum to name
 *   @code: code to convert
 */
const char *vector_code_to_name(vector_code_t code)
{
    if (code == VC_NONE) {
	return "~";
    }
    if ((int)code >= 0 && (int)code < 16) {
	return vector_names[code];
    }
    abort();
}

/*
 * vector_name_to_code: convert measurement name to enum value
 *   @name: measurement name, e.g. a11, b21, v22, i12
 */
vector_code_t vector_name_to_code(const char *name)
{
    int code = 0;

    switch (name[0]) {
    case 'a':
	break;

    case 'b':
	code |= 1;
	break;

    case 'i':
	code |= 3;
	break;

    case 'v':
	code |= 2;
	break;

    default:
	return VC_NONE;
    }
    switch (name[1]) {
    case '1':
	break;

    case '2':
	code |= 8;
	break;

    default:
	return VC_NONE;
    }
    switch (name[2]) {
    case '1':
	break;

    case '2':
	code |= 4;
	break;

    default:
	return VC_NONE;
    }
    if (name[3] != '\000') {
	return VC_NONE;
    }
    return (vector_code_t)code;
}

/*
 * measurement_matrix_init: init the measurmenet_matrix_t structure
 *   @mmp: measurement matrix structure
 *   @map: measurement argument structure
 */
static int measurement_matrix_init(measurement_matrix_t *mmp,
	const measurement_args_t *map)
{
    (void)memset((void *)mmp, 0, sizeof(*mmp));
    if ((mmp->mm_matrix = calloc(map->ma_rows * map->ma_columns,
		    sizeof(measurement_matrix_t))) == NULL) {
	(void)fprintf(stderr, "%s: calloc: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    mmp->mm_map = map;
    return 0;
}

/*
 * measurement_matrix_add: add new vectors to the measurement matrix
 *   @mmp:     measurement matrix structure
 *   @mp:      measurement structure
 *   @vectors: vector of two measurement vectors
 */
static int measurement_matrix_add(measurement_matrix_t *mmp,
	measurement_t *mp, double complex **vectors)
{
    const measurement_args_t *map = mmp->mm_map;

    for (int i = 0; i < 2; ++i) {
	vector_code_t code = mp->m_detectors[i];
	int m_row, m_column;
	measurement_cell_t *mcp;
	measurement_mask_t mask;
	measurement_vector_t *mvp;

	if (mp->m_detectors[i] == VC_NONE) {
	    continue;
	}
	if ((m_row = VC_ROW(code)) >= map->ma_rows) {
	    continue;
	}
	if ((m_column = VC_COLUMN(code)) >= map->ma_columns) {
	    continue;
	}
	mask = VC_MASK(code);
	mcp = &mmp->mm_matrix[map->ma_columns * m_row + m_column];
	if ((mvp = malloc(sizeof(measurement_vector_t))) == NULL) {
	    (void)fprintf(stderr, "%s: malloc: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	(void)memset((void *)mvp, 0, sizeof(*mvp));
	mvp->mv_code = code;
	mvp->mv_vector = vectors[i];
	vectors[i] = NULL;

	/*
	 * Push the new element onto mc_vectors.
	 */
	mcp->mc_mask |= mask;
	mvp->mv_next = mcp->mc_vectors;
	mcp->mc_vectors = mvp;
	++mcp->mc_count;
    }
    return 0;
}

/*
 * measurement_matrix_solve: create the measurement result matrix
 *   @mmp: measurement matrix structure
 *   @mrp: measurement result structure
 */
static int measurement_matrix_solve(measurement_matrix_t *mmp,
	measurement_result_t *mrp)
{
    const measurement_args_t *map = mmp->mm_map;
    const int rows    = map->ma_rows;
    const int columns = map->ma_columns;
    const int a_rows  = map->ma_colsys ? 1 : columns;
    bool need_a_matrix = false;

    /*
     * Determine if we need the A matrix.  We need it if any cell
     * contains two different measurement types, e.g. a & b, i & v,
     * b & v, etc.
     */
    for (int cell = 0; cell < rows * columns; ++cell) {
	measurement_cell_t *mcp = &mmp->mm_matrix[cell];
	measurement_mask_t mask = mcp->mc_mask;

	if ((mask & (mask - 1)) != 0) {
	    need_a_matrix = true;
	    break;
	}
    }

    /*
     * Allocate the result matrices.
     */
    if (need_a_matrix) {
	if ((mrp->mr_a_matrix = calloc(a_rows * columns,
			sizeof(double complex *))) == NULL) {
	    (void)fprintf(stderr, "%s: calloc: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	mrp->mr_a_rows    = a_rows;
	mrp->mr_a_columns = columns;
    }
    if ((mrp->mr_b_matrix = calloc(rows * columns,
			sizeof(double complex *))) == NULL) {
	(void)fprintf(stderr, "%s: calloc: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    mrp->mr_b_rows    = rows;
    mrp->mr_b_columns = columns;

    /*
     * Main loop
     */
    for (int row = 0; row < rows; ++row) {
	for (int column = 0; column < columns; ++column) {
	    int cell = columns * row + column;
	    measurement_cell_t *mcp = &mmp->mm_matrix[cell];
	    if ((mcp->mc_mask & 0xCCCC) == 0) {
		double complex *a_vector = NULL, *b_vector = NULL;
		int a_count = 0, b_count = 0;
		measurement_vector_t *mvp;

		while ((mvp = mcp->mc_vectors) != NULL) {
		    mcp->mc_vectors = mvp->mv_next;
		    switch (VC_TO_11(mvp->mv_code, row, column)) {
		    case VC_A11:
			if (a_vector == NULL) {
			    a_vector = mvp->mv_vector;
			} else {
			    for (int k = 0; k < map->ma_frequencies; ++k) {
				a_vector[k] += mvp->mv_vector[k];
			    }
			    free((void *)mvp->mv_vector);
			}
			mvp->mv_vector = NULL;
			++a_count;
			continue;

		    case VC_B11:
			if (b_vector == NULL) {
			    b_vector = mvp->mv_vector;
			} else {
			    for (int k = 0; k < map->ma_frequencies; ++k) {
				b_vector[k] += mvp->mv_vector[k];
			    }
			    free((void *)mvp->mv_vector);
			}
			mvp->mv_vector = NULL;
			++b_count;
			continue;

		    default:
			abort();
		    }
		}
		if (a_count > 1) {
		    for (int k = 0; k < map->ma_frequencies; ++k) {
			a_vector[k] /= a_count;
		    }
		} else if (a_vector == NULL && need_a_matrix &&
			(row == column || !map->ma_colsys)) {
		    if ((a_vector = malloc(map->ma_frequencies *
				    sizeof(double complex))) == NULL) {
			(void)fprintf(stderr, "%s: malloc: %s\n",
				progname, strerror(errno));
			exit(N2PKVNA_EXIT_SYSTEM);
		    }
		    for (int k = 0; k < map->ma_frequencies; ++k) {
			a_vector[k] = row == column ? 1.0 : 0.0;
		    }
		}
		assert(b_count > 0);
		if (b_count > 1) {
		    for (int k = 0; k < map->ma_frequencies; ++k) {
			b_vector[k] /= b_count;
		    }
		}
		if (a_vector != NULL) {
		    if (map->ma_colsys) {
			if (row != column) {
			    free((void *)a_vector);
			    a_vector = NULL;
			} else {
			    mrp->mr_a_matrix[column] = a_vector;
			}
		    } else {
			mrp->mr_a_matrix[columns * row + column] = a_vector;
		    }
		}
		mrp->mr_b_matrix[columns * row + column] = b_vector;

	    } else if ((mcp->mc_mask & (mcp->mc_mask - 1)) == 0) {
		double complex a[mcp->mc_count];
		double complex b[mcp->mc_count];
		double complex *b_vector = NULL;
		double K = 1.0 / sqrt(cabs(creal(map->ma_z0)));
		double complex KV = map->ma_z0 / (K * creal(map->ma_z0));
		double complex KI =       -1.0 / (K * creal(map->ma_z0));

		if ((b_vector = malloc(map->ma_frequencies *
				sizeof(double complex))) == NULL) {
		    (void)fprintf(stderr, "%s: malloc: %s\n",
			    progname, strerror(errno));
		    exit(N2PKVNA_EXIT_SYSTEM);
		}
		for (int findex = 0; findex < map->ma_frequencies; ++findex) {
		    int row = 0;

		    for (measurement_vector_t *mvp = mcp->mc_vectors;
			    mvp != NULL; mvp = mvp->mv_next, ++row) {

			switch (VC_TO_11(mvp->mv_code, row, column)) {
			case VC_B11:
			    a[row] = 1.0;
			    break;
			case VC_V11:
			    a[row] = KV;
			    break;
			case VC_I11:
			    a[row] = KI;
			    break;
			default:
			    abort();
			}
			b[row] = mvp->mv_vector[findex];
		    }
		    assert(row >= 1);
		    if (row == 1) {
			b_vector[findex] = b[0] / a[0];
		    } else {
			int rank = _vnacommon_qrsolve(&b_vector[findex],
				a, b, row, 1, 1);
			if (rank == 0) {
			    (void)fprintf(stderr, "%s: singular matrix\n",
				    progname);
			    free((void *)b_vector);
			    return -1;
			}
		    }
		}
		mrp->mr_b_matrix[columns * row + column] = b_vector;

	    } else {
		double complex a[mcp->mc_count][2];
		double complex b[mcp->mc_count];
		double complex *a_vector = NULL;
		double complex *b_vector = NULL;
		double K = 1.0 / sqrt(cabs(creal(map->ma_z0)));
		double complex KV1 = conj(map->ma_z0) / (K * creal(map->ma_z0));
		double complex KV2 =      map->ma_z0  / (K * creal(map->ma_z0));
		double complex KI1 =  1.0             / (K * creal(map->ma_z0));
		double complex KI2 = -1.0             / (K * creal(map->ma_z0));

		if ((b_vector = malloc(map->ma_frequencies *
				sizeof(double complex))) == NULL) {
		    (void)fprintf(stderr, "%s: malloc: %s\n",
			    progname, strerror(errno));
		    exit(N2PKVNA_EXIT_SYSTEM);
		}
		for (int findex = 0; findex < map->ma_frequencies; ++findex) {
		    int row = 0;
		    double complex x[2];

		    for (measurement_vector_t *mvp = mcp->mc_vectors;
			    mvp != NULL; mvp = mvp->mv_next, ++row) {

			switch (VC_TO_11(mvp->mv_code, row, column)) {
			case VC_A11:
			    a[row][0] = 1.0;
			    a[row][1] = 0.0;
			    break;
			case VC_B11:
			    a[row][0] = 0.0;
			    a[row][1] = 1.0;
			    break;
			case VC_V11:
			    a[row][0] = KV1;
			    a[row][1] = KV2;
			    break;
			case VC_I11:
			    a[row][0] = KI1;
			    a[row][1] = KI2;
			    break;
			default:
			    abort();
			}
			b[row] = mvp->mv_vector[findex];
		    }
		    assert(row >= 2);
		    if (row == 2) {
			double complex d;

			d = _vnacommon_mldivide(x, &a[0][0], b, 2, 1);
			if (d == 0.0) {
			    (void)fprintf(stderr, "%s: singular matrix\n",
				    progname);
			    free((void *)a_vector);
			    free((void *)b_vector);
			    return -1;
			}

		    } else {
			int rank;

			rank = _vnacommon_qrsolve(x, &a[0][0], b, row, 2, 1);
			if (rank == 0) {
			    (void)fprintf(stderr, "%s: singular matrix\n",
				    progname);
			    free((void *)a_vector);
			    free((void *)b_vector);
			    return -1;
			}
		    }
		    a_vector[findex] = x[0];
		    b_vector[findex] = x[1];
		}
		if (map->ma_colsys) {
		    if (row != column) {
			free((void *)a_vector);
			a_vector = NULL;
		    } else {
			mrp->mr_a_matrix[column] = a_vector;
		    }
		} else {
		    mrp->mr_a_matrix[columns * row + column] = a_vector;
		}
		mrp->mr_b_matrix[2 * row + column] = b_vector;
	    }
	}
    }
    return 0;
}

/*
 * measurement_matrix_free: free measurement matrix resources
 *   @mmp: measurement matrix structure
 */
static void measurement_matrix_free(measurement_matrix_t *mmp)
{
    const measurement_args_t *map = mmp->mm_map;
    int cells = map->ma_rows * map->ma_columns;

    for (int cell = 0; cell < cells; ++cell) {
	measurement_cell_t *mcp = &mmp->mm_matrix[cell];
	while (mcp->mc_vectors != NULL) {
	    measurement_vector_t *mvp = mcp->mc_vectors;

	    mcp->mc_vectors = mvp->mv_next;
	    free((void *)mvp->mv_vector);
	    free((void *)mvp);
	}
    }
}

/*
 * find_best_measurement: choose the next measurement
 *   @setup: VNA setup in-use
 *   @remaining_mask: mask of measurements remaining
 */
static measurement_t *find_best_measurement(setup_t *setup,
	const measurement_mask_t remaining_mask)
{
    measurement_t *best_measurement = NULL;
    int best_cost = INT_MAX;

    if (remaining_mask == 0) {
	return NULL;
    }
    assert((remaining_mask & ~setup->su_mask) == 0);
    for (mstep_t *msp = setup->su_steps; msp != NULL; msp = msp->ms_next) {
	for (measurement_t *mp = msp->ms_measurements; mp != NULL;
		mp = mp->m_next) {
	    int cost = 0;

	    if (mp->m_used) {
		continue;
	    }
	    if (!(mp->m_mask & remaining_mask)) {
		mp->m_used = true;
		continue;
	    }
	    if (mp->m_mstep->ms_name != NULL && gs.gs_mstep != NULL &&
		    mp->m_mstep != gs.gs_mstep) {
		cost += 2;
	    }
	    if (mp->m_switch >= 0 && gs.gs_switch >= 0 &&
		    mp->m_switch != gs.gs_switch) {
		cost += 1;
	    }
	    if (cost < best_cost) {
		best_cost = cost;
		best_measurement = mp;
	    }
	}
    }
    assert(best_measurement != NULL);
    return best_measurement;
}

/*
 * make_measurements: set switches, prompt and make measurements
 *   @map: measurement options
 *   @mrp: measurement result
 */
int make_measurements(const measurement_args_t *map, measurement_result_t *mrp)
{
    setup_t *setup = map->ma_setup;
    measurement_mask_t remaining_mask;
    double *frequency_vector = NULL;
    double complex *vectors[2] = { NULL, NULL };
    bool measuring = false;
    measurement_matrix_t mm;
    int rc = -1;

    /*
     * Clear result structure.
     */
    (void)memset((void *)mrp, 0, sizeof(*mrp));

    /*
     * Validate dimensions.
     */
    if (map->ma_rows > setup->su_rows ||
	    map->ma_columns > setup->su_columns) {
	message_error("measure requires dimensions %dx%d but "
		"setup dimensions are %dx%d",
		map->ma_rows, map->ma_columns,
		setup->su_rows, setup->su_columns);
	return -1;
    }

    /*
     * Initialize the mask of needed measurements.
     */
    remaining_mask = setup->su_mask &
	((map->ma_rows == 1 && map->ma_columns == 1) ? 0x000F :
	 (map->ma_rows == 1 && map->ma_columns == 2) ? 0x00FF :
	 (map->ma_rows == 2 && map->ma_columns == 1) ? 0x0F0F :
       /*(map->ma_rows == 2 && map->ma_columns == 2)*/ 0xFFFF);

    /*
     * Init the measurement matrix.
     */
    if (measurement_matrix_init(&mm, map) == -1) {
	goto out;
    }

    /*
     * Allocate the frequency vector.
     */
    if ((frequency_vector = calloc(map->ma_frequencies,
		    sizeof(double))) == NULL) {
	(void)fprintf(stderr, "%s: calloc: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    mrp->mr_frequency_vector = frequency_vector;

    /*
     * Main loop
     */
    while (remaining_mask != 0) {
	measurement_t *mp;
	mstep_t *msp;

	mp = find_best_measurement(setup, remaining_mask);
	if (mp == NULL) {
	    break;
	}
	msp = mp->m_mstep;

	/*
	 * If starting a new manual step, instruct the user
	 * to prepare the VNA for the given step.
	 */
	if (msp->ms_name != NULL && msp != gs.gs_mstep) {
	    if (measuring) {
		if (!gs.gs_opt_Y) {
		    (void)printf("done\n\n");
		}
		measuring = false;
	    }
	    if (msp->ms_text != NULL) {
		message_add_instruction(msp->ms_text);
	    } else {
		message_add_instruction("Prepare VNA for %s measurement.\n",
			msp->ms_name);
	    }
	    gs.gs_mstep = msp;
	}

	/*
	 * Control the VNA switches as needed.
	 */
	if (mp->m_switch != -1 && mp->m_switch != gs.gs_switch) {
	    if (n2pkvna_switch(gs.gs_vnap, mp->m_switch, -1,
			SWITCH_DELAY) == -1) {
		goto out;
	    }
	    gs.gs_switch = mp->m_switch;
	}

	/*
	 * If needed, prompt the user for confirmation.
	 */
	if (gs.gs_need_ack) {
	    if ((rc = message_wait_for_acknowledgement()) == -1) {
		goto out;
	    }
	}

	/*
	 * Allocate measurement vectors, run the VNA scan, and add
	 * the vectors to the mcells structure.
	 */
	for (int i = 0; i < 2; ++i) {
	    if (mp->m_detectors[i] != VC_NONE) {
		if ((vectors[i] = calloc(map->ma_frequencies,
				sizeof(double complex))) == NULL) {
		    (void)fprintf(stderr, "%s: calloc: %s\n",
			    progname, strerror(errno));
		    exit(N2PKVNA_EXIT_SYSTEM);
		}
	    }
	}
	if (!measuring) {
	    if (!gs.gs_opt_Y) {
		(void)printf("Measuring...\n");
	    }
	    measuring = true;
	}
	if (n2pkvna_scan(gs.gs_vnap, map->ma_fmin, map->ma_fmax,
		    map->ma_frequencies, map->ma_linear, frequency_vector,
		    vectors[0], vectors[1]) == -1) {
	    gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	    goto out;
	}
	if (measurement_matrix_add(&mm, mp, vectors) == -1) {
	    goto out;
	}
	frequency_vector = NULL;	/* pass only first time */

	/*
	 * Mark this measurement as used and remove the vectors
	 * we just measured from the remaining mask.
	 */
	mp->m_used = true;
	remaining_mask &= ~mp->m_mask;
    }
    if (measuring) {
	if (!gs.gs_opt_Y) {
	    (void)printf("done\n\n");
	}
	measuring = false;
    }
    if (measurement_matrix_solve(&mm, mrp) == -1) {
	goto out;
    }
    rc = 0;

out:
    measurement_matrix_free(&mm);
    for (mstep_t *msp = setup->su_steps; msp != NULL; msp = msp->ms_next) {
	for (measurement_t *mp = msp->ms_measurements; mp != NULL;
		mp = mp->m_next) {
	    for (int i = 0; i < 2; ++i) {
		mp->m_used = false;
	    }
	}
    }
    if (rc != 0) {
	measurement_result_free(mrp);
    }
    return rc;
}

/*
 * measurement_result_free: free resources of the measurement result structure
 */
void measurement_result_free(measurement_result_t *mrp)
{
    free((void *)mrp->mr_frequency_vector);
    mrp->mr_frequency_vector = NULL;
    if (mrp->mr_a_matrix != NULL) {
	for (int cell = 0; cell < mrp->mr_a_rows * mrp->mr_a_columns; ++cell) {
	    free((void *)mrp->mr_a_matrix[cell]);
	}
	free((void *)mrp->mr_a_matrix);
	mrp->mr_a_matrix = NULL;
	mrp->mr_a_rows = 0;
	mrp->mr_a_columns = 0;
    }
    if (mrp->mr_b_matrix != NULL) {
	for (int cell = 0; cell < mrp->mr_b_rows * mrp->mr_b_columns; ++cell) {
	    free((void *)mrp->mr_b_matrix[cell]);
	}
	free((void *)mrp->mr_b_matrix);
	mrp->mr_b_matrix = NULL;
	mrp->mr_b_rows = 0;
	mrp->mr_b_columns = 0;
    }
}

/*
 * measurement_free: free a measurement structure
 *   @m: measurement_t structure to free
 */
static void measurement_free(measurement_t *mp)
{
    free((void *)mp);
}

/*
 * mstep_free: free an mstep_t structure and its descendants
 *   @msp: mstep_t sturcture to free
 */
static void mstep_free(mstep_t *msp)
{
    if (msp != NULL) {
	while (msp->ms_measurements != NULL) {
	    measurement_t *mp = msp->ms_measurements;

	    msp->ms_measurements = mp->m_next;
	    measurement_free(mp);
	}
	free((void *)msp->ms_name);
	free((void *)msp->ms_text);
	free((void *)msp);
    }
}

/*
 * mstep_add_measurement: add a new measurement to an mstep_t structure
 *   @msp: mstep_t structure
 *   @switch_value: switch setting for this measurement
 *   @detector1: what detector 1 measures
 *   @detector2: what detector 2 measures
 */
measurement_t *mstep_add_measurement(mstep_t *msp, int switch_value,
	vector_code_t detector1, vector_code_t detector2)
{
    measurement_t *mp;
    measurement_t **mpp = &msp->ms_measurements;

    assert(switch_value >= -1 && switch_value <= 3);
    if ((mp = malloc(sizeof(measurement_t))) == NULL) {
	(void)fprintf(stderr, "%s: malloc: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    (void)memset((void *)mp, 0, sizeof(*mp));
    mp->m_switch = switch_value;
    mp->m_detectors[0] = detector1;
    mp->m_detectors[1] = detector2;
    if (detector1 != VC_NONE) {
	mp->m_mask |= VC_MASK(detector1);
    }
    if (detector2 != VC_NONE) {
	mp->m_mask |= VC_MASK(detector2);
    }
    mp->m_mstep = msp;
    while (*mpp != NULL) {
	mpp = &(*mpp)->m_next;
    }
    *mpp = mp;
    msp->ms_mask |= mp->m_mask;
    msp->ms_setup->su_mask |= mp->m_mask;

    return mp;
}

/*
 * setup_alloc: allocate a new setup_t structure
 *   @name: name of setup
 *   @rows: rows measured matrix
 *   @columns: columns in measured matrix
 */
setup_t *setup_alloc(const char *name, int rows, int columns)
{
    setup_t *sup;

    if ((sup = malloc(sizeof(setup_t))) == NULL) {
	(void)fprintf(stderr, "%s: malloc: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    (void)memset((void *)sup, 0, sizeof(*sup));
    if ((sup->su_name = strdup(name)) == NULL) {
	(void)fprintf(stderr, "%s: strdup: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    sup->su_rows = rows;
    sup->su_columns = columns;

    return sup;
}

/*
 * setup_add_mstep: add a new mstep_t structure to a setup_t structure
 *   @sup: setup structure
 *   @name: optional name of manual step
 *   @text: optional text describing manual step
 */
mstep_t *setup_add_mstep(setup_t *sup, const char *name, const char *text)
{
    mstep_t **mspp = &sup->su_steps;
    mstep_t *msp;

    if ((msp = malloc(sizeof(mstep_t))) == NULL) {
	(void)fprintf(stderr, "%s: malloc: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    (void)memset((void *)msp, 0, sizeof(*msp));
    if (name != NULL) {
	if ((msp->ms_name = strdup(name)) == NULL) {
	    (void)fprintf(stderr, "%s: strdup: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    if (text != NULL) {
	if ((msp->ms_text = strdup(text)) == NULL) {
	    (void)fprintf(stderr, "%s: strdup: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    msp->ms_setup = sup;
    while (*mspp != NULL) {
	mspp = &(*mspp)->ms_next;
    }
    *mspp = msp;

    return msp;
}

/*
 * setup_free: free a setup_t structure and its descendants
 *   @sup: setup_t structure to free
 */
void setup_free(setup_t *sup)
{
    if (sup != NULL) {
	while (sup->su_steps != NULL) {
	    mstep_t *msp = sup->su_steps;

	    sup->su_steps = msp->ms_next;
	    mstep_free(msp);
	}
	free((void *)sup->su_name);
	free((void *)sup);
    }
}

/*
 * setup_lookup: search for a setup_t structure by name
 *   @name: name of setup
 */
setup_t *setup_lookup(const char *name)
{
    for (setup_t *sup = gs.gs_setups; sup != NULL; sup = sup->su_next) {
	int cmp;

	if ((cmp = strcmp(sup->su_name, name) == 0)) {
	    return sup;
	}
	if (cmp > 0) {
	    break;
	}
    }
    return NULL;
}

/*
 * setup_update: add a new setup, replacing if exists
 *   @sup_new: updated setup to install
 */
void setup_update(setup_t *sup_new)
{
    const char *name = sup_new->su_name;
    setup_t *sup, **supp;

    for (supp = &gs.gs_setups; (sup = *supp) != NULL; supp = &sup->su_next) {
	int cmp;

	if ((cmp = strcmp(sup->su_name, name) == 0)) {
	    *supp = sup->su_next;
	    setup_free(sup);
	    break;
	}
	if (cmp > 0) {
	    break;
	}
    }
    sup_new->su_next = *supp;
    *supp = sup_new;
}

/*
 * setup_delete: delete the named setup
 *   @name: name of setup to delete
 */
int setup_delete(const char *name)
{
    setup_t *sup, **supp;

    for (supp = &gs.gs_setups; (sup = *supp) != NULL; supp = &sup->su_next) {
	int cmp;

	if ((cmp = strcmp(sup->su_name, name) == 0)) {
	    *supp = sup->su_next;
	    setup_free(sup);
	    return 0;
	}
	if (cmp > 0) {
	    break;
	}
    }
    errno = ENOENT;
    return -1;
}
