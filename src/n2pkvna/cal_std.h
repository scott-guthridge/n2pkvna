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

#ifdef CAL_STD_H
#define CAL_STD_H

#include <vnacal.h>

typedef enum {
    CAL_STD_SINGLE_REFLECT,
    CAL_STD_SINGLE1,
    CAL_STD_SINGLE2,
    CAL_STD_DOUBLE_REFLECT,
    CAL_STD_LINE
} cal_std_type_t;

typedef struct cal_std {
    cal_std_type_t cs_type;
    int *cs_matrix;
    struct cal_std *cs_next;
    vnacal_t *cs_vcp;
    //ZZ: We also need labels for generating prompts, either
    //    the name of a full port standard, or the names of two
    //    single port standards used in a double reflect.
} cal_std_t;

extern cal_std_t *cal_std_alloc(const char *standards);
extern void cal_std_free(cal_std_t *csp);

#endif /* CAL_STD_H */
