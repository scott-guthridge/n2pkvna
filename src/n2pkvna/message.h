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

#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdarg.h>
#include <stdio.h>

/* message_add_instruction: add a step to to list of instructions */
extern void message_add_instruction(const char *format, ...)
#ifdef __GNUC__
    __attribute__((__format__(__printf__, 1, 0)))
#endif /* __GNUC__ */
;

/* message_error_np: report an error message */
extern void message_error(const char *format, ...)
#ifdef __GNUC__
    __attribute__((__format__(__printf__, 1, 0)))
#endif /* __GNUC__ */
;

/* message_error_np: report an error message without command name prefix */
extern void message_error_np(const char *format, ...)
#ifdef __GNUC__
    __attribute__((__format__(__printf__, 1, 0)))
#endif /* __GNUC__ */
;

/* message_get_config: include config properties in response */
extern void message_get_config();

/* message_wait_for_acknowledgement: wait for a newline from the user */
extern int message_wait_for_acknowledgement();

/* message_prompt: flush response and prompt for next command */
extern void message_prompt();

/* message_get_measured_frequency: prompt for frequency measurement */
extern int message_get_measured_frequency(double *measured);

#endif /* MESSAGE_H */
