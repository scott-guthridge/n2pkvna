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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "attenuate.h"
#include "calibrate.h"
#include "cf.h"
#include "cli.h"
#include "configure.h"
#include "generate.h"
#include "measure.h"
#include "n2pkvna.h"
#include "switch.h"

#include "main.h"

FILE *fp_err = NULL;
n2pkvna_t *vnap = NULL;

/*
 * global options
 */
char *progname;
static const char short_options[] = "+a:hN:U:";
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
    "  cal|calibrate [-lL]  [-D description][-f fMin:fMax],",
    "      [-n frequencies] [-s setup] [-S standards]",
    "    Calibrate the VNA against known standards.",
    "",
    "  cf [-f frequency]",
    "    Calibrate the VNA timebase.",
    // "",
    // "  config|configure                         configure hardware",
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
 */
/*ARGSUSED*/
void print_error(const char *message, void *arg)
{
    (void)fprintf(fp_err, "%s: %s\n", progname, message);
}

/*
 * print_libvna_error: print errors from the libvna library
 */
void print_libvna_error(vnaerr_category_t category, const char *message,
	void *arg)
{
    (void)fprintf(fp_err, "%s: %s\n", progname, message);
}

/*
 * print_usage: print a usage message
 */
void print_usage(const char *command, const char *const *usage,
	const char *const *help)
{
    const char *const *cpp;

    for (cpp = usage; *cpp != NULL; ++cpp) {
	if (fp_err == stderr) {
	    (void)fprintf(fp_err, "%s: ", progname);
	}
	(void)fprintf(fp_err, "usage: ");
	if (command != NULL) {
	    (void)fprintf(fp_err, "%s ", command);
	}
	(void)fprintf(fp_err, "%s\n", *cpp);
    }
    if (help != NULL) {
	for (cpp = help; *cpp != NULL; ++cpp) {
	    (void)fprintf(fp_err, "%s\n", *cpp);
	}
    }
    (void)fprintf(fp_err, "\n");
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
    (void)fprintf(fp_err, "error: attenuation value must be 0, 10, "
	    "20, 30, 40, 50, 60, or 70\n");
    return -1;
}

/*
 * command_t: command code
 */
typedef enum {
    CMD_ATTENUATE,
    CMD_CALIBRATE,
    CMD_CF,
    CMD_CONFIGURE,
    CMD_EXIT,
    CMD_GENERATE,
    CMD_HELP,
    CMD_MEASURE,
    CMD_SWITCH,
} command_t;

/*
 * keyword_t: keyword table entry
 */
typedef struct keyword {
    const char	k_name[10];
    command_t	k_code;
} keyword_t;

/*
 * keywords: keyword table
 *   Must be sorted in LANG=C order.
 */
static const keyword_t keywords[] = {
    { "?",		CMD_HELP	},
    { "a",		CMD_ATTENUATE	},
    { "attenuate",	CMD_ATTENUATE	},
    { "cal",		CMD_CALIBRATE	},
    { "calibrate",	CMD_CALIBRATE	},
    { "cf",		CMD_CF		},
    { "config",		CMD_CONFIGURE	},
    { "configure",	CMD_CONFIGURE	},
    { "exit",		CMD_EXIT	},
    { "gen",		CMD_GENERATE	},
    { "generate",	CMD_GENERATE	},
    { "help",		CMD_HELP	},
    { "m",		CMD_MEASURE	},
    { "measure",	CMD_MEASURE	},
    { "q",		CMD_EXIT	},
    { "quit",		CMD_EXIT	},
    { "sw",		CMD_SWITCH	},
    { "switch",		CMD_SWITCH	},
    { "x",		CMD_EXIT	},
};
#define N_KEYWORDS	(sizeof(keywords) / sizeof(keyword_t))

/*
 * run_command
 *   @argc: argument count
 *   @argv: argument vector
 */
int run_command(int argc, char **argv)
{
    int low = 0;
    int high = N_KEYWORDS - 1;
    int cur, cmp;

    /*
     * Binary search for the command.
     */
    while (low <= high) {
	cur = (low + high) / 2;
	cmp = strcmp(argv[0], keywords[cur].k_name);

	if (cmp == 0) {
	    break;
	}
	if (cmp < 0) {
	    high = cur - 1;
	} else {
	    low = cur + 1;
	}
    }
    if (cmp != 0) {
	(void)fprintf(fp_err, "%s: %s: unknown command\n", progname, argv[0]);
	return N2PKVNA_EXIT_USAGE;
    }

    /*
     * Call the command interpreter.
     */
    optind = 0;						/* reset getopt */
    switch (keywords[cur].k_code) {
    case CMD_ATTENUATE:
	return attenuate_main(argc, argv);

    case CMD_CF:
	return cf_main(argc, argv);

    case CMD_CALIBRATE:
	return calibrate_main(argc, argv);

    case CMD_CONFIGURE:
	return configure_main(argc, argv);

    case CMD_EXIT:
	return -1;

    case CMD_GENERATE:
	return generate_main(argc, argv);

    case CMD_HELP:
	print_usage(/*command*/NULL, usage, help);
	return 0;

    case CMD_MEASURE:
	return measure_main(argc, argv);

    case CMD_SWITCH:
	return switch_main(argc, argv);

    default:
	abort();
    }
}

/*
 * main
 */
int main(int argc, char **argv)
{
    int   opt_a = -1;
    char *opt_N = NULL;
    char *opt_U = NULL;
    int rc = 0;

    /*
     * Default the error file handle to stderr.
     */
    fp_err = stderr;

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
	    print_usage(/*command*/NULL, usage, help);
	    exit(N2PKVNA_EXIT_SUCCESS);

	case 'N':
	    opt_N = optarg;
	    continue;

	case 'U':
	    opt_U = optarg;
	    continue;

	default:
	    print_usage(/*command*/NULL, usage, help);
	    exit(N2PKVNA_EXIT_USAGE);
	}
	break;
    }
    argc -= optind;
    argv += optind;

    /*
     * Open the VNA
     */
    if ((vnap = open_device(opt_N, opt_U, true)) == NULL) {
	exit(N2PKVNA_EXIT_VNAOPEN);
    }

    /*
     * If an attenuation was given, set the attenuator.
     */
    if (opt_a >= 0) {
	if (n2pkvna_switch(vnap, -1, opt_a, SWITCH_DELAY) == -1) {
	    exit(N2PKVNA_EXIT_VNAOP);
	}
    }

    /*
     * If we're given a single command on the command line, run it.
     * Else drop into an interactive CLI.
     */
    if (argc != 0) {
	rc = run_command(argc, argv);
    } else {
	fp_err = stdout;
	cli();
    }

    n2pkvna_close(vnap);
    exit(rc);
}
