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

#include "cli.h"
#include "main.h"
#include "message.h"

/*
 * cli_scan_init: init a cli_scan_t structure
 *   @cssp: scanner state
 */
void cli_scan_init(cli_scan_t *cssp)
{
    (void)memset((void *)cssp, 0, sizeof(*cssp));
    cssp->css_cur = '\n';
}

/*
 * cli_scan_free: free resources of the cli_scan_t structure
 */
void cli_scan_free(cli_scan_t *cssp)
{
    free((void *)cssp->css_buffer);
    free((void *)cssp->css_arg_index);
    free((void *)cssp->css_argv);
}

/*
 * scan_add_char: add c to the current word
 *   @cssp: scanner state
 *   @c: character to add
 */
static void scan_add_char(cli_scan_t *cssp, char c)
{
    if (cssp->css_buffer_length >= cssp->css_buffer_size) {
	if (cssp->css_buffer_size == 0) {
	    cssp->css_buffer_size = 128;
	} else {
	    cssp->css_buffer_size *= 2;
	}
	if ((cssp->css_buffer = realloc(cssp->css_buffer,
			cssp->css_buffer_size)) == NULL) {
	    (void)fprintf(stderr, "%s: realloc: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    cssp->css_buffer[cssp->css_buffer_length++] = c;
}

/*
 * scan_start_word: start a new argv entry
 *   @cssp: scanner state
 */
static void scan_start_word(cli_scan_t *cssp)
{
    if (cssp->css_argc >= cssp->css_arg_slots) {
	if (cssp->css_arg_slots == 0) {
	    cssp->css_arg_slots = 16;
	} else {
	    cssp->css_arg_slots *= 2;
	}
	if ((cssp->css_arg_index = realloc(cssp->css_arg_index,
			cssp->css_arg_slots * sizeof(char *))) == NULL) {
	    (void)fprintf(stderr, "%s: realloc: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    cssp->css_arg_index[cssp->css_argc++] = cssp->css_buffer_length;
}

/*
 * cli_scan: scan a command, breaking it into words with simple shell quoting
 *   @cssp: scanner state
 */
int cli_scan(cli_scan_t *cssp, int *argc, char ***argv)
{
    /*
     * Init return values.
     */
    *argc = 0;
    *argv = NULL;

    /*
     * Process the newline from the previous line.  For interactive
     * input, we don't want to read past the newline character until
     * the next command; otherwise the user would have to hit return
     * twice to get the command to run.
     */
    if (cssp->css_cur == '\n') {
	cssp->css_cur = getchar();
	cssp->css_buffer_length = 0;
	cssp->css_argc = 0;
    }

    /*
     * Run Duff's device state machine to parse words and quotes.
     */
    for (;;) {
	switch (cssp->css_state) {
	case 0:	/* not in word */
	    switch (cssp->css_cur) {
	    case EOF:			/* end of input */
		return -1;

	    case '\n':			/* end of current command */
		break;

	    case ' ':			/* whitespace */
	    case '\f':
	    case '\r':
	    case '\t':
	    case '\v':
		cssp->css_cur = getchar();
		continue;

	    case '\\':			/* backslash escape */
		cssp->css_cur = getchar();
		cssp->css_state = 1;
		continue;

	    case '\'':			/* start word on single quote */
		scan_start_word(cssp);
		cssp->css_cur = getchar();
		cssp->css_state = 4;
		continue;

	    case '\"':			/* start word on double quote */
		scan_start_word(cssp);
		cssp->css_cur = getchar();
		cssp->css_state = 5;
		continue;

	    default:			/* start word on other char */
		scan_start_word(cssp);
		scan_add_char(cssp, cssp->css_cur);
		cssp->css_cur = getchar();
		cssp->css_state = 2;
		continue;
	    }
	    break;

	case 1:	/* not in word, backslash */
	    switch (cssp->css_cur) {
	    case EOF:			/* backslash EOF: warn and ignore */
		message_error("warning: unexpected EOF after backslash");
		cssp->css_state = 0;
		continue;

	    case '\n':			/* backslash newline: ignore */
		cssp->css_cur = getchar();
		cssp->css_state = 0;
		continue;

	    default:			/* start word on backslashed char */
		scan_start_word(cssp);
		scan_add_char(cssp, cssp->css_cur);
		cssp->css_cur = getchar();
		cssp->css_state = 2;
		continue;
	    }
	    break;

	case 2:	/* in word */
	    switch (cssp->css_cur) {
	    case EOF:
	    case ' ':
	    case '\f':
	    case '\n':
	    case '\r':
	    case '\t':
	    case '\v':
	    case '&':
	    case '(':
	    case ')':
	    case ';':
	    case '<':
	    case '>':
	    case '`':
	    case '|':
	    case '$':
		/*
		 * On EOF, space or reserved metacharacter, end the
		 * word and return to state zero without removing the
		 * look-ahead character.
		 */
		scan_add_char(cssp, '\000');
		cssp->css_state = 0;
		continue;

	    case '\\':			/* backslash escape in word */
		cssp->css_cur = getchar();
		cssp->css_state = 3;
		continue;

	    case '\'':			/* single quote in word */
		cssp->css_cur = getchar();
		cssp->css_state = 4;
		continue;

	    case '\"':			/* double quote in word */
		cssp->css_cur = getchar();
		cssp->css_state = 5;
		continue;

	    default:			/* ordinary character in word */
		scan_add_char(cssp, cssp->css_cur);
		cssp->css_cur = getchar();
		continue;
	    }
	    break;

	case 3: /* in word, backslash */
	    switch (cssp->css_cur) {
	    case EOF:
		message_error("warning: unexpected EOF after backslash");
		cssp->css_state = 2;
		continue;

	    case '\n':			/* backslash-newline: empty string */
		cssp->css_cur = getchar();
		cssp->css_state = 2;
		continue;

	    default:			/* backslashed character in word */
		scan_add_char(cssp, cssp->css_cur);
		cssp->css_cur = getchar();
		cssp->css_state = 2;
		continue;
	    }
	    break;

	case 4: /* in word, in single quote */
	    switch (cssp->css_cur) {
	    case EOF:
		message_error("warning: unexpected EOF in string");
		cssp->css_state = 2;
		continue;

	    case '\'':			/* end of single quote */
		cssp->css_cur = getchar();
		cssp->css_state = 2;
		continue;

	    default:			/* single quoted char */
		scan_add_char(cssp, cssp->css_cur);
		cssp->css_cur = getchar();
		continue;
	    }
	    break;

	case 5: /* in word, in double quote */
	    switch (cssp->css_cur) {
	    case EOF:
		message_error("warning: unexpected EOF in string");
		cssp->css_state = 2;
		continue;

	    case '\"':			/* end of double quote */
		cssp->css_cur = getchar();
		cssp->css_state = 2;
		continue;

	    case '\\':			/* backslash in double quote in word */
		cssp->css_cur = getchar();
		cssp->css_state = 6;
		continue;

	    default:			/* double quoted character */
		scan_add_char(cssp, cssp->css_cur);
		cssp->css_cur = getchar();
		continue;
	    }
	    break;

	case 6: /* in word, in double quote, after backslash */
	    switch (cssp->css_cur) {
	    case EOF:
		message_error("warning: unexpected EOF in string");
		cssp->css_state = 2;
		continue;

	    case '\n':			/* backslash newline: empty string */
		cssp->css_cur = getchar();
		cssp->css_state = 5;
		continue;

	    case '$':			/* these are quoted under backslash */
	    case '`':
	    case '\"':
	    case '\\':
		scan_add_char(cssp, cssp->css_cur);
		cssp->css_cur = getchar();
		cssp->css_state = 5;
		continue;

	    default:			/* backslash and char are literal */
		scan_add_char(cssp, '\\');
		scan_add_char(cssp, cssp->css_cur);
		cssp->css_cur = getchar();
		cssp->css_state = 5;
		continue;
	    }
	    break;

	default:
	    abort();
	}
	break;
    }

    /*
     * Now that css_buffer has settled into its final address (after
     * reallocs), build argv.
     */
    free((void *)cssp->css_argv);
    if ((cssp->css_argv = calloc(cssp->css_argc + 1, sizeof(char *))) == NULL) {
	(void)fprintf(stderr, "%s: calloc: %s\n", progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    for (int i = 0; i < cssp->css_argc; ++i) {
	cssp->css_argv[i] = &cssp->css_buffer[cssp->css_arg_index[i]];
    }
    *argc = cssp->css_argc;
    *argv = cssp->css_argv;
    return 0;
}

/*
 * is_quit: test if command is: exit, quit, x or q
 */
bool is_quit(const char *command)
{
    switch (command[0]) {
    case 'e':
	return strcmp(&command[1], "xit") == 0;

    case 'q':
	return command[1] == '\000' || strcmp(&command[1], "uit") == 0;

    case 'x':
	return command[1] == '\000';

    default:
	break;
    }
    return false;
}

/*
 * run: binary search for command and call
 *   @command_table: table of command_t sorted by name
 *   @table_length: number of entries in command table
 *   @argc: argument count for this command
 *   @argv: argument vector for this command
 */
static int run(command_t *command_table, int table_length,
	int argc, char **argv)
{
    int low = 0;
    int high = table_length - 1;
    int cur, cmp;
    int rc;

    /*
     * Binary search for the command.
     */
    assert(argc > 0);
    while (low <= high) {
	cur = (low + high) / 2;
	cmp = strcmp(argv[0], command_table[cur].cmd_name);

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
	message_error("%s: unknown command\n", argv[0]);
	gs.gs_exitcode = N2PKVNA_EXIT_USAGE;
	return -1;
    }
    optind = 1;				/* reset optind */
    gs.gs_command = argv[0];
    rc = command_table[cur].cmd_function(argc, argv);
    gs.gs_command = NULL;

    return rc;
}

/*
 * cli: command line interface
 *   @command_table: table of command_t sorted by name
 *   @table_length: number of entries in command table
 *   @prompt: interactive prompt text
 *   @argc: argument count for this command
 *   @argv: argument vector for this command
 */
int cli(command_t *command_table, int table_length, const char *prompt,
	int argc, char **argv)
{
    /*
     * If we were given arguments, run the single command.
     */
    if (argc != 0) {
	int rc = 0;

	if (!is_quit(argv[0])) {
	    if (run(command_table, table_length, argc, argv) == -1) {
		rc = -1;
	    }
	}
	return rc;
    }

    /*
     * Otherwise, drop into an interactive CLI.
     */
    {
	cli_scan_t css;

	gs.gs_interactive = true;
	cli_scan_init(&css);
	for (;;) {
	    message_prompt();
	    gs.gs_exitcode = 0;
	    if (cli_scan(&css, &argc, &argv) == -1) {
		break;
	    }
	    if (css.css_argc > 0) {
		if (is_quit(css.css_argv[0])) {
		    break;
		}
		(void)run(command_table, table_length, argc, argv);
	    }
	}
	(void)printf("\n");
	cli_scan_free(&css);
    }
    return 0;
}
