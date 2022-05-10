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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "attenuate.h"
#include "main.h"


/*
 * n2pkvna attenuate options
 */
static const char short_options[] = "h";
static const struct option long_options[] = {
    { "help",			0, NULL, 'h' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    "attenuation-dB",
    NULL
};
static const char *const help[] = {
    " -h|--help       print this help message",
    " attenuation-dB  attenuation in dB",
    NULL
};

/*
 * attenuate_main
 */
int attenuate_main(int argc, char **argv)
{
    int attenuation;

    /*
     * Parse options.
     */
    for (;;) {
	switch (getopt_long(argc, argv, short_options, long_options, NULL)) {
	case -1:
	    break;

	case 'h':
	    print_usage(usage, help);
	    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	    return -1;

	default:
	    print_usage(usage, help);
	    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	    return -1;
	}
	break;
    }
    argc -= optind;
    argv += optind;

    if (argc != 1) {
	print_usage(usage, help);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
    }
    if ((attenuation = parse_attenuation(argv[0])) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
    }
    if (n2pkvna_switch(gs.gs_vnap, -1, attenuation, SWITCH_DELAY) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	return -1;
    }
    gs.gs_attenuation = attenuation;
    return 0;
}
