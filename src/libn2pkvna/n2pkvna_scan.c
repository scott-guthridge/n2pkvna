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
 * MERCHANTABILITY or FITNESS FOR A11 PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "archdep.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "n2pkvna_internal.h"


/*
 * n2pkvna_scan: run a frequency scan and collect detector voltages
 *   @vnap: n2pkvna handle
 *   @f0: starting frequency (Hz)
 *   @ff: ending frequency (Hz)
 *   @n: number of points in scan
 *   @linear: true for linear spacing, false for logarithmic
 *   @frequency: recevies frequency vector if non-NULL
 *   @detector1: recevies detector1 values if non-NULL
 *   @detector2: recevies detector2 values if non-NULL
 * 
 * Return:
 *   0: success
 *  -1: error (errno set)
 *
 * Caller can free the memory of the returned vectors by a call to free.
 */
int n2pkvna_scan(n2pkvna_t *vnap, double f0, double ff,
	unsigned int n, bool linear,
	double *frequency_vector,
	double complex *detector1_vector,
	double complex *detector2_vector)
{
    double values[2];
    double frequency;
    double step_size;
    uint32_t frequency_code;
    double f_reference = vnap->vna_config.nci_reference_frequency;

    if (n < 1) {
	_n2pkvna_error(vnap,
		"invalid number of frequencies: %d", n);
	errno = EINVAL;
	return -1;
    }
    if (f0 < 0.0 || f0 > f_reference / 2.0) {
	_n2pkvna_error(vnap,
		"invalid frequency value %f", f0);
	errno = EINVAL;
	return -1;
    }
    if (ff < 0.0 || ff > f_reference / 2.0) {
	_n2pkvna_error(vnap,
		"invalid frequency value %f", ff);
	errno = EINVAL;
	return -1;
    }

    /*
     * Flush any unread data from the input queue.
     */
    if (_n2pkvna_flush_input(vnap) == -1) {
	goto error;
    }

    /*
     * Set DDS for the first measurement to prime the pipeline.  Throughout
     * the scan, we maintain two outstanding requests and read behind.
     */
    frequency = f0;
    frequency_code = _n2pkvna_frequency_to_code(f_reference, frequency);
    if (_n2pkvna_set_dds(vnap, true, HOLD_DELAY0, frequency_code,
		frequency_code, _n2pkvna_phase_to_code(0.0)) == -1) {
	goto error;
    }

    /*
     * Sweep over the range of frequencies.  For each frequency, change
     * the phase of the local oscillator in 45 degree steps around the
     * circle, measuring the detector values and summing the projections
     * into complex voltages v1 and v2.
     */
    if (n < 2) {
	step_size = 0.0;
    } else if (linear) {
	step_size = (ff - f0) / (double)(n - 1);
    } else {
	step_size = log(ff / f0) / (double)(n - 1);
    }
    for (int i = 0; i < n; ++i) {
	double complex v1 = 0.0;
	double complex v2 = 0.0;

	/*
	 * Save the actual frequency, if requested.
	 */
	if (frequency_vector != NULL)
	    frequency_vector[i] = _n2pkvna_code_to_frequency(f_reference,
					frequency_code);

	/*
	 * Set 45 degrees.
	 */
	if (_n2pkvna_set_dds(vnap, true, HOLD_DELAY1, frequency_code,
		    frequency_code, _n2pkvna_phase_to_code(45.0)) < 0) {
	    return -1;
	}

	/*
	 * Read 0 degrees.
	 *   The phase detectors both return the negative of the product.  But
	 *   the local oscillator signal into detector 2 is also inverted, so
	 *   the signal from detector 1 is negative while the signal from
	 *   detector 2 is double negative or positive.
	 */
	if (_n2pkvna_read_status(vnap, 0x55, 2, values) < 0) {
	    return -1;
	}
	v1 -= values[0];
	v2 += values[1];

	/*
	 * Set 90 degrees.
	 */
	if (_n2pkvna_set_dds(vnap, true, HOLD_DELAY1, frequency_code,
		    frequency_code, _n2pkvna_phase_to_code(90.0)) < 0) {
	    return -1;
	}

	/*
	 * Read 45 degrees.
	 *   LO 1 leads RF by 45 degrees.  If RF out through the DUT is
	 *   in phase with LO 1, then it contributes +I, with the same
	 *   sign reversals above.
	 */
	if (_n2pkvna_read_status(vnap, 0x55, 2, values) < 0) {
	    return -1;
	}
	v1 -= (1.0 / SQRT2 + I / SQRT2) * values[0];
	v2 += (1.0 / SQRT2 + I / SQRT2) * values[1];

	/*
	 * Set 135 degrees.
	 */
	if (_n2pkvna_set_dds(vnap, true, HOLD_DELAY1, frequency_code,
		    frequency_code, _n2pkvna_phase_to_code(135.0)) < 0) {
	    return -1;
	}

	/*
	 * Read 90 degrees.
	 */
	if (_n2pkvna_read_status(vnap, 0x55, 2, values) < 0) {
	    return -1;
	}
	v1 -= I * values[0];
	v2 += I * values[1];

	/*
	 * Set 180 degrees.
	 */
	if (_n2pkvna_set_dds(vnap, true, HOLD_DELAY1, frequency_code,
		    frequency_code, _n2pkvna_phase_to_code(180.0)) < 0) {
	    return -1;
	}

	/*
	 * Read 135 degrees.
	 */
	if (_n2pkvna_read_status(vnap, 0x55, 2, values) < 0) {
	    return -1;
	}
	v1 -= (-1.0 / SQRT2 + I / SQRT2) * values[0];
	v2 += (-1.0 / SQRT2 + I / SQRT2) * values[1];

	/*
	 * Set 225 degrees
	 */
	if (_n2pkvna_set_dds(vnap, true, HOLD_DELAY1, frequency_code,
		    frequency_code, _n2pkvna_phase_to_code(225.0)) < 0) {
	    return -1;
	}

	/*
	 * Read 180 degrees.
	 *	Signs are opposite the 0 degree case.
	 */
	if (_n2pkvna_read_status(vnap, 0x55, 2, values) < 0) {
	    return -1;
	}
	v1 -= -1.0 * values[0];
	v2 += -1.0 * values[1];

	/*
	 * Set 270 degrees
	 */
	if (_n2pkvna_set_dds(vnap, true, HOLD_DELAY1, frequency_code,
		    frequency_code, _n2pkvna_phase_to_code(270.0)) < 0) {
	    return -1;
	}

	/*
	 * Read 225 degrees.
	 */
	if (_n2pkvna_read_status(vnap, 0x55, 2, values) < 0) {
	    return -1;
	}
	v1 -= (-1.0 / SQRT2 + -I / SQRT2) * values[0];
	v2 += (-1.0 / SQRT2 + -I / SQRT2) * values[1];

	/*
	 * Set 315 degrees
	 */
	if (_n2pkvna_set_dds(vnap, true, HOLD_DELAY1, frequency_code,
		    frequency_code, _n2pkvna_phase_to_code(315.0)) < 0) {
	    return -1;
	}

	/*
	 * Read 270 degrees.
	 */
	if (_n2pkvna_read_status(vnap, 0x55, 2, values) < 0) {
	    return -1;
	}
	v1 -= -I * values[0];
	v2 += -I * values[1];

	if (i + 1 < n) {
	    /*
	     * Calculate the next frequency
	     */
	    if (linear) {
		frequency = f0 + (double)(i + 1) * step_size;
	    } else {
		frequency = f0 * exp((double)(i + 1) * step_size);
	    }
	    frequency_code = _n2pkvna_frequency_to_code(f_reference,
							frequency);

	    /*
	     * Set 0 degrees.
	     */
	    if (_n2pkvna_set_dds(vnap, true, HOLD_DELAY2, frequency_code,
			frequency_code, _n2pkvna_phase_to_code(0.0)) < 0) {
		return -1;
	    }
	}

	/*
	 * Read 315 degrees.
	 */
	if (_n2pkvna_read_status(vnap, 0x55, 2, values) < 0) {
	    return -1;
	}
	v1 -= (1.0 / SQRT2 - I / SQRT2) * values[0];
	v2 += (1.0 / SQRT2 - I / SQRT2) * values[1];

	/*
	 * Copy requested values to caller's vectors.
	 */
	if (detector1_vector != NULL)
	    detector1_vector[i] = v1 / 4.0;
	if (detector2_vector != NULL)
	    detector2_vector[i] = v2 / 4.0;
    }

    /*
     * Disable output.
     */
    (void)_n2pkvna_set_dds(vnap, 0.0, false, 0, 0, 0);

    return 0;

error:
    return -1;
}
