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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "n2pkvna.h"

/*
 * print_error
 */
void print_error(const char *msg, void *arg)
{
    (void)fprintf(stderr, "n2pkvna-test: %s\n", msg);
}

/*
 * main
 */
int main(int argc, char **argv)
{
    n2pkvna_t *vnap = NULL;
    double frequency_vector[100];
    double complex detector1_vector[100];
    double complex detector2_vector[100];

    vnap = n2pkvna_open(/*name*/NULL, /*create*/true, /*address*/NULL,
	    /*config_vector*/NULL, &print_error, /*error_arg*/NULL);
    if (vnap == NULL) {
        exit(1);
    }
    if (n2pkvna_scan(vnap, 50.0e+3, 60.0e+6, 100, /*linear*/false,
                frequency_vector, detector1_vector, detector2_vector) == -1) {
        exit(2);
    }
    for (int i = 0; i < 100; ++i) {
        (void)printf("%13.7e %14.7e %14.7e %14.7e %14.7e\n",
                frequency_vector[i],
		creal(detector1_vector[i]), cimag(detector1_vector[i]),
                creal(detector2_vector[i]), cimag(detector2_vector[i]));
    }
    n2pkvna_close(vnap);
    vnap = NULL;

    exit(0);
}
