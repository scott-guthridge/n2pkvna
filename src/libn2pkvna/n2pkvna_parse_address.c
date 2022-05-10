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
 * MERCHANTABILITY or FITNESS FOR A11 PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "archdep.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2pkvna.h"

/*
 * token_type: tokens for address parser
 */
typedef enum {
    T_WORD,
    T_COLON,
    T_DOT,
    T_SLASH,
    T_COMMA,
    T_ERROR,
    T_EOF
} token_type;

/*
 * parse_address_state_t: address parser state
 */
typedef struct parse_address_state {
    token_type	as_token0;		/* current token */
    token_type	as_token1;		/* look-ahead token */
    const char *as_cur;			/* current position in string */
    const char *as_text0;		/* current scanned text */
    const char *as_end0;		/* one past current text */
    const char *as_text1;		/* look-ahead text */
    const char *as_end1;		/* one past look-ahead text */
} parse_address_state_type;

/*
 * scan: scan the next token
 *   @asp: parse address state object
 */
static void scan(parse_address_state_type *asp)
{
    asp->as_token0 = asp->as_token1;
    asp->as_text0 = asp->as_text1;
    asp->as_end0 = asp->as_end1;
    for (;;) {
	asp->as_text1 = asp->as_cur;
	if (*asp->as_cur == '\000') {
	    asp->as_token1 = T_EOF;
	    break;
	}
	if (isalnum(*asp->as_cur)) {
	    do {
		++asp->as_cur;
	    } while (isalnum(*asp->as_cur));
	    asp->as_token1 = T_WORD;
	    break;
	}
	switch (*asp->as_cur++) {
	case ',':
	    asp->as_token1 = T_COMMA;
	    break;

	case '.':
	    asp->as_token1 = T_DOT;
	    break;

	case '/':
	    asp->as_token1 = T_SLASH;
	    break;

	case ':':
	    asp->as_token1 = T_COLON;
	    break;

	default:
	    if (isspace(asp->as_cur[-1])) {
		continue;
	    }
	    --asp->as_cur;
	    asp->as_token1 = T_ERROR;
	    break;
	}
	break;
    }
    asp->as_end1 = asp->as_cur;
}

/*
 * decode_hex16: decode a 16-bit unsigned hexadecimal number
 *   @string: string to decode
 *   @end:    one byte beyond the end of the string
 *   @value:  address where result should be saved
 *
 * Return:
 *    0: success
 *   -1: failure
 */
static int decode_hex16(const char *string, const char *end, uint16_t *value)
{
    long int li;
    char *temp;

    li = strtol(string, &temp, 16);
    if (temp < end)  {
	errno = EINVAL;
	return -1;
    }
    if (li < 0 || li > 0xFFFF) {
	errno = EDOM;
	return -1;
    }
    *value = (uint16_t)li;
    return 0;
}

/*
 * decode_int8: decode an 8-bit unsigned decimal number
 *   @string: string to decode
 *   @end:    one byte beyond the end of the string
 *   @value:  address where result should be saved
 *
 * Return:
 *    0: success
 *   -1: failure
 */
static int decode_int8(const char *string, const char *end, uint8_t *value)
{
    long int li;
    char *temp;

    li = strtol(string, &temp, 10);
    if (temp < end)  {
	errno = EINVAL;
	return -1;
    }
    if (li < 0 || li > 0xFF) {
	errno = EDOM;
	return -1;
    }
    *value = (uint8_t)li;
    return 0;
}

/*
 * n2pkvna_alloc_address_from_string: init an address object from string
 *   @adrp: address object
 *   @unit: device unit address string to parse
 */
int _n2pkvna_parse_address(n2pkvna_address_t *adrp, const char *unit)
{
    parse_address_state_type as;

    /*
     * Prime the scanner.
     */
    as.as_cur = unit;
    scan(&as);
    scan(&as);

    /*
     * Parse a USB address.  To support other than USB addresses,
     * handle them above this block.
     *
     * usb_list		: usb_term
     *			| usb_list ',' usb_term
     *			;
     *
     * usb_term		: vendor [followed by ':' product]
     *			| vendor ':'
     *			| bus [followed by '.' device]
     *			| bus '.'
     *			| bus [followed by '/' port]
     *			| bus '/'
     *			| suffix
     *			| usb_term suffix
     *			;
     *
     * suffix		| ':' product
     *			| '.' device
     *			| '/' port
     *			;
     *
     * where vendor, product, bus, device and port are WORD
     */
    (void)memset((void *)adrp, 0, sizeof(*adrp));
    adrp->adr_type = N2PKVNA_ADR_USB;
    for (;;) {
	switch (as.as_token0) {
	case T_WORD:
	    switch (as.as_token1) {
	    case T_COLON:
		if (decode_hex16(as.as_text0, as.as_end0,
			    &adrp->adr_usb_vendor) == -1) {
		    return -1;
		}
		scan(&as);

		/*
		 * If we have a vendor without a product, discard
		 * the colon; otherwise leave it to be processed as a
		 * suffix below.  Special case double colon which is a
		 * syntax error.
		 */
		if (as.as_token1 != T_WORD && as.as_token1 != T_COLON) {
		    scan(&as);
		}
		break;

	    case T_DOT:
	    case T_SLASH:
		if (decode_int8(as.as_text0, as.as_end0,
			    &adrp->adr_usb_bus) == -1) {
		    return -1;
		}
		scan(&as);

		/*
		 * If we have a bus without a device or port, discard
		 * the separator; otherwise leave it to be processed
		 * as a suffix below.  Special case .. or // which are
		 * syntax errors.
		 */
		if (as.as_token1 != T_WORD && as.as_token1 != as.as_token0) {
		    scan(&as);
		}
		break;

	    default:
		errno = EINVAL;
		return -1;
	    }
	    break;

	case T_COLON:
	case T_DOT:
	case T_SLASH:
	    break;

	default:
	    errno = EINVAL;
	    return -1;
	}

	/*
	 * Parse optional suffix list.
	 */
	for (;;) {
	    switch (as.as_token0) {
	    case T_COLON:
		scan(&as);
		if (as.as_token0 != T_WORD) {
		    errno = EINVAL;
		    return -1;
		}
		if (decode_hex16(as.as_text0, as.as_end0,
			    &adrp->adr_usb_product) == -1) {
		    return -1;
		}
		scan(&as);
		continue;

	    case T_DOT:
		scan(&as);
		if (as.as_token0 != T_WORD) {
		    errno = EINVAL;
		    return -1;
		}
		if (decode_int8(as.as_text0, as.as_end0,
			    &adrp->adr_usb_device) == -1) {
		    return -1;
		}
		scan(&as);
		continue;

	    case T_SLASH:
		scan(&as);
		if (as.as_token0 != T_WORD) {
		    errno = EINVAL;
		    return -1;
		}
		if (decode_int8(as.as_text0, as.as_end0,
			    &adrp->adr_usb_port) == -1) {
		    return -1;
		}
		scan(&as);
		continue;

	    default:
		break;
	    }
	    break;
	}
	if (as.as_token0 == T_COMMA)  {
	    scan(&as);
	    continue;
	}
	break;
    }
    if (as.as_token0 != T_EOF) {
	errno = EINVAL;
	return -1;
    }
    return 0;
}
