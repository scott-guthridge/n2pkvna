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
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cli.h"
#include "main.h"
#include "message.h"
#include "properties.h"
#include "setup.h"


/*
 * n2pkvna setup options
 */
static const char short_options[] = "h";
static const struct option long_options[] = {
    { "help",			0, NULL, 'h' },
    { NULL,			0, NULL,  0  }
};
static const char *const usage[] = {
    " -h|--help         print this help message",
    "[setup-command [args]]",
    NULL
};
static const char *const help[] = {
    "Setup commands:",
    "?|help",
    "  show this help message",
    "",
#if 0
    "RB",
    "  quick setup for reflection bridge"
    "",
    "RFIV",
    "  quick setup for RFIV test set",
    "",
    "S",
    "  quick setup for full S parameter test set",
    "",
    "create name",
    "  create a new VNA setup (advanced version)",
    "",
    "delete name",
    "  deleted the named VNA setup",
    ""
    "diable name",
    "  disable (and hide) the named VNA setup",
    "",
    "edit name",
    "  edit an existing VNA setup (advanced version)",
    "",
    "enable name",
    "  enable the named VNA setup",
    "",
    "list",
    "  list all VNA setups",
    "",
    "ydump",
    "  dump all VNA setups in YAML",
    "",
#endif
    "yload"
    "  update VNA setups from YAML",
    "",
    NULL
};

/*
 * setup_RB_main: quick setup for reflection bridge
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_RB_main(int argc, char **argv)
{
    (void)printf("This command is not yet implemented.  Use GUI.\n");
    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ
#if 0
    cli_scan_t css;
    int rc = -1;
    int b11_detector, b21_detector;
    int b11_switch = -1, b21_switch = -1;
    bool uses_switch = false;

    cli_scan_init(&css);

    /*
     * Get detectors.
     */
    for (;;) {
	(void)printf("Which detector measures b11? [1] ");
	if (cli_scan(&css, &argc, &argv) == -1) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    goto out;
	}
	if (argc == 0) {
	    b11_detector = 1;
	    break;
	}
	if (is_quit(argv[0])) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    goto out;
	}
	b11_detector = atoi(argv[0]);
	if (b11_detector == 1 || b11_detector == 2)
	    break;

	message_error("Error: answer must be 1, 2 or quit.\n");
    }
    for (;;) {
	int default_value = (b11_detector == 1) ? 2 : 1;

	(void)printf("Which detector measures b21? [%d] ", default_value);
	if (cli_scan(&css, &argc, &argv) == -1) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    goto out;
	}
	if (argc == 0) {
	    b21_detector = default_value;
	    break;
	}
	if (is_quit(argv[0])) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    goto out;
	}
	b21_detector = atoi(argv[0]);
	if (b21_detector == 1 || b21_detector == 2)
	    break;

	message_error("Error: answer must be 1, 2 or quit.\n");
    }

    /*
     * Handle the normal case of different detectors.
     */
    if (b11_detector != b21_detector) {
	measurement_t *mp;
	mstep_t *msp;
	setup_t *sup;
	vector_code_t codes[2];

	codes[b11_detector - 1] = VC_B11;
	codes[b21_detector - 1] = VC_B21;

	if ((sup = setup_alloc("RB", 2, 1)) == NULL) {
	    goto out;
	}
	if ((msp = setup_add_mstep(sup, NULL, NULL)) == NULL) {
	    setup_free(sup);
	    goto out;
	}
	if ((mp = mstep_add_measurement(msp, -1, codes[0], codes[1])) == NULL) {
	    setup_free(sup);
	    goto out;
	}
	setup_update(sup);
	rc = 0;
	goto out;
    }

    /*
     * Ask if switches are used.  If so, get the switch values.
     */
    for (;;) {
	(void)printf("Are reflection and transmission controlled by "
		"automatic switch? [no] ");
	if (cli_scan(&css, &argc, &argv) == -1) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    goto out;
	}
	if (argc == 0) {
	    uses_switch = false;
	    break;
	}
	if (is_quit(argv[0])) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    goto out;
	}
	switch (argv[0][0]) {
	case 'N':
	case 'n':
	    uses_switch = false;
	    break;

	case 'Y':
	case 'y':
	    uses_switch = true;
	    break;

	default:
	    message_error("Error: answer yes, no or quit.\n");
	    continue;
	}
	break;
    }
    if (uses_switch) {
	for (;;) {
	    (void)printf("Which switch setting measures reflection? [0] ");
	    if (cli_scan(&css, &argc, &argv) == -1) {
		gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
		goto out;
	    }
	    if (argc == 0) {
		b11_switch = 0;
		break;
	    }
	    if (is_quit(argv[0])) {
		gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
		goto out;
	    }
	    b11_switch = atoi(argv[0]);
	    if (b11_switch >= 0 && b11_switch <= 3)
		break;

	    message_error("Error: answer must be 0-3 or quit.\n");
	}
	for (;;) {
	    int default_value = (b11_switch == 0) ? 1 : 0;

	    (void)printf("Which switch setting measures "
		    "transmission? [%d] ", default_value);
	    if (cli_scan(&css, &argc, &argv) == -1) {
		gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
		goto out;
	    }
	    if (argc == 0) {
		b21_switch = default_value;
		break;
	    }
	    if (is_quit(argv[0])) {
		gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
		goto out;
	    }
	    b21_switch = atoi(argv[0]);
	    if (b21_switch >= 0 && b21_switch <= 3)
		break;

	    message_error("Error: answer must be 0-3 or quit.\n");
	}
    }
    //ZZ: create setup
    //    if switches are different
    //      add mstep
    //        add measurement1
    //        add measurement2
    //      else
    //        add mstep "reflection"
    //          add measurement1
    //        add mstep "transmission"
    //          add measurement2
    rc = 0;

out:
    if (rc == 0) {
	if (properties_save() == -1) {
	    gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	}
    }
    cli_scan_free(&css);
    return rc;
#endif
}

/*
 * setup_RFIV_main: quick setup for single RF I/V test set
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_RFIV_main(int argc, char **argv)
{
    (void)printf("This command is not yet implemented.  Use GUI.\n");
    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ
}

/*
 * setup_S_main: quick setup for full S parameter test set
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_S_main(int argc, char **argv)
{
    (void)printf("This command is not yet implemented.  Use GUI.\n");
    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ
}

/*
 * edit: create/edit the named configuration
 */
static int edit(const char *name, bool create)
{
    (void)printf("This command is not yet implemented.  Use GUI.\n");
    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ

#if 0
    setup_t *sup_old;
    setup_t *sup = NULL;
    cli_scan_t css;
    int argc;
    char **argv;
    int rows = 2, columns = 1;
    int msteps = 0;
    int detectors = 2;
    bool uses_switch = false;
    mstep_t *msp_old = NULL;
    int rc = -1;

    cli_scan_init(&css);

    /*
     * If the named setup exists, analyze the existing setup for defaults.
     */
    sup_old = setup_lookup(name);
    if (sup_old != NULL) {
	bool second_detector_used = false;

	if (create) {
	    message_error("create: %s already exists -- "
		    "use edit\n", argv[1]);
	    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	    return -1;
	}
	rows    = sup_old->su_rows;
	columns = sup_old->su_columns;
	for (mstep_t *msp = sup_old->su_steps; msp != NULL;
		msp = msp->ms_next) {
	    ++msteps;
	    for (measurement_t *mp = msp->ms_measurements; mp != NULL;
		    mp = mp->m_next) {
		if (mp->m_switch != -1) {
		    uses_switch = true;
		}
		if (mp->m_detectors[1] != VC_NONE) {
		    second_detector_used = true;
		}
	    }
	}
	if (msteps == 1 && sup_old->su_steps->ms_name == NULL &&
		sup_old->su_steps->ms_text == NULL) {
	    msteps = 0;
	}
	if (!second_detector_used) {
	    detectors = 1;
	}

    /*
     * Otherwise, if not create, report error.
     */
    } else if (!create) {
	message_error("edit: %s doesn't exist -- use create\n", argv[1]);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
    }

    /*
     * Get the setup dimensions.
     */
    for (;;) {
	(void)printf("1x1: setup measures reflection only\n");
	(void)printf("1x2: setup measures S11 & S12 only\n");
	(void)printf("2x1: setup measures S11 & S21 only\n");
	(void)printf("2x2: setup measures full S parameters\n");
	(void)printf("Enter setup dimensions: [%dx%d] ", rows, columns);
	if (cli_scan(&css, &argc, &argv) == -1) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    rc = 0;
	    goto out;
	}
	if (argc == 0) {
	    break;
	}
	if (is_quit(argv[0])) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    rc = 0;
	    goto out;
	}
	if (sscanf(argv[1], "%d x %d", &rows, &columns) == 2 &&
		rows >= 1 && rows <= 2 && columns >= 1 && columns <= 2) {
	    break;
	}
	message_error("Error: answer must be 1x1, 1x2, 2x1, or 2x2\n");
    }

    /*
     * Get the number of manual steps.
     */
    for (;;) {
	char *end;

	(void)printf("How many manual steps, e.g. reflection, transmission "
		"are needed to\n");
	(void)printf("perform the measurement?  Note: don't include swapping "
		"of the DUT\n");
	(void)printf("ports when measuring with a 2x1 setup [%d]\n", msteps);
	if (cli_scan(&css, &argc, &argv) == -1) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    rc = 0;
	    goto out;
	}
	if (argc == 0) {
	    break;
	}
	msteps = (int)strtol(argv[1], &end, 10);
	if (end != argv[1] && msteps >= 0) {
	    break;
	}
	message_error("Error: answer must be a non-negative number\n");
    }

    /*
     * Get the number of VNA detectors.
     */
    for (;;) {
	char *end;

	(void)printf("How many detectors does the VNA have in this "
		"setup? [%d] ", detectors);
	if (cli_scan(&css, &argc, &argv) == -1) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    rc = 0;
	    goto out;
	}
	if (argc == 0) {
	    break;
	}
	detectors = (int)strtol(argv[1], &end, 10);
	if (end != argv[1] && detectors >= 1 && detectors <= 2) {
	    break;
	}
	message_error("Error: answer must be 1 or 2\n");
    }

    /*
     * Ask if switches are used.
     */
    for (;;) {
	(void)printf("Does this setup use automatic switches? [%s] ",
		uses_switch ? "yes" : "no");
	if (cli_scan(&css, &argc, &argv) == -1) {
	    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
	    rc = 0;
	    goto out;
	}
	if (argc == 0) {
	    break;
	}
	switch (argv[0][0]) {
	case 'N':
	case 'n':
	    uses_switch = false;
	    break;

	case 'Y':
	case 'y':
	    uses_switch = true;
	    break;

	default:
	    message_error("Error: answer yes, no or quit.\n");
	    continue;
	}
    }

    /*
     * Create the new setup.
     */
    if ((sup = setup_alloc(name, rows, columns)) == NULL) {
	goto out;
    }

    if (sup_old != NULL) {
	msp_old = sup_old->su_steps;
    }
    for (int mstep = 0; mstep < MAX(1, msteps); ++mstep,
	    msp_old = (msp_old == NULL) ? NULL : msp_old->ms_next) {
	const char *name = NULL;
	mstep_t *msp;
	int switch_settings = 0;
	measurement_t *mp_old = NULL;
	uint32_t used_switch_mask = 0;

	/*
	 * If there are manual steps, get the step name.
	 */
	if (msteps != 0) {
	    const char *default_value = msp_old->ms_name;

	    for (;;) {
		(void)printf("What is the name of manual step %d? ", mstep + 1);
		if (default_value != NULL) {
		    (void)printf("[%s] ", default_value);
		}
		if (cli_scan(&css, &argc, &argv) == -1) {
		    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
		    rc = 0;
		    goto out;
		}
		if (argc == 0) {
		    if (default_value != NULL) {
			name = default_value;
			break;
		    }
		    continue;
		}
		if (is_quit(argv[0])) {
		    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
		    rc = 0;
		    goto out;
		}
		name = argv[0];
		break;
	    }
	}

	/*
	 * Add the mstep_t structure.
	 */
	if ((msp = setup_add_mstep(sup, name, NULL)) == NULL) {
	    goto out;
	}

	/*
	 * Get the number of switch settings used in this step.
	 */
	if (uses_switch) {
	    int default_switch_settings = (msteps > 1) ? 1 : 2;

	    if (msp_old != NULL) {
		int temp = 0;

		for (measurement_t *mp = msp_old->ms_measurements;
			mp != NULL; mp = mp->m_next) {
		    ++temp;
		}
		default_switch_settings = temp;
	    }
	    for (;;) {
		char *end;

		(void)printf("How many switch settings are used in "
			"this step? [%d] ", default_switch_settings);
		if (cli_scan(&css, &argc, &argv) == -1) {
		    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
		    rc = 0;
		    goto out;
		}
		if (argc == 0) {
		    switch_settings = default_switch_settings;
		    break;
		}
		switch_settings = (int)strtol(argv[1], &end, 10);
		if (end != argv[1] &&
			switch_settings >= 0 && switch_settings <= 4) {
		    break;
		}
		message_error("Error: answer must be between 0 and 4\n");
	    }
	}

	/*
	 * For each switch...
	 */
	if (msp_old != NULL) {
	    mp_old = msp_old->ms_measurements;
	}
	for (int sindex = 0; sindex < MAX(1, switch_settings); ++sindex,
		mp_old = (mp_old == NULL) ? NULL : mp_old->m_next) {
	    int switch_code = -1;

	    /*
	     * Get the switch code.
	     */
	    if (switch_settings != 0) {
		for (;;) {
		    int default_switch_code = 0;
		    char *end;

		    while ((1U << default_switch_code) & used_switch_mask) {
			++default_switch_code;
		    }
		    if (mp_old != NULL && mp_old->m_switch != -1 &&
			    !((1U << mp_old->m_switch) & used_switch_mask)) {
			default_switch_code = mp_old->m_switch;
		    }
		    (void)printf("Enter switch code %d: [%d] ",
			    sindex + 1, default_switch_code);
		    if (cli_scan(&css, &argc, &argv) == -1) {
			gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
			rc = 0;
			goto out;
		    }
		    if (argc == 0) {
			switch_code = default_switch_code;
			break;
		    }
		    switch_code = (int)strtol(argv[1], &end, 10);
		    if (end == argv[1] || switch_code < 0 || switch_code > 3) {
			message_error("Error: answer must be between "
				"0 and 3.\n");
			continue;
		    }
		    if ((1U << switch_code) & used_switch_mask) {
			message_error("Error: code %d was already used.\n",
				switch_code);
			continue;
		    }
		    break;
		}
		used_switch_mask |= 1U << switch_code;
	    }

	    /*
	     * Determine what each detector measures.
	     */
	    for (int detector = 0; detector < detectors; ++detector) {
		for (;;) {
		    (void)printf("What does detector %d measure? {a,b,i,v}",
			    detector + 1);
		    if (rows == 1) {
			(void)printf("1");
		    } else {
			(void)printf("{1,2}");
		    }
		    if (columns == 1) {
			(void)printf("1");
		    } else {
			(void)printf("{1,2}");
		    }
		    (void)printf("\n");
		    //ZZ: here
		    if (cli_scan(&css, &argc, &argv) == -1) {
			gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
			rc = 0;
			goto out;
		    }
		    if (argc == 0) {
			break;
		    }
		    if (is_quit(argv[0])) {
			gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
			rc = 0;
			goto out;
		    }
		    if (sscanf(argv[1], "%d x %d", &rows, &columns) == 2 &&
			    rows >= 1 && rows <= 2 &&
			    columns >= 1 && columns <= 2) {
			break;
		    }
		    message_error("Error: answer must be 1x1, 1x2, 2x1, "
			    "or 2x2\n");
		}
	    }
	}
    }

out:
    cli_scan_free(&css);
    setup_free(sup);
    return rc;
#endif
}

/*
 * setup_create_main: create a new VNA setup
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_create_main(int argc, char **argv)
{
    if (argc != 2) {
	print_usage(usage, help);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
    }
    if (edit(argv[1], true) == -1) {
	return -1;
    }
    return 0;
}

/*
 * setup_delete_main: delete a VNA setup
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_delete_main(int argc, char **argv)
{
    (void)printf("This command is not yet implemented.  Use GUI.\n");
    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ
}

/*
 * setup_disable_main: disable (and hide) a VNA setup
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_disable_main(int argc, char **argv)
{
    (void)printf("This command is not yet implemented.  Use GUI.\n");
    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ
}

/*
 * setup_edit_main: edit a VNA setup
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_edit_main(int argc, char **argv)
{
    if (argc != 2) {
	print_usage(usage, help);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
    }
    if (edit(argv[1], false) == -1) {
	return -1;
    }
    return 0;
}

/*
 * setup_enable_main: enable a VNA setup
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_enable_main(int argc, char **argv)
{
    (void)printf("This command is not yet implemented.  Use GUI.\n");
    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ
#if 0
    //ZZ: debug code only
    FILE *fp;
    vnaproperty_t *root = NULL;

    if ((fp = fopen("in.yml", "r")) == NULL) {
	(void)fprintf(stderr, "%s: fopen: %s: %s\n",
		progname, "in.yml", strerror(errno));
	return -1;
    }
    for (;;) {
	if (vnaproperty_import_yaml_from_file(&root, fp, "in.yml",
		    print_libvna_error, NULL) == -1) {
	    break;
	}
	(void)printf("START\n");
	(void)vnaproperty_export_yaml_to_file(root, stdout,
		"-", print_libvna_error, NULL);
	(void)printf("END\n");
	(void)fflush(stdout);
	{
	    char buf[100];
	    buf[0] = '\000';
	    if (fgets(buf, 100, fp) != NULL) {
		(void)printf("[%s]\n", buf);
		fflush(stdout);
		break;
	    } else if (feof(fp)) {
		(void)printf("EOF\n");
	    } else {
		(void)printf("OTHER\n");
	    }
	}

    }
    (void)vnaproperty_delete(&root, ".");
    (void)fclose(fp);

    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ
#endif
}

/*
 * setup_help_main: display the help text
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_help_main(int argc, char **argv)
{
    print_usage(usage, help);
    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ
}

/*
 * setup_list_main: list all VNA setups
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_list_main(int argc, char **argv)
{
    (void)printf("This command is not yet implemented.  Use GUI.\n");
    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ
}

/*
 * setup_ydump_main: dump all setups in YAML
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_ydump_main(int argc, char **argv)
{
    (void)printf("This command is not yet implemented.  Use GUI.\n");
    gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
    return -1; //ZZ
}

/*
 * setup_RB_main: updates setups from YAML
 *   @argc: argument count
 *   @argv: argument vector
 */
static int setup_yload_main(int argc, char **argv)
{
    vnaproperty_t *root = NULL;
    char *input_buffer = NULL;
    size_t allocation = 0;
    size_t length = 0;
    int state = 0;
    int cur;
    int rc = -1;

    while ((cur = getchar()) != EOF) {
	if (length == allocation) {
	    size_t new_allocation = (allocation == 0) ? 4096 : allocation * 2;
	    char *cp;

	    if ((cp = realloc(input_buffer, new_allocation)) == NULL) {
		message_error("malloc: %s\n", strerror(errno));
		exit(N2PKVNA_EXIT_SYSTEM);
	    }
	    input_buffer = cp;
	    allocation = new_allocation;
	}
	input_buffer[length++] = cur;

	/*
	 * Detect "...\n" and stop.
	 */
	switch (state) {
	case 0:
	case 1:
	case 2:
	    switch (cur) {
	    case '\n':
		state = 0;
		continue;
	    case '.':
		++state;
		continue;
	    default:
		state = 4;
		continue;
	    }
	    break;

	case 3:
	    switch (cur) {
	    case '\n':
		state = 5;
		break;
	    default:
		state = 4;
		continue;
	    }
	    break;

	case 4:
	    switch (cur) {
	    case '\n':
		state = 0;
		continue;
	    default:
		state = 4;
		continue;
	    }
	    break;
	}
	break;
    }
    input_buffer[length] = '\000';
    if (vnaproperty_import_yaml_from_string(&root, input_buffer,
	    &print_libvna_error, NULL) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	goto out;
    }
    if (parse_setups(root) == -1) {
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	goto out;
    }
    if (properties_save()) {
	gs.gs_exitcode = N2PKVNA_EXIT_VNAOP;
	goto out;
    }
    rc = 0;

out:
    (void)vnaproperty_delete(&root, ".");
    free((void *)input_buffer);
    return rc;
}

static command_t setup_commands[] = {
    { "?",		setup_help_main },
    { "RB",		setup_RB_main },
    { "RFIV",		setup_RFIV_main },
    { "S",		setup_S_main },
    { "create",		setup_create_main },
    { "delete",		setup_delete_main },
    { "disable",	setup_disable_main },
    { "edit",		setup_edit_main },
    { "enable",		setup_enable_main },
    { "help",		setup_help_main },
    { "list",		setup_list_main },
    { "ydump",		setup_ydump_main },
    { "yload",		setup_yload_main },
};
#define N_SETUP_COMMANDS	(sizeof(setup_commands) / sizeof(command_t))

/*
 * setup_main
 */
int setup_main(int argc, char **argv)
{
    /*
     * Parse options.
     */
    for (;;) {
	switch (getopt_long(argc, argv, short_options, long_options, NULL)) {
	case -1:
	    break;

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
     * Interpret commands.
     */
    return cli(setup_commands, N_SETUP_COMMANDS, "setup", argc, argv);
}
