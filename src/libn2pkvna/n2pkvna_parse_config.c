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
#include <vnaproperty.h>

#include "n2pkvna_internal.h"

/*
 * n2pkvna_get_property_root: return the address of the property root
 *   @vnap: n2pkvna handle
 */
vnaproperty_t **n2pkvna_get_property_root(n2pkvna_t *vnap)
{
    return vnaproperty_set_subtree(&vnap->vna_property_root, "properties");
}

/*
 * parse_int: parse an int value from a
 *   @vnap: n2pkvna handle
 *   @filename: YAML filename (for error messages)
 *   @map: root of a vnaproperty_t map
 *   @key: map key
 *   @min: minimum allowed value
 *   @max: maximum allowed value
 *   @result: address of result
 *
 * Return:
 *    0: success
 *   -1: error
 */
static int parse_int(n2pkvna_t *vnap, const char *filename,
	const vnaproperty_t *map, const char *key,
	int min, int max, int *result)
{
    const char *string;
    int value;
    char c;

    if ((string = vnaproperty_get(map, "%s", key)) == NULL) {
	_n2pkvna_error(vnap,
		"%s: error: %s: unexpected null value",
		filename, key);
	return -1;
    }
    if (sscanf(string, "%i %c", &value, &c) != 1) {
	_n2pkvna_error(vnap,
		"%s: error: %s: \"%s\": invalid integer",
		filename, key, string);
	return -1;
    }
    if (value < min || value > max) {
	_n2pkvna_error(vnap,
		"%s: error: %s: value must be in range %d .. %d",
		filename, key, min, max);
	return -1;
    }
    *result = value;
    return 0;
}

/*
 * parse_double: parse a double value from a YAML map
 *   @vnap: n2pkvna handle
 *   @filename: YAML filename (for error messages)
 *   @map: root of a vnaproperty_t map
 *   @key: map key
 *   @min: minimum allowed value
 *   @max: maximum allowed value
 *   @result: address of result
 *
 * Return:
 *    0: success
 *   -1: error
 */
static int parse_double(n2pkvna_t *vnap, const char *filename,
	const vnaproperty_t *map, const char *key,
	double min, double max, double *result)
{
    const char *string;
    double value;
    char c;

    if ((string = vnaproperty_get(map, "%s", key)) == NULL) {
	_n2pkvna_error(vnap,
		"%s: error: %s: unexpected null value",
		filename, key);
	return -1;
    }
    if (sscanf(string, "%lf %c", &value, &c) != 1) {
	_n2pkvna_error(vnap,
		"%s: error: %s: \"%s\": invalid number",
		filename, key, string);
	return -1;
    }
    if (value < min || value > max) {
	_n2pkvna_error(vnap,
		"%s: error: %s: value must be in range %g .. %g",
		filename, key, min, max);
	return -1;
    }
    *result = value;
    return 0;
}

/*
 * _n2pkvna_parse_config: parse an n2pkvna config file
 *   @vnap: n2pkvna handle
 *   @ncip: logical device information
 *   @create: true if it's not an error for the configuration not to exit
 */
int _n2pkvna_parse_config(n2pkvna_t *vnap,
	n2pkvna_config_internal_t *ncip, bool create)
{
    char *filename = NULL;
    FILE *fp = NULL;
    const char **element_names = NULL;
    int i_temp;
    double lf_temp;
    int rv = -1;

    /*
     * Init the members of the logical device struct that come from the config
     * file.
     */
    ncip->nci_type = '\000';
    (void)memset((void *)&ncip->u, 0, sizeof(ncip->u));
    ncip->nci_reference_frequency = AD9851_CLOCK;

    /*
     * Load the config file.  If create is true, then it's not an error
     * for it not to exist.
     */
    if (asprintf(&filename, "%s/config", ncip->nci_directory) == -1) {
	_n2pkvna_error(vnap, "asprintf: %s", strerror(errno));
	goto out;
    }
    if ((fp = fopen(filename, "r")) == NULL) {
	if (errno == ENOENT && create) {
	    rv = 0;
	    goto out;
	}
	_n2pkvna_error(vnap, "%s: %s", filename, strerror(errno));
	goto out;
    }
    {
	char config_line[80];
	char *cp;

	if (fgets(config_line, sizeof(config_line), fp) == NULL) {
	    _n2pkvna_error(vnap, "warning: %s: unexpected empty config file",
		filename);
	    rv = 0;
	    goto out;
	}
	config_line[sizeof(config_line) - 1] = '\000';
	if ((cp = strrchr(config_line, '\n')) != NULL) {
	    *cp = '\000';
	}
	if (strcmp(config_line, "#N2PKVNA_CONFIG") != 0) {
	    _n2pkvna_error(vnap, "error: %s: expected #N2PKVNA_CONFIG "
		    "header line", filename);
	    goto out;
	}
    }
    if (vnaproperty_import_yaml_from_file(&vnap->vna_property_root,
		fp, filename, _n2pkvna_libvna_errfn, NULL) == -1) {
	goto out;
    }
    if (vnap->vna_property_root == NULL) {
	rv = 0;
	goto out;
    }

    /*
     * Parse the existing configuration.
     */
    errno = 0;
    if ((element_names = vnaproperty_keys(vnap->vna_property_root,
		    "{}")) == NULL) {
	if (errno == ENOENT || errno == EINVAL) {
	    _n2pkvna_error(vnap,
		    "%s: error: cannot parse config file", filename);
	    goto out;
	}
    }
    for (const char **cpp = element_names; *cpp != NULL; ++cpp) {
	switch (**cpp) {
	case 'p':
	    if (strcmp(*cpp, "properties") == 0) {
		continue;
	    }
	    break;

	case 'r':
	    if (strcmp(*cpp, "referenceFrequency") == 0) {
		if (parse_double(vnap, filename, vnap->vna_property_root, *cpp,
			    MIN_CLOCK, MAX_CLOCK, &lf_temp) == -1) {
		    goto out;
		}
		ncip->nci_reference_frequency = lf_temp;
		continue;
	    }
	    break;

	case 'u':
	    if (strcmp(*cpp, "usbVendor") == 0) {
		if (parse_int(vnap, filename, vnap->vna_property_root, *cpp,
			    0, 0xFFFF, &i_temp) == -1) {
		    goto out;
		}
		ncip->nci_type = N2PKVNA_ADR_USB;
		ncip->nci_usb_vendor = i_temp;
		continue;
	    }
	    if (strcmp(*cpp, "usbProduct") == 0) {
		if (parse_int(vnap, filename, vnap->vna_property_root, *cpp,
			    0, 0xFFFF, &i_temp) == -1) {
		    goto out;
		}
		ncip->nci_type = N2PKVNA_ADR_USB;
		ncip->nci_usb_product = i_temp;
		continue;
	    }
	    break;

	default:
	    break;
	}
	_n2pkvna_error(vnap, "%s: warning: %s: unknown attribute",
		filename, *cpp);
	continue;
    }
    rv = 0;

out:
    free((void *)element_names);
    if (fp != NULL) {
	(void)fclose(fp);
	fp = NULL;
    }
    free((void *)filename);
    return rv;
}
