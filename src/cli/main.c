/****************************************************************************
 *  Copyright (C) 2017 Kron Technologies Inc <http://www.krontech.ca>.      *
 *                                                                          *
 *  This program is free software: you can redistribute it and/or modify    *
 *  it under the terms of the GNU General Public License as published by    *
 *  the Free Software Foundation, either version 3 of the License, or       *
 *  (at your option) any later version.                                     *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include "cli.h"
#include "fpga.h"

extern int optind;

int
main(int argc, char *const argv[])
{
	const struct cli_subcmd *cmd = NULL;
	struct fpga *fpga;
	int ret;
	const char *shortopts = "hv";
	const struct option options[] = {
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'v'},
		{0, 0, 0, 0}
	};

	/*
	 * Parse command line options until we encounter the first positional
	 * argument, which denodes the subcommand we want to execute. This
	 * would be to handle stuff relating to how we connect to the FPGA.
	 */
	optind = 1;
	while ((optind < argc) && (*argv[optind] == '-')) {
		int c = getopt_long(argc, argv, shortopts, options, NULL);
		if (c < 0) {
			/* End of options */
			break;
		}
		switch (c) {
			case 'h':
			case 'v':
				/* TODO: Implement Help and Version output. */
				break;
		}
	}

	/* The next positional argument should be the subcommand to run, look it up. */
	if (optind < argc) {
		int i;
		const char *name = argv[optind];
		for (i = 0; cli_cmd_table[i]; i++) {
			if (strcmp(cli_cmd_table[i]->name, name) == 0) {
				cmd = cli_cmd_table[i];
				break;
			}
		}
		if (!cmd) {
			fprintf(stderr, "%s: %s is not a valid command.\n", argv[0], name);
			return 1;
		}
	}
	/* Otherwise, if no command was given, then default to the 'help' command. */
	else {
		static char *helpargs[] = {"help", NULL};
		optind = 0;
		argc = 1;
		argv = helpargs;
		cmd = &cli_cmd_help;
	}

	/* Open the FPGA before continuing. */
	fpga = fpga_open();
	if (!fpga) {
		fprintf(stderr, "Failed to map FPGA, aborting.\n");
		return 1;
	}

	/* Do the command. */
	ret = cmd->function(fpga, &argv[optind], argc - optind);
	fpga_close(fpga);
	return ret;
}
