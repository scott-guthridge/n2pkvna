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
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <n2pkvna.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "generate.h"
#include "main.h"

/*
 * n2pkvna gen options
 */
static const char short_options[] = "h";
static const struct option long_options[] = {
    { "help",			0, NULL, 'h' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    "RF-MHz [[LO-Mhz] phase-deg]",
    NULL
};
static const char *const help[] = {
    " -h|--help  print this help message",
    " RF-Mhz     frequency at RF out",
    " LO-MHz     frequency at LO out",
    " phase-deg  phase of LO relative to RF",
    NULL
};

/*
 * generate_main
 */
int generate_main(int argc, char **argv)
{
    const char *command = argv[0];
    double rf_frequency;
    double lo_frequency;
    double phase = 90.0;

    /*
     * Parse options.
     */
    for (;;) {
	switch (getopt_long(argc, argv, short_options, long_options, NULL)) {
	case -1:
	    break;

	case 'h':
	    print_usage(command, usage, help);
	    return N2PKVNA_EXIT_USAGE;

	default:
	    print_usage(command, usage, help);
	    return N2PKVNA_EXIT_USAGE;
	}
	break;
    }
    argc -= optind;
    argv += optind;

    switch (argc) {
    case 1:	/* RF_MHz */
	rf_frequency = atof(argv[0]) * 1.0e+6;
	lo_frequency = rf_frequency;
	break;

    case 2:	/* RF_MHz phase_deg */
	rf_frequency = atof(argv[0]) * 1.0e+6;
	lo_frequency = rf_frequency;
	phase = atof(argv[1]);
	break;

    case 3:	/* RF_MHz LO_Mhz phase_deg */
	rf_frequency = atof(argv[0]) * 1.0e+6;
	lo_frequency = atof(argv[1]) * 1.0e+6;
	phase = atof(argv[2]);
	break;

    default:
	print_usage(command, usage, help);
	return N2PKVNA_EXIT_USAGE;
    }
    if (n2pkvna_reset(vnap) == -1) {	/* reset to synchronize phase */
	return N2PKVNA_EXIT_VNAOP;
    }
    if (n2pkvna_generate(vnap, rf_frequency, lo_frequency, phase) == -1) {
	return N2PKVNA_EXIT_VNAOP;
    }
    return 0;
}
