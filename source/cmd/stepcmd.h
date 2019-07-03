#ifndef _STEPCMD_H_
#define _STEPCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_step_into(struct cmd_args_t *, int, char **, char **);

static const char *STEP_COMMAND_DOCUMENTATION =
    "'step' describes the group of commands which deal with stepping.\n";

static const char *STEP_INTO_COMMAND_DOCUMENTATION =
    "Step into the next machine instruction.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tstep into\n"
    "\n";


#endif
