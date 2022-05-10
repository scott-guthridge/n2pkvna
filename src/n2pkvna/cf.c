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
#include <unistd.h>

#include "cf.h"
#include "main.h"
#include "message.h"

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
    static double opt_f = 10.0;
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
	    print_usage(usage, help);
	    return 0;

	default:
	    print_usage(usage, help);
	    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	    return -1;
	}
	break;
    }
    argc -= optind;
    argv += optind;

    if (n2pkvna_generate(gs.gs_vnap,
		1.0e+6 * opt_f, 1.0e+6 * opt_f, 0.0) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	return -1;
    }
    if (message_get_measured_frequency(&measured) == -1) {
	return -1;
    }
    reference = n2pkvna_get_reference_frequency(gs.gs_vnap);
    reference *= measured / opt_f;
    if (n2pkvna_set_reference_frequency(gs.gs_vnap, reference) == -1) {
	if (errno == EINVAL) {	/* value out of range */
	    message_error("%s: cf: %f: value out of range\n", measured);
	}
	gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	return -1;
    }
    if (n2pkvna_save(gs.gs_vnap) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	return -1;
    }
    if (n2pkvna_generate(gs.gs_vnap,
		1.0e+6 * opt_f, 1.0e+6 * opt_f, 0.0) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	return -1;
    }
    return 0;
}
