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
#include "cal_standard.h"
#include "main.h"
#include "message.h"

/*
 * n2pkvna cal options
 */
static const char short_options[] = "D:f:hlLn:s:S:t:z:";

static const struct option long_options[] = {
    { "description",		1, NULL, 'D' },
    { "frequency-range",	1, NULL, 'f' },
    { "help",			0, NULL, 'h' },
    { "linear",			0, NULL, 'l' },
    { "log",			0, NULL, 'L' },
    { "frequencies",		1, NULL, 'n' },
    { "setup",			1, NULL, 's' },
    { "standards",		1, NULL, 'S' },
    { "type",			1, NULL, 't' },
    { "z0",			1, NULL, 'z' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    "[-lL] [-D description] [-f fMin:fMax] [-n frequencies]\n"
    "     [-s setup] [-S standards] [-t error-term-type] name",
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
    " -t|--type=error-term-type	      set error term type (default E12)",
    "      T8      8-term T parameters",
    "      U8      8-term U (inverse T) parameters",
    "      TE10    8-term T plus off-diagonal E11 leakage terms",
    "      UE10    8-term U plus off-diagonal E11 leakage terms",
    "      E12    12-term generalized classic SOLT",
    "      UE14   14-term columns x (rows x 1) U7 systems",
    "      T16    16-term T parameters",
    "      U16    16-term U (inverse T) parameters",
    " -z|--z0=z0                      set the system impedance (default 50)",
    " name                            name for this calibration",
    NULL
};

/*
 * Describes where a VNA port is connected.
 */
typedef struct probe_connection {
    const cal_standard_t       *pc_standard;
    int				pc_port;
} probe_connection_t;

/*
 * calibrate_main
 */
int calibrate_main(int argc, char **argv)
{
    double f_min = 50.0e+3;
    double f_max = 60.0e+6;
    const char *opt_D = NULL;
    char opt_l = '\000';
    int opt_n = 50;
    const char *opt_s = "RB";
    const char *opt_S = "SOLT";
    const char *opt_t = "E12";
    double complex opt_z = 50.0;
    cal_step_list_t *calibration_steps = NULL;
    char c;
    vnacal_type_t c_type;
    int c_rows, c_columns;
    char *filename = NULL;
    vnacal_t *vcp = NULL;
    vnacal_new_t *vnp = NULL;
    setup_t *setup;
    probe_connection_t pc_cur1 = { NULL, 0 };
    probe_connection_t pc_cur2 = { NULL, 0 };
    measurement_args_t ma;
    measurement_result_t mr;
    bool need_frequency_vector = true;
    int end;
    int rc = -1;

    /*
     * Init the measurement result structure.
     */
    (void)memset((void *)&mr, 0, sizeof(mr));

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
	    if (sscanf(optarg, "%lf : %lf %c", &f_min, &f_max, &c) != 2) {
		message_error("frequency range format is: MHz_Min:MHz_Max\n");
		gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
		return -1;
	    }
	    if (f_min < 0.0 || f_min > f_max) {
		message_error("invalid frequency range\n");
		gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
		return -1;
	    }
	    f_min *= 1.0e+6;
	    f_max *= 1.0e+6;
	    continue;

	case 'h':
	    print_usage(usage, help);
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
		message_error("expected positive integer for frequencies\n");
		gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
		return -1;
	    }
	    continue;

	case 's':
	    opt_s = optarg;
	    continue;

	case 'S':
	    opt_S = optarg;
	    continue;

	case 't':
	    opt_t = optarg;
	    continue;

	case 'z':
	    {
		double r, i;

		switch (scanf("%lf %lf", &r, &i)) {
		case 1:
		    opt_z = r;
		    break;

		case 2:
		    opt_z = r + i * I;
		    break;

		default:
		    message_error("%s: invalid system impedance\n", optarg);
		    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
		    return -1;
		}
	    }
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
    if (argc != 1) {
	print_usage(usage, help);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
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
     * Look-up the setup.
     */
    if ((setup = setup_lookup(opt_s)) == NULL) {
	message_error("vna setup %s not found; run config to create\n", opt_s);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
    }
    c_rows    = setup->su_rows;
    c_columns = setup->su_columns;

    /*
     * Look-up the error term type.
     */
    if ((c_type = vnacal_name_to_type(opt_t)) == VNACAL_NOTYPE) {
	message_error("%s: invalid error paramter type\n", opt_t);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
    }

    /*
     * Create vnacal_t and vnacal_new_t structures.
     */
    if ((vcp = vnacal_create(print_libvna_error, NULL)) == NULL) {
	goto out;
    }
    if ((vnp = vnacal_new_alloc(vcp, c_type, c_rows, c_columns,
		    opt_n)) == NULL) {
	goto out;
    }
    if (vnacal_new_set_z0(vnp, opt_z) == -1) {
	goto out;
    }

    /*
     * Parse the calibration steps string.
     */
    if ((calibration_steps = cal_standards_parse(gs.gs_vnap,
		    setup, vcp, opt_S)) == NULL) {
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	goto out;
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
		    n2pkvna_get_directory(gs.gs_vnap), argv[0]) == -1) {
	    (void)fprintf(stderr, "%s: asprintf: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }

    /*
     * Init the measurement arguments.
     */
    (void)memset((void *)&ma, 0, sizeof(ma));
    ma.ma_setup		= setup;
    ma.ma_fmin		= f_min;
    ma.ma_fmax		= f_max;
    ma.ma_frequencies	= opt_n;
    ma.ma_rows		= c_rows;
    ma.ma_columns	= c_columns;
    ma.ma_linear	= opt_l == 'l';
    ma.ma_colsys	= c_type == VNACAL_E12 || c_type == VNACAL_UE14;
    ma.ma_z0		= opt_z;

    /*
     * Set the attenuation to zero.
     */
    if (n2pkvna_switch(gs.gs_vnap, -1, 0, SWITCH_DELAY) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	goto out;
    }
    gs.gs_attenuation = 0;

    /*
     * For each standard...
     */
    for (cal_step_t *cstp = calibration_steps->csl_steps; cstp != NULL;
	    cstp = cstp->cst_next) {
	probe_connection_t pc1;
	probe_connection_t pc2;

	/*
	 * Print instructions.
	 */
	switch (cstp->cst_type) {
	case CALS_SINGLE_REFLECT:
	case CALS_DOUBLE_REFLECT:
	    pc1.pc_standard = cstp->cst_standards[0];
	    pc1.pc_port     = cstp->cst_standards[0] != NULL ? 1 : 0;
	    pc2.pc_standard = cstp->cst_standards[1];
	    pc2.pc_port     = cstp->cst_standards[1] != NULL ? 1 : 0;
	    break;

	case CALS_THROUGH:
	case CALS_LINE:
	    pc1.pc_standard = cstp->cst_standards[0];
	    pc1.pc_port     = 1;
	    pc2.pc_standard = cstp->cst_standards[0];
	    pc2.pc_port     = 2;
	    break;

	default:
	    abort();
	}
	if (pc1.pc_standard == pc_cur1.pc_standard &&
		pc1.pc_port == pc_cur1.pc_port &&
	    pc2.pc_standard == pc_cur2.pc_standard &&
		pc2.pc_port == pc_cur2.pc_port) {
	    /*NULL*/;

	} else if (pc1.pc_standard == pc_cur2.pc_standard &&
			pc1.pc_port == pc_cur2.pc_port &&
		   pc2.pc_standard == pc_cur1.pc_standard &&
			pc2.pc_port == pc_cur1.pc_port) {
	    message_add_instruction("Swap VNA probes 1 & 2.\n");

	} else if (cstp->cst_type == CALS_THROUGH) {
	    message_add_instruction("Connect VNA probes 1 & 2 to the "
		    "through standard.\n");

	} else if (cstp->cst_type == CALS_LINE) {
	    message_add_instruction("Connect VNA probe 1 to %s port 1.\n",
		cstp->cst_standards[0]->cs_text);
	    message_add_instruction("Connect VNA probe 2 to %s port 2.\n",
		cstp->cst_standards[0]->cs_text);

	} else {
	    if (pc1.pc_standard != pc_cur1.pc_standard ||
		    pc1.pc_port     != pc_cur1.pc_port) {
		message_add_instruction("Connect VNA probe 1 to %s.\n",
		    pc1.pc_standard->cs_text);
	    }
	    if ((pc2.pc_standard != pc_cur2.pc_standard ||
		     pc2.pc_port     != pc_cur2.pc_port)) {
		message_add_instruction("Connect VNA probe 2 to %s.\n",
		    pc2.pc_standard->cs_text);
	    }
	}
	if (make_measurements(&ma, &mr) == -1) {
	    goto out;
	}
	if (need_frequency_vector) {
	    if (vnacal_new_set_frequency_vector(vnp,
			mr.mr_frequency_vector) == -1) {
		goto out;
	    }
	    need_frequency_vector = false;
	}
	pc_cur1 = pc1;
	pc_cur2 = pc2;
	switch (cstp->cst_type) {
	case CALS_SINGLE_REFLECT:
	    if (cstp->cst_standards[0] != &cs_terminator) {
		if (vnacal_new_add_single_reflect(vnp,
			    mr.mr_a_matrix, mr.mr_a_rows, mr.mr_a_columns,
			    mr.mr_b_matrix, mr.mr_b_rows, mr.mr_b_columns,
			    cstp->cst_standards[0]->cs_matrix[0][0], 1) == -1) {
		    goto out;
		}
	    } else {
		if (vnacal_new_add_single_reflect(vnp,
			    mr.mr_a_matrix, mr.mr_a_rows, mr.mr_a_columns,
			    mr.mr_b_matrix, mr.mr_b_rows, mr.mr_b_columns,
			    cstp->cst_standards[1]->cs_matrix[0][0], 2) == -1) {
		    goto out;
		}
	    }
	    break;

	case CALS_DOUBLE_REFLECT:
		if (vnacal_new_add_double_reflect(vnp,
			    mr.mr_a_matrix, mr.mr_a_rows, mr.mr_a_columns,
			    mr.mr_b_matrix, mr.mr_b_rows, mr.mr_b_columns,
			    cstp->cst_standards[0]->cs_matrix[0][0],
			    cstp->cst_standards[1]->cs_matrix[0][0],
			    1, 2) == -1) {
		    goto out;
		}
		break;

	case CALS_THROUGH:
		if (vnacal_new_add_through(vnp,
			    mr.mr_a_matrix, mr.mr_a_rows, mr.mr_a_columns,
			    mr.mr_b_matrix, mr.mr_b_rows, mr.mr_b_columns,
			    1, 2) == -1) {
		    goto out;
		}
		break;

	case CALS_LINE:
		if (vnacal_new_add_line(vnp,
			    mr.mr_a_matrix, mr.mr_a_rows, mr.mr_a_columns,
			    mr.mr_b_matrix, mr.mr_b_rows, mr.mr_b_columns,
			    &cstp->cst_standards[0]->cs_matrix[0][0],
			    1, 2) == -1) {
		    goto out;
		}
		break;

	default:
	    abort();
	}
	measurement_result_free(&mr);
    }

    /*
     * Solve for the error parameters, add to the calibration and save.
     */
    if (vnacal_new_solve(vnp) == -1) {
	goto out;
    }
    if (vnacal_add_calibration(vcp, argv[0], vnp) == -1) {
	goto out;
    }
    {
	time_t t;
	struct tm tm;
	char tbuf[32];

	(void)time(&t);
	(void)localtime_r(&t, &tm);
	(void)strftime(tbuf, sizeof(tbuf), "%Y-%m-%d_%H:%M:%S%z", &tm);
	if (vnacal_property_set(vcp, 0, "date=%s", tbuf) == -1) {
	    goto out;
	}
    }
    if (opt_D != NULL) {
	if (vnacal_property_set(vcp, 0, "description=%s", opt_D) == -1) {
	    goto out;
	}
    }
    if (vnacal_property_set(vcp, 0, "frequencySpacing=%s",
		opt_l == 'l' ? "linear" : "log") == -1) {
	goto out;
    }
    if (vnacal_property_set(vcp, 0, "setupName=%s", opt_s) == -1) {
	goto out;
    }
    {
	vnaproperty_t **rootptr = n2pkvna_get_property_root(gs.gs_vnap);
	vnaproperty_t *source, **destination;

	source = vnaproperty_get_subtree(*rootptr, "setups.%s", opt_s);
	destination = vnacal_property_set_subtree(vcp, 0, "setup");
	if (destination == NULL) {
	    (void)fprintf(stderr, "%s: vnacal_set_subtree: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_copy(destination, source) == -1) {
	    (void)fprintf(stderr, "%s: vnacal_set_subtree: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    if (vnacal_save(vcp, filename) == -1) {
	goto out;
    }
    end = vnacal_get_calibration_end(vcp);
    for (int ci = 0; ci < end; ++ci) {
	vnaproperty_t **subptr = NULL;
	vnaproperty_t **properties = NULL;
	const char *name;
	vnacal_type_t type;
	int rows, columns, frequencies;
	double fmin, fmax;

	if ((name = vnacal_get_name(vcp, ci)) == NULL) {
	    continue;
	}
	type	    = vnacal_get_type(vcp, ci);
	rows        = vnacal_get_rows(vcp, ci);
	columns     = vnacal_get_columns(vcp, ci);
	frequencies = vnacal_get_frequencies(vcp, ci);
	fmin        = vnacal_get_fmin(vcp, ci);
	fmax        = vnacal_get_fmax(vcp, ci);

	if ((subptr = vnaproperty_set_subtree(&gs.gs_messages,
			"calibrations[+]")) == NULL) {
	    (void)fprintf(stderr, "%s: vnaproperty_set_subtree: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(subptr, "name=%s", name) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(subptr, "type=%s",
		    vnacal_type_to_name(type)) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(subptr, "rows=%d", rows) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(subptr, "columns=%d", columns) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(subptr, "frequencies=%d", frequencies) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(subptr, "fmin=%e", fmin) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(subptr, "fmax=%e", fmax) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if ((properties = vnaproperty_set_subtree(subptr,
			"properties")) == NULL) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_copy(properties,
		    vnacal_property_get_subtree(vcp, ci, ".")) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    rc = 0;

out:
    measurement_result_free(&mr);
    vnacal_new_free(vnp);
    vnacal_free(vcp);
    if (filename != argv[0]) {
	free((void *)filename);
    }
    return rc;
}
