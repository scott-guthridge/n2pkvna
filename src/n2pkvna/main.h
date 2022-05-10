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

#ifndef MAIN_H
#define MAIN_H

#include <n2pkvna.h>
#include <stdbool.h>
#include <stdio.h>
#include <vnaerr.h>
#include "measurement.h"

#define SWITCH_DELAY	0.1		/* switch delay, seconds */

/*
 * Exit Codes
 */
#define N2PKVNA_EXIT_SUCCESS	0	/* success */
#define N2PKVNA_EXIT_CANCEL	1	/* user requested abort */
#define N2PKVNA_EXIT_USAGE	2	/* bad command-line options */
#define N2PKVNA_EXIT_VNAOP      3       /* error from VNA device */
#define N2PKVNA_EXIT_ERROR	4	/* other error */
#define N2PKVNA_EXIT_SYSTEM	5	/* ENOMEM or similar system error */

/*
 * global_state_t: program global state
 */
typedef struct global_state {
    bool		gs_interactive;	/* true for interactive session */
    int			gs_exitcode;	/* program exit code */
    bool		gs_opt_Y;	/* invoked by program instead of user */
    bool		gs_canceled;	/* operation canceled by user */
    vnaproperty_t      *gs_messages;	/* error messages, instructions, data */
    n2pkvna_t	       *gs_vnap;	/* N2PK VNA device */
    int			gs_switch;	/* switch code 0-3 or unknown -1 */
    int			gs_attenuation;	/* attenuation code 0-7 or unknown -1 */
    const char	       *gs_command;	/* command name or NULL */
    bool		gs_need_ack;	/* need acknowledgement from user */
    setup_t	       *gs_setups;	/* configured measurement setup list */
    mstep_t	       *gs_mstep;	/* measurement step name (or NULL) */
} global_state_t;


extern char *progname;
extern global_state_t gs;
extern int parse_attenuation(const char *arg);
extern void print_error(const char *message, void *arg);
extern void print_libvna_error(const char *message, void *arg,
	vnaerr_category_t category);
extern void print_usage(const char *const *usage, const char *const *help);
extern int run_command(int argc, char **argv);

#endif /* MAIN_H */
