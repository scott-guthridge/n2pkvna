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
 * MERCHANTABILITY or FITNESS FOR A11 PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * vector_code_t: names of the vectors the VNA measures
 *
 * Note: don't change the values of this enum without also fixing at
 * all the places in the c file that depend on this order.
 */
typedef enum vector_code {
    VC_NONE		= -1,
    VC_A11		=  0,
    VC_B11		=  1,
    VC_V11		=  2,
    VC_I11		=  3,
    VC_A12		=  4,
    VC_B12		=  5,
    VC_V12		=  6,
    VC_I12		=  7,
    VC_A21		=  8,
    VC_B21		=  9,
    VC_V21		= 10,
    VC_I21		= 11,
    VC_A22		= 12,
    VC_B22		= 13,
    VC_V22		= 14,
    VC_I22		= 15,
} vector_code_t;

/*
 * VC_MASK: vector_code_t to mask bit
 * VC2CELL: vector_code_t to matrix cell
 * VC_SMASH: map X22 -> X11 and X12 -> X21
 */
#define VC_MASK(vc)	((vc) < 0 ? 0U : 1U << (vc))
#define VC_ROW(vc)	(((vc) & 0x8) >> 3)
#define VC_COLUMN(vc)	(((vc) & 0x4) >> 2)
#define VC_TO_11(vc, row, column) \
	((vector_code_t)((vc) - 4 * (2 * (row) + (column))))

/*
 * measurement_mask_t: type for bitmap of M2MASK values
 */
typedef uint16_t measurement_mask_t;

/*
 * measurement_t: what the VNA measures with given switch & manual step codes
 */
typedef struct measurement {
    int			m_switch;		/* key: 0-3 or -1 */
    vector_code_t	m_detectors[2];		/* vectors detectors measure */
    measurement_mask_t	m_mask;			/* vectors bitmask */
    bool		m_used;			/* no longer avialable */
    struct mstep       *m_mstep;		/* parent pointer */
    struct measurement *m_next;			/* next measurement */
} measurement_t;

/*
 * mstep_t: a manual step such preparing VNA for reflection vs. transmission
 */
typedef struct mstep {
    char	       *ms_name;		/* step name or NULL */
    char	       *ms_text;		/* instructions or NULL */
    measurement_t      *ms_measurements;	/* list of measurements */
    measurement_mask_t	ms_mask;		/* vectors bitmask */
    struct setup       *ms_setup;		/* parent pointer */
    struct mstep       *ms_next;		/* next step */
} mstep_t;

/*
 * setup_t: VNA configuration such as reflection bridge, full-S or RF-IV
 */
typedef struct setup {
    const char	       *su_name;		/* setup name */
    int			su_rows;		/* number of measured rows */
    int			su_columns;		/* number of measured cols */
    bool		su_enabled;		/* setup is enabled */
    double		su_fmin;		/* minimum VNA frequency */
    double		su_fmax;		/* maximum VNA frequency */
    double		su_fosc;		/* transferter osc. or 0.0 */
    mstep_t	       *su_steps;		/* list of steps */
    measurement_mask_t	su_mask;		/* vectors bitmask */
    struct setup       *su_next;		/* next setup */
} setup_t;

/*
 * measurement_args_t: options for make_measurements
 */
typedef struct measurement_args {
    setup_t	       *ma_setup;		/* VNA setup */
    double		ma_fmin;		/* starting frequency */
    double		ma_fmax;		/* ending frequency */
    int			ma_frequencies;		/* number of frequencies */
    int			ma_rows;		/* measure this many rows */
    int			ma_columns;		/* measure this many columns */
    bool		ma_linear;		/* true for linear f spacing */
    bool		ma_colsys;		/* true for column systems */
    double complex      ma_z0;			/* reference impedance */
} measurement_args_t;

/*
 * measurement_result_t: result of measurement
 */
typedef struct measurement_result {
    int                 mr_a_rows;		/* rows in a_matrix */
    int			mr_a_columns;		/* columns in a_matrix */
    int			mr_b_rows;		/* rows in b_matrix */
    int			mr_b_columns;		/* columns in b_matrix */
    double	       *mr_frequency_vector;	/* resulting frequency vector */
    double complex    **mr_a_matrix;		/* resulting A matrix */
    double complex    **mr_b_matrix;		/* resulting B matrix */
} measurement_result_t;

extern const char *vector_code_to_name(vector_code_t code);
extern vector_code_t vector_name_to_code(const char *name);

extern setup_t *setup_alloc(const char *name, int rows, int columns);
extern void setup_free(setup_t *sup);
extern mstep_t *setup_add_mstep(setup_t *sup,
	const char *name, const char *text);
extern measurement_t *mstep_add_measurement(mstep_t *msp,
	int switch_value, vector_code_t detector1, vector_code_t detector2);

extern setup_t *setup_lookup(const char *name);
extern void setup_update(setup_t *sup_new);
extern int setup_delete(const char *name);

extern int make_measurements(const measurement_args_t *map,
	measurement_result_t *mrp);
extern void measurement_result_free(measurement_result_t *mrp);

extern setup_t default_RB_setup;

#endif /* MEASUREMENT_H */
