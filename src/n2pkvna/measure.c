/*
 * N2PK Vector Network Analyzer
 * Copyright © 2021 D Scott Guthridge <scott_guthridge@rompromity.net>
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
#include <n2pkvna.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vnacal.h>
#include <vnadata.h>

#include "measure.h"
#include "main.h"

/*
 * n2pkvna gen options
 */
static const char short_options[] = "c:f:hlLn:o:p:";
static const struct option long_options[] = {
    { "calibration",		1, NULL, 'c' },
    { "frequency-range",	1, NULL, 'f' },
    { "help",			0, NULL, 'h' },
    { "linear",			0, NULL, 'l' },
    { "log",                    0, NULL, 'L' },
    { "nfrequencies",		1, NULL, 'n' },
    { "output",			1, NULL, 'o' },
    { "parameters",		1, NULL, 'p' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    "[-lL] -c calibration [-f fMin:fMax] [-n nfrequencies]\n"
    "    [-o output-file] [-p parameters]",
    NULL
};
static const char *const help[] = {
    " -l|--linear                       force linear frequency spacing",
    " -L|--log                          force logarithmic frequency spacing",
    " -c|--calibration=name             specify which calibration to use",
    " -f|--frequency-range=fMin:fMax    override calibration range (MHz)",
    " -h|--help                         show this help message",
    " -n|--nfrequencies=n               override the frequency count",
    " -o|--output=file			example \"filter.s2p\"",
    " -p|--parameters=parameter-format  default Sri",
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
    const char *command = argv[0];
    char *opt_c = NULL;
    char *opt_f = NULL;
    char  opt_l = '\000';		/* 'l' for linear; 'L' for log */
    int   opt_n = -1;
    char *opt_o = NULL;
    char *opt_p = "Sri";
    char *calibration_file = NULL;
    char *output_file = NULL;
    int calset = 0;
    vnacal_t *vcp = NULL;
    int c_rows, c_columns;
    double f_min, f_max;
    int i_temp;
    vnadata_t *vdp = NULL;
    double *frequency_vector = NULL;
    double complex *m[2][2] = {{ NULL, NULL }, { NULL, NULL }};
    int rc = N2PKVNA_EXIT_USAGE;

    /*
     * Parse options.
     */
    for (;;) {
	switch (getopt_long(argc, argv, short_options, long_options, NULL)) {
	case -1:
	    break;

	case 'c':
	    opt_c = optarg;
	    continue;

	case 'f':
	    opt_f = optarg;
	    continue;

	case 'h':
	    print_usage(command, usage, help);
	    return N2PKVNA_EXIT_SUCCESS;

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

	default:
	    print_usage(command, usage, help);
	    rc = N2PKVNA_EXIT_USAGE;
	    goto out;
	}
	break;
    }
    argc -= optind;
    argv += optind;

    /*
     * The -c option is required.  No arguments are expected.
     */
    if (opt_c == NULL) {
	(void)fprintf(fp_err, "%s: error: -c option is required\n", command);
	print_usage(command, usage, help);
	rc = N2PKVNA_EXIT_USAGE;
	goto out;
    }
    if (argc != 0) {
	(void)fprintf(fp_err, "%s: error: no arguments expected\n", command);
	print_usage(command, usage, help);
	rc = N2PKVNA_EXIT_USAGE;
	goto out;
    }

    /*
     * Open the calibration file.
     */
    if (opt_c[0] == '/') {
	calibration_file = opt_c;
    } else {
	char *cp;

	if ((cp = strrchr(opt_c, '.')) != NULL &&
		strcmp(cp, ".vnacal") == 0) {
	    *cp = '\000';
	}
	if (asprintf(&calibration_file, "%s/%s.vnacal",
		n2pkvna_get_directory(vnap), opt_c) == -1) {
	    (void)fprintf(stderr, "%s: asprintf: %s\n",
		    progname, strerror(errno));
	    calibration_file = NULL;
	    rc = N2PKVNA_EXIT_SYSTEM;
	    goto out;
	}
    }
    if ((vcp = vnacal_load(calibration_file, &print_libvna_error,
		    NULL)) == NULL) {
	rc = N2PKVNA_EXIT_CALLOAD;
	goto out;
    }

    /*
     * Get c_rows, c_columns and validate.
     */
    c_rows = vnacal_get_rows(vcp, calset);
    c_columns = vnacal_get_columns(vcp, calset);
    if ((c_rows != 1 || c_columns != 1) &&
        (c_rows != 2 || c_columns != 1) &&
	(c_rows != 2 || c_columns != 2)) {
	(void)fprintf(fp_err, "%s: error: %s: calibration dimensions must "
		"be 1x1, 2x1, or 2x2\n", command, calibration_file);
	rc = N2PKVNA_EXIT_CALPARSE;
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
	    (void)fprintf(fp_err, "%s: frequency range format is: "
		    "MHz_Min:MHz_Max\n", command);
	    return N2PKVNA_EXIT_USAGE;
	}
	if (t_min < 0.0 || t_min > t_max) {
	    (void)fprintf(fp_err, "%s: invalid frequency range\n",
		command);
	    return N2PKVNA_EXIT_USAGE;
	}
	t_min *= 1.0e+6;
	t_max *= 1.0e+6;

	if (t_min < f_min || t_max > f_max) {
	    (void)fprintf(fp_err, "%s: freuency range must be in "
		    "%g .. %g MHz\n",
		    progname, f_min * 1.0e-6, f_max * 1.0e-6);
	    return N2PKVNA_EXIT_USAGE;
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
	    (void)fprintf(fp_err, "%s: asprintf: %s\n",
		    command, strerror(errno));
	    rc = N2PKVNA_EXIT_SYSTEM;
	    goto out;
	}
    }

    /*
     * Allocate the VNA data object to hold the parameter data.
     */
    if ((vdp = vnadata_alloc_and_init(&print_libvna_error, NULL, VPT_S,
		    c_rows, c_rows, opt_n)) == NULL) {
	(void)fprintf(fp_err, "%s: vnadata_alloc_and_init: %s\n",
		command, strerror(errno));
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }

    /*
     * Validate the output parameter format.
     */
    if (vnadata_set_format(vdp, opt_p) == -1) {
	rc = N2PKVNA_EXIT_USAGE;
	goto out;
    }
    (void)vnadata_set_fprecision(vdp, 7);	/* measured precision */
    (void)vnadata_set_dprecision(vdp, 6);	/* measured precision */
    if (vnadata_cksave(vdp, output_file) == -1) {
	rc = N2PKVNA_EXIT_USAGE;
	goto out;
    }

    /*
     * Allocate the measurement vectors.
     */
    if ((frequency_vector = calloc(opt_n, sizeof(double))) == NULL) {
	(void)fprintf(stderr, "%s: calloc: %s\n", progname, strerror(errno));
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }
    for (int i = 0; i < 2; ++i) {
	for (int j = 0; j < 2; ++j) {
	    ;
	    if ((m[i][j] = calloc(opt_n, sizeof(double complex))) == NULL) {
		(void)fprintf(stderr, "%s: calloc: %s\n",
			progname, strerror(errno));
		rc = N2PKVNA_EXIT_SYSTEM;
		goto out;
	    }
	}
    }

    /*
     * Make the VNA measurements.
     */
    (void)printf("Connect VNA probe 1 to DUT port 1\n");
    (void)printf("Connect VNA probe 2 to DUT port 2\n");
    if (prompt_for_ready() == -1) {
	goto out;
    }
    (void)printf("Measuring...\n");
    if (n2pkvna_scan(vnap, f_min, f_max, opt_n, opt_l == 'l',
		frequency_vector, m[0][0], m[1][0]) == -1) {
	rc = N2PKVNA_EXIT_VNAOP;
	goto out;
    }
    (void)printf("done\n\n");
    (void)printf("Connect VNA probe 1 to DUT port 2\n");
    (void)printf("Connect VNA probe 2 to DUT port 1\n");
    if (prompt_for_ready() == -1) {
	goto out;
    }
    (void)printf("Measuring...\n");
    if (n2pkvna_scan(vnap, f_min, f_max, opt_n, opt_l == 'l',
		NULL, m[1][1], m[0][1]) == -1) {
	rc = N2PKVNA_EXIT_VNAOP;
	goto out;
    }
    (void)printf("done\n\n");

    /*
     * Apply the calibration.
     */
    if (vnacal_apply_m(vcp, calset, frequency_vector, opt_n,
		&m[0][0], c_rows, c_rows, vdp) == -1) {
	rc = N2PKVNA_EXIT_VNAOP;
	goto out;
    }

    /*
     * Save
     */
    if (vnadata_save(vdp, output_file) == -1) {
	rc = N2PKVNA_EXIT_SYSTEM;
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
    vnacal_free(vcp);
    if (calibration_file != opt_c) {
	free((void *)calibration_file);
	calibration_file = NULL;
    }
    return rc;
}
