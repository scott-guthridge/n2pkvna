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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2pkvna_internal.h"

/*
 * n2pkvna_reset: reset an N2PK VNA
 *   @vnap: n2pkvna handle
 * 
 * Return:
 *   0: success
 *  -1: error (errno set)
 */
int n2pkvna_reset(n2pkvna_t *vnap)
{
    unsigned char buffer[32];
    int transferred;
    int rv;

    /*
     * Flush any unread data.
     */
    _n2pkvna_flush_input(vnap);

    /*
     * Send N2PK VNA configure command.
     */
    (void)memset((void *)buffer, 0, sizeof(buffer));
    buffer[0] = 0xA5;	/* configure */
    buffer[1] = 0xC0;	/* set OSR override, minDelay */
    buffer[2] = 0xFF;	/* disable OSR override */
    buffer[3] = 0x04;	/* minDelay set to 4us default */
    if ((rv = libusb_bulk_transfer(vnap->vna_udhp, WRITE_ENDPOINT, buffer, 4,
					&transferred, USB_TIMEOUT)) < 0) {
	_n2pkvna_error(vnap,
		"%s: n2pkvna_reset: libusb_bulk_transfer: %s",
		vnap->vna_config.nci_basename, libusb_error_name(rv));
	_n2pkvna_set_usb_errno(rv);
	return -1;
    }
    if (transferred < 4) {
	_n2pkvna_error(vnap,
		"%s: n2pkvna_reset: libusb_bulk_transfer: short write",
		vnap->vna_config.nci_basename);
	_n2pkvna_set_usb_errno(rv);
	return -1;
    }

    /*
     * Send N2PK VNA reset command.
     */
    (void)memset((void *)buffer, 0, sizeof(buffer));
    buffer[0] = 0x55;
    buffer[1] = 0x80;	/* reset */
    if ((rv = libusb_bulk_transfer(vnap->vna_udhp, WRITE_ENDPOINT, buffer, 15,
					&transferred, USB_TIMEOUT)) < 0) {
	_n2pkvna_error(vnap,
		"%s: n2pkvna_reset: libusb_bulk_transfer: %s",
		vnap->vna_config.nci_basename, libusb_error_name(rv));
	_n2pkvna_set_usb_errno(rv);
	return -1;
    }
    if (transferred < 15) {
	_n2pkvna_error(vnap,
		"%s: n2pkvna_reset: libusb_bulk_transfer: short write",
		vnap->vna_config.nci_basename);
	_n2pkvna_set_usb_errno(rv);
	return -1;
    }

    /*
     * Flush any data that was already in the pipeline at the time of
     * the first flush.
     */
    _n2pkvna_flush_input(vnap);

    return 0;
}
