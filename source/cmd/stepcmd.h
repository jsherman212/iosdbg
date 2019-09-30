#ifndef _STEPCMD_H_
#define _STEPCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_step_inst_into(struct cmd_args *, int, char **, char **);
enum cmd_error_t cmdfunc_step_inst_over(struct cmd_args *, int, char **, char **);

static const char *STEP_COMMAND_DOCUMENTATION =
    "'step' describes the group of commands which deal with stepping.\n";

static const char *STEP_INST_INTO_COMMAND_DOCUMENTATION =
    "Execute the next machine instruction.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tstep inst-into\n"
    "\n";

static const char *STEP_INST_OVER_COMMAND_DOCUMENTATION =
    "Execute the next machine instruction.\n"
    "If a function call is encountered, proceed until the function returns.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tstep inst-over\n"
    "\n";

#endif
