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

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2pkvna_internal.h"

#define MAX_FRACTION	1000

/*
 * adjust_ratio: try to adjust the codes such that their ratio is equal to x
 *   @x: ratio of RF frequency to LO frequency
 *   @rf_code: address of RF frequency DDS code
 *   @lo_code: address of LO frequency DDS code
 */
static void adjust_ratio(double x, uint32_t *rf_code, uint32_t *lo_code)
{
    uint32_t a = 0, b = 1, c = 1, d = 0;
    uint32_t p, q;
    uint32_t base;
    double m;

    /*
     * Do a Stern-Brocot search to find a rational number p/q equal to x
     * where 1 <= p,q <= MAX_FRACTION, returning if no such number exists.
     */
    for (;;) {
	p = a + c;
	q = b + d;
	m = (double)p / (double)q;

	if (p > MAX_FRACTION && q > MAX_FRACTION) {
	    return;	/* failed to match x to a rational number */
	}
	if (fabs(x / m - 1.0) < 0.01 / (double)MAX_FRACTION) {
	    break;
	}
	if (x > m) {
	    a = p;
	    b = q;
	} else {
	    c = p;
	    d = q;
	}
    }

    /*
     * Round the RF code to the nearest multiple of p and compute the
     * corresponding LO code such that RF code / LO code == p / q.
     */
    base = (*rf_code + p / 2) / p;
    *rf_code = p * base;
    *lo_code = q * base;
}


/* n2pkvna_generate: generate signals with the given frequencies and phases
 *   @vnap: n2pkvna handle
 *   @rf_frequency: RF frequency
 *   @lo_frequency: LO frequency
 *   @phase: phase of LO1 out relative to RF out (degrees)
 *
 * A zero value for frequency disables output.
 *
 * Return:
 *   0: success
 *  -1: error (errno set)
 */
int n2pkvna_generate(n2pkvna_t *vnap, double rf_frequency, double lo_frequency,
	double phase)
{
    double f_reference = vnap->vna_config.nci_reference_frequency;
    uint32_t rf_code, lo_code;

    if (rf_frequency < 0.0 || rf_frequency > f_reference / 2.0) {
	_n2pkvna_error(vnap,
		"invalid frequency value %f", rf_frequency);
	errno = EINVAL;
	return -1;
    }
    if (lo_frequency < 0.0 || lo_frequency > f_reference / 2.0) {
	_n2pkvna_error(vnap,
		"invalid frequency value %f", lo_frequency);
	errno = EINVAL;
	return -1;
    }
    rf_code = _n2pkvna_frequency_to_code(f_reference, rf_frequency);
    lo_code = _n2pkvna_frequency_to_code(f_reference, lo_frequency);
    if (rf_frequency != 0.0 && lo_frequency != 0.0 &&
	    rf_frequency != lo_frequency) {
	adjust_ratio(rf_frequency / lo_frequency, &rf_code, &lo_code);
    }
    if (_n2pkvna_set_dds(vnap, 0.0, false,
		lo_code, rf_code, _n2pkvna_phase_to_code(phase)) < 0) {
	return -1;
    }
    if (_n2pkvna_read_status(vnap, 0x55, 0, NULL) < 0) {
	return -1;
    }
    return 0;
}
