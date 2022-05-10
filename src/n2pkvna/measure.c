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
#include "measurement.h"
#include "message.h"
#include "properties.h"

/*
 * n2pkvna measure options
 */
static const char short_options[] = "f:hlLn:o:p:Pxy";
static const struct option long_options[] = {
    { "frequency-range",	1, NULL, 'f' },
    { "help",			0, NULL, 'h' },
    { "linear",			0, NULL, 'l' },
    { "log",                    0, NULL, 'L' },
    { "nfrequencies",		1, NULL, 'n' },
    { "output",			1, NULL, 'o' },
    { "parameters",		1, NULL, 'p' },
    { "prompt",			0, NULL, 'P' },
    { "hexfloat",		0, NULL, 'x' },
    { "symmetric",		0, NULL, 'y' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    "[-lLPxy] [-f fMin:fMax] [-n nfrequencies]\n"
    "    [-o output-file] [-p parameters] calibration",
    NULL
};
static const char *const help[] = {
    " -l|--linear                       force linear frequency spacing",
    " -L|--log                          force logarithmic frequency spacing",
    " -f|--frequency-range=fMin:fMax    override calibration range (MHz)",
    " -h|--help                         show this help message",
    " -n|--nfrequencies=n               override the frequency count",
    " -o|--output=file			example \"filter.s2p\"",
    " -p|--parameters=parameter-format  default Sri",
    " -P|--prompt                       always prompt before measuring",
    " -x|--hexfloat                     use hexadecimal floating point",
    " -y|--symmetric                    DUT is symmetric",
    " calibration                       which calibration to use",
    "",
    "  where parameter-format is a comma-separated list of:",
    "    s[ri|ma|dB]  scattering parameters",
    "    t[ri|ma|dB]  scattering-transfer parameters",
    "    z[ri|ma]     impedance parameters",
    "    y[ri|ma]     admittance parameters",
    "    h[ri|ma]     hybrid parameters",
    "    g[ri|ma]     inverse-hybrid parameters",
    "    a[ri|ma]     ABCD parameters",
    "    b[ri|ma]     inverse ABCD parameters",
    "    Zin[ri|ma]   input impedances",
    "    PRC          Zin as parallel RC",
    "    PRL          Zin as parallel RL",
    "    SRC          Zin as series RC",
    "    SRL          Zin as series RL",
    "    IL           insertion loss",
    "    RL           return loss",
    "    VSWR         voltage standing wave ratio",
    "",
    "  with coordinates",
    "    ri  real, imaginary",
    "    ma  magnitude, angle",
    "    dB  decibels, angle",
    "",
    "  Parameters are case-insensitive.",
    NULL
};

/*
 * measure_main
 */
int measure_main(int argc, char **argv)
{
    char *opt_f = NULL;
    char  opt_l = '\000';		/* 'l' for linear; 'L' for log */
    int   opt_n = -1;
    char *opt_o = NULL;
    char *opt_p = "Sri";
    bool opt_P = false;
    bool opt_x = false;
    bool opt_y = false;
    char *calibration = NULL;
    char *calibration_file = NULL;
    char *output_file = NULL;
    int calset = 0;
    vnacal_t *vcp = NULL;
    vnacal_type_t c_type;
    int c_rows, c_columns;
    double f_min, f_max;
    setup_t *setup = NULL;
    vnadata_t *vdp = NULL;
    measurement_args_t ma;
    int rc = -1;

    /*
     * Parse options.
     */
    for (;;) {
	switch (getopt_long(argc, argv, short_options, long_options, NULL)) {
	case -1:
	    break;

	case 'f':
	    opt_f = optarg;
	    continue;

	case 'h':
	    print_usage(usage, help);
	    return 0;

	case 'l':
	    opt_l = 'l';
	    continue;

	case 'L':
	    opt_l = 'L';
	    continue;

	case 'n':
	    opt_n = atoi(optarg);
	    continue;

	case 'o':
	    opt_o = optarg;
	    continue;

	case 'p':
	    opt_p = optarg;
	    continue;

	case 'P':
	    opt_P = true;
	    continue;

	case 'x':
	    opt_x = true;
	    continue;

	case 'y':
	    opt_y = true;
	    continue;

	default:
	    print_usage(usage, help);
	    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	    goto out;
	}
	break;
    }
    argc -= optind;
    argv += optind;

    /*
     * One argument is expected.
     */
    if (argc != 1) {
	message_error("calibration must be given\n");
	print_usage(usage, help);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	goto out;
    }
    calibration = argv[0];

    /*
     * Open the calibration file.
     */
    if (calibration[0] == '/') {
	calibration_file = calibration;
    } else {
	char *cp;

	if ((cp = strrchr(calibration, '.')) != NULL &&
		strcmp(cp, ".vnacal") == 0) {
	    *cp = '\000';
	}
	if (asprintf(&calibration_file, "%s/%s.vnacal",
		n2pkvna_get_directory(gs.gs_vnap), calibration) == -1) {
	    (void)fprintf(stderr, "%s: asprintf: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    if ((vcp = vnacal_load(calibration_file, &print_libvna_error,
		    NULL)) == NULL) {
	goto out;
    }

    /*
     * Get c_type, c_rows, c_columns and validate.
     */
    c_type = vnacal_get_type(vcp, calset);
    c_rows = vnacal_get_rows(vcp, calset);
    c_columns = vnacal_get_columns(vcp, calset);
    if ((c_rows != 1 || c_columns != 1) &&
        (c_rows != 2 || c_columns != 1) &&
	(c_rows != 2 || c_columns != 2)) {
	message_error("error: %s: calibration dimensions "
		"must be 1x1, 2x1, or 2x2\n", calibration_file);
	goto out;
    }

    /*
     * Get f_min and f_max.
     */
    f_min = vnacal_get_fmin(vcp, calset);
    f_max = vnacal_get_fmax(vcp, calset);
    if (opt_f != NULL) {
	char c_temp;
	double t_min, t_max;

	if (sscanf(opt_f, "%lf : %lf %c", &t_min, &t_max, &c_temp) != 2) {
	    message_error("frequency range format is: "
		    "MHz_Min:MHz_Max\n");
	    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	    return -1;
	}
	if (t_min < 0.0 || t_min > t_max) {
	    message_error("invalid frequency range\n");
	    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	    return -1;
	}
	t_min *= 1.0e+6;
	t_max *= 1.0e+6;

	if (t_min < f_min || t_max > f_max) {
	    message_error("freuency range must be in "
		    "%g .. %g MHz\n",
		    progname, f_min * 1.0e-6, f_max * 1.0e-6);
	    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	    return -1;
	}
	f_min = t_min;
	f_max = t_max;
    }

    /*
     * Get frequencies.
     */
    if (opt_n == -1) {
	opt_n = vnacal_get_frequencies(vcp, calset);
    }

    /*
     * Get linear vs. log format.
     */
    if (opt_l == '\000') {
	const char *value;

	value = vnacal_property_get(vcp, calset, "frequencySpacing");
	if (value == NULL) {
	    value = vnacal_property_get(vcp, -1, "frequencySpacing");
	}
	if (value != NULL && strcmp(value, "linear") == 0) {
	    opt_l = 'l';
	} else {
	    opt_l = 'L';
	}
    }

    /*
     * Get the VNA setup.
     */
    for (;;) {
	vnaproperty_t *root;
	const char *setup_name;

	if ((root = vnacal_property_get_subtree(vcp, calset,
			"setup")) == NULL) {
	    if ((root = vnacal_property_get_subtree(vcp, -1,
			    "setup")) == NULL) {
		setup = &default_RB_setup;
		break;
	    }
	}
	if ((setup_name = vnacal_property_get(vcp, calset,
			"setupName")) == NULL) {
	    setup_name = "(unnamed)";
	}
	if ((setup = parse_setup(root, setup_name)) == NULL) {
	    goto out;
	}
	break;
    }

    /*
     * Provide a default output filename if -o not given.
     */
    if (opt_o != NULL) {
	output_file = opt_o;

    } else {
	time_t t;
	struct tm tm;
	char tbuf[32];

	(void)time(&t);
	(void)localtime_r(&t, &tm);
	(void)strftime(tbuf, sizeof(tbuf), "%Y-%m-%d_%H:%M:%S%z", &tm);
	if (asprintf(&output_file, "%s-%s.s2p", progname, tbuf) == -1) {
	    (void)fprintf(stderr, "%s: asprintf: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }

    /*
     * Fill in the ma structure.
     */
    (void)memset((void *)&ma, 0, sizeof(ma));
    ma.ma_rows = c_rows;
    ma.ma_columns = c_columns;
    ma.ma_setup = setup;
    ma.ma_fmin = f_min;
    ma.ma_fmax = f_max;
    ma.ma_frequencies = opt_n;
    ma.ma_linear = opt_l == 'l';
    ma.ma_colsys = c_type == VNACAL_E12 || c_type == VNACAL_UE14;
    ma.ma_z0 = vnacal_get_z0(vcp, calset);

    /*
     * Allocate the VNA data object to hold the parameter data.
     */
    if ((vdp = vnadata_alloc_and_init(&print_libvna_error, NULL, VPT_S,
		    c_rows, c_rows, opt_n)) == NULL) {
	message_error("vnadata_alloc_and_init: %s\n", strerror(errno));
	goto out;
    }

    /*
     * Validate the output parameter format.
     */
    if (vnadata_set_format(vdp, opt_p) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	goto out;
    }
    if (opt_x) {
	(void)vnadata_set_fprecision(vdp, VNADATA_MAX_PRECISION);
	(void)vnadata_set_dprecision(vdp, VNADATA_MAX_PRECISION);
    } else {
	(void)vnadata_set_fprecision(vdp, 7);	/* measured precision */
	(void)vnadata_set_dprecision(vdp, 6);	/* measured precision */
    }
    if (vnadata_cksave(vdp, output_file) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	goto out;
    }

    /*
     * Set the attenuation to zero.
     */
    if (n2pkvna_switch(gs.gs_vnap, -1, 0, SWITCH_DELAY) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	goto out;
    }
    gs.gs_attenuation = 0;

    /*
     * Make measurements and apply the calibration.
     */
    gs.gs_need_ack = opt_P;
    if (c_rows == c_columns || opt_y) {
	double complex *a_matrix[2][2] = {{ NULL, NULL }, { NULL, NULL }};
	double complex *b_matrix[2][2] = {{ NULL, NULL }, { NULL, NULL }};
	double complex **app = NULL;
	int a_rows = 0, a_columns = 0;
	int b_rows = 0, b_columns = 0;
	measurement_result_t mr;

	if (make_measurements(&ma, &mr) == -1) {
	    goto out;
	}
	assert(mr.mr_b_rows    == c_rows);
	assert(mr.mr_b_columns == c_columns);

	/*
	 * Handle the A matrix if it exists.
	 */
	if (mr.mr_a_matrix != NULL) {
	    /*
	     * If the calibration type uses column systems, then the
	     * measured A matrix should already be a row vector; use
	     * it as-is.
	     */
	    if (ma.ma_colsys) {
		assert(mr.mr_a_rows == 1);
		app       = mr.mr_a_matrix;
		a_rows    = mr.mr_a_rows;
		a_columns = mr.mr_a_columns;

	    } else {
		/*
		 * Get the square A matrix.
		 */
		app = &a_matrix[0][0];
		if (mr.mr_a_rows == 2 && mr.mr_a_columns == 2) {
#if 0
		    /*
		     * ZZ: TODO: If symmetrical, multiply B * A^-1
		     * here then set app to NULL and fall into the code
		     * below to average the resulting B's diagonally,
		     * using the apply_m API below.  For now, ignore
		     * symmetric if we're using A-B.
		     */
		    if (opt_y) {
		    }
#endif
		    app = mr.mr_a_matrix;
		    a_rows    = 2;
		    a_columns = 2;

		} else if ((mr.mr_a_rows == 1 && mr.mr_a_columns == 2) ||
			   (mr.mr_a_rows == 2 && mr.mr_a_columns == 1)) {
		    a_matrix[0][0] = a_matrix[1][1] = mr.mr_a_matrix[0];
		    a_matrix[0][1] = a_matrix[1][0] = mr.mr_a_matrix[1];
		    a_rows    = 2;
		    a_columns = 2;

		} else if (mr.mr_a_rows == 1 && mr.mr_a_columns == 1) {
		    a_matrix[0][0] = mr.mr_a_matrix[0];
		    a_rows    = 1;
		    a_columns = 1;

		} else {
		    abort();
		}
	    }
	}

	/*
	 * Get the square B matrix.
	 */
	if (mr.mr_b_rows == 2 && mr.mr_b_columns == 2) {
	    /*
	     * If symmetrical and no A matrix, average the measurements
	     * diagonally.
	     */
	    if (opt_y && app == NULL) {
		for (int findex = 0; findex < opt_n; ++findex) {
		    double complex **m = mr.mr_b_matrix;

		    m[0][findex] += m[3][findex];
		    m[0][findex] /= 2.0;
		    m[3][findex] =  m[0][findex];
		    m[1][findex] += m[2][findex];
		    m[1][findex] /= 2.0;
		    m[2][findex] =  m[1][findex];
		}
	    }
	    b_matrix[0][0] = mr.mr_b_matrix[0];
	    b_matrix[0][1] = mr.mr_b_matrix[1];
	    b_matrix[1][0] = mr.mr_b_matrix[2];
	    b_matrix[1][1] = mr.mr_b_matrix[3];
	    b_rows    = 2;
	    b_columns = 2;

	} else if ((mr.mr_b_rows == 2 && mr.mr_b_columns == 1) ||
		   (mr.mr_b_rows == 1 && mr.mr_b_columns == 2)) {
	    b_matrix[0][0] = b_matrix[1][1] = mr.mr_b_matrix[0];
	    b_matrix[0][1] = b_matrix[1][0] = mr.mr_b_matrix[1];
	    b_rows    = 2;
	    b_columns = 2;

	} else if (mr.mr_b_rows == 1 && mr.mr_b_columns == 1) {
	    b_matrix[0][0] = mr.mr_b_matrix[0];
	    b_rows    = 1;
	    b_columns = 1;

	} else {
	    abort();
	}

	/*
	 * Apply the calibration.
	 */
	if (vnacal_apply(vcp, calset, mr.mr_frequency_vector, opt_n,
		    app, a_rows, a_columns,
		    &b_matrix[0][0], b_rows, b_columns,
		    vdp) == -1) {
	    goto out;
	}
	measurement_result_free(&mr);

    } else {
	double complex *a_matrix[2][2] = {{ NULL, NULL }, { NULL, NULL }};
	double complex *b_matrix[2][2] = {{ NULL, NULL }, { NULL, NULL }};
	double complex **app = NULL;
	int a_rows = 0, a_columns = 0;
	measurement_result_t mr1, mr2;

	message_add_instruction("Connect VNA probe 1 to DUT port 1.\n");
	message_add_instruction("Connect VNA probe 2 to DUT port 2.\n");
	if (make_measurements(&ma, &mr1) == -1) {
	    goto out;
	}
	assert(mr1.mr_b_rows    == c_rows);
	assert(mr1.mr_b_columns == c_columns);
	message_add_instruction("Connect VNA probe 1 to DUT port 2.\n");
	message_add_instruction("Connect VNA probe 2 to DUT port 1.\n");
	if (make_measurements(&ma, &mr2) == -1) {
	    goto out;
	}
	assert(mr2.mr_b_rows    == c_rows);
	assert(mr2.mr_b_columns == c_columns);

	if (mr1.mr_b_rows == 2 && mr1.mr_b_columns == 1) {
	    assert(mr2.mr_b_rows    == 2);
	    assert(mr2.mr_b_columns == 1);
	    if (mr1.mr_a_matrix != NULL) {
		assert(mr1.mr_b_matrix != NULL);
		if (ma.ma_colsys) {
		    assert(mr1.mr_a_rows    == 1);
		    assert(mr1.mr_a_columns == 1);
		    assert(mr2.mr_a_rows    == 1);
		    assert(mr2.mr_a_columns == 1);
		    a_matrix[0][0] = mr1.mr_a_matrix[0];
		    a_matrix[0][1] = mr2.mr_a_matrix[0];
		    app = &a_matrix[0][0];
		    a_rows    = 1;
		    a_columns = 2;
		} else {
		    assert(mr1.mr_a_rows    == 2);
		    assert(mr1.mr_a_columns == 1);
		    assert(mr2.mr_a_rows    == 2);
		    assert(mr2.mr_a_columns == 1);
		    a_matrix[0][0] = mr1.mr_a_matrix[0];
		    a_matrix[0][1] = mr2.mr_a_matrix[1];
		    a_matrix[1][0] = mr1.mr_a_matrix[1];
		    a_matrix[1][1] = mr2.mr_a_matrix[0];
		    app = &a_matrix[0][0];
		    a_rows    = 2;
		    a_columns = 2;
		}
	    }
	    b_matrix[0][0] = mr1.mr_b_matrix[0];
	    b_matrix[0][1] = mr2.mr_b_matrix[1];
	    b_matrix[1][0] = mr1.mr_b_matrix[1];
	    b_matrix[1][1] = mr2.mr_b_matrix[0];

	} else if (mr1.mr_b_rows == 1 && mr1.mr_b_columns == 2) {
	    assert(mr2.mr_b_rows    == 1);
	    assert(mr2.mr_b_columns == 2);
	    if (mr1.mr_a_matrix != NULL) {
		assert(mr1.mr_b_matrix != NULL);
		if (ma.ma_colsys) {
		    assert(mr1.mr_a_rows    == 1);
		    assert(mr1.mr_a_columns == 1);
		    assert(mr2.mr_a_rows    == 1);
		    assert(mr2.mr_a_columns == 1);
		    a_matrix[0][0] = mr1.mr_a_matrix[0];
		    a_matrix[0][1] = mr2.mr_a_matrix[0];
		    app = &a_matrix[0][0];
		    a_rows    = 1;
		    a_columns = 2;
		} else {
		    assert(mr1.mr_a_rows    == 1);
		    assert(mr1.mr_a_columns == 2);
		    assert(mr2.mr_a_rows    == 1);
		    assert(mr2.mr_a_columns == 2);
		    a_matrix[0][0] = mr1.mr_a_matrix[0];
		    a_matrix[0][1] = mr1.mr_a_matrix[1];
		    a_matrix[1][0] = mr2.mr_a_matrix[1];
		    a_matrix[1][1] = mr2.mr_a_matrix[0];
		    app = &a_matrix[0][0];
		    a_rows    = 2;
		    a_columns = 2;
		}
	    }
	    b_matrix[0][0] = mr1.mr_b_matrix[0];
	    b_matrix[0][1] = mr1.mr_b_matrix[1];
	    b_matrix[1][0] = mr2.mr_b_matrix[1];
	    b_matrix[1][1] = mr2.mr_b_matrix[0];
	}
	if (vnacal_apply(vcp, calset,
		    mr1.mr_frequency_vector, opt_n,
		    app, a_rows, a_columns,
		    &b_matrix[0][0], 2, 2, vdp) == -1) {
	    measurement_result_free(&mr1);
	    measurement_result_free(&mr1);
	    goto out;
	}
	measurement_result_free(&mr1);
	measurement_result_free(&mr1);
    }

    /*
     * Save
     */
    if (vnadata_save(vdp, output_file) == -1) {
	goto out;
    }
    if (output_file != opt_o) {
	(void)printf("Saved to %s\n", output_file);
    }
    rc = 0;

out:
    vnadata_free(vdp);
    if (output_file != opt_o) {
	free((void *)output_file);
	output_file = NULL;
    }
    if (setup != NULL && setup != &default_RB_setup) {
	setup_free(setup);
	setup = NULL;
    }
    vnacal_free(vcp);
    if (calibration_file != calibration) {
	free((void *)calibration_file);
	calibration_file = NULL;
    }
    return rc;
}
