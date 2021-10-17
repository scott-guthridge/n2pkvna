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

#define SWITCH_DELAY	0.1		/* switch delay, seconds */

/*
 * Exit Codes
 */
#define N2PKVNA_EXIT_SUCCESS	0	/* success */
#define N2PKVNA_EXIT_QUIT	1	/* user requested abort */
#define N2PKVNA_EXIT_USAGE	2	/* bad command-line options */
#define N2PKVNA_EXIT_SYSTEM	3	/* ENOMEM or similar system error */
#define N2PKVNA_EXIT_CALLOAD	4	/* can't load calibration file */
#define N2PKVNA_EXIT_CALPARSE	5	/* bad data in calibration file */
#define N2PKVNA_EXIT_VNAOPEN	6	/* can't open VNA */
#define N2PKVNA_EXIT_VNAOP	7	/* run-time error in VNA */


extern char *progname;
extern FILE *fp_err;
extern n2pkvna_t *vnap;
extern int parse_attenuation(const char *arg);
extern void print_error(const char *message, void *arg);
extern void print_libvna_error(vnaerr_category_t category,
	const char *message, void *arg);
extern void print_usage(const char *command, const char *const *usage,
	const char *const *help);
extern int prompt_for_ready();
extern int run_command(int argc, char **argv);

#endif /* MAIN_H */
