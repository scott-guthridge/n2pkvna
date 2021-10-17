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
#include <unistd.h>

#include "cf.h"
#include "main.h"

/*
 * n2pkvna cf options
 */
static const char short_options[] = "h:f:";
static const struct option long_options[] = {
    { "frequency",		1, NULL, 'f' },
    { "help",			0, NULL, 'h' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    "[-f frequency]",
    NULL
};
static const char *const help[] = {
    " -f|--frequency=frequency    target frequency in MHz (default 10)",
    " -h|--help                   print this help message",
    NULL
};

/*
 * main
 */
int cf_main(int argc, char **argv)
{
    const char *command = argv[0];
    static double opt_f = 10.0;
    char line[82];
    double measured, reference;

    /*
     * Parse options.
     */
    for (;;) {
	switch (getopt_long(argc, argv, short_options, long_options, NULL)) {
	case -1:
	    break;

	case 'f':
	    opt_f = atof(optarg);
	    continue;

	case 'h':
	    print_usage(command, usage, help);
	    return 0;

	default:
	    print_usage(command, usage, help);
	    return N2PKVNA_EXIT_USAGE;
	}
	break;
    }
    argc -= optind;
    argv += optind;

    if (n2pkvna_generate(vnap, 1.0e+6 * opt_f, 1.0e+6 * opt_f, 0.0) == -1) {
	return N2PKVNA_EXIT_VNAOP;
    }
    (void)printf("measured frequency (MHz) ? ");
    if (fgets(line, sizeof(line) - 1, stdin) == NULL) {
	return 0;
    }
    if (sscanf(line, "%lf", &measured) != 1) {
	return N2PKVNA_EXIT_USAGE;
    }
    reference = n2pkvna_get_reference_frequency(vnap);
    reference *= measured / opt_f;
    if (n2pkvna_set_reference_frequency(vnap, reference) == -1) {
	if (errno == EINVAL) {	/* value out of range */
	    (void)fprintf(fp_err, "%s: cf: %f: value out of range\n",
		    progname, measured);
	}
	return N2PKVNA_EXIT_VNAOP;
    }
    if (n2pkvna_save(vnap) == -1) {
	return N2PKVNA_EXIT_CALLOAD;
    }
    if (n2pkvna_generate(vnap, 1.0e+6 * opt_f, 1.0e+6 * opt_f, 0.0) == -1) {
	return N2PKVNA_EXIT_VNAOP;
    }
    return 0;
}
