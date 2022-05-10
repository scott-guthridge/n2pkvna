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
#include <vnadata.h>

#include "switch.h"
#include "main.h"
#include "message.h"

/*
 * n2pkvna convert options
 */
static const char short_options[] = "hp:xz:";
static const struct option long_options[] = {
    { "help",			0, NULL, 'h' },
    { "parameters",		1, NULL, 'p' },
    { "hexfloat",		0, NULL, 'x' },
    { "z0",			1, NULL, 'z' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    "[-x] [-p parameters] [-z z0] input-file output-file",
    NULL
};
static const char *const help[] = {
    " -h|--help                         print this help message",
    " -p|--parameters=parameter-format  default Sri",
    " -x|--hexfloat                     use hexadecimal floating point",
    " -z|--z0                           reference impedance of output",
    " input-file                        .npd, .ts, or .sNp input file",
    " output-file                       .npd, .ts, or .sNp output file",
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
 * convert_main
 */
int convert_main(int argc, char **argv)
{
    const char *opt_p = NULL;
    bool opt_x = false;
    char *opt_z = NULL;
    const char *input_filename = NULL;
    const char *output_filename = NULL;
    vnadata_t *vdp = NULL;
    int rc = -1;

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

	case 'p':
	    opt_p = optarg;
	    continue;

	case 'x':
	    opt_x = true;
	    continue;

	case 'z':
	    opt_z = optarg;
	    continue;

	default:
	    print_usage(usage, help);
	    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	    return -1;
	}
	break;
    }
    argc -= optind;
    argv += optind;

    /*
     * Expect two arguments.
     */
    if (argc != 2) {
	print_usage(usage, help);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
    }
    input_filename  = argv[0];
    output_filename = argv[1];

    /*
     * Allocate the VNA data object to hold the parameter data.
     */
    if ((vdp = vnadata_alloc(&print_libvna_error, NULL)) == NULL) {
	message_error("vnadata_alloc_and_init: %s\n", strerror(errno));
	goto out;
    }

    /*
     * Load from the input file.
     */
    if (vnadata_load(vdp, input_filename) == -1) {
	goto out;
    }

    /*
     * If -z was given, set the system impedance.
     */
    if (opt_z != NULL) {
	double r, i = 0.0;

	switch (sscanf(opt_z, "%lf %lf", &r, &i)) {
	case 1:
	case 2:
	    break;

	default:
	    message_error("invalid z0 value\n", opt_z);
	    goto out;
	}
	(void)vnadata_set_all_z0(vdp, r + i * I);
    }

    /*
     * Validate and set the output parameter format.
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

    /*
     * Set the filetype back to auto so that saving to a .ts file forces
     * Touchstone 2 format.
     */
    if (vnadata_set_filetype(vdp, VNADATA_FILETYPE_AUTO) == -1) {
        goto out;
    }

    /*
     * Save to the output file.
     */
    if (vnadata_save(vdp, output_filename) == -1) {
	goto out;
    }

    /*
     * Under -Y, return metadata.
     */
    if (gs.gs_opt_Y) {
	vnaproperty_t **root;

	if ((root = vnaproperty_set_subtree(&gs.gs_messages,
			"metadata.{}")) == NULL) {
	    (void)fprintf(stderr, "%s: vnaproperty_set_subtree: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(root, "ports=%d",
			vnadata_get_columns(vdp)) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(root, "frequencies=%d",
			vnadata_get_frequencies(vdp)) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(root, "fmin=%.7e",
			vnadata_get_fmin(vdp)) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(root, "fmax=%.7e",
			vnadata_get_fmax(vdp)) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(root, "parameters=%s",
			vnadata_get_format(vdp)) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    rc = 0;

out:
    vnadata_free(vdp);
    return rc;
}
