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

#ifndef CLI_H
#define CLI_H

#include <stdbool.h>

/*
 * command_t: name of command and function to call
 */
typedef struct command {
    const char *cmd_name;
    int (*cmd_function)(int argc, char **argv);
} command_t;

/*
 * cli_scan_t: command scanner state
 */
typedef struct cli_scan {
    char	css_cur;		/* look-ahead character */
    char       *css_buffer;		/* command buffer */
    int	        css_buffer_length;	/* current command length */
    int		css_buffer_size;	/* buffer allocation */
    int	       *css_arg_index;		/* vector of argument indices */
    int		css_arg_slots;		/* allocated slots in arg_index */
    int		css_argc;		/* number of arguments */
    char      **css_argv;		/* argument vector */
    int		css_state;		/* scanner quote state */
} cli_scan_t;

extern void cli_scan_init(cli_scan_t *cssp);
extern int  cli_scan(cli_scan_t *cssp, int *argc, char ***argv);
extern void cli_scan_free(cli_scan_t *cssp);

extern bool is_quit(const char *command);

extern int cli(command_t *cmdp, int n_entries, const char *prompt,
	int argc, char **argv);

#endif /* CLI_H */
