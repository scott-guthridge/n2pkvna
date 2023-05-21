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

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "n2pkvna_internal.h"

/*
 * _n2pkvna_decode_ltc2440: convert LTC2440 ADC result to voltage
 *   @vnap: n2pkvna handle
 *   @ucp: four byte big-endian code from LTC2240
 */
static double _n2pkvna_decode_ltc2440(n2pkvna_t *vnap, unsigned char *ucp)
{
    int32_t value = ntohl(*(uint32_t *)ucp);

    /*
     * Fail if conversion is still in-progress.
     */
    if (value & 0x80000000) {
	_n2pkvna_error(vnap, "unexpected BUSY from LTC2240");
	return NAN;
    }

    /*
     * Fail if the "zero" bit is non-zero.
     */
    if ((value & 0x40000000)) {
	_n2pkvna_error(vnap, "invalid code from LTC2240");
	return NAN;
    }

    /*
     * Subtract the "sign" bit to move "zero" to 0.  Fail if the
     * value is outside of the defined range.
     */
    value -= 0x20000000;
    if (value < -0x10000001 || value > 0x10000000) {
	_n2pkvna_error(vnap, "invalid code from LTC2240");
	return NAN;
    }
    return LTC2440_REF / 2.0 / LTC2440_FULL * value;
}

/*
 * _n2pkvna_flush_input: flush unread input from the N2PK VNA
 *   @vnap: n2pkvna handle
 */
int _n2pkvna_flush_input(n2pkvna_t *vnap)
{
    unsigned char buffer[USB_BUFSIZE];
    int transferred;
    int rv;

    for (int i = 0; i < 2; ++i) {
	if ((rv = libusb_bulk_transfer(vnap->vna_udhp, READ_ENDPOINT,
		buffer, sizeof(buffer),
		&transferred, USB_TIMEOUT)) < 0) {
	   _n2pkvna_error(vnap, "%s: libusb_bulk_transfer: %s",
		    vnap->vna_config.nci_basename, libusb_error_name(rv));
	    _n2pkvna_set_usb_errno(rv);
	    return -1;
	}
    }
    return 0;
}

/*
 * _n2pkvna_read_status: read status from the N2PK VNA
 *   @vnap: n2pkvna handle
 *   @opcode: expected opcode
 *   @n: number of values to read
 *   @values: returned values
 *
 * Return:
 *     0: success
 *    -1: error
 */
int _n2pkvna_read_status(n2pkvna_t *vnap, uint8_t opcode,
	int n, double *values)
{
    uint32_t backoff = 10000;	/* microseconds */

    /*
     * Validate arguments.
     */
    if (n > 0 && opcode != 0x55) {
	_n2pkvna_error(vnap, "%s: _n2pkvna_read_status: %s",
		vnap->vna_config.nci_basename, strerror(EINVAL));
	errno = EINVAL;
	return -1;
    }

    /*
     * Init output vector.
     */
    for (int i = 0; i < n; ++i)
	values[i] = NAN;

    /*
     * Try the read with exponential backoff.
     *	 max wait is 650ms
     */
    for (int try = 0; try < 9; ++try) {
	int i;
	unsigned char buffer[USB_BUFSIZE];
	int transferred;
	int rv;

	/*
	 * Read
	 */
	if ((rv = libusb_bulk_transfer(vnap->vna_udhp, READ_ENDPOINT,
		buffer, sizeof(buffer), &transferred, USB_TIMEOUT)) < 0) {
	   _n2pkvna_error(vnap, "%s: libusb_bulk_transfer: %s",
		    vnap->vna_config.nci_basename, libusb_error_name(rv));
	    _n2pkvna_set_usb_errno(rv);
	    return -1;
	}
	if (transferred < 5) {
	   _n2pkvna_error(vnap, "%s: libusb_bulk_transfer: short read",
		    vnap->vna_config.nci_basename);
	    _n2pkvna_set_usb_errno(rv);
	    return -1;
	}

	/*
	 * Check status
	 */
	if (buffer[0] != opcode) {
	    goto backoff;
	}
	if ((buffer[1] & 0x80) != 0) {
	   _n2pkvna_error(vnap, "%s: ADC read time-out",
		    vnap->vna_config.nci_basename);
	    errno = EIO;
	    return -1;
	}
	if ((buffer[1] & 0x40) != 0) {
	   _n2pkvna_error(vnap, "%s: VNA powered off",
		   vnap->vna_config.nci_basename);
	    errno = EIO;
	    return -1;
	}
	if (!(buffer[1] & 0x20) != !(buffer[4] > 0)) {
	   _n2pkvna_error(vnap, "%s: invalid status response",
		    vnap->vna_config.nci_basename);
	    errno = EIO;
	    return -1;
	}
	if (5 + 4 * buffer[4] > transferred) {
	   _n2pkvna_error(vnap, "%s: libusb_bulk_transfer: short read",
		    vnap->vna_config.nci_basename);
	    errno = EIO;
	    return -1;
	}
	if ((buffer[1] & 0x08) != 0) {
	   _n2pkvna_error(vnap, "%s: ADC not responding",
		   vnap->vna_config.nci_basename);
	    errno = EIO;
	    return -1;
	}

	/*
	 * If no values are expected, return.
	 */
	if (n == 0)
	    return 0;

	/*
	 * If values aren't yet available, back off and return.
	 */
	if (buffer[4] == 0)
	    goto backoff;

	/*
	 * Make sure the correct number of values were returned.
	 */
	if (buffer[4] < n) {
	   _n2pkvna_error(vnap,
		    "%s: _n2pkvna_read_status: not enough values returned",
		    vnap->vna_config.nci_basename);
	    errno = EIO;
	    return -1;
	}

	/*
	 * Convert values.
	 */
	for (i = 0; i < n; ++i) {
	    values[i] = _n2pkvna_decode_ltc2440(vnap, &buffer[5 + 4 * i]);
	    if (isnan(values[i])) {
		errno = EIO;
		return -1;
	    }
	}
	return 0;

    backoff:
	usleep(backoff);
	backoff <<= 1;
	if (backoff > 100000)	/* cap poll at 100ms */
	    backoff = 100000;
    }
   _n2pkvna_error(vnap,
	    "%s: _n2pkvna_read_status: too many tries",
	    vnap->vna_config.nci_basename);
    errno = EIO;
    return -1;
}

/*
 * _n2pkvna_frequency_to_code: convert frequency to DDS code
 *   @frequency: frequency (Hz)
 */
uint32_t _n2pkvna_frequency_to_code(double f0, double frequency)
{
    frequency /= f0;
    frequency *= 4.294967296e+9;
    frequency = rint(frequency);
    if (frequency > 4294967295.0) {
	frequency = 4294967295.0;
    } else if (frequency < 0.0) {
	frequency = 0.0;
    }
    return (uint32_t)frequency;
}

/*
 * _n2pkvna_code_to_frequency: convert DDS frequency code to Hz
 *   @code: DDS frequency code
 */
double _n2pkvna_code_to_frequency(double f0, uint32_t code)
{
    double temp = (double)code;

    temp /= 4.294967296e+9;
    temp *= f0;
    return temp;
}

/*
 * _n2pkvna_phase_to_code: convert phase in degrees to DDS code (not shifted)
 *   @phase: degrees
 */
uint8_t _n2pkvna_phase_to_code(double phase)
{
    if (phase < 0.0 || phase >= 360.0) {
	phase -= 360.0 * floor(phase / 360.0);
    }
    phase /= 11.25;
    phase = rint(phase);
    if (phase < 0.0) {
	phase = 0.0;
    } else if (phase > 31.0) {
	phase = 31.0;
    }
    return (uint8_t)phase;
}

/*
 * _n2pkvna_set_dds: set the DDS
 *   @vnap: n2pkvna handle
 *   @measure: start a measurement after setting the frequency
 *   @start_delay: delay between DDS setting and ADC conversion (s)
 *   @lo_frequency_code: AD9851 frequency code (LO out)
 *   @rf_frequency_code: AD9851 frequency code (RF out)
 *   @phase_code: AD9851 phase code (LO out)
 */
int _n2pkvna_set_dds(n2pkvna_t *vnap, bool measure, double start_delay,
	uint32_t lo_frequency_code, uint32_t rf_frequency_code,
	uint8_t phase_code)
{
    uint8_t flags = measure ? 0x79 : 0x60;
    uint8_t delay_code;
    unsigned char buffer[25];
    int transferred = 0;
    int rv;

    /*
     * Convert start_delay.  There are two ranges:
     *   8us to 2040us
     *   1ms to 255ms
     */
    assert(start_delay >= 0.0);
    flags &= ~0x20;
    if (start_delay <= 255.0 * 8.0e-6) {
	start_delay = ceil((start_delay / 8.0e-6) - 0.01);
	flags |= 0x20;
    } else {
	start_delay = ceil((start_delay / 1.0e-3) - 0.01);
    }
    if (start_delay < 1.0) {
	start_delay = 1.0;
    } else if (start_delay > 255.0) {
	start_delay = 255.0;
    }
    delay_code = (uint8_t)start_delay;

    /*
     * Send the set DDS command.
     */
    (void)memset((void *)buffer, 0, sizeof(buffer));
    buffer[0] = 0x55;
    buffer[1] = flags;
    buffer[2] = delay_code;
    buffer[3] = measure ? 1 : 0;
    buffer[4] = measure ? ADC_MODE : 0;
    if (lo_frequency_code == 0) {
	buffer[5] = 0x04;	/* power down */
    } else {
	buffer[5] = phase_code << 3;
    }
    *(uint32_t *)&buffer[6] = htonl(lo_frequency_code);
    if (rf_frequency_code == 0) {
	buffer[10] = 0x04;	/* power down */
    }
    *(uint32_t *)&buffer[11] = htonl(rf_frequency_code);
    (void)memset(&buffer[15], 0xff, 10);
    rv = libusb_bulk_transfer(vnap->vna_udhp, WRITE_ENDPOINT,
		buffer, sizeof(buffer), &transferred, USB_TIMEOUT);
    if (rv < 0) {
	_n2pkvna_error(vnap,
		"%s: libusb_bulk_transfer: %s",
		vnap->vna_config.nci_basename, libusb_error_name(rv));
	_n2pkvna_set_usb_errno(rv);
	return -1;
    }

    /*
     * Fail if we didn't transfer all bytes.
     */
    if (transferred != sizeof(buffer)) {
	_n2pkvna_error(vnap,
		"%s: libusb_bulk_transfer: short write",
		vnap->vna_config.nci_basename);
	_n2pkvna_set_usb_errno(rv);
	return -1;
    }
    return 0;
}
