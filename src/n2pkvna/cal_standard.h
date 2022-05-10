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

#ifndef CAL_STANDARD_H
#define CAL_STANDARD_H

#include <n2pkvna.h>
#include <stdint.h>
#include <vnacal.h>
#include "measurement.h"

typedef struct cal_standard {
    int				cs_token;	/* element type */
    char		       *cs_name;	/* name of standard */
    const char                 *cs_text;	/* verbose name */
    int				cs_ports;	/* number of ports */
    int			        cs_matrix[2][2];/* matrix vnacal_parameter's */
    struct cal_standard	       *cs_next;	/* next in list */
} cal_standard_t;

typedef enum {
    CALS_INVALID,
    CALS_SINGLE_REFLECT,			/* single reflect standard */
    CALS_DOUBLE_REFLECT,			/* double reflect standard */
    CALS_THROUGH,				/* through standard */
    CALS_LINE					/* arbitrary 2x2 standard */
} cal_standard_type_t;

typedef struct cal_step {
    cal_standard_type_t		cst_type;	/* type of step */
    const cal_standard_t       *cst_standards[2];/* standards */
    struct cal_step	       *cst_next;	/* next step */
} cal_step_t;

typedef struct cal_step_list {
    n2pkvna_t		       *csl_vnap;
    setup_t		       *csl_setup;
    vnacal_t		       *csl_vcp;
    cal_standard_t	       *csl_standards;
    cal_step_t		       *csl_steps;
} cal_step_list_t;

extern const cal_standard_t cs_match;
extern const cal_standard_t cs_open;
extern const cal_standard_t cs_short;
extern const cal_standard_t cs_through;
extern const cal_standard_t cs_terminator;

extern cal_step_list_t *cal_standards_parse(n2pkvna_t *vnap,
	setup_t *sup, vnacal_t *vcp, const char *standards);
extern void cal_standards_free(cal_step_list_t *head);


#endif /* CAL_STANDARD_H */
