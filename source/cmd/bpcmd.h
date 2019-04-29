#ifndef _BPCMD_H_
#define _BPCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_break(struct cmd_args_t *, int, char **);

static const char *BREAKPOINT_COMMAND_DOCUMENTATION =
    "Set a breakpoint. Include '--no-aslr' to keep ASLR from being added.\n"
    "This command has one mandatory argument and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tThis expression will be evaluated and used as the location for the breakpoint.\n"
    "\t\tThis command accepts an arbitrary amount of this argument,"
    " allowing you to set multiple breakpoints.\n"
    "\nSyntax:\n"
    "\tbreak location\n"
    "\n"
    "\nThis command has an alias: 'b'\n"
    "\n";

/*
 * Regexes
 */
static const char *BREAKPOINT_COMMAND_REGEX =
    "(?<args>[\\w+\\-*\\/\\$()]+)";

/*
 * Regex groups
 */
static const char *BREAKPOINT_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "args" };

#endif
