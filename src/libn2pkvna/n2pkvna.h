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

#ifndef VNA_H
#define VNA_H

#ifndef _COMPLEX_H
#include <complex.h>
#endif /* _COMPLEX_H */

#ifndef _STDINT_H
#include <stdint.h>
#endif /* _STDINT_H */

#ifndef _STDLIB_H
#include <stdlib.h>
#endif /* _STDLIB_H */

#ifndef _STDBOOL_H
#include <stdbool.h>
#endif /* _STDBOOL_H */

#ifdef __cplusplus
extern "C" {
#endif

/* n2pkvna_error_t: prototype for optional error function */
typedef void n2pkvna_error_t(const char *message, void *arg);

/* n2pkvna_t: opaque handle returned from n2pkvna_open */
typedef struct n2pkvna n2pkvna_t;

/* values for adr_type field */
#define N2PKVNA_ADR_ANY		0x00000000	/* unspecified interface */
#define N2PKVNA_ADR_USB		0x75736201	/* USB (v1 struct) */

/* n2pkvna_address_v1_t: device address information (version 1) */
typedef struct n2pkvna_address {
    uint32_t		adr_type;
    uint32_t		_adr_reserved4;		/* reserved (zero) */
    union {
	struct {
	    uint16_t	_adr_usb_vendor;	/* USB vendor  ID or 0 */
	    uint16_t	_adr_usb_product;	/* USB product ID or 0 */
	    uint8_t	_adr_usb_bus;		/* USB bus	  or 0 */
	    uint8_t	_adr_usb_port;		/* USB port	  or 0 */
	    uint8_t	_adr_usb_device;	/* USB device	  or 0 */
	    uint8_t	_adr_usb_reserved;	/* reserved (zero)     */
	} usb;
    } u;
} n2pkvna_address_v1_t;

/* hide the union */
#define adr_usb_vendor	u.usb._adr_usb_vendor
#define adr_usb_product	u.usb._adr_usb_product
#define adr_usb_bus	u.usb._adr_usb_bus
#define adr_usb_port	u.usb._adr_usb_port
#define adr_usb_device	u.usb._adr_usb_device

/* n2pkvna_address_t: current version */
typedef n2pkvna_address_v1_t n2pkvna_address_t;

/* n2pkvna_device_t: config directory and matching devices */
typedef struct n2pkvna_config {
    char	       *nc_directory;	/* full path to config directory */
    n2pkvna_address_t **nc_addresses;	/* vector of pointers to addresses */
    size_t		nc_count;	/* number of addresses */
} n2pkvna_config_t;

/* n2pkvna_open: open and reset the n2pkvna device */
extern n2pkvna_t *n2pkvna_open(const char *name, bool create,
	const char *unit, n2pkvna_config_t ***config_vector,
	n2pkvna_error_t *error_fn, void *error_arg);

/* n2pkvna_get_directory: return the configuration directory for this device */
extern const char *n2pkvna_get_directory(n2pkvna_t *vnap);

/* n2pkvna_get_address: return the current device address info */
extern const n2pkvna_address_t *n2pkvna_get_address(n2pkvna_t *vnap);

/* n2pkvna_get_reference_frequency: return the internal oscillator frequency */
extern double n2pkvna_get_reference_frequency(const n2pkvna_t *vnap);

/* n2pkvna_set_reference_frequency: set the internal oscillator frequency */
extern int n2pkvna_set_reference_frequency(n2pkvna_t *vnap, double frequency);

/* n2pkvna_free_config_vector: free a device vector */
extern void n2pkvna_free_config_vector(n2pkvna_config_t **ncpp);

/* n2pkvna_scan: scan frequency range and measure */
extern int n2pkvna_scan(n2pkvna_t *vnap, double f0, double ff,
	unsigned int n, bool linear, double *frequency_vector,
	double complex *detector_vector1, double complex *detector_vector2);

/* n2pkvna_generate: generate signals with the given frequencies and phase */
extern int n2pkvna_generate(n2pkvna_t *vnap, double rf_frequency,
	double lo_frequency, double phase);

/* n2pkvna_switch: change the N2PK VNA switch values */
extern int n2pkvna_switch(n2pkvna_t *vnap, int switch_value,
	int attenuator_value, double delay_time);

/* n2pkvna_reset: reset and re-synchronize the RF signal generators */
extern int n2pkvna_reset(n2pkvna_t *vnap);

/* n2pkvna_save: write a new config file */
extern int n2pkvna_save(n2pkvna_t *vnap);

/* close an N2PK VNA and release resources obtained in open */
extern void n2pkvna_close(n2pkvna_t *vnap);

#ifdef __cplusplus
} /* extern "C" */
#endif
			
#endif /* VNA_H */
