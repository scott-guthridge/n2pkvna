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
#include "calibrate.h"
#include "cf.h"
#include "cli.h"
#include "convert.h"
#include "generate.h"
#include "main.h"
#include "measure.h"
#include "measurement.h"
#include "message.h"
#include "n2pkvna.h"
#include "properties.h"
#include "setup.h"
#include "switch.h"

/*
 * gs: program global state
 */
global_state_t gs = {
    .gs_interactive	= false,
    .gs_exitcode	= 0,
    .gs_opt_Y		= false,
    .gs_canceled	= false,
    .gs_vnap		= NULL,
    .gs_switch		= -1,
    .gs_attenuation	= -1,
    .gs_command		= NULL,
    .gs_need_ack	= false,
    .gs_setups		= NULL,
    .gs_mstep		= NULL
};

/*
 * global options
 */
char *progname;
static const char short_options[] = "+a:hN:U:Y";
static const struct option long_options[] = {
    { "attenuation",		1, NULL, 'a' },
    { "help",			0, NULL, 'h' },
    { "name",			1, NULL, 'N' },
    { "unit",		        1, NULL, 'U' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    "[-a attenuation] [-N name] [-U unit] [command command-options...]",
    "-h",
    NULL
};
static const char *const help[] = {
    "Options",
    " -a|--attenuation=attenuation  set the attentuation in dB",
    " -h|--help                     print this help message",
    " -N|--name=name                select the VNA configuration directory",
    " -U|--unit=unit-address        select the VNA device by USB address",
    " where:",
    "    attenuation is: 0, 10, 20, 30, 40, 50, 60 or 70",
    "    unit-address is: vendor:product | bus.device | bus/port",
    "",
    "Commands",
    "  a|attenuate attenuation_dB",
    "    Set the attenuation.",
    "",
    "  cal|calibrate [-lL]  [-D description] [-f fMin:fMax] [-n frequencies]",
    "       [-s setup] [-S standards] [-t error-term-type] name",
    "    Calibrate the VNA using known standards.",
    "",
    "  cf [-f frequency]",
    "    Calibrate the VNA timebase.",
    "",
    "  conv|convert [-x] [-p parameters] [-z z0] input-file output-file",
    "    Convert network parameters and file types.",
    "",
    "  gen|generate RF-MHz [[LO-MHz] phase-deg]",
    "    Generate RF signals.",
    "",
    "  ?|help",
    "    Print this help text.",
    "",
    "  m|measure [-lL] -c calibration [-f fMin:fMax] [-n nfrequencies]",
    "      [-o output-file] [-p parameters]",
    "    Measure an unknown device under test and save the S-parameters.",
    "",
    "  setup [command [args...]]        set up the VNA",
    "",
    "  sw|switch [0-3]",
    "    Manually set the VNA switches.",
    "",
    "  x|exit",
    "  q|quit",
    "    Exit the CLI.",
    "",
    "  Use command -h for more detailed help on the command.",
    NULL
};

/*
 * print_error: print errors from the n2pkvna library
 *   @message: error message (without newline)
 *   @arg: data passed through from n2pkvna_open
 */
/*ARGSUSED*/
void print_error(const char *message, void *arg)
{
    message_error("%s", message);
}

/*
 * print_libvna_error: print errors from the libvna library
 *   @message: error message (without newline)
 *   @arg: data passed through from vnacal_open
 *   @category: error category
 */
void print_libvna_error(const char *message, void *arg,
	vnaerr_category_t category)
{
    message_error("%s", message);
}

/*
 * print_usage: print a usage message
 */
void print_usage(const char *const *usage, const char *const *help)
{
    const char *const *cpp;

    for (cpp = usage; *cpp != NULL; ++cpp) {
	message_error("usage: %s\n", *cpp);
    }
    if (help != NULL) {
	for (cpp = help; *cpp != NULL; ++cpp) {
	    message_error_np("%s\n", *cpp);
	}
    }
    message_error_np("\n");
}

/*
 * print_help: show main help text
 */
static int print_help(int argc, char **argv)
{
    print_usage(usage, help);
    return 0;
}

/*
 * open_device: open an n2pkvna device
 *   @create: create the config file if it doesn't already exist
 */
static n2pkvna_t *open_device(const char *device, const char *unit, bool create)
{
    n2pkvna_t *vnap;
    n2pkvna_config_t **config_vector = NULL;

    if ((vnap = n2pkvna_open(device, create, unit, &config_vector,
		    print_error, NULL)) != NULL) {
	n2pkvna_free_config_vector(config_vector);
	if (n2pkvna_reset(vnap) == -1) {
	    n2pkvna_close(vnap);
	    return NULL;
	}
	return vnap;
    }
    if (config_vector != NULL && errno == ERANGE) {
	bool multiple_configs = config_vector[1] != NULL;
	n2pkvna_config_t **ncpp;

	(void)fprintf(stderr, "%s: Select a device using one of the "
		"following:\n", progname);
	for (ncpp = config_vector; *ncpp != NULL; ++ncpp) {
	    n2pkvna_config_t *ncp = *ncpp;
	    char *config_name;

	    if (multiple_configs) {
		if ((config_name = strrchr(ncp->nc_directory, '/')) == NULL) {
		    config_name = ncp->nc_directory;
		} else {
		    ++config_name;
		}
	    }
	    for (size_t s = 0; s < ncp->nc_count; ++s) {
		n2pkvna_address_t *adrp = ncp->nc_addresses[s];

		assert(adrp->adr_type == N2PKVNA_ADR_USB);
		assert(multiple_configs || ncp->nc_count > 1);
		(void)fprintf(stderr, "%s:  ", progname);
		if (multiple_configs) {
		    (void)fprintf(stderr, " -c %s", config_name);
		}
		if (ncp->nc_count > 1) {
		    (void)fprintf(stderr, " -u %d.%d",
			    adrp->adr_usb_bus, adrp->adr_usb_device);
		}
		(void)fprintf(stderr, "\n");
	    }
	}
    } else {
	(void)fprintf(stderr, "%s: error opening n2pkvna device: %s\n",
		progname, strerror(errno));
    }
    n2pkvna_free_config_vector(config_vector);
    return NULL;
}

/*
 * parse_attenuation: parse and validate the attenuation value
 *   @arg: attenuation string
 */
int parse_attenuation(const char *arg)
{
    char *end;
    long int li;

    li = strtol(arg, &end, 10);
    if (end > arg && *end == '\000') {
	if (li >= 10 && li % 10 == 0) {
	    li /= 10;
	}
	if (li >= 0 && li <= 7) {
	    return (int)li;
	}
    }
    message_error("attenuation value must be 0, 10, 20, 30, 40, 50, 60, "
	    "or 70\n");
    return -1;
}

/*
 * main_commands: main command table
 *   Must be sorted in LANG=C order.
 */
static command_t main_commands[] = {
    { "?",		print_help },
    { "a",		attenuate_main },
    { "attenuate",	attenuate_main	},
    { "cal",		calibrate_main	},
    { "calibrate",	calibrate_main	},
    { "cf",		cf_main		},
    { "conv",		convert_main	},
    { "convert",	convert_main	},
    { "gen",		generate_main	},
    { "generate",	generate_main	},
    { "help",		print_help	},
    { "m",		measure_main	},
    { "measure",	measure_main	},
    { "setup",		setup_main	},
    { "sw",		switch_main	},
    { "switch",		switch_main	},
};
#define N_MAIN_COMMANDS	(sizeof(main_commands) / sizeof(command_t))

/*
 * main
 */
int main(int argc, char **argv)
{
    int   opt_a = -1;
    char *opt_N = NULL;
    char *opt_U = NULL;

    /*
     * Parse options.
     */
    if ((progname = strrchr(argv[0], '/')) == NULL) {
	progname = argv[0];
    } else {
	++progname;
    }
    for (;;) {
	switch (getopt_long(argc, argv, short_options, long_options, NULL)) {
	case -1:
	    break;

	case 'a':
	    if ((opt_a = parse_attenuation(optarg)) == -1) {
		exit(N2PKVNA_EXIT_USAGE);
	    }
	    continue;

	case 'h':
	    print_usage(usage, help);
	    exit(0);

	case 'N':
	    opt_N = optarg;
	    continue;

	case 'U':
	    opt_U = optarg;
	    continue;

	case 'Y':
	    gs.gs_opt_Y = true;
	    continue;

	default:
	    print_usage(usage, help);
	    exit(N2PKVNA_EXIT_USAGE);
	}
	break;
    }
    argc -= optind;
    argv += optind;

    /*
     * Open the VNA
     */
    if ((gs.gs_vnap = open_device(opt_N, opt_U, true)) == NULL) {
	exit(N2PKVNA_EXIT_VNAOP);
    }

    /*
     * Load VNA properties from the config file.
     */
    if (properties_load() != 0) {
	gs.gs_exitcode = N2PKVNA_EXIT_ERROR;
	goto out;
    }

    /*
     * If -Y, include device configuration information in the open
     * response.
     */
    if (gs.gs_opt_Y) {
	message_get_config();
    }

    /*
     * If an attenuation was given, set the attenuator.
     */
    if (opt_a >= 0) {
	if (n2pkvna_switch(gs.gs_vnap, -1, opt_a, SWITCH_DELAY) == -1) {
	    gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	    goto out;
	}
	gs.gs_attenuation = opt_a;
    }

    /*
     * Run commands.
     */
    if (cli(main_commands, N_MAIN_COMMANDS, "n2pkvna", argc, argv) != 0 &&
	    gs.gs_exitcode == 0) {
	gs.gs_exitcode = N2PKVNA_EXIT_ERROR;
    }

    /*
     * If -Y and error, flush any error messages.
     */
out:
    if (gs.gs_opt_Y && gs.gs_exitcode != 0) {
	message_prompt();
    }

    n2pkvna_close(gs.gs_vnap);
    exit(gs.gs_exitcode);
}
