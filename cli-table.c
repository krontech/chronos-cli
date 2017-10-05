#include <stdlib.h>
#include "cli.h"

extern struct cli_subcmd cli_cmd_fpga;
extern struct cli_subcmd cli_cmd_lux1310;

const struct cli_subcmd *cli_cmd_table[] = {
    &cli_cmd_help,
    &cli_cmd_lux1310,
    /* End of list */
    NULL
};
