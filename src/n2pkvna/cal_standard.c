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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vnacal.h>

#include "cal_standard.h"
#include "main.h"
#include "message.h"

/*
 * token_t: tokens returned by the scanner
 */
typedef enum {
    T_ERROR,
    T_MATCH,
    T_OPEN,
    T_SHORT,
    T_THROUGH,
    T_SOLT,
    T_OSLT,
    T_IDENTIFIER,
    T_DASH,
    T_COMMA,
    T_EOS
} token_t;

/*
 * scan_state_t: lexical analyzer state
 */
typedef struct scan_state {
    char       *ss_input;			/* input string to scan */
    char       *ss_position;			/* current position in string */
    char	ss_savechar;			/* used to restore NUL'd char */
    int		ss_length;			/* input string length */
    token_t	ss_token;			/* current token */
    char       *ss_text;			/* identifier text */
} scan_state_t;

/*
 * scan: scan the next token
 *   @ssp: scanner state
 */
static void scan(scan_state_t *ssp)
{
    *ssp->ss_position = ssp->ss_savechar;
    for (;;) {
	/*
	 * Skip whitespace
	 */
	if (isascii(*ssp->ss_position) && isspace(*ssp->ss_position)) {
	    ++ssp->ss_position;
	    continue;
	}

	/*
	 * Handle simple tokens.
	 */
	switch (*ssp->ss_position) {
	case '\000':
	    ssp->ss_token = T_EOS;
	    return;

	case ',':
	    ssp->ss_text = ssp->ss_position++;
	    ssp->ss_token = T_COMMA;
	    goto save;

	case '-':
	    ssp->ss_text = ssp->ss_position++;
	    ssp->ss_token = T_DASH;
	    goto save;

	default:
	    break;
	}

	/*
	 * Handle keywords and identifiers.
	 */
	if (isascii(*ssp->ss_position) &&
		(isalpha(*ssp->ss_position) || *ssp->ss_position == '_')) {
	    ssp->ss_text = ssp->ss_position;
	    do {
		++ssp->ss_position;
	    } while (isascii(*ssp->ss_position) &&
		    (isalnum(*ssp->ss_position) || *ssp->ss_position == '_'));
	    ssp->ss_savechar = *ssp->ss_position;
	    *ssp->ss_position = '\000';
	    if (ssp->ss_position - ssp->ss_text == 1) {
		switch (ssp->ss_text[0]) {
		case 'M':
		    ssp->ss_token = T_MATCH;
		    return;
		case 'O':
		    ssp->ss_token = T_OPEN;
		    return;
		case 'S':
		    ssp->ss_token = T_SHORT;
		    return;
		case 'T':
		    ssp->ss_token = T_THROUGH;
		    return;
		default:
		    break;
		}
	    } else if (ssp->ss_position - ssp->ss_text == 4) {
		if (strcmp(ssp->ss_text, "SOLT") == 0) {
		    ssp->ss_token = T_SOLT;
		    return;
		}
		if (strcmp(ssp->ss_text, "OSLT") == 0) {
		    ssp->ss_token = T_OSLT;
		    return;
		}
	    }
	    ssp->ss_token = T_IDENTIFIER;
	    return;
	}

	/*
	 * All other characters are reserved.
	 */
	if (isascii(*ssp->ss_position) && isprint(*ssp->ss_position)) {
	    message_error("unexpected character '%c' in standards string\n",
		    *ssp->ss_position);
	} else {
	    message_error("unexpected byte '\\x%02x' in standards string\n",
		    *(unsigned char *)ssp->ss_position);
	}
	ssp->ss_token = T_ERROR;
	return;
    }

save:
    ssp->ss_savechar = *ssp->ss_position;
    *ssp->ss_position = '\000';
    return;
}

/*
 * scan_init: init the scanner and scan the first token
 *   @ssp: scanner state
 *   @standards: string to parse
 */
static void scan_init(scan_state_t *ssp, const char *standards)
{
    (void)memset((void *)ssp, 0, sizeof(*ssp));
    ssp->ss_token = T_ERROR;
    if ((ssp->ss_input = strdup(standards)) == NULL) {
	(void)fprintf(stderr, "%s: strdup: %s\n", progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    ssp->ss_position = ssp->ss_input;
    ssp->ss_savechar = standards[0];
    ssp->ss_length = strlen(standards);
    scan(ssp);
}

/*
 * scan_free: free scanner resources
 */
static void scan_free(scan_state_t *ssp)
{
    if (ssp != NULL) {
	free((void *)ssp->ss_input);
    }
}

/*
 * scan_substitute: replace the current token with given text and rescan
 *   @ssp: scanner state
 *   @text: new text to replace current token
 */
static void scan_substitute(scan_state_t *ssp, const char *text)
{
    int offset1 = ssp->ss_text     - ssp->ss_input;
    int offset2 = ssp->ss_position - ssp->ss_input;
    int insert_length = strlen(text);
    int delete_length = offset2 - offset1;
    int delta = insert_length - delete_length;

    /*
     * Restore the save char.
     */
    *ssp->ss_position = ssp->ss_savechar;

    /*
     * Lengthen the buffer as needed.
     */
    if (delta > 0) {
	char *cp;

	if ((cp = realloc(ssp->ss_input, ssp->ss_length + delta + 1)) == NULL) {
	    (void)fprintf(stderr, "%s: realloc: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	ssp->ss_input    = cp;
	ssp->ss_text     = &cp[offset1];
	ssp->ss_position = &cp[offset2];
    }

    /*
     * Move the tail and insert the next text.
     */
    (void)memmove((void *)&ssp->ss_position[delta], (void *)ssp->ss_position,
	    ssp->ss_length - offset2 + 1);
    (void)memcpy((void *)ssp->ss_text, (void *)text, insert_length);

    /*
     * Reset the scan position to the beginning of the next text,
     * fix the length and re-scan.
     */
    ssp->ss_position = ssp->ss_text;
    ssp->ss_savechar = ssp->ss_text[0];
    ssp->ss_length += delta;
    scan(ssp);
}

/*
 * print_here: show the location of an error in the standards list
 */
static void print_here(scan_state_t *ssp)
{
    char c;

    *ssp->ss_position = ssp->ss_savechar;
    c = *ssp->ss_text;
    *ssp->ss_text = '\000';
    message_error_np("%s<HERE>", ssp->ss_input);
    *ssp->ss_text = c;
    message_error_np("%s", ssp->ss_text);
    *ssp->ss_position = '\000';
}

const cal_standard_t cs_match = {
    .cs_token = T_MATCH,
    .cs_name  = "M",
    .cs_text  = "match standard",
    .cs_ports = 1,
    .cs_matrix = {{ VNACAL_MATCH }},
};

const cal_standard_t cs_open = {
    .cs_token = T_OPEN,
    .cs_name  = "O",
    .cs_text  = "open standard",
    .cs_ports = 1,
    .cs_matrix = {{ VNACAL_OPEN }},
};

const cal_standard_t cs_short = {
    .cs_token = T_SHORT,
    .cs_name  = "S",
    .cs_text  = "short standard",
    .cs_ports = 1,
    .cs_matrix = {{ VNACAL_SHORT }},
};

const cal_standard_t cs_through = {
    .cs_token = T_THROUGH,
    .cs_name  = "T",
    .cs_text  = "through standard",
    .cs_ports = 2,
    .cs_matrix = {
	{ VNACAL_ZERO, VNACAL_ONE },
	{ VNACAL_ONE, VNACAL_ZERO },
    },
};

const cal_standard_t cs_terminator = {
    .cs_token = T_DASH,
    .cs_name  = "<terminator>",
    .cs_text  = "terminator",
    .cs_ports = 1,
    .cs_matrix = {{ VNACAL_ZERO }},
};

/*
 * lookup: look-up an standard by name
 *   @cslp: cal_step_list_t structure
 *   @name: key for look-up
 */
static cal_standard_t *lookup(cal_step_list_t *cslp, const char *name)
{
    cal_standard_t *csp;
    int cmp;

    for (csp = cslp->csl_standards; csp != NULL; csp = csp->cs_next) {
	if ((cmp = strcmp(name, csp->cs_name)) == 0) {
	    return csp;
	}
	if (cmp < 0) {
	    break;
	}
    }
    return NULL;
}

/*
 * insert: insert a new element into the list
 *   @cslp: cal_step_list_t structure
 */
static void insert(cal_step_list_t *cslp, cal_standard_t *csp_new)
{
    cal_standard_t *csp, **cspp;

    cspp = &cslp->csl_standards;
    for (; (csp = *cspp) != NULL; cspp = &csp->cs_next) {
	if (strcmp(csp_new->cs_name, csp->cs_name) < 0) {
	    break;
	}
    }
    csp_new->cs_next = csp;
    *cspp = csp_new;
}

/*
 * get_standard: read a standard from file
 *   @cslp: cal_step_list_t structure
 *   @name: name of standard
 */
static cal_standard_t *get_standard(cal_step_list_t *cslp, const char *name)
{
    cal_standard_t *csp;
    FILE *fp = NULL;
    const char *directory;
    char *filename = NULL;
    vnadata_t *vdp = NULL;
    int rows, columns, frequencies;
    const double *frequency_vector;
    double complex *vector = NULL;
    static const char *extensions[] = {
	".npd",
	".ts",
	".s1p",
	".s2p",
	NULL
    };

    /*
     * Look-up the standard.  If found, return it.
     */
    if ((csp = lookup(cslp, name)) != NULL) {
	return csp;
    }

    /*
     * Get the configuration directory name.
     */
    if ((directory = n2pkvna_get_directory(cslp->csl_vnap)) == NULL) {
	return NULL;
    }

    /*
     * For each file extension, try to open the standard file.
     */
    for (int i = 0; extensions[i] != NULL; ++i) {
	if (asprintf(&filename, "%s/%s%s",
		    directory, name, extensions[i]) == -1) {
	    (void)fprintf(stderr, "%s: asprintf: %s\n",
		    progname, strerror(errno));
	    exit(N2PKVNA_EXIT_SYSTEM);
	}
	if ((fp = fopen(filename, "r")) != NULL) {
	    break;
	}
	if (errno != ENOENT) {
	    message_error("fopen: %s: %s\n", filename, strerror(errno));
	    goto error;
	}
	free((void *)filename);
	filename = NULL;
    }
    if (fp == NULL) {
	message_error("%s/%s.{npd,ts,s1p,s2p}: not found\n",
		directory, name);
	goto error;
    }

    /*
     * Allocate the vnadata structure, load the file and convert
     * to S paramters.
     */
    if ((vdp = vnadata_alloc(print_libvna_error, NULL)) == NULL) {
	goto error;
    }
    if (vnadata_fload(vdp, fp, filename) == -1) {
	goto error;
    }
    if (vnadata_convert(vdp, vdp, VPT_S) == -1) {
	goto error;
    }
    rows    = vnadata_get_rows(vdp);
    columns = vnadata_get_columns(vdp);
    if (rows != columns || rows < 1 || rows > 2) {
	message_error("%s: standard must be 1x1 or 2x2\n", filename);
	goto error;
    }
    frequencies = vnadata_get_frequencies(vdp);
    frequency_vector = vnadata_get_frequency_vector(vdp);
    if ((vector = calloc(frequencies, sizeof(double complex))) == NULL) {
	(void)fprintf(stderr, "%s: calloc: %s\n", progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    if ((csp = malloc(sizeof(cal_standard_t))) == NULL) {
	(void)fprintf(stderr, "%s: malloc: %s\n", progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    (void)memset((void *)csp, 0, sizeof(*csp));
    csp->cs_token = T_IDENTIFIER;
    if ((csp->cs_name = strdup(name)) == NULL) {
	(void)fprintf(stderr, "%s: strdup: %s\n", progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    csp->cs_text = csp->cs_name;
    csp->cs_ports = rows;
    for (int row = 0; row < rows; ++row) {
	for (int column = 0; column < columns; ++column) {
	    if (vnadata_get_to_vector(vdp, row, column, vector) == -1) {
		goto error;
	    }
	    if ((csp->cs_matrix[row][column] =
			vnacal_make_vector_parameter(cslp->csl_vcp,
			    frequency_vector, frequencies, vector)) == -1) {
		goto error;
	    }
	}
    }
    insert(cslp, csp);
    goto out;

error:
    free((void *)csp);
    csp = NULL;
out:
    free((void *)vector);
    vnadata_free(vdp);
    if (fp != NULL) {
	fclose(fp);
    }
    free((void *)filename);
    return csp;
}

/*
 * parse_standard: parse an element of a calibration standard
 *   @cslp: cal_step_list_t structure
 *   @ssp:  scanner state
 */
static const cal_standard_t *parse_standard(cal_step_list_t *cslp,
	scan_state_t *ssp)
{
    setup_t *sup = cslp->csl_setup;
    cal_standard_t *csp;

    for (;;) {
	switch (ssp->ss_token) {
	case T_MATCH:
	    scan(ssp);
	    return &cs_match;

	case T_OPEN:
	    scan(ssp);
	    return &cs_open;

	case T_SHORT:
	    scan(ssp);
	    return &cs_short;

	case T_THROUGH:
	    scan(ssp);
	    return &cs_through;

	case T_SOLT:
	    switch (sup->su_rows * sup->su_columns) {
	    case 1:
		scan_substitute(ssp, "S,O,M");
		continue;
	    case 2:
		scan_substitute(ssp, "S-,O-,M-,T");
		continue;
	    case 4:
		scan_substitute(ssp, "S-,-S,-O,O-,-M,M-,T");
		continue;
	    default:
		abort();
	    }
	    continue;

	case T_OSLT:
	    switch (sup->su_rows * sup->su_columns) {
	    case 1:
		scan_substitute(ssp, "O,S,M");
		continue;
	    case 2:
		scan_substitute(ssp, "O-,S-,M-,T");
		continue;
	    case 4:
		scan_substitute(ssp, "O-,-O,-S,S-,-M,M-,T");
		continue;
	    default:
		abort();
	    }
	    continue;

	default:
	    break;
	}
	break;
    }
    if (ssp->ss_token != T_IDENTIFIER) {
	if (*ssp->ss_position != '\000') {
	    message_error("invalid standards list: "
		    "expected name or dash before \"%s\"\n",
		    ssp->ss_position);
	} else {
	    message_error("invalid standards list: "
		    "expected name or dash before end of string\n");
	}
	return NULL;
    }

    /*
     * Get the standard from a file.
     */
    if ((csp = get_standard(cslp, ssp->ss_text)) != NULL) {
	scan(ssp);
	return csp;
    }
    return NULL;
}

/*
 * is_name: test if the current token names a standard
 *   @token: lexical token to test
 */
static bool is_name(token_t token)
{
    switch (token) {
    case T_MATCH:
    case T_OPEN:
    case T_SHORT:
    case T_THROUGH:
    case T_SOLT:
    case T_OSLT:
    case T_IDENTIFIER:
	return true;

    default:
	break;
    }
    return false;
}

/*
 * parse_calibration_step: parse a single calibration step
 *   @cslp: cal_step_list_t structure
 *   @ssp:  scanner state
 */
static cal_step_t *parse_calibration_step(cal_step_list_t *cslp,
	scan_state_t *ssp)
{
    cal_step_t *cstp;
    setup_t *sup = cslp->csl_setup;
    const cal_standard_t *csp1, *csp2;

    /*
     * Allocate and init cal_step_t structure.
     */
    if ((cstp = malloc(sizeof(cal_step_t))) == NULL) {
	(void)fprintf(stderr, "%s: malloc: %s\n", progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    (void)memset((void *)cstp, 0, sizeof(*cstp));

    /*
     * Parse the description of the standard.
     */
    if (is_name(ssp->ss_token)) {
	if ((cstp->cst_standards[0] = parse_standard(cslp, ssp)) == NULL) {
	    goto error;
	}
    } else {
	cstp->cst_standards[0] = &cs_terminator;
    }
    if (ssp->ss_token == T_DASH) {
	scan(ssp);
	if (is_name(ssp->ss_token)) {
	    if ((cstp->cst_standards[1] = parse_standard(cslp, ssp)) == NULL) {
		goto error;
	    }
	} else {
	    cstp->cst_standards[1] = &cs_terminator;
	}
    }
    csp1 = cstp->cst_standards[0];
    csp2 = cstp->cst_standards[1];
    if (csp1 == &cs_terminator && (csp2 == NULL || csp2 == &cs_terminator)) {
	message_error("syntax error in standards string: ");
	print_here(ssp);
	message_error_np("\n");
	goto error;
    }

    /*
     * Validate the dimensions and set cst_type.  First handle lone
     * standards.
     */
    if (csp2 == NULL) {
	/*
	 * Interpret a lone single-port standard in a multi-port setup as
	 * single reflect on port 1.
	 */
	if (csp1->cs_ports == 1 && sup->su_rows > 1) {
	    cstp->cst_standards[1] = csp2 = &cs_terminator;
	    goto reflect;
	}
	/*
	 * Otherwise, the lone standard must have the same number of ports
	 * as the calibration.
	 */
	if (csp1->cs_ports != sup->su_rows) {
	    message_error("expected a %d port standard, "
		    "but %s is a %d port standard: ",
		    sup->su_rows, csp1->cs_name, csp1->cs_ports);
	    print_here(ssp);
	    message_error_np("\n");
	    goto error;
	}
	if (csp1->cs_ports == 1) {			/* reflection only */
	    cstp->cst_type = CALS_SINGLE_REFLECT;
	} else if (csp1 == &cs_through) {		/* through */
	    cstp->cst_type = CALS_THROUGH;
	} else {					/* arbitrary 2x2 */
	    cstp->cst_type = CALS_LINE;
	}
	return cstp;
    }
    /*
     * Handle single reflect and double reflect standards, e.g. "S-" or "S-O".
     */
reflect:
    if (csp1->cs_ports != 1) {
	message_error("cannot use %s as a reflect standard: ", csp1->cs_name);
	print_here(ssp);
	message_error_np("\n");
	goto error;
    }
    if (csp2->cs_ports != 1) {
	message_error("%s: "
		"cannot use %s as a reflect standard: ", csp2->cs_name);
	print_here(ssp);
	message_error_np("\n");
	goto error;
    }
    if (csp1 == &cs_terminator || csp2 == &cs_terminator) {
	cstp->cst_type = CALS_SINGLE_REFLECT;
    } else {
	cstp->cst_type = CALS_DOUBLE_REFLECT;
    }
    return cstp;

error:
    free((void *)cstp);
    return NULL;
}

/*
 * cal_standards_parse: parse a list of calibration standard specifiers
 *   @vnap: n2pkvna_t structure
 *   @sup: VNA setup structure
 *   @vcp: vnacal_t structure
 *   @standards: string describing the standards, e.g. "S-,O-,M-,T"
 */
cal_step_list_t *cal_standards_parse(n2pkvna_t *vnap, setup_t *sup,
	vnacal_t *vcp, const char *standards)
{
    scan_state_t ss;
    cal_step_list_t *cslp;
    cal_step_t **cstpp;

    /*
     * Allocate and init the cal_step_list_t structure.
     */
    if ((cslp = malloc(sizeof(cal_step_list_t))) == NULL) {
	(void)fprintf(stderr, "%s: malloc: %s\n", progname, strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    (void)memset((void *)cslp, 0, sizeof(*cslp));
    cslp->csl_vnap = vnap;
    cslp->csl_setup = sup;
    cslp->csl_vcp = vcp;
    cstpp = &cslp->csl_steps;

    /*
     * Parse.
     */
    scan_init(&ss, standards);
    for (;;) {
	cal_step_t *cstp;

	/*
	 * Parse the next calibratino step and add it to the list.
	 */
	if ((cstp = parse_calibration_step(cslp, &ss)) == NULL) {
	    goto error;
	}
	*cstpp = cstp;
	cstpp = &cstp->cst_next;

	/*
	 * If end of string, break.
	 */
	if (ss.ss_token == T_EOS) {
	    break;
	}

	/*
	 * Otherwise, expect a comma.
	 */
	if (ss.ss_token != T_COMMA) {
	    message_error("syntax error in standards string");
	    print_here(&ss);
	    message_error_np("\n");
	    goto error;
	}
	scan(&ss);
    }
    scan_free(&ss);
    return cslp;

error:
    scan_free(&ss);
    cal_standards_free(cslp);
    return NULL;
}

/*
 * cal_standards_free: free the resources of cal_step_list_t structure
 *   @cslp: address of vnacal_standards_t structure
 */
void cal_standards_free(cal_step_list_t *cslp)
{
    if (cslp != NULL) {
	vnacal_t *vcp = cslp->csl_vcp;
	cal_standard_t *csp;

	while (cslp->csl_steps != NULL) {
	    cal_step_t *cstp = cslp->csl_steps;

	    cslp->csl_steps = cstp->cst_next;
	    free((void *)cstp);
	}
	while ((csp = cslp->csl_standards) != NULL) {
	    int ports = csp->cs_ports;

	    cslp->csl_standards = csp->cs_next;
	    for (int row = 0; row < ports; ++row) {
		for (int col = 0; col < ports; ++col) {
		    if (csp->cs_matrix[row][col] != 0) {
			(void)vnacal_delete_parameter(vcp,
				csp->cs_matrix[row][col]);
		    }
		}
	    }
	    free((void *)csp->cs_name);
	    free((void *)csp);
	}
    }
}
