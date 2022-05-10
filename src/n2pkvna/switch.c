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

#include "switch.h"
#include "main.h"
#include "message.h"

/*
 * n2pkvna switch options
 */
static const char short_options[] = "h";
static const struct option long_options[] = {
    { "help",			0, NULL, 'h' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    "switch_code",
    NULL
};
static const char *const help[] = {
    " -h|--help         print this help message",
    " switch_code       switch code [0-3]",
    NULL
};

/*
 * switch_main
 */
int switch_main(int argc, char **argv)
{
    int code;

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
    code = atoi(argv[0]);
    if (code < 0 || code > 3) {
	message_error("invalid switch code: %s: expected 0-3\n", argv[0]);
	print_usage(usage, help);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
    }
    if (n2pkvna_switch(gs.gs_vnap, code, -1, SWITCH_DELAY) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	return -1;
    }
    return 0;
}
