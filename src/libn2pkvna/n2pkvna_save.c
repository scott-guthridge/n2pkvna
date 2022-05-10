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
#include <unistd.h>

#include "n2pkvna_internal.h"


/*
 * n2pkvna_save: save device address and oscillator frequency to config file
 *   @vnap: n2pkvna handle
 */
int n2pkvna_save(n2pkvna_t *vnap)
{
    char *cur_filename = NULL;
    char *new_filename = NULL;
    char *bak_filename = NULL;
    FILE *fp = NULL;
    int rv = -1;

    if (asprintf(&cur_filename, "%s/config",
		vnap->vna_config.nci_directory) == -1) {
       _n2pkvna_error(vnap, "%s: malloc: %s",
		vnap->vna_config.nci_basename, strerror(errno));
	goto out;
    }
    if (asprintf(&new_filename, "%s/config.new",
		vnap->vna_config.nci_directory) == -1) {
       _n2pkvna_error(vnap, "%s: malloc: %s",
		vnap->vna_config.nci_basename, strerror(errno));
	goto out;
    }
    if (asprintf(&bak_filename, "%s/config.bak",
		vnap->vna_config.nci_directory) == -1) {
       _n2pkvna_error(vnap, "%s: malloc: %s",
		vnap->vna_config.nci_basename, strerror(errno));
	goto out;
    }
    if ((fp = fopen(new_filename, "w")) == NULL) {
       _n2pkvna_error(vnap, "%s: fopen: %s: %s",
		vnap->vna_config.nci_basename, new_filename,
		strerror(errno));
	goto out;
    }

    /*
     * Add usbVendor
     */
    if (vnaproperty_set(&vnap->vna_property_root, "usbVendor=0x%04x",
		vnap->vna_address.adri_usb_vendor) == -1) {
       _n2pkvna_error(vnap, "%s: vnaproperty_set: %s: %s",
		vnap->vna_config.nci_basename, new_filename,
		strerror(errno));
	goto out;
    }

    /*
     * Add usbProduct
     */
    if (vnaproperty_set(&vnap->vna_property_root, "usbProduct=0x%04x",
		vnap->vna_address.adri_usb_product) == -1) {
       _n2pkvna_error(vnap, "%s: vnaproperty_set: %s: %s",
		vnap->vna_config.nci_basename, new_filename,
		strerror(errno));
	goto out;
    }

    /*
     * Add referenceFrequency
     */
    if (vnaproperty_set(&vnap->vna_property_root, "referenceFrequency=%.5f",
		vnap->vna_config.nci_reference_frequency) == -1) {
       _n2pkvna_error(vnap, "%s: vnaproperty_set: %s: %s",
		vnap->vna_config.nci_basename, new_filename,
		strerror(errno));
	goto out;
    }

    /*
     * Write and update the file
     */
    (void)fprintf(fp, "#N2PKVNA_CONFIG\n");
    if (vnaproperty_export_yaml_to_file(vnap->vna_property_root,
		fp, new_filename, _n2pkvna_libvna_errfn, NULL) == -1) {
	goto out;
    }
    if (fflush(fp) == -1) {
       _n2pkvna_error(vnap, "%s: fflush: %s: %s",
		vnap->vna_config.nci_basename, new_filename,
		strerror(errno));
	goto out;
    }
    if (fsync(fileno(fp)) == -1) {
       _n2pkvna_error(vnap, "%s: fsync: %s: %s",
		vnap->vna_config.nci_basename, new_filename,
		strerror(errno));
	goto out;
    }
    (void)fclose(fp);
    fp = NULL;
    (void)unlink(bak_filename);
    if (link(cur_filename, bak_filename) == -1 && errno != ENOENT) {
       _n2pkvna_error(vnap, "%s: link: %s %s: %s",
		vnap->vna_config.nci_basename, cur_filename,
		bak_filename, strerror(errno));
	goto out;
    }
    if (rename(new_filename, cur_filename) == -1) {
       _n2pkvna_error(vnap, "%s: rename: %s %s: %s",
		vnap->vna_config.nci_basename, new_filename,
		cur_filename, strerror(errno));
	goto out;
    }
    rv = 0;

out:
    if (fp != NULL) {
	fclose(fp);
	fp = NULL;
    }
    free((void *)bak_filename);
    free((void *)new_filename);
    free((void *)cur_filename);
    return rv;
}
