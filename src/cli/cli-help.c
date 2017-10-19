
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "cli.h"
#include "fpga.h"

static int
do_help(struct fpga *fpga, char *const argv[], int argc)
{
    fprintf(stderr, "Implement Me!\n");
    return 0;
}

/* The lux1310 subcommand */
const struct cli_subcmd cli_cmd_help = {
    .name = "help",
    .desc = "Show commands and syntax.",
    .function = do_help,
};
