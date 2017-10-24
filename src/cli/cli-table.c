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
#include <stdlib.h>
#include "cli.h"

extern struct cli_subcmd cli_cmd_fpga;
extern struct cli_subcmd cli_cmd_fpgaload;
extern struct cli_subcmd cli_cmd_info;
extern struct cli_subcmd cli_cmd_lux1310;

const struct cli_subcmd *cli_cmd_table[] = {
    &cli_cmd_help,
    &cli_cmd_fpgaload,
    &cli_cmd_info,
    &cli_cmd_lux1310,
    /* End of list */
    NULL
};
