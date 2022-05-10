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

#ifndef _N2PKVNA_INTERNAL_H
#define _N2PKVNA_INTERNAL_H

#include <libusb-1.0/libusb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <vnaerr.h>
#include <vnaproperty.h>

#include "n2pkvna.h"

#define LTC2440_REF		2.5		/* V */
#define LTC2440_FULL		268435456.0	/* 2^28 */
#define MIN_CLOCK		50.0e+6		/* Hz */
#define MAX_CLOCK		500.e+6		/* Hz */
#define AD9851_CLOCK		156.25e+6	/* Hz */
#define USB_BUFSIZE		512		/* bytes */
#define WRITE_ENDPOINT		0x02
#define READ_ENDPOINT		0x86
#define USB_TIMEOUT		2000		/* ms */
#define ADC_MODE		0x47
#define HOLD_DELAY0		10e-3		/* 1st measurement (s) */
#define HOLD_DELAY1		62e-6		/* same frequency (s) */
#define HOLD_DELAY2		250e-6		/* new frequency (s) */
#define SWITCH_DELAY		0.25		/* s */

#define SQRT2	1.41421356237309504880168872420969807856967187537694

/*
 * n2pkvna_address_internal_t: physical device information (internal)
 */
typedef struct n2pkvna_address_internal {
    n2pkvna_address_t adri_address;		/* user visible struct */
    union {
	struct {
	    libusb_device *_adri_usb_devicep;
	} usb;
    } u;
} n2pkvna_address_internal_t;

#define adri_type		adri_address.adr_type
#define adri_usb_vendor		adri_address.adr_usb_vendor
#define adri_usb_product	adri_address.adr_usb_product
#define adri_usb_bus		adri_address.adr_usb_bus
#define adri_usb_port		adri_address.adr_usb_port
#define adri_usb_device		adri_address.adr_usb_device
#define adri_usb_devicep	u.usb._adri_usb_devicep

/*
 * n2pkvna_config_internal_t: configuration information (internal)
 */
typedef struct n2pkvna_config_internal {
    char *nci_directory;		/* full path to device directory */
    char *nci_basename;			/* short directory name */
    uint32_t nci_type;			/* N2PKVNA_ADR_* */
    union {
	struct {
	    uint16_t _nci_usb_vector;	/* USB vendor  ID or 0 */
	    uint16_t _nci_usb_product;	/* USB product ID or 0 */
	} usb;
    } u;
    double nci_reference_frequency;	/* reference oscillator frequency */
    n2pkvna_address_internal_t **nci_addresses; /* matching physical devices */
    size_t nci_count;			/* number of physical devices */
} n2pkvna_config_internal_t;

#define nci_usb_vendor	u.usb._nci_usb_vector
#define nci_usb_product	u.usb._nci_usb_product

/*
 * n2pkvna_t: N2PK VNA device handle
 */
struct n2pkvna {
    n2pkvna_config_internal_t vna_config;
    n2pkvna_address_internal_t vna_address;
    int vna_lockfd;
    struct libusb_context *vna_ctxp;
    struct libusb_device_handle *vna_udhp;
    n2pkvna_error_t *vna_error_fn;
    void *vna_error_arg;
    vnaproperty_t *vna_property_root;
};

/* _n2pkvna_error: report errors if error_fn is non-NULL */
extern void _n2pkvna_error(n2pkvna_t *vnap, const char *format, ...);

extern void _n2pkvna_libvna_errfn(const char *message, void *error_arg,
	vnaerr_category_t category);

/* _n2pkvna_flush_input: flush unread input from the N2PK VNA */
extern int _n2pkvna_flush_input(n2pkvna_t *vnap);

/* _n2pkvna_read_status: read status from the N2PK VNA */
extern int _n2pkvna_read_status(n2pkvna_t *vnap, uint8_t opcode,
	int n, double *values);

/* _n2pkvna_frequency_to_code: convert frequency to DDS code */
extern uint32_t _n2pkvna_frequency_to_code(double f0, double frequency);

/* _n2pkvna_code_to_frequency: convert DDS frequency code to Hz */
extern double _n2pkvna_code_to_frequency(double f0, uint32_t code);

/* _n2pkvna_phase_to_code: convert phase in degrees to DDS code (not shifted) */
extern uint8_t _n2pkvna_phase_to_code(double phase);

/* _n2pkvna_set_dds: set the DDS */
extern int _n2pkvna_set_dds(n2pkvna_t *vnap, bool measure, double start_delay,
	uint32_t lo_frequency_code, uint32_t rf_frequency_code,
	uint8_t phase_code);

/* _n2pkvna_parse_config: parse an n2pkvna config file */
extern int _n2pkvna_parse_config(n2pkvna_t *vnap,
	n2pkvna_config_internal_t *ncip, bool create);

/* _n2pkvna_set_usb_errno: set errno based on the libusb error code */
extern void _n2pkvna_set_usb_errno(int usb_error);

/* n2pkvna_parse_address: init address object from string */
extern int _n2pkvna_parse_address(n2pkvna_address_t *adrp,
	const char *string);

#endif /* _N2PKVNA_INTERNAL_H */
