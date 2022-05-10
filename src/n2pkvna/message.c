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

#include <sys/types.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vnacal.h>

#include "main.h"

/*
 * prompt_for_ready: ask user to hit enter when ready
 */
static int prompt_for_ready()
{
    char buf[33];

    buf[32] = '\000';
    for (;;) {
	if (!gs.gs_opt_Y) {
	    (void)printf("Enter when ready> ");
	}
	if (fgets(buf, sizeof(buf) - 1, stdin) == NULL) {
	    return -1;
	}
	if (strchr(buf, '\n') == NULL) {
	    int i;
	    while ((i = getchar()) != '\n') {
		if (i == EOF) {
		    return -1;
		}
	    }
	}
	if (buf[0] == '\n') {
	    return 0;
	}
	if (strchr("qQxX", buf[0]) != NULL && buf[1] == '\n') {
	    return -1;
	}
	if (strncmp(buf, "exit", 4) == 0) {
	    return -1;
	}
	if (strncmp(buf, "quit", 4) == 0) {
	    return -1;
	}
	(void)printf("Unexpected response.\n\n");
    }
}

/*
 * message_add_instruction: add a step to to list of instructions
 *   @format: printf-like format
 *   @...:    arguments for format
 */
void message_add_instruction(const char *format, ...)
{
    va_list ap;
    char *text = NULL;
    char *cp;

    /*
     * Format the message.
     */
    va_start(ap, format);
    if (vasprintf(&text, format, ap) == -1) {
	(void)fprintf(stderr, "%s: vasprintf: %s\n", progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    va_end(ap);

    /*
     * Remove final newline.
     */
    cp = text + strlen(text);
    if (cp[-1] == '\n') {
	cp[-1] = '\000';
    }

    /*
     * Add it to the list.
     */
    if (vnaproperty_set(&gs.gs_messages, "instructions[+]=%s", text) == -1) {
	(void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    gs.gs_need_ack = true;

    free((void *)text);
}

/*
 * message_verror_np: report an error message without progname
 *   @format: printf-like format
 *   @ap:     variable argument pointer
 */
static void message_verror_np(const char *format, va_list ap)
{
    const char *existing;
    char *text = NULL;

    /*
     * Without -Y, just print the message.
     */
    if (!gs.gs_opt_Y) {
	FILE *fp = gs.gs_interactive ? stdout : stderr;

	(void)vfprintf(fp, format, ap);
	return;
    }

    /*
     * Format the new message.
     */
    if (vasprintf(&text, format, ap) == -1) {
	(void)fprintf(stderr, "%s: vasprintf: %s\n", progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }

    /*
     * If there's already an old value, concatenate the strings.
     */
    if ((existing = vnaproperty_get(gs.gs_messages, "errors")) != NULL) {
	char *new_text;
	size_t length1 = strlen(existing), length2 = strlen(text);

	if ((new_text = malloc(length1 + length2 + 1)) == NULL) {
	    (void)fprintf(stderr, "%s: malloc: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	(void)memcpy((void *)new_text, existing, length1);
	(void)memcpy((void *)&new_text[length1], text, length2);
	new_text[length1 + length2] = '\000';
	free((void *)text);
	text = new_text;
    }

    /*
     * Set and clean up.
     */
    if (vnaproperty_set(&gs.gs_messages, "errors=%s", text) == -1) {
	(void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    free((void *)text);
}

/*
 * message_error_np: report an error message without progname
 */
void message_error_np(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    message_verror_np(format, ap);
    va_end(ap);
}

/*
 * message_add_instruction: add a step to to list of instructions
 *   @format: printf-like format
 *   @...:    arguments for format
 */
void message_error(const char *format, ...)
{
    va_list ap;

    message_error_np("%s: ", gs.gs_command != NULL ? gs.gs_command : progname);
    va_start(ap, format);
    message_verror_np(format, ap);
    va_end(ap);

    if (!gs.gs_opt_Y) {
	FILE *fp = gs.gs_interactive ? stdout : stderr;

	(void)fprintf(fp, "\n");
    }
}

/*
 * add_calibrations
 */
static int add_calibrations()
{
    glob_t globbuf;
    char *pattern = NULL;
    bool free_globbuf = false;
    vnaproperty_t **subtree = NULL;
    int rc = -1;

    if (asprintf(&pattern, "%s/*.vnacal",
		n2pkvna_get_directory(gs.gs_vnap)) == -1) {
	(void)fprintf(stderr, "%s: asprintf: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    (void)memset((void *)&globbuf, 0, sizeof(globbuf));
    if (glob(pattern, 0, NULL, &globbuf) != 0) {
	goto out;
    }
    free_globbuf = true;
    if ((subtree = vnaproperty_set_subtree(&gs.gs_messages,
		    "calibration_files[]")) == NULL) {
	(void)fprintf(stderr, "%s: vnaproperty_set_subtree: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    for (size_t s = 0; s < globbuf.gl_pathc; ++s) {
	char *basename, *cp;
	vnacal_t *vcp;
	int end;
	vnaproperty_t **rootptr = NULL;

	if ((vcp = vnacal_load(globbuf.gl_pathv[s], NULL, NULL)) == NULL) {
	    continue;
	}
	if ((basename = strrchr(globbuf.gl_pathv[s], '/')) == NULL) {
	    basename = globbuf.gl_pathv[s];
	} else {
	    ++basename;
	}
	if ((cp = strrchr(basename, '.')) != NULL) {
	    *cp = '\000';
	}
	if ((rootptr = vnaproperty_set_subtree(subtree, "[+]")) == NULL) {
	    (void)fprintf(stderr, "%s: vnaproperty_set_subtree: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_set(rootptr, "calfile=%s", basename) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
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
	    type	= vnacal_get_type(vcp, ci);
	    rows        = vnacal_get_rows(vcp, ci);
	    columns     = vnacal_get_columns(vcp, ci);
	    frequencies = vnacal_get_frequencies(vcp, ci);
	    fmin        = vnacal_get_fmin(vcp, ci);
	    fmax        = vnacal_get_fmax(vcp, ci);

	    if ((subptr = vnaproperty_set_subtree(rootptr,
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
	vnacal_free(vcp);
    }

out:
    if (free_globbuf) {
	globfree(&globbuf);
    }
    free((void *)pattern);
    return rc;
}

/*
 * add_stock_standard
 */
static void add_stock_standard(int ports, const char *name, const char *text)
{
    vnaproperty_t **subtree;

    if ((subtree = vnaproperty_set_subtree(&gs.gs_messages,
		    "standards_%dport[+]{}", ports)) == NULL) {
	(void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    if (name != NULL) {
	if (vnaproperty_set(subtree, "name=%s", name) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    } else {
	if (vnaproperty_set(subtree, "name#") == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    if (text != NULL) {
	if (vnaproperty_set(subtree, "text=%s", text) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
}

/*
 * add_standards
 */
static int add_standards()
{
    const char *directory = n2pkvna_get_directory(gs.gs_vnap);
    char *pathname = NULL;
    char *basename;
    DIR *dirp = NULL;
    struct dirent *dp = NULL;
    regex_t re;
    int errcode;
    bool regfree_needed = false;
    vnadata_t *vdp = NULL;
    char *cp;
    int rc = -1;

    if ((pathname = malloc(strlen(directory) + 1 + 256 + 1)) == NULL) {
	(void)fprintf(stderr, "%s: malloc: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    (void)strcpy(pathname, directory);
    basename = pathname + strlen(pathname);
    *basename++ = '/';
    if ((errcode = regcomp(&re, "^[A-Za-z_][A-Za-z0-9_]*\\.(npd|ts|s2p|s1p)$",
		    REG_EXTENDED | REG_NOSUB)) != 0) {
	char errbuf[80];

	(void)regerror(errcode, &re, errbuf, sizeof(errbuf));
	errbuf[sizeof(errbuf) - 1] = '\000';
	message_error("regcomp: %s", errbuf);
	goto out;
    }
    if ((dirp = opendir(directory)) == NULL) {
	message_error("opendir: %s: %s", directory, strerror(errno));
	goto out;
    }
    if ((vdp = vnadata_alloc(NULL, NULL)) == NULL) {
	goto out;
    }
    add_stock_standard(1, "S", "(S)hort");
    add_stock_standard(1, "O", "(O)pen");
    add_stock_standard(1, "M", "(M)atch");
    add_stock_standard(1, NULL, "terminator");
    add_stock_standard(2, "T", "(T)hrough");
    while ((dp = readdir(dirp)) != NULL) {
	if (regexec(&re, dp->d_name, 0, NULL, 0) == 0) {
	    int ports;
	    vnaproperty_t **subtree;

	    (void)strcpy(basename, dp->d_name);
	    if (vnadata_load(vdp, pathname) == -1) {
		continue;
	    }
	    if ((ports = vnadata_get_rows(vdp)) != 1 && ports != 2) {
		continue;
	    }
	    /* expensive way to test if the parameters are convertable to S */
	    if (vnadata_convert(vdp, vdp, VPT_S) == -1) {
		continue;
	    }
	    if ((cp = strrchr(basename, '.')) != NULL) {
		*cp = '\000';
	    }
	    if ((subtree = vnaproperty_set_subtree(&gs.gs_messages,
			    "standards_%dport[+]{}", ports)) == NULL) {
		(void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
			progname, strerror(errno));
		exit(N2PKVNA_EXIT_SYSTEM);
	    }
	    if (vnaproperty_set(subtree, "name=%s", basename) == -1) {
		(void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
			progname, strerror(errno));
		exit(N2PKVNA_EXIT_SYSTEM);
	    }
	    if (vnaproperty_set(subtree, "fmin=%e",
			vnadata_get_fmin(vdp)) == -1) {
		(void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
			progname, strerror(errno));
		exit(N2PKVNA_EXIT_SYSTEM);
	    }
	    if (vnaproperty_set(subtree, "fmax=%e",
			vnadata_get_fmax(vdp)) == -1) {
		(void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
			progname, strerror(errno));
		exit(N2PKVNA_EXIT_SYSTEM);
	    }
	}
    }
    rc = 0;

out:
    vnadata_free(vdp);
    if (regfree_needed) {
	regfree(&re);
    }
    if (dirp != NULL) {
	(void)closedir(dirp);
    }
    free((void *)pathname);
    return rc;
}

/*
 * message_get_config: include config properties in response
 */
void message_get_config()
{
    vnaproperty_t **destination;

    /*
     * Add the configuration directory name.
     */
    if (vnaproperty_set(&gs.gs_messages, "config_dir=%s",
		n2pkvna_get_directory(gs.gs_vnap))) {
	(void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }

    /*
     * Add the properties.
     */
    if ((destination = vnaproperty_set_subtree(&gs.gs_messages,
		    "properties")) == NULL) {
	(void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    if (vnaproperty_copy(destination,
		*n2pkvna_get_property_root(gs.gs_vnap)) == -1) {
	(void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }

    /*
     * Add the calibrations and standards.
     */
    add_calibrations();
    add_standards();
}

/*
 * message_wait_for_acknowledgement: wait for a newline from the user
 */
int message_wait_for_acknowledgement()
{
    int count = vnaproperty_count(gs.gs_messages, "instructions[]");
    int rc;

    /*
     * If not -Y, print the instructions
     */
    if (!gs.gs_opt_Y) {
	if (count == 1) {
	    (void)printf("%s\n", vnaproperty_get(gs.gs_messages,
			"instructions[0]"));
	    printf("\n");

	} else if (count > 1) {
	    for (int i = 0; i < count; ++i) {
		const char *message = vnaproperty_get(gs.gs_messages,
			"instructions[%d]", i);

		(void)printf("- ");
		for (const char *cp = message; *cp != '\000'; ++cp) {
		    putchar(cp[0]);
		    if (cp[0] == '\n' && cp[1] != '\n') {
			(void)printf("  ");
		    }
		}
		printf("\n");
	    }
	    printf("\n");
	}
	(void)vnaproperty_delete(&gs.gs_messages, ".");

    } else {
	/*
	 * Set status to needsACK, write the YAML doc and reset messages.
	 */
	if (vnaproperty_set(&gs.gs_messages, "status=needsACK") == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_export_yaml_to_file(gs.gs_messages, stdout, "-",
		    print_libvna_error, NULL) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_export_yaml_to_file: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	(void)fflush(stdout);
	(void)vnaproperty_delete(&gs.gs_messages, ".");
    }
    rc = prompt_for_ready();
    if (rc != 0) {
	gs.gs_canceled = true;
	gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
    }
    gs.gs_need_ack = false;
    return rc;
}

/*
 * message_prompt: flush current response and prompt for next command
 */
void message_prompt()
{
    /*
     * If not -Y, just prompt if ready.
     */
    if (!gs.gs_opt_Y) {
	(void)printf("%s> ", gs.gs_command != NULL ?
		gs.gs_command : progname);
	return;
    }

    /*
     * Set the status.
     */
    if (vnaproperty_type(gs.gs_messages, "errors") == 's') {
	if (vnaproperty_set(&gs.gs_messages, "status=error") == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    } else if (gs.gs_canceled) {
	if (vnaproperty_set(&gs.gs_messages, "status=canceled") == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    } else {
	if (vnaproperty_set(&gs.gs_messages, "status=ok") == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    gs.gs_canceled = false;

    /*
     * Print the YAML document and reset gs_messages.
     */
    if (vnaproperty_export_yaml_to_file(gs.gs_messages, stdout, "-",
		print_libvna_error, NULL) == -1) {
	(void)fprintf(stderr, "%s: vnaproperty_export_yaml_to_file: %s\n",
		progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    (void)fflush(stdout);
    (void)vnaproperty_delete(&gs.gs_messages, ".");
}

/*
 * message_get_measured_frequency: prompt for frequency measurement
 *   @measured: returned frequency in MHz
 */
int message_get_measured_frequency(double *measured)
{
    char line[80];
    char *end;

    if (!gs.gs_opt_Y) {
	(void)printf("measured frequency (MHz) ? ");

    } else {
	if (vnaproperty_set(&gs.gs_messages, "status=needsMeasuredF") == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_set: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if (vnaproperty_export_yaml_to_file(gs.gs_messages, stdout, "-",
		    print_libvna_error, NULL) == -1) {
	    (void)fprintf(stderr, "%s: vnaproperty_export_yaml_to_file: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	(void)fflush(stdout);
	(void)vnaproperty_delete(&gs.gs_messages, ".");
    }
    if (fgets(line, sizeof(line) - 1, stdin) == NULL) {
	goto cancel;
    }
    *measured = strtod(line, &end);
    if (end == line) {
	goto cancel;
    }
    return 0;

cancel:
    gs.gs_canceled = true;
    gs.gs_exitcode = N2PKVNA_EXIT_CANCEL;
    return -1;
}
