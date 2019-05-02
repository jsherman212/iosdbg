#ifndef _BPCMD_H_
#define _BPCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_breakpoint_delete(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_breakpoint_list(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_breakpoint_set(struct cmd_args_t *, int, char **);

static const char *BREAKPOINT_COMMAND_DOCUMENTATION =
    "'breakpoint' describes the group of commands which deal with breakpoints.\n";

static const char *BREAKPOINT_DELETE_COMMAND_DOCUMENTATION =
    "Delete a breakpoint. TODO fill in\n";

static const char *BREAKPOINT_LIST_COMMAND_DOCUMENTATION =
    "Breakpoint list\n";

static const char *BREAKPOINT_SET_COMMAND_DOCUMENTATION =
    "Set a breakpoint. Include '--no-aslr' to keep ASLR from being added.\n"
    "This command has one mandatory argument and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tThis expression will be evaluated and used as the location for the breakpoint.\n"
    "\t\tThis command accepts an arbitrary amount of this argument,"
    " allowing you to set multiple breakpoints.\n"
    "\nSyntax:\n"
    "\tbreak location\n"
    "\n";

/*
 * Regexes
 */
static const char *BREAKPOINT_DELETE_COMMAND_REGEX =
    "(?<ids>\\d+)";

static const char *BREAKPOINT_SET_COMMAND_REGEX =
    "(?<args>[\\w+\\-*\\/\\$()]+)";

/*
 * Regex groups
 */
static const char *BREAKPOINT_DELETE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "ids" };

static const char *BREAKPOINT_SET_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "args" };

#endif
