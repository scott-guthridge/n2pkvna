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

#include "main.h"

/*
 * scan_t: command scanner state
 */
typedef struct scan {
    char	ss_cur;			/* look-ahead character */
    char       *ss_buffer;		/* command buffer */
    int	        ss_buffer_length;	/* current command length */
    int		ss_buffer_size;		/* buffer allocation */
    int	       *ss_arg_index;		/* vector of argument indices */
    int		ss_arg_alloc;		/* allocated slots in arg_index */
    int		ss_argc;		/* number of arguments */
    char      **ss_argv;		/* argument vector */
    int		ss_state;		/* scanner quote state */
} scan_t;

/*
 * scan_init: init a scan_t structure
 *   @ssp: scanner state
 */
static void scan_init(scan_t *ssp)
{
    (void)memset((void *)ssp, 0, sizeof(*ssp));
    ssp->ss_cur = '\n';
}

/*
 * scan_free: free resources of teh scan_t structure
 */
static void scan_free(scan_t *ssp)
{
    free((void *)ssp->ss_buffer);
    free((void *)ssp->ss_arg_index);
    free((void *)ssp->ss_argv);
}

/*
 * scan_add_char: add c to the current word
 *   @ssp: scanner state
 *   @c: character to add
 */
static void scan_add_char(scan_t *ssp, char c)
{
    if (ssp->ss_buffer_length >= ssp->ss_buffer_size) {
	if (ssp->ss_buffer_size == 0) {
	    ssp->ss_buffer_size = 128;
	} else {
	    ssp->ss_buffer_size *= 2;
	}
	if ((ssp->ss_buffer = realloc(ssp->ss_buffer,
			ssp->ss_buffer_size)) == NULL) {
	    (void)fprintf(stderr, "%s: realloc: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    ssp->ss_buffer[ssp->ss_buffer_length++] = c;
}

/*
 * scan_start_word: start a new argv entry
 *   @ssp: scanner state
 */
static void scan_start_word(scan_t *ssp)
{
    if (ssp->ss_argc >= ssp->ss_arg_alloc) {
	if (ssp->ss_arg_alloc == 0) {
	    ssp->ss_arg_alloc = 16;
	} else {
	    ssp->ss_arg_alloc *= 2;
	}
	if ((ssp->ss_arg_index = realloc(ssp->ss_arg_index,
			ssp->ss_arg_alloc)) == NULL) {
	    (void)fprintf(stderr, "%s: realloc: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
    }
    ssp->ss_arg_index[ssp->ss_argc++] = ssp->ss_buffer_length;
}

/*
 * scan: scan a command, breaking it into words with simple shell quoting
 *   @ssp: scanner state
 */
static int scan(scan_t *ssp)
{
    /*
     * Process the newline from the previous line.  We defer this to
     * the next command so that we don't do an extra getchar after
     * reading the newline.
     */
    if (ssp->ss_cur == '\n') {
	ssp->ss_cur = getchar();
	ssp->ss_buffer_length = 0;
	ssp->ss_argc = 0;
    }
    for (;;) {
	switch (ssp->ss_state) {
	case 0:	/* not in word */
	    switch (ssp->ss_cur) {
	    case EOF:			/* end of input */
		return -1;

	    case '\n':			/* end of current command */
		break;

	    case ' ':			/* whitespace */
	    case '\f':
	    case '\r':
	    case '\t':
	    case '\v':
		ssp->ss_cur = getchar();
		continue;

	    case '\\':			/* backslash escape */
		ssp->ss_cur = getchar();
		ssp->ss_state = 1;
		continue;

	    case '\'':			/* start word on single quote */
		scan_start_word(ssp);
		ssp->ss_cur = getchar();
		ssp->ss_state = 4;
		continue;

	    case '\"':			/* start word on double quote */
		scan_start_word(ssp);
		ssp->ss_cur = getchar();
		ssp->ss_state = 5;
		continue;

	    default:			/* start word on other char */
		scan_start_word(ssp);
		scan_add_char(ssp, ssp->ss_cur);
		ssp->ss_cur = getchar();
		ssp->ss_state = 2;
		continue;
	    }
	    break;

	case 1:	/* not in word, backslash */
	    switch (ssp->ss_cur) {
	    case EOF:			/* backslash EOF: warn and ignore */
		(void)fprintf(fp_err, "%s: warning: unexpected EOF "
			"after backslash", progname);
		ssp->ss_state = 0;
		continue;

	    case '\n':			/* backslash newline: empty string */
		ssp->ss_cur = getchar();
		ssp->ss_state = 0;
		continue;

	    default:			/* start word on backslashed char */
		scan_start_word(ssp);
		scan_add_char(ssp, ssp->ss_cur);
		ssp->ss_cur = getchar();
		ssp->ss_state = 2;
		continue;
	    }
	    break;

	case 2:	/* in word */
	    switch (ssp->ss_cur) {
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
		scan_add_char(ssp, '\000');
		ssp->ss_state = 0;
		continue;

	    case '\\':			/* backslash escape in word */
		ssp->ss_cur = getchar();
		ssp->ss_state = 3;
		continue;

	    case '\'':			/* single quote in word */
		ssp->ss_cur = getchar();
		ssp->ss_state = 4;
		continue;

	    case '\"':			/* double quote in word */
		ssp->ss_cur = getchar();
		ssp->ss_state = 5;
		continue;

	    default:			/* ordinary character in word */
		scan_add_char(ssp, ssp->ss_cur);
		ssp->ss_cur = getchar();
		continue;
	    }
	    break;

	case 3: /* in word, backslash */
	    switch (ssp->ss_cur) {
	    case EOF:
		(void)fprintf(fp_err, "%s: warning: unexpected EOF "
			"after backslash", progname);
		ssp->ss_state = 2;
		continue;

	    case '\n':			/* backslash-newline: empty string */
		ssp->ss_cur = getchar();
		ssp->ss_state = 2;
		continue;

	    default:			/* backslashed character in word */
		scan_add_char(ssp, ssp->ss_cur);
		ssp->ss_cur = getchar();
		ssp->ss_state = 2;
		continue;
	    }
	    break;

	case 4: /* in word, single quote */
	    switch (ssp->ss_cur) {
	    case EOF:
		(void)fprintf(fp_err, "%s: warning: unexpected EOF "
			"in string", progname);
		ssp->ss_state = 2;
		continue;

	    case '\'':			/* end of single quote */
		ssp->ss_cur = getchar();
		ssp->ss_state = 2;
		continue;

	    default:			/* single quoted char */
		scan_add_char(ssp, ssp->ss_cur);
		ssp->ss_cur = getchar();
		continue;
	    }
	    break;

	case 5: /* in word, double quote */
	    switch (ssp->ss_cur) {
	    case EOF:
		(void)fprintf(fp_err, "%s: warning: unexpected EOF "
			"in string", progname);
		ssp->ss_state = 2;
		continue;

	    case '\"':			/* end of double quote */
		ssp->ss_cur = getchar();
		ssp->ss_state = 2;
		continue;

	    case '\\':			/* backslash in double quote in word */
		ssp->ss_cur = getchar();
		ssp->ss_state = 6;
		continue;

	    default:			/* double quoted character */
		scan_add_char(ssp, ssp->ss_cur);
		ssp->ss_cur = getchar();
		continue;
	    }
	    break;

	case 6: /* in word, double quote, backslash */
	    switch (ssp->ss_cur) {
	    case EOF:
		(void)fprintf(fp_err, "%s: warning: unexpected EOF "
			"in string", progname);
		ssp->ss_state = 2;
		continue;

	    case '\n':			/* backslash newline: empty string */
		ssp->ss_cur = getchar();
		ssp->ss_state = 5;
		continue;

	    case '$':			/* quoted under backslash */
	    case '`':
	    case '\"':
	    case '\\':
		scan_add_char(ssp, ssp->ss_cur);
		ssp->ss_cur = getchar();
		ssp->ss_state = 5;
		continue;

	    default:			/* backslash and char are literal */
		scan_add_char(ssp, '\\');
		scan_add_char(ssp, ssp->ss_cur);
		ssp->ss_cur = getchar();
		ssp->ss_state = 5;
		continue;
	    }
	    break;

	default:
	    abort();
	}
	break;
    }

    /*
     * Now that ss_buffer has settled into its final address, build argv.
     */
    free((void *)ssp->ss_argv);
    if ((ssp->ss_argv = calloc(ssp->ss_argc + 1, sizeof(char *))) == NULL) {
	(void)fprintf(stderr, "%s: calloc: %s\n", progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    for (int i = 0; i < ssp->ss_argc; ++i) {
	ssp->ss_argv[i] = &ssp->ss_buffer[ssp->ss_arg_index[i]];
    }
    return 0;
}

/*
 * cli: command line interface
 */
void cli()
{
    scan_t ss;

    scan_init(&ss);
    for (;;) {
	(void)printf("n2pkvna> ");
	if (scan(&ss) == -1) {
	    break;
	}
	if (ss.ss_argc > 0) {
	    if (run_command(ss.ss_argc, ss.ss_argv) == -1) {
		break;
	    }
	}
    }
    (void)printf("\n");
    scan_free(&ss);
}
