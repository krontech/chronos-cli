#ifndef _CLI_H
#define _CLI_H

#include "fpga.h"

/*
 * TODO: If option parsing gets complicated, it might be make sense
 * to make it data-driven and then have main() sort out the parsing
 * and validation stuff before invoking ->function().
 */
struct cli_subcmd {
    const char *name;
    const char *desc;
    const char *syntax;
    int (*function)(struct fpga *fpga, char *const argv[], int argc);
};

/* Some junk for linking the commands together into one big table. */
extern const struct cli_subcmd cli_cmd_help;
extern const struct cli_subcmd *cli_cmd_table[];

#endif /* _CLI_H */
