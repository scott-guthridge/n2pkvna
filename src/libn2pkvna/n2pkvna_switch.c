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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2pkvna_internal.h"

/*
 * n2pkvna_switch: change VNA switch settings
 *   @vnap: n2pkvna handle
 *   @switch_value: new switch setting [0..3], or -1 for no change
 *   @attentuator_value: new attenuator setting [0..7], or -1 for no change
 *   @delay: delay time in s for the new settings to settle
 *
 * Return:
 *   0: success
 *  -1: error (errno set)
 */
int n2pkvna_switch(n2pkvna_t *vnap, int switch_value, int attenuator_value,
	double delay)
{
    unsigned char cmd[7];
    int transferred;
    int rv;

    /*
     * Validate parameters.
     */
    if (switch_value < -1 || switch_value > 3) {
	_n2pkvna_error(vnap,
		"invalid switch value %d", switch_value);
	errno = EINVAL;
	return -1;
    }
    if (switch_value < -1 || switch_value > 7) {
	_n2pkvna_error(vnap,
		"invalid attenuator value %d", attenuator_value);
	errno = EINVAL;
	return -1;
    }
    if (delay < 0.0 || delay > 100.0) {
	_n2pkvna_error(vnap,
		"invalid delay value %f", delay);
	errno = EINVAL;
	return -1;
    }

    /*
     * Send N2PK VNA raw command.
     *	 cmd[0] op code (0x5A=raw)
     *	 cmd[1] flags (0x08=set switch, 0x20=set attenuator)
     *	 cmd[2] port A value
     *	 cmd[3] port B value
     *	 cmd[4] attenuator value
     *	 cmd[5] port D value
     *	 cmd[6] switch value
     */
    (void)memset((void *)cmd, 0, sizeof(cmd));
    cmd[0] = 0x5A;	/* raw */
    if (switch_value >= 0) {
	cmd[1] |= 0x08;
	cmd[6] = (unsigned char)switch_value;
    }
    if (attenuator_value >= 0) {
	cmd[1] |= 0x20;
	cmd[4] = (unsigned char)attenuator_value;
    }
    if ((rv = libusb_bulk_transfer(vnap->vna_udhp, WRITE_ENDPOINT, cmd, 7,
					&transferred, USB_TIMEOUT)) < 0) {
	_n2pkvna_error(vnap,
		"%s: n2pkvna_switch: libusb_bulk_transfer: %s",
		vnap->vna_config.nci_basename, libusb_error_name(rv));
	_n2pkvna_set_usb_errno(rv);
	return -1;
    }
    if (transferred < 7) {
	_n2pkvna_error(vnap,
		"%s: n2pkvna_switch: libusb_bulk_transfer: short write",
		vnap->vna_config.nci_basename);
	_n2pkvna_set_usb_errno(rv);
	return -1;
    }
    if (_n2pkvna_read_status(vnap, 0x5A, 0, NULL)) {
	return -1;
    }

    /*
     * Delay.
     */
    if (delay > 0.0) {
	double integral, fractional;
	struct timespec tv;

	fractional = modf(delay, &integral);
	tv.tv_sec  = (time_t)integral;
	tv.tv_nsec = (long)floor(1.0e+9 * fractional);
	while (nanosleep(&tv, &tv) == -1 && errno == EINTR)
	    /*NULL*/;
    }
    return 0;
}
