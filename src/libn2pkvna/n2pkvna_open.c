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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>	/* for mkdir and stat */
#include <sys/types.h>
#include <unistd.h>

#include "n2pkvna_internal.h"


/*
 * n2pkvna_get_directory: return the configuration directory for this device
 *   @vnap: n2pkvna handle
 */
const char *n2pkvna_get_directory(n2pkvna_t *vnap)
{
    return vnap->vna_config.nci_directory;
}

/*
 * n2pkvna_get_address: return the current device address info
 *   @vnap: n2pkvna handle
 */
const n2pkvna_address_t *n2pkvna_get_address(n2pkvna_t *vnap)
{
    n2pkvna_address_internal_t *adrip = &vnap->vna_address;

    return &adrip->adri_address;
}

/*
 * n2pkvna_get_reference_frequency: get the internal reference frequency
 *   @vnap: n2pkvna handle
 */
double n2pkvna_get_reference_frequency(const n2pkvna_t *vnap)
{
    return vnap->vna_config.nci_reference_frequency;
}

/*
 * n2pkvna_set_reference_frequency: change internal reference frequency
 *   @vnap: n2pkvna handle
 *   @frequency: new reference frequency, or 0.0 to restore the default value
 */
int n2pkvna_set_reference_frequency(n2pkvna_t *vnap, double frequency)
{
    if (frequency == 0.0) {
	vnap->vna_config.nci_reference_frequency = AD9851_CLOCK;
	return 0;
    }
    if (frequency < MIN_CLOCK || frequency > MAX_CLOCK) {
	_n2pkvna_error(vnap,
		"invalid reference frequency %f", frequency);
	errno = EINVAL;
	return -1;
    }
    vnap->vna_config.nci_reference_frequency = frequency;
    return 0;
}

/*
 * n2pkvna_open: open and reset the n2pkvna device
 *   @name: optional N2PKVNA device name or path to device directory
 *   @unit: optional device unit address string
 *   @create: create the config file if it doesn't exist
 *   @config_vector: optional address of pointer to receive config vector
 *   @error_fn: optional error reporting function
 *   @error_arg: optional argument to error reporting function
 */
n2pkvna_t *n2pkvna_open(const char *name, bool create, const char *unit,
	n2pkvna_config_t ***config_vector,
	n2pkvna_error_t *error_fn, void *error_arg)
{
    n2pkvna_t *vnap = NULL;
    static const char dotdir[] = ".n2pkvna";
    char *home = NULL;
    n2pkvna_config_internal_t *ncip_vector = NULL;
    size_t config_count = 0;
    n2pkvna_address_t address;
    n2pkvna_address_internal_t **adripp_vector = NULL;
    size_t address_count = 0;
    size_t matching_addresss = 0;
    n2pkvna_config_internal_t *ncip_match = NULL;
    libusb_device **usb_device_vector = NULL;
    ssize_t usb_device_count;
    int rv;
    char *lock_filename = NULL;
    bool success = false;

    /*
     * If config_vector was given, initialize the returned value to NULL.
     */
    if (config_vector != NULL)
	*config_vector = NULL;

    /*
     * Allocate and init the n2pkvna_t object.
     */
    if ((vnap = malloc(sizeof(n2pkvna_t))) == NULL) {
	if (error_fn != NULL) {
            char buf[80];

            (void)snprintf(buf, sizeof(buf), "n2pkvna_open: %s",
		    strerror(errno));
            buf[sizeof(buf)-1] = '\000';
            (*error_fn)(error_arg, buf);
        }
        return NULL;
    }
    (void)memset((void *)vnap, 0, sizeof(n2pkvna_t));
    vnap->vna_lockfd = -1;
    vnap->vna_error_fn  = error_fn;
    vnap->vna_error_arg = error_arg;

    /*
     * If a device address was given, parse the address.
     */
    (void)memset((void *)&address, 0, sizeof(address));
    if (unit != NULL) {
	if (_n2pkvna_parse_address(&address, unit) == -1) {
	    _n2pkvna_error(vnap, "invalid device unit address: %s", unit);
	    errno = EINVAL;
	    goto out;
	}
    }

    /*
     * Get the home direcotry from the environment.
     */
    home = getenv("HOME");

    /*
     * If we're creating and home is defined, create the top-level
     * directory if needed.
     */
    if (create && home != NULL) {
	struct stat st;
	char *path = NULL;

	if (asprintf(&path, "%s/%s", home, dotdir) == -1) {
	    _n2pkvna_error(vnap, "asprintf: %s", strerror(errno));
	    goto out;
	}
	if (stat(path, &st) == 0) {
	    if (!S_ISDIR(st.st_mode)) {
		_n2pkvna_error(vnap, "stat: %s: not a directory", path);
		free((void *)path);
		goto out;
	    }
	} else {
	    if (errno != ENOENT) {
		_n2pkvna_error(vnap, "stat: %s: %s", strerror(errno));
		free((void *)path);
		goto out;
	    }
	    if (mkdir(path, 0777) == -1) {
		_n2pkvna_error(vnap, "mkdir: %s: %s", strerror(errno));
		free((void *)path);
		goto out;
	    }
	}
	free((void *)path);
    }

    /*
     * If no config name was given, glob search ~/.n2pkvna for directories
     * with config files.
     */
    if (name == NULL) {
	char *pattern;
	glob_t dir_glob;

	if (home == NULL) {
	    _n2pkvna_error(vnap, "no configuration name was given and HOME "
		    "is not set");
	    errno = ESRCH;
	    goto out;
	}
	if (asprintf(&pattern, "%s/%s/*/config",
		    home, dotdir) == -1) {
	    _n2pkvna_error(vnap, "asprintf: %s", strerror(errno));
	    goto out;
	}
	(void)memset((void *)&dir_glob, 0, sizeof(dir_glob));
	switch (glob(pattern, 0, NULL, &dir_glob)) {
	case 0:
	    break;

	case GLOB_NOMATCH:
	    free((void *)pattern);
	    pattern = NULL;
	    goto get_addresss;

	case GLOB_NOSPACE:
	    free((void *)pattern);
	    pattern = NULL;
	    errno = ENOMEM;
	    _n2pkvna_error(vnap, "glob: %s", strerror(errno));
	    goto out;

        case GLOB_ABORTED:
	default:
	    free((void *)pattern);
	    pattern = NULL;
	    errno = EIO;
	    _n2pkvna_error(vnap, "glob: %s", strerror(errno));
	    goto out;
	}
	free((void *)pattern);
	pattern = NULL;
	ncip_vector = calloc(dir_glob.gl_pathc,
		sizeof(n2pkvna_config_internal_t));
	if (ncip_vector == NULL) {
	    globfree(&dir_glob);
	    _n2pkvna_error(vnap, "calloc: %s", strerror(errno));
	    goto out;
	}
	for (size_t s = 0; s < dir_glob.gl_pathc; ++s) {
	    char *cp;
	    n2pkvna_config_internal_t *ncip = &ncip_vector[s];

	    if ((cp = strrchr(dir_glob.gl_pathv[s], '/')) != NULL) {
		*cp = '\000';
	    }
	    ncip->nci_directory = strdup(dir_glob.gl_pathv[s]);
	    if (ncip->nci_directory == NULL) {
		globfree(&dir_glob);
		_n2pkvna_error(vnap, "strdup: %s", strerror(errno));
		goto out;
	    }
	}
	config_count = dir_glob.gl_pathc;
	globfree(&dir_glob);

    /*
     * The caller specified a config name.
     */
    } else {
	if ((ncip_vector = calloc(1,
			sizeof(n2pkvna_config_internal_t))) == NULL) {
	    _n2pkvna_error(vnap, "calloc: %s", strerror(errno));
	    goto out;
	}

	/*
	 * If the config name doesn't contain a slash, use
	 * $HOME/.n2kpvna/<name>.  Otherwise, use the caller's
	 * name as the config directory.
	 */
	if (strchr(name, '/') == NULL) {
	    if (home == NULL) {
		_n2pkvna_error(vnap, "no configuration name was given and "
			"HOME is not set");
		errno = ESRCH;
		goto out;
	    }
	    if (asprintf(&ncip_vector->nci_directory, "%s/%s/%s",
			home, dotdir, name) == -1) {
		_n2pkvna_error(vnap, "asprintf: %s", strerror(errno));
		goto out;
	    }
	} else {
	    ncip_vector->nci_directory = strdup(name);
	    if (ncip_vector->nci_directory == NULL) {
		_n2pkvna_error(vnap, "strdup: %s", strerror(errno));
		goto out;
	    }
	}
	config_count = 1;
    }

    /*
     * For each device configuration, set the basename and parse the
     * config file.
     */
    for (size_t s = 0; s < config_count; ++s) {
	n2pkvna_config_internal_t *ncip = &ncip_vector[s];
	char *cp;

	if ((cp = strrchr(ncip->nci_directory, '/')) != NULL) {
	    ncip->nci_basename = cp + 1;
	} else {
	    ncip->nci_basename = ncip->nci_directory;
	}
	if (_n2pkvna_parse_config(vnap, ncip, create) == -1) {
	    goto out;
	}
    }

get_addresss:
    /*
     * Open the USB library, get the device list and create adripp_vector.
     * Note: if future interfaces are added (e.g. parallel), add the devices
     * here.  Those may be based on address information we parsed from the
     * device config files, information we can discover from the system, or
     * static addresses.
     */
    if ((rv = libusb_init(&vnap->vna_ctxp)) < 0) {
	_n2pkvna_error(vnap, "libusb_init: %s", libusb_error_name(rv));
	_n2pkvna_set_usb_errno(rv);
	goto out;
    }
    if ((usb_device_count = libusb_get_device_list(vnap->vna_ctxp,
	    &usb_device_vector)) < 0) {
	_n2pkvna_error(vnap, "libusb_get_device_list: %s",
		libusb_error_name(usb_device_count));
	_n2pkvna_set_usb_errno(usb_device_count);
	goto out;
    }
    if ((adripp_vector = calloc(usb_device_count,
		    sizeof(n2pkvna_address_internal_t *))) == NULL) {
	_n2pkvna_error(vnap, "calloc: %s", strerror(errno));
	goto out;
    }
    for (ssize_t i = 0; i < usb_device_count; ++i) {
	libusb_device *device = usb_device_vector[i];
	struct libusb_device_descriptor descriptor;
	n2pkvna_address_internal_t *adrip;

	if (libusb_get_device_descriptor(device, &descriptor) < 0) {
	    continue;
	}
	if (address.adr_usb_vendor != 0) {
	    if (descriptor.idVendor != address.adr_usb_vendor) {
		continue;
	    }
	/* If the vendor wasn't given, use the default of 0x0547. */
	} else if (descriptor.idVendor != 0x0547) {	/* Anchor Chips */
	    continue;
	}
	if (address.adr_usb_product != 0) {
	    if (descriptor.idProduct != address.adr_usb_product) {
		continue;
	    }
	/* If neither vendor nor product was given, the product has to
	   be one of these. */
	} else if (address.adr_usb_vendor == 0)
	    if (descriptor.idProduct != 0x100d &&	/* Ivan v5 500mA */
		descriptor.idProduct != 0x100b &&	/* Ivan v5 100mA */
		descriptor.idProduct != 0x1009 &&	/* Orig 500mA */
		descriptor.idProduct != 0x1005) {	/* Orig 100mA */
	    continue;
	}
	if (address.adr_usb_bus != 0 &&
		libusb_get_bus_number(device) != address.adr_usb_bus) {
	    continue;
	}
	if (address.adr_usb_port != 0 &&
		libusb_get_port_number(device) != address.adr_usb_port) {
	    continue;
	}
	if (address.adr_usb_device != 0 &&
		libusb_get_device_address(device) != address.adr_usb_device) {
	    continue;
	}
	if ((adrip = malloc(sizeof(n2pkvna_address_internal_t))) == NULL) {
	    _n2pkvna_error(vnap, "calloc: %s", strerror(errno));
	    goto out;
	}
	(void)memset((void *)adrip, 0, sizeof(*adrip));
	adrip->adri_type = N2PKVNA_ADR_USB;
	adrip->adri_usb_devicep = device;
	adrip->adri_usb_vendor = descriptor.idVendor;
	adrip->adri_usb_product = descriptor.idProduct;
	adrip->adri_usb_bus = libusb_get_bus_number(device);
	adrip->adri_usb_port = libusb_get_port_number(device);
	adrip->adri_usb_device = libusb_get_device_address(device);
	adripp_vector[address_count++] = adrip;
    }

    /*
     * Special-case.  If we're creating, we don't have any configuration
     * names, we have exactly one physical device and it's one of the
     * standard models, automatically form a configuration name for it.
     */
    if (config_count == 0 && create && address_count == 1) {
	n2pkvna_address_internal_t *adrip = adripp_vector[0];
	const char *home;

	if (adrip->adri_type != N2PKVNA_ADR_USB)
	    goto skip_special_case;

	if (adrip->adri_usb_vendor != 0x0547)
	    goto skip_special_case;

	switch (adrip->adri_usb_product) {
	case 0x100d:
	    name = "n2pkvna-v5-500mA";
	    break;
	case 0x100b:
	    name = "n2pkvna-v5-100mA";
	    break;
	case 0x1009:
	    name = "n2pkvna-500mA";
	    break;
	case 0x1005:
	    name = "n2pkvna-100mA";
	    break;
	default:
	    goto skip_special_case;
	}
	if ((home = getenv("HOME")) == NULL) {
	    goto skip_special_case;
	}
	ncip_vector = calloc(1, sizeof(n2pkvna_config_internal_t));
	if (ncip_vector == NULL) {
	    _n2pkvna_error(vnap, "calloc: %s", strerror(errno));
	    goto out;
	}
	if (asprintf(&ncip_vector->nci_directory, "%s/%s/%s",
		    home, dotdir, name) == -1) {
	    _n2pkvna_error(vnap, "asprintf: %s", strerror(errno));
	    goto out;
	}
	ncip_vector->nci_basename =
	    strrchr(ncip_vector->nci_directory, '/') + 1;
        ncip_vector->nci_reference_frequency = AD9851_CLOCK;
	config_count = 1;
    }
skip_special_case:

    /*
     * For each configuration, find all matching physical devices.
     */
    for (size_t u = 0; u < config_count; ++u) {
	n2pkvna_config_internal_t *ncip = &ncip_vector[u];

	if ((ncip->nci_addresses = calloc(address_count,
			sizeof(n2pkvna_address_internal_t *))) == NULL) {
	    _n2pkvna_error(vnap, "calloc: %s", strerror(errno));
	    goto out;
	}
	for (size_t v = 0; v < address_count; ++v) {
	    n2pkvna_address_internal_t *adrip = adripp_vector[v];

	    if (ncip->nci_type != '\000') {
		if (ncip->nci_type != adrip->adri_type) {
		    continue;
		}
		switch (adrip->adri_type) {
		case N2PKVNA_ADR_USB:
		    if (ncip->nci_usb_vendor != 0 &&
			    ncip->nci_usb_vendor != adrip->adri_usb_vendor) {
			continue;
		    }
		    if (ncip->nci_usb_product != 0 &&
			    ncip->nci_usb_product != adrip->adri_usb_product) {
			continue;
		    }
		    break;

		default:
		    abort();
		}
	    }
	    ncip->nci_addresses[ncip->nci_count++] = adrip;
	    ++matching_addresss;
	}
	if (ncip->nci_count == 1) {
	    ncip_match = ncip;
	}
    }

    /*
     * If config_vector is non-NULL, build the external device vector.
     */
    if (config_vector != NULL) {
	size_t lcount = 0;

	if ((*config_vector = calloc(config_count + 1,
			sizeof(n2pkvna_config_t *))) == NULL) {
	    _n2pkvna_error(vnap, "calloc: %s", strerror(errno));
	    goto out;
	}
	for (size_t u = 0; u < config_count; ++u) {
	    n2pkvna_config_internal_t *ncip = &ncip_vector[u];
	    n2pkvna_config_t *ncp;

	    /* skip configurations with no physical devices */
	    if (ncip->nci_count == 0) {
		continue;
	    }
	    if ((ncp = malloc(sizeof(n2pkvna_config_t))) == NULL) {
		_n2pkvna_error(vnap, "malloc: %s", strerror(errno));
		n2pkvna_free_config_vector(*config_vector);
		*config_vector = NULL;
		goto out;
	    }
	    (void)memset((void *)ncp, 0, sizeof(*ncp));
	    if ((ncp->nc_directory = strdup(ncip->nci_directory)) == NULL) {
		_n2pkvna_error(vnap, "strdup: %s", strerror(errno));
		n2pkvna_free_config_vector(*config_vector);
		*config_vector = NULL;
		goto out;
	    }
	    (*config_vector)[lcount++] = ncp;
	    if ((ncp->nc_addresses = calloc(ncip->nci_count + 1,
			    sizeof(n2pkvna_address_t *))) == NULL) {
		_n2pkvna_error(vnap, "calloc: %s", strerror(errno));
		n2pkvna_free_config_vector(*config_vector);
		*config_vector = NULL;
		goto out;
	    }
	    for (size_t v = 0; v < address_count; ++v) {
		n2pkvna_address_internal_t *adrip = adripp_vector[v];
		n2pkvna_address_t *adrp;

		if ((adrp = malloc(sizeof(n2pkvna_address_t))) == NULL) {
		    _n2pkvna_error(vnap, "malloc: %s", strerror(errno));
		    n2pkvna_free_config_vector(*config_vector);
		    *config_vector = NULL;
		    goto out;
		}
		(void)memset((void *)adrp, 0, sizeof(*adrp));
		adrp->adr_type = adrip->adri_type;
		switch (adrip->adri_type) {
		case N2PKVNA_ADR_USB:
		    *adrp = adrip->adri_address;
		    break;

		default:
		    abort();
		}
		ncp->nc_addresses[ncp->nc_count++] = adrp;
	    }
	}
    }

    /*
     * If there are no matching name/physical device pairs, or if
     * there is more than one matching name (with or without phyiscal
     * devices), fail.  Don't print error messages if we're returning
     * the device vector.
     */
    if (matching_addresss == 0) {
	if (config_vector == NULL)
	    _n2pkvna_error(vnap, "no matching VNA devices found");
	errno = ENOENT;
	goto out;
    }
    if (config_count > 1 || matching_addresss > 1) {
	if (config_vector == NULL)
	    _n2pkvna_error(vnap, "more than one matching VNA device found");
	errno = ERANGE;
	goto out;
    }

    /*
     * Take the directory name and basename from *ncip_match and
     * transfer them to vna_config.
     */
    vnap->vna_config.nci_directory = ncip_match->nci_directory;
    vnap->vna_config.nci_basename  = ncip_match->nci_basename;
    ncip_match->nci_directory = NULL;
    ncip_match->nci_basename  = NULL;
    assert(ncip_match->nci_count == 1);
    vnap->vna_address = *ncip_match->nci_addresses[0];
    /* nci_addresses and nci_count are unused and remain zero */

    /*
     * Lock the device.
     */
    if (create) {
	if (mkdir(vnap->vna_config.nci_directory, 0777) == -1 &&
		errno != EEXIST) {
	    _n2pkvna_error(vnap, "%s: mkdir: %s",
		    vnap->vna_config.nci_directory,
		    strerror(errno));
	    goto out;
	}
    }
    if (asprintf(&lock_filename, "%s/config.lck",
		vnap->vna_config.nci_directory) == -1) {
	_n2pkvna_error(vnap, "%s: asprintf: %s",
		vnap->vna_config.nci_basename, strerror(errno));
	goto out;
    }
    vnap->vna_lockfd = open(lock_filename, O_CREAT|O_RDWR, 0666);
    if (vnap->vna_lockfd == -1) {
	_n2pkvna_error(vnap, "%s: open: %s",
		vnap->vna_config.nci_basename, strerror(errno));
	goto out;
    }
    {
	struct flock lck;

	(void)memset((void *)&lck, 0, sizeof(lck));
	lck.l_type = F_WRLCK;
	lck.l_whence = SEEK_SET;
	lck.l_start = 0;
	lck.l_len = 0;
	if (fcntl(vnap->vna_lockfd, F_SETLK, &lck) == -1) {
	    if (errno == EACCES || errno == EAGAIN) {
		_n2pkvna_error(vnap, "%s: device is locked",
		    vnap->vna_config.nci_basename, strerror(errno));
		errno = EBUSY;
		goto out;
	    }
	    _n2pkvna_error(vnap, "%s: fcncl(F_SETLK): %s",
		    vnap->vna_config.nci_basename, strerror(errno));
	    goto out;
	}
    }

    /*
     * Re-read the config file under lock and make sure the device
     * information didn't become inconsistent with the physical device.
     * Update unknown values from the physical device.
     */
    {
	n2pkvna_config_internal_t *ncip = &vnap->vna_config;
	n2pkvna_address_internal_t *adrip = &vnap->vna_address;

	if (_n2pkvna_parse_config(vnap, &vnap->vna_config, create) == -1) {
	    goto out;
	}
	if (ncip->nci_type == 0) {
	    ncip->nci_type = adrip->adri_type;
	} else if (ncip->nci_type != adrip->adri_type) {
	    goto config_changed;
	}
	switch (ncip->nci_type) {
	case N2PKVNA_ADR_USB:
	    if (ncip->nci_usb_vendor == 0) {
		ncip->nci_usb_vendor = adrip->adri_usb_vendor;

	    } else if (ncip->nci_usb_vendor != adrip->adri_usb_vendor) {
		goto config_changed;
	    }
	    if (ncip->nci_usb_product == 0) {
		ncip->nci_usb_product = adrip->adri_usb_product;

	    } else if (ncip->nci_usb_product != adrip->adri_usb_product) {
		goto config_changed;
	    }
	    break;

	config_changed:
	    _n2pkvna_error(vnap,
		    "%s: device configuration changed after acquiring lock",
		    vnap->vna_config.nci_basename);
	    errno = EBUSY;
	    goto out;

	default:
	    abort();
	}
    }

    /*
     * Open the USB handle.
     */
    if ((rv = libusb_open(vnap->vna_address.adri_usb_devicep,
		    &vnap->vna_udhp)) < 0) {
	_n2pkvna_error(vnap, "%s: libusb_open: %s",
	    vnap->vna_config.nci_basename, libusb_error_name(rv));
	_n2pkvna_set_usb_errno(rv);
	goto out;
    }

    /*
     * If create, write the config file.
     */
    if (create) {
	if (n2pkvna_save(vnap) == -1) {
	    goto out;
	}
    }
    success = true;

out:
    free((void *)lock_filename);
    lock_filename = NULL;
    if (ncip_vector != NULL) {
	for (size_t u = 0; u < config_count; ++u) {
	    n2pkvna_config_internal_t *ncip = &ncip_vector[u];

	    free((void *)ncip->nci_directory);
	    free((void *)ncip->nci_addresses);
	}
	free((void *)ncip_vector);
	ncip_vector = NULL;
    }
    if (adripp_vector != NULL) {
	for (size_t v = 0; v < address_count; ++v) {
	    free((void *)adripp_vector[v]);
	}
	free((void *)adripp_vector);
	adripp_vector = NULL;
    }
    if (usb_device_vector != NULL) {
	libusb_free_device_list(usb_device_vector, /*unref_devices=*/1);
	usb_device_vector = NULL;
    }
    if (!success) {
	n2pkvna_close(vnap);
	vnap = NULL;
    }
    return vnap;
}

/*
 * n2pkvna_free_config_vector: free an n2pkvna_config_t vector
 *   @vector: pointer to vector of pointers to n2pkvna_config_t
 */
void n2pkvna_free_config_vector(n2pkvna_config_t **vector)
{

    if (vector != NULL) {
	for (n2pkvna_config_t **ncpp = vector; *ncpp != NULL; ++ncpp) {
	    n2pkvna_config_t *ncp = *ncpp;

	    free((void *)ncp->nc_directory);
	    for (size_t u = 0; u < ncp->nc_count; ++u) {
		free((void *)ncp->nc_addresses[u]);
	    }
	    free((void *)ncp->nc_addresses);
	    free((void *)ncp);
	}
	free((void *)vector);
    }
}

/*
 * n2pkvna_close: free an n2pkvna structure allocated by n2pkvna_open
 *   @vnap: n2pkvna handle
 */
void n2pkvna_close(n2pkvna_t *vnap)
{
    if (vnap->vna_udhp != NULL) {
	libusb_close(vnap->vna_udhp);
	vnap->vna_udhp = NULL;
    }
    if (vnap->vna_ctxp != NULL) {
	libusb_exit(vnap->vna_ctxp);
	vnap->vna_ctxp = NULL;
    }
    if (vnap->vna_lockfd != -1) {
	(void)close(vnap->vna_lockfd);
	vnap->vna_lockfd = -1;
    }
    free((void *)vnap->vna_config.nci_directory);
    free((void *)vnap);
    vnap = NULL;
}
