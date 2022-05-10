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
#include <n2pkvna.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vnacal.h>

#include "calibrate.h"
#include "main.h"

/*
 * n2pkvna cal options
 */
static const char short_options[] = "D:f:hlLn:s:S:";

static const struct option long_options[] = {
    { "description",		1, NULL, 'D' },
    { "frequency-range",	1, NULL, 'f' },
    { "help",			0, NULL, 'h' },
    { "linear",			0, NULL, 'l' },
    { "log",			0, NULL, 'L' },
    { "frequencies",		1, NULL, 'n' },
    { "setup",			1, NULL, 's' },
    { "standards",		1, NULL, 'S' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    "[-lL] [-D description] [-f fMin:fMax] [-n frequencies]\n"
    "     [-s setup] [-S standards] name",
    NULL
};
static const char *const help[] = {
    " -D|--description=text           describe the calibration",
    " -f|--frequency-range=fMin:fMax  frequency range in MHz (default 0.05:60)",
    " -h|--help                       print this help message",
    " -l|--linear                     use linear frequency spacing",
    " -L|--log                        use logarithmic frequency spacing",
    " -n|--frequencies                number of frequencies (default 100)",
    " -s|--setup                      hardware setup",
    " -S|--standards=std1,std2,...    calibration standards",
    " name                            name for this calibration",
    NULL
};

/*
 * calibrate_main
 */
int calibrate_main(int argc, char **argv)
{
    const char *command = argv[0];
    double f_min = 50.0e+3;
    double f_max = 60.0e+6;
    const char *opt_D = NULL;
    char opt_l = '\000';
    int opt_n = 50;
    char c_temp;
    char *filename = NULL;
    vnacal_t *vcp = NULL;
    vnacal_new_t *vnp = NULL;
    double *frequency_vector = NULL;
    double complex *vector1 = NULL;
    double complex *vector2 = NULL;
    double complex *m[2][1];
    int rc = 0;

    /*
     * Parse options.
     */
    for (;;) {
	switch (getopt_long(argc, argv, short_options, long_options, NULL)) {
	case -1:
	    break;

	case 'D':
	    opt_D = optarg;
	    continue;

	case 'f':
	    if (sscanf(optarg, "%lf : %lf %c", &f_min, &f_max, &c_temp) != 2) {
		(void)fprintf(fp_err, "%s: frequency range format is: "
			"MHz_Min:MHz_Max\n", command);
		return N2PKVNA_EXIT_USAGE;
	    }
	    if (f_min < 0.0 || f_min > f_max) {
		(void)fprintf(fp_err, "%s: invalid frequency range\n",
		    command);
		return N2PKVNA_EXIT_USAGE;
	    }
	    f_min *= 1.0e+6;
	    f_max *= 1.0e+6;
	    continue;

	case 'h':
	    print_usage(command, usage, help);
	    return 0;

	case 'l':
	    opt_l = 'l';
	    continue;

	case 'L':
	    opt_l = 'L';
	    continue;

	case 'n':
	    opt_n = atoi(optarg);
	    if (opt_n < 1) {
		(void)fprintf(fp_err, "%s: expected positive integer for "
			"frequencies\n", progname);
		return N2PKVNA_EXIT_USAGE;
	    }
	    continue;

	case 's':
	    //ZZ: here
	    continue;

	case 'S':
	    //ZZ
	    continue;

	default:
	    print_usage(command, usage, help);
	    return N2PKVNA_EXIT_USAGE;
	}
	break;
    }
    argc -= optind;
    argv += optind;
    if (argc != 1) {
	print_usage(command, usage, help);
	return N2PKVNA_EXIT_USAGE;
    }

    /*
     * If -l wasn't given, choose a default.  If the frequency range
     * spans more than a factor of 10, use log; otherwise use linear.
     */
    if (opt_l == '\000') {
	if (f_min != 0.0 && f_max / f_min > 10.0) {
	    opt_l = 'L';	/* log */
	} else {
	    opt_l = 'l';	/* linear */
	}
    }

    /*
     * Find the save filename.
     */
    if (argv[0][0] == '/') {
	filename = argv[0];
    } else {
	char *cp;

	if ((cp = strrchr(argv[0], '.')) != NULL &&
		strcmp(cp, ".vnacal") == 0) {
	    *cp = '\000';
	}
	if (asprintf(&filename, "%s/%s.vnacal",
		n2pkvna_get_directory(vnap), argv[0]) == -1) {
	    (void)fprintf(stderr, "%s: asprintf: %s\n",
		    progname, strerror(errno));
	    filename = NULL;
	    rc = N2PKVNA_EXIT_SYSTEM;
	    goto out;
	}
    }

    /*
     * Create vnacal_t and vnacal_new_t structures.
     */
    if ((vcp = vnacal_create(print_libvna_error, NULL)) == NULL) {
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }
    if ((vnp = vnacal_new_alloc(vcp, VNACAL_E12, 2, 1, opt_n)) == NULL) {
	vnacal_free(vcp);
	rc = N2PKVNA_EXIT_SYSTEM;
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
    if ((vector1 = calloc(opt_n, sizeof(double complex))) == NULL) {
	(void)fprintf(stderr, "%s: calloc: %s\n", progname, strerror(errno));
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }
    if ((vector2 = calloc(opt_n, sizeof(double complex))) == NULL) {
	(void)fprintf(stderr, "%s: calloc: %s\n", progname, strerror(errno));
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }
    m[0][0] = vector1;
    m[1][0] = vector2;

    /*
     * Perform short calibration.
     */
    (void)printf("Connect VNA probe 1 to the short standard.\n");
    (void)printf("Connect VNA probe 2 to a terminator.\n");
    if (prompt_for_ready() == -1) {
	goto out;
    }
    (void)printf("Measuring...\n");
    if (n2pkvna_scan(vnap, f_min, f_max, opt_n, opt_l == 'l', frequency_vector,
		vector1, vector2) == -1) {
	rc = N2PKVNA_EXIT_VNAOP;
	goto out;
    }
    (void)printf("done\n\n");
    if (vnacal_new_set_frequency_vector(vnp, frequency_vector) == -1) {
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }
    if (vnacal_new_add_single_reflect_m(vnp, &m[0][0], 2, 1, VNACAL_SHORT,
		1) == -1) {
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }

    /*
     * Perform open calibration.
     */
    (void)printf("Connect VNA probe 1 to the open standard.\n");
    if (prompt_for_ready() == -1) {
	goto out;
    }
    (void)printf("Measuring...\n");
    if (n2pkvna_scan(vnap, f_min, f_max, opt_n, opt_l == 'l', NULL,
		vector1, vector2) == -1) {
	rc = N2PKVNA_EXIT_VNAOP;
	goto out;
    }
    (void)printf("done\n\n");
    if (vnacal_new_add_single_reflect_m(vnp, &m[0][0], 2, 1, VNACAL_OPEN,
		1) == -1) {
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }

    /*
     * Perform match calibration.
     */
    (void)printf("Connect VNA probe 1 to the load standard.\n");
    if (prompt_for_ready() == -1) {
	goto out;
    }
    (void)printf("Measuring...\n");
    if (n2pkvna_scan(vnap, f_min, f_max, opt_n, opt_l == 'l', NULL,
		vector1, vector2) == -1) {
	rc = N2PKVNA_EXIT_VNAOP;
	goto out;
    }
    (void)printf("done\n\n");
    if (vnacal_new_add_single_reflect_m(vnp, &m[0][0], 2, 1, VNACAL_MATCH,
		1) == -1) {
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }

    /*
     * Perform through calibration.
     */
    (void)printf("Connect VNA probes 1 & 2 to the through standard.\n");
    if (prompt_for_ready() == -1) {
	goto out;
    }
    (void)printf("Measuring...\n");
    if (n2pkvna_scan(vnap, f_min, f_max, opt_n, opt_l == 'l', NULL,
		vector1, vector2) == -1) {
	rc = N2PKVNA_EXIT_VNAOP;
	goto out;
    }
    (void)printf("done\n\n");
    if (vnacal_new_add_through_m(vnp, &m[0][0], 2, 1, 1, 2) == -1) {
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }

    /*
     * Solve for the error parameters, add to the calibration and save.
     */
    if (vnacal_new_solve(vnp) == -1) {
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }
    if (vnacal_add_calibration(vcp, "2x1-SOLT", vnp) == -1) {
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }
    if (opt_D != NULL) {
	if (vnacal_property_set(vcp, 0, "description=%s", opt_D) == -1) {
	    rc = N2PKVNA_EXIT_SYSTEM;
	    goto out;
	}
    }
    {
	time_t t;
	struct tm tm;
	char tbuf[32];

	(void)time(&t);
	(void)localtime_r(&t, &tm);
	(void)strftime(tbuf, sizeof(tbuf), "%Y-%m-%d_%H:%M:%S%z", &tm);
	if (vnacal_property_set(vcp, 0, "date=%s", tbuf) == -1) {
	    rc = N2PKVNA_EXIT_SYSTEM;
	    goto out;
	}
    }
    if (vnacal_property_set(vcp, 0, "frequencySpacing=%s",
		opt_l == 'l' ? "linear" : "log") == -1) {
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }
    /* TODO: record the setup and standards properties */
    if (vnacal_save(vcp, filename) == -1) {
	rc = N2PKVNA_EXIT_SYSTEM;
	goto out;
    }
    rc = N2PKVNA_EXIT_SUCCESS;

out:
    free((void *)vector1);
    free((void *)vector2);
    free((void *)frequency_vector);
    vnacal_new_free(vnp);
    vnacal_free(vcp);
    if (filename != argv[0]) {
	free((void *)filename);
    }
    return rc;
}
