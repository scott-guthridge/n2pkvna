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
#include <yaml.h>

#include "n2pkvna_internal.h"

/*
 * _n2pkvna_parse_int: parse an int value from a YAML map
 *   @vnap: n2pkvna handle
 *   @filename: YAML filename (for error messages)
 *   @key: YAML map key
 *   @value: YAML map value
 *   @min: minimum allowed value
 *   @max: maximum allowed value
 *   @result: address of result
 *
 * Return:
 *    0: success
 *   -1: error
 */
static int _n2pkvna_parse_int(n2pkvna_t *vnap, const char *filename,
	yaml_node_t *key, yaml_node_t *value,
	int min, int max, int *result)
{
    int temp;

    if (value->type != YAML_SCALAR_NODE) {
	_n2pkvna_error(vnap,
		"%s (line %ld): error: unexpected non-scalar value",
		filename, value->start_mark.line + 1);
	return -1;
    }
    if (sscanf((char *)value->data.scalar.value, "%i %*c", &temp) != 1 ||
	    temp < min || temp > max) {
	_n2pkvna_error(vnap,
		"%s (line %ld): error: %s: invalid value",
		filename, value->start_mark.line + 1,
		key->data.scalar.value);
	return -1;
    }
    *result = temp;
    return 0;
}

/*
 * _n2pkvna_parse_int: parse a double value from a YAML map
 *   @vnap: n2pkvna handle
 *   @filename: YAML filename (for error messages)
 *   @key: YAML map key
 *   @value: YAML map value
 *   @min: minimum allowed value
 *   @max: maximum allowed value
 *   @result: address of result
 *
 * Return:
 *    0: success
 *   -1: error
 */
static int _n2pkvna_parse_double(n2pkvna_t *vnap, const char *filename,
	yaml_node_t *key, yaml_node_t *value,
	double min, double max, double *result)
{
    double temp;

    if (value->type != YAML_SCALAR_NODE) {
	_n2pkvna_error(vnap,
		"%s (line %ld): error: unexpected non-scalar value",
		filename, value->start_mark.line + 1);
	return -1;
    }
    if (sscanf((char *)value->data.scalar.value, "%lf %*c", &temp) != 1 ||
	    temp < min || temp > max) {
	_n2pkvna_error(vnap,
		"%s (line %ld): error: %s: invalid value",
		filename, value->start_mark.line + 1,
		key->data.scalar.value);
	return -1;
    }
    *result = temp;
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
    yaml_parser_t parser;
    yaml_document_t document;
    bool parser_active = false;
    bool document_active = false;
    yaml_node_t *root;
    yaml_node_pair_t *pair;
    int rv = -1;

    /*
     * Init the members of the logical device struct that come from the config
     * file.
     */
    ncip->nci_type = '\000';
    (void)memset((void *)&ncip->u, 0, sizeof(ncip->u));
    ncip->nci_reference_frequency = AD9851_CLOCK;

    /*
     * Open the config file.  If create is true, then it's not an error for it
     * not to exist.
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
    yaml_parser_initialize(&parser);
    parser_active = true;
    yaml_parser_set_input_file(&parser, fp);
    if (!yaml_parser_load(&parser, &document)) {
	_n2pkvna_error(vnap, "%s (line %ld) error: %s",
		filename, (long)parser.problem_mark.line + 1, parser.problem);
	goto out;
    }
    document_active = true;
    (void)fclose(fp);
    fp = NULL;
    root = yaml_document_get_root_node(&document);
    if (root == NULL) {
	_n2pkvna_error(vnap, "%s: empty config file", filename);
	goto out;
    }
    if (root->type != YAML_MAPPING_NODE) {
	_n2pkvna_error(vnap, "%s (line %ld): error: expected map",
	    filename, root->start_mark.line + 1);
	goto out;
    }
    for (pair = root->data.mapping.pairs.start;
         pair < root->data.mapping.pairs.top; ++pair) {
	yaml_node_t *key, *value;
	int temp;
	double lf;

	key   = yaml_document_get_node(&document, pair->key);
	value = yaml_document_get_node(&document, pair->value);
	if (key->type != YAML_SCALAR_NODE) {
	    _n2pkvna_error(vnap,
		    "%s (line %ld): warning: ignoring non-scalar key",
		    filename, key->start_mark.line + 1);
	    continue;
	}
	if (strcmp((char *)key->data.scalar.value, "usbVendor") == 0) {
	    if (_n2pkvna_parse_int(vnap, filename, key, value,
			0, 0xFFFF, &temp) == -1) {
		goto out;
	    }
	    ncip->nci_type = N2PKVNA_ADR_USB;
	    ncip->nci_usb_vendor = temp;
	    continue;
	}
	if (strcmp((char *)key->data.scalar.value, "usbProduct") == 0) {
	    if (_n2pkvna_parse_int(vnap, filename, key, value,
			0, 0xFFFF, &temp) == -1) {
		goto out;
	    }
	    ncip->nci_type = N2PKVNA_ADR_USB;
	    ncip->nci_usb_product = temp;
	    continue;
	}
	if (strcmp((char *)key->data.scalar.value, "referenceFrequency") == 0) {
	    if (_n2pkvna_parse_double(vnap, filename, key, value,
			MIN_CLOCK, MAX_CLOCK, &lf) == -1) {
		goto out;
	    }
	    ncip->nci_reference_frequency = lf;
	    continue;
	}
	_n2pkvna_error(vnap,
		"%s (line %ld): warning: %s: unknown attribute",
		filename, key->start_mark.line + 1, key->data.scalar.value);
	continue;
    }
    rv = 0;

out:
    if (document_active) {
	yaml_document_delete(&document);
	document_active = false;
    }
    if (parser_active) {
	yaml_parser_delete(&parser);
	parser_active = false;
    }
    if (fp != NULL) {
	(void)fclose(fp);
	fp = NULL;
    }
    free((void *)filename);
    return rv;
}
