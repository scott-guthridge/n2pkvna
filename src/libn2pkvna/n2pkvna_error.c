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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2pkvna_internal.h"

/*
 * _n2pkvna_error: report errors if error_fn is non-NULL
 *   @vnap: n2pkvna handle
 *   @format: printf format
 *   @...: var args
 */
void _n2pkvna_error(n2pkvna_t *vnap, const char *format, ...)
{
    va_list ap;

    if (vnap->vna_error_fn != NULL) {
	char *message = NULL;

	va_start(ap, format);
	if (vasprintf(&message, format, ap) == -1) {
	    char backup[80];

	    (void)snprintf(backup, sizeof(backup),
			   "vasprintf: %s", strerror(errno));
	    backup[sizeof(backup) - 1] = '\000';
	    (*vnap->vna_error_fn)(backup, vnap->vna_error_arg);
	}
	va_end(ap);
	(*vnap->vna_error_fn)(message, vnap->vna_error_arg);
	free((void *)message);
    }
}

/*
 * _n2pkvna_set_usb_errno: set errno based on the libusb error code
 *   @usb_error: LIBUSB_ERROR value from enum libusb_error
 */
void _n2pkvna_set_usb_errno(int usb_error)
{
    switch (usb_error) {
    case LIBUSB_SUCCESS:
	errno = 0;
	break;
    case LIBUSB_ERROR_IO:
	errno = EIO;
	break;
    case LIBUSB_ERROR_INVALID_PARAM:
	errno = EINVAL;
	break;
    case LIBUSB_ERROR_ACCESS:
	errno = EACCES;
	break;
    case LIBUSB_ERROR_NO_DEVICE:
	errno = ENODEV;
	break;
    case LIBUSB_ERROR_NOT_FOUND:
	errno = ENOENT;
	break;
    case LIBUSB_ERROR_BUSY:
	errno = EBUSY;
	break;
    case LIBUSB_ERROR_TIMEOUT:
	errno = ETIMEDOUT;
	break;
    case LIBUSB_ERROR_OVERFLOW:
	errno = ERANGE;
	break;
    case LIBUSB_ERROR_PIPE:
	errno = ESPIPE;
	break;
    case LIBUSB_ERROR_INTERRUPTED:
	errno = EINTR;
	break;
    case LIBUSB_ERROR_NO_MEM:
	errno = ENOMEM;
	break;
    case LIBUSB_ERROR_NOT_SUPPORTED:
	errno = ENOTSUP;
	break;
    case LIBUSB_ERROR_OTHER:
    default:
	errno = EIO;
	break;
    }
}
