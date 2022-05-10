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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archdep.h"

#include "main.h"
#include "measurement.h"
#include "message.h"
#include "n2pkvna.h"
#include "properties.h"

/*
 * is_yaml_true: test a string for true values in a YAML-like accepting way
 *   @value: value to test
 */
static bool is_yaml_true(const char *value)
{
    switch (value[0]) {
    case 'y':
    case 'Y':
    case 't':
    case 'T':
    case '1':
	return true;

    case 'o':
    case 'O':
	if (strcasecmp(value, "on") == 0) {
	    return true;
	}
	break;
    }
    return false;
}

/*
 * parse_measurement: parse measurement at a given switch setting
 *   @root: root of the property subtree at ...steps[i].measurements[j]
 *   @mstep: pointer to parent structure
 *   @step_index: index of step for error messages
 *   @measurement_index: index of measurement for error messages
 */
static measurement_t *parse_measurement(vnaproperty_t *root,
	mstep_t *msp, int step_index, int measurement_index)
{
    const char *setup_name = msp->ms_setup->su_name;
    const char **element_names = NULL;
    int switch_code = -1;
    vector_code_t detectors[2] = { VC_NONE, VC_NONE };
    int count;

    if ((element_names = vnaproperty_keys(root, "{}")) == NULL) {
	if (errno == ENOENT || errno == EINVAL) {
	    message_error("%s/config: "
		    "setups.%s.steps[%d].measurements[%d]: must be a map\n",
		    n2pkvna_get_directory(gs.gs_vnap),
		    setup_name, step_index, measurement_index);
	    goto error;
	}
	message_error("vnaproperty_keys: %s\n", strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    for (const char **cpp = element_names; *cpp != NULL; ++cpp) {
	switch (**cpp) {
	case 'd':
	    if (strcmp(*cpp, "detectors") == 0) {
		int count;

		if ((count = vnaproperty_count(root, "detectors[]")) == -1) {
		    message_error("%s/config: "
			    "setups.%s.steps[%d].measurements[%d].detectors: "
			    "must be a list\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name, step_index, measurement_index);
		    goto error;
		}
		if (count < 1 || count > 2) {
		    message_error("%s/config: "
			    "setups.%s.steps[%d].measurements[%d].detectors: "
			    "expected 1 or 2 measurement codes\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name, step_index, measurement_index);
		    goto error;
		}
		for (int i = 0; i < count; ++i) {
		    const char *value;

		    errno = 0;
		    value = vnaproperty_get(root, "detectors[%d]", i);
		    if (value == NULL) {
			if (errno == 0) {
			    continue;
			}
			message_error("%s/config: "
				"setups.%s.steps[%d].measurements[%d]."
				"detectors[%d]: must be a scalar\n",
				n2pkvna_get_directory(gs.gs_vnap),
				setup_name, step_index, measurement_index, i);
			goto error;
		    }
		    detectors[i] = vector_name_to_code(value);
		    if (detectors[i] == VC_NONE) {
			message_error("%s/config: "
				"setups.%s.steps[%d].measurements[%d]."
				"detectors[%d]: %s: invalid vector name\n",
				n2pkvna_get_directory(gs.gs_vnap),
				setup_name, step_index, measurement_index,
				i, value);
			goto error;
		    }
		}
		continue;
	    }
	    break;

	case 's':
	    if (strcmp(*cpp, "switch") == 0) {
		const char *value;

		errno = 0;
		if ((value = vnaproperty_get(root, "switch")) == NULL) {
		    if (errno == 0) {
			continue;
		    }
		    message_error("%s/config: "
			    "setups.%s.steps[%d].measurements[%d].switch: "
			    "must be a scalar\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name, step_index, measurement_index);
		    goto error;
		}
		if (sscanf(value, "%d", &switch_code) != 1 ||
			switch_code < 0 || switch_code > 3) {
		    message_error("%s/config: "
			    "setups.%s.steps[%d].measurements[%d].switch: "
			    "value %s: must be 0-3 or ~\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name, step_index, measurement_index, value);
		    goto error;
		}
		continue;
	    }
	    break;

	default:
	    break;
	}
	message_error("%s/config: "
		"setups.%s.steps[%d].measurements[%d].%s: unexpected\n",
		n2pkvna_get_directory(gs.gs_vnap),
		setup_name, step_index, measurement_index, *cpp);
	goto error;
    }
    free((void *)element_names);
    element_names = NULL;

    /*
     * Make sure that each measurement has a unique switch code.
     */
    count = 0;
    for (measurement_t *mp = msp->ms_measurements;
	    mp != NULL; mp = mp->m_next, ++count) {
	if (switch_code == -1 ||
		mp->m_switch == -1 ||
		switch_code == mp->m_switch) {
	    message_error("%s/config: setups.%s.steps[%d].measurements[%d]: "
		    "switch ",
		    n2pkvna_get_directory(gs.gs_vnap),
		    setup_name, step_index, count);
	    if (switch_code > -1) {
		message_error_np("%d", switch_code);
	    } else {
		message_error_np("~");
	    }
	    message_error_np(": duplicate code\n");
	    goto error;
	}
    }

    /*
     * Add the measurement.
     */
    return mstep_add_measurement(msp, switch_code, detectors[0], detectors[1]);

error:
    free((void *)element_names);
    return NULL;
}

/*
 * parse_mstep: parse a measurement step from the config file
 *   @root: root of the property subtree at ...setups.<name>.steps[i]
 *   @setup: pointer to parent structure
 *   @step_index: index of step for error messages
 */
static mstep_t *parse_mstep(vnaproperty_t *root, setup_t *setup,
	int step_index)
{
    const char *setup_name = setup->su_name;
    const char **element_names = NULL;
    vnaproperty_t *measurements_root = NULL;
    const char *name = NULL;
    const char *text = NULL;
    mstep_t *msp = NULL;
    int count;

    if ((element_names = vnaproperty_keys(root, "{}")) == NULL) {
	if (errno == ENOENT || errno == EINVAL) {
	    message_error("%s/config: "
		    "setups.%s.steps[%d]: must be a map\n",
		    n2pkvna_get_directory(gs.gs_vnap),
		    setup_name, step_index);
	    goto error;
	}
	message_error("vnaproperty_keys: %s\n", strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    for (const char **cpp = element_names; *cpp != NULL; ++cpp) {
	switch (**cpp) {
	case 'm':
	    if (strcmp(*cpp, "measurements") == 0) {
		if ((measurements_root = vnaproperty_get_subtree(root,
				"measurements[]")) == NULL) {
		    message_error("%s/config: "
			    "setups.%s.steps[%d].measurements: "
			    "must be a list\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name, step_index);
		    goto error;
		}
		continue;
	    }
	    break;

	case 'n':
	    if (strcmp(*cpp, "name") == 0) {
		if ((name = vnaproperty_get(root, "name")) == NULL) {
		    message_error("%s/config: "
			    "setups.%s.steps[%d].name: must be a scalar\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name, step_index);
		    goto error;
		}
		continue;
	    }
	    break;

	case 't':
	    if (strcmp(*cpp, "text") == 0) {
		if ((name = vnaproperty_get(root, "text")) == NULL) {
		    message_error("%s/config: "
			    "setups.%s.steps[%d].text: must be a scalar\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name, step_index);
		    goto error;
		}
		continue;
	    }
	    break;

	default:
	    break;
	}
	message_error("%s/config: setups.%s.steps[%d].%s: unexpected\n",
		n2pkvna_get_directory(gs.gs_vnap),
		setup_name, step_index, *cpp);
	goto error;
    }
    free((void *)element_names);
    element_names = NULL;

    /*
     * Enforce that name is required when text given.
     */
    if (text != NULL && name == NULL) {
	message_error("%s/config: setups.%s.seteps[%d]: "
		"name required when text given\n",
		n2pkvna_get_directory(gs.gs_vnap),
		setup_name, step_index);
	goto error;
    }

    /*
     * Make sure each mstep_t structure has a unique name.
     */
    for (msp = setup->su_steps; msp != NULL; msp = msp->ms_next) {
	if (name == NULL || msp->ms_name == NULL ||
		strcmp(name, msp->ms_name) == 0) {
	    message_error("%s/config: setups.%s: "
		    "steps must have unique names\n",
		    n2pkvna_get_directory(gs.gs_vnap),
		    setup_name);
	    goto error;
	}
    }

    /*
     * Add the mstep_t structure.
     */
    if ((msp = setup_add_mstep(setup, name, text)) == NULL) {
	goto error;
    }

    /*
     * Add the measurements.
     */
    count = vnaproperty_count(measurements_root, ".");
    for (int i = 0; i < count; ++i) {
	vnaproperty_t *measurement_root;

	measurement_root = vnaproperty_get_subtree(measurements_root,
		"[%d]", i);
	if (parse_measurement(measurement_root, msp, step_index, i) == NULL) {
	    goto error;
	}
    }
    return msp;

error:
    free((void *)element_names);
    return NULL;
}

/*
 * parse_setup: parse a setup entry
 *   @root: root of the property subtree properties.setups.<name>
 *   @setup_name: name of this VNA setup
 */
setup_t *parse_setup(vnaproperty_t *root, const char *setup_name)
{
    const char **element_names = NULL;
    int rows = 0, columns = 0;
    bool enabled = true;
    double fmin = 50.0e+3;
    double fmax = 60.0e+6;
    double fosc = 0.0;
    vnaproperty_t *steps_root = NULL;
    setup_t *sup = NULL;
    int count;
    measurement_mask_t allowed_mask;

    /*
     * For each key in the setup...
     */
    if ((element_names = vnaproperty_keys(root, "{}")) == NULL) {
	if (errno == ENOENT || errno == EINVAL) {
	    message_error("%s/config: setups.%s: must be a map\n",
		    n2pkvna_get_directory(gs.gs_vnap),
		    setup_name);
	    goto error;
	}
	message_error("vnaproperty_keys: %s\n", strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    for (const char **cpp = element_names; *cpp != NULL; ++cpp) {
	switch (**cpp) {
	case 'd':
	    if (strcmp(*cpp, "dimensions") == 0) {
		const char *value;

		value = vnaproperty_get(root, "dimensions");
		if (value == NULL) {
		    message_error("%s/config: "
			    "setups.%s.dimensions: must be a scalar\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name);
		    goto error;
		}
		if (sscanf(value, "%d x%d", &rows, &columns) != 2 ||
			(rows != 1 && rows != 2) ||
			(columns != 1 && columns != 2)) {
		    message_error("%s/config: "
			    "setups.%s.dimensions: "
			    "must be 1x1, 1x2, 2x1 or 2x2\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name);
		    goto error;
		}
		continue;
	    }
	    break;

	case 'e':
	    if (strcmp(*cpp, "enabled") == 0) {
		const char *string;

		string = vnaproperty_get(root, "enabled");
		if (string == NULL) {
		    message_error("%s/config: "
			    "setups.%s.enabled: must be a scalar\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name);
		    goto error;
		}
		enabled = is_yaml_true(string);
		continue;
	    }
	    break;

	case 'f':
	    if (strcmp(*cpp, "fmin") == 0 || strcmp(*cpp, "fmax") == 0 ||
		    strcmp(*cpp, "fosc") == 0) {
		const char *string;
		char *end;
		double value;

		string = vnaproperty_get(root, "%s", *cpp);
		if (string == NULL) {
		    message_error("%s/config: "
			    "setups.%s.%s: must be a scalar\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    *cpp, setup_name);
		    goto error;
		}
		value = strtod(string, &end);
		if (end == string || *end != '\000' || value < 0.0) {
		    message_error("%s/config: "
			    "setups.%s.%s: must be a non-negative number\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    *cpp, setup_name);
		    goto error;
		}
		switch (cpp[0][2]) {
		case 'a':	/* fM(a)x */
		    fmax = value;
		    break;
		case 'i':	/* fM(i)n */
		    fmin = value;
		    break;
		case 's':	/* fO(s)c */
		    fosc = value;
		    break;
		}
		continue;
	    }
	    break;

	case 's':
	    if (strcmp(*cpp, "steps") == 0) {
		if ((steps_root = vnaproperty_get_subtree(root,
				"steps[]")) == NULL) {
		    message_error("%s/config: "
			    "setups.%s.steps: must be a list\n",
			    n2pkvna_get_directory(gs.gs_vnap),
			    setup_name);
		    goto error;
		}
		continue;
	    }
	    break;

	default:
	    break;
	}
	message_error("%s/config: "
		"setups.%s.%s: unexpected\n",
		n2pkvna_get_directory(gs.gs_vnap), setup_name, *cpp);
	goto error;
    }
    free((void *)element_names);
    element_names = NULL;

    /*
     * Make sure the dimensions were given.
     */
    if (rows == 0) {
	message_error("%s/config: "
		"setups.%s: dimensions must be given\n",
		n2pkvna_get_directory(gs.gs_vnap), setup_name);
	goto error;
    }

    /*
     * Make sure the frequency range is valid.
     */
    if (fmin > fmax) {
	message_error("%s/config: "
		"setups.%s: fmin cannot be greater than fmax\n",
		n2pkvna_get_directory(gs.gs_vnap), setup_name);
	goto error;
    }
    if (fosc > fmin && fosc < fmax) {
	message_error("%s/config: "
		"setups.%s: fosc cannot be within fmin..fmax\n",
		n2pkvna_get_directory(gs.gs_vnap), setup_name);
	goto error;
    }

    /*
     * Allocate the setup structure.
     */
    if ((sup = setup_alloc(setup_name, rows, columns)) == NULL) {
	goto error;
    }
    sup->su_enabled = enabled;
    sup->su_fmin = fmin;
    sup->su_fmax = fmax;
    sup->su_fosc = fosc;

    /*
     * Add the steps.
     */
    count = vnaproperty_count(steps_root, ".");
    for (int i = 0; i < count; ++i) {
	vnaproperty_t *mstep_root;

	mstep_root = vnaproperty_get_subtree(steps_root, "[%d]", i);
	if (parse_mstep(mstep_root, sup, i) == NULL) {
	    goto error;
	}
    }

    /*
     * Validate that the given vectors are consistent with the dimensions
     * and that at least one required vector is present in each cell.
     *
     * For calibration types E12 or UE14, we need any one of the b, i,
     * or v vectors; for the other calibrations, we need any two of a,
     * b, i and v.  At this point, we don't know what calibration type
     * will be used, so verify that we have at least one aside from a.
     */
    if (sup->su_rows == 1) {
	if (sup->su_columns == 1) {
	    allowed_mask = 0x000F;	/* 1x1 */
	} else {
	    allowed_mask = 0x00FF;	/* 1x2 */
	}
    } else {
	if (sup->su_columns == 1) {
	    allowed_mask = 0x0F0F;	/* 2x1 */
	} else {
	    allowed_mask = 0xFFFF;	/* 2x2 */
	}
    }
    if (sup->su_mask & ~allowed_mask) {
	bool first = true;

	message_error("%s/config: "
		"setups.%s: vectors inconsistent with dimensions: ",
		n2pkvna_get_directory(gs.gs_vnap),
		setup_name);
	for (vector_code_t code = 0; code < 16; ++code) {
	    if ((1U << code) & sup->su_mask & ~allowed_mask) {
		if (!first) {
		    message_error_np(",");
		} else {
		    first = false;
		}
		message_error_np("%s", vector_code_to_name(code));
	    }
	}
	message_error_np("\n");
	goto error;
    }
    for (int row = 0; row < sup->su_rows; ++row) {
	for (int column = 0; column < sup->su_columns; ++column) {
	    int cell = 2 * row + column;
	    measurement_mask_t ivb_mask = 0xE << (4 * cell);

	    if (!(sup->su_mask & ivb_mask)) {
		message_error("%s/config: "
			"setups.%s: cannot determine b%d%d vector\n",
			n2pkvna_get_directory(gs.gs_vnap),
			setup_name, row + 1, column + 1);
		goto error;
	    }
	}
    }
    return sup;

error:
    free((void *)element_names);
    setup_free(sup);
    return NULL;
}

/*
 * parse_setups: parse the setups property from the N2PK VNA config file
 *
 * Example syntax:
 *   setups:
 *     single_detector_2x2:
 *       dimensions: 2x2
 *       enabled: y
 *       fmin: 50.0e+3
 *       fmax: 60.0e+6
 *       fosc: 0.0
 *       steps:
 *       - name: reflection
 *         text: Configure the VNA for reflection.
 *         measurements:
 *         - detectors: [ b11, ~ ]
 *           switch: 0
 *         - detectors: [ b22, ~ ]
 *           switch: 1
 *       - name: transmission
 *         text: Configure the VNA for transmission.
 *         measurements:
 *         - detectors: [ b12, ~ ]
 *           switch: 0
 *         - detectors: [ b21, ~ ]
 *           switch: 1
 *     reflection_bridge:
 *       dimensions: 2x1
 *       steps:
 *         - measurements:
 *           - detectors: [ b11, b21 ]
 */
int parse_setups(vnaproperty_t *root)
{
    const char **property_names = NULL;
    int rv = -1;

    errno = 0;
    if ((property_names = vnaproperty_keys(root, "{}")) == NULL) {
	//ZZ: improve error message on EINVAL
	if (errno == 0 || errno == ENOENT) {	/* OK if empty */
	    return 0;
	}
	message_error("vnaproperty_keys: %s\n", strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
	goto out;
    }
    for (const char **cpp = property_names; *cpp != NULL; ++cpp) {
	vnaproperty_t *setup_root;
	setup_t *sup;

	setup_root = vnaproperty_get_subtree(root, "%s", *cpp);
	if ((sup = parse_setup(setup_root, *cpp)) == NULL)
	    goto out;

	setup_update(sup);
    }
    rv = 0;

out:
    free((void *)property_names);
    return rv;
}

/*
 * properties_load: parse the property list from the N2PK VNA config file
 */
int properties_load()
{
    vnaproperty_t *root;
    const char **property_names;
    int rv = -1;

    root = *n2pkvna_get_property_root(gs.gs_vnap);
    if ((property_names = vnaproperty_keys(root, "{}")) == NULL) {
	//ZZ: improve error message on EINVAL
	if (errno == 0 || errno == ENOENT) {	/* OK if empty */
	    return 0;
	}
	message_error("vnaproperty_keys: %s\n", strerror(errno));
	exit(N2PKVNA_EXIT_SYSTEM);
    }
    for (const char **cpp = property_names; *cpp != NULL; ++cpp) {
	switch (**cpp) {
	case 's':
	    if (strcmp(*cpp, "setups") == 0) {
		vnaproperty_t *setup_root;

		setup_root = vnaproperty_get_subtree(root, "%s", *cpp);
		if (parse_setups(setup_root) == -1) {
		    goto out;
		}
		continue;
	    }
	    break;

	default:
	    break;
	}
	message_error("%s/config: "
		"ignoring unknown property %s\n",
		n2pkvna_get_directory(gs.gs_vnap), *cpp);
    }
    rv = 0;

out:
    free((void *)property_names);
    return rv;
}

/*
 * properties_save: save the VNA properties
 */
int properties_save()
{
    vnaproperty_t **rootptr = n2pkvna_get_property_root(gs.gs_vnap);
    vnaproperty_t **setups_rootptr;

    if ((setups_rootptr = vnaproperty_set_subtree(rootptr, "setups")) == NULL) {
	return -1;
    }
    (void)vnaproperty_delete(setups_rootptr, ".");
    for (setup_t *sup = gs.gs_setups; sup != NULL; sup = sup->su_next) {
	const char *setup_name = sup->su_name;
	vnaproperty_t **steps_rootptr;

	if (vnaproperty_set(setups_rootptr, "%s.dimensions=%dx%d",
		    setup_name, sup->su_rows, sup->su_columns) == -1) {
	    return -1;
	}
	if (vnaproperty_set(setups_rootptr, "%s.enabled=%c",
		    setup_name, sup->su_enabled ? 'y' : 'n') == -1) {
	    return -1;
	}
	if (vnaproperty_set(setups_rootptr, "%s.fmin=%e",
		    setup_name, sup->su_fmin) == -1) {
	    return -1;
	}
	if (vnaproperty_set(setups_rootptr, "%s.fmax=%e",
		    setup_name, sup->su_fmax) == -1) {
	    return -1;
	}
	if (sup->su_fosc != 0.0) {
	    if (vnaproperty_set(setups_rootptr, "%s.fosc=%e",
			setup_name, sup->su_fosc) == -1) {
		return -1;
	    }
	}
	if ((steps_rootptr = vnaproperty_set_subtree(setups_rootptr,
			"%s.steps[]", setup_name)) == NULL) {
	    return -1;
	}
	for (mstep_t *msp = sup->su_steps; msp != NULL; msp = msp->ms_next) {
	    vnaproperty_t **step_rootptr, **measurements_rootptr;

	    if ((step_rootptr = vnaproperty_set_subtree(steps_rootptr,
			    "[+]")) == NULL) {
		return -1;
	    }
	    if (msp->ms_name != NULL) {
		if (vnaproperty_set(step_rootptr, "name=%s",
			    msp->ms_name) == -1) {
		    return -1;
		}
	    }
	    if (msp->ms_text != NULL) {
		if (vnaproperty_set(step_rootptr, "text=%s",
			    msp->ms_text) == -1) {
		    return -1;
		}
	    }
	    if ((measurements_rootptr = vnaproperty_set_subtree(step_rootptr,
			    "measurements[]")) == NULL) {
		return -1;
	    }
	    for (measurement_t *mp = msp->ms_measurements; mp != NULL;
		    mp = mp->m_next) {
		vnaproperty_t **measurement;

		if ((measurement = vnaproperty_set_subtree(measurements_rootptr,
				"[+]")) == NULL) {
		    return -1;
		}
		if (mp->m_switch >= 0) {
		    if (vnaproperty_set(measurement, "switch=%d",
				mp->m_switch) == -1) {
			return -1;
		    }
		} else {
		    if (vnaproperty_set(measurement, "switch#") == -1) {
			return -1;
		    }
		}
		for (int detector = 0; detector < 2; ++detector) {
		    if (mp->m_detectors[detector] != VC_NONE) {
			const char *name;

			name = vector_code_to_name(mp->m_detectors[detector]);
			if (vnaproperty_set(measurement, "detectors[%d]=%s",
				    detector, name) == -1) {
			    return -1;
			}

		    } else {
			if (vnaproperty_set(measurement, "detectors[%d]#",
				    detector) == -1) {
			    return -1;
			}
		    }
		}
	    }
	}
    }
    if (n2pkvna_save(gs.gs_vnap) == -1) {
	return -1;
    }
    return 0;
}
