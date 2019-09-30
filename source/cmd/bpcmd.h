#ifndef _BPCMD_H_
#define _BPCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_breakpoint_delete(struct cmd_args *, int, char **, char **);
enum cmd_error_t cmdfunc_breakpoint_list(struct cmd_args *, int, char **, char **);
enum cmd_error_t cmdfunc_breakpoint_set(struct cmd_args *, int, char **, char **);

static const char *BREAKPOINT_COMMAND_DOCUMENTATION =
    "'breakpoint' describes the group of commands which deal with breakpoints.\n";

static const char *BREAKPOINT_DELETE_COMMAND_DOCUMENTATION =
    "Delete breakpoints.\n"
    "This command has no mandatory arguments and one optional argument.\n"
    "\nOptional arguments:\n"
    "\tid\n"
    "\t\tThe ID of the breakpoint you want to delete.\n"
    "\t\tThis command accepts an arbitrary amount of this argument.\n"
    "\t\tOmit this argument to delete all breakpoints.\n"
    "\nSyntax:\n"
    "\tbreakpoint delete id?\n"
    "\n";

static const char *BREAKPOINT_LIST_COMMAND_DOCUMENTATION =
    "List breakpoints.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tbreakpoint list\n"
    "\n";

static const char *BREAKPOINT_SET_COMMAND_DOCUMENTATION =
    "Set a breakpoint.\n"
    "This command has one mandatory argument and one optional argument.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tThis expression will used as the location for the breakpoint.\n"
    "\t\tThis command accepts an arbitrary amount of this argument.\n"
    "\n"
    "\nOptional arguments:\n"
    "\ttid\n"
    "\t\t'tid' ensures a breakpoint is only active for a specific thread.\n"
    "\t\tiosdbg will notify you if this thread goes away.\n"
    "\t\tIf this argument is omitted, this breakpoint applies to all threads.\n"
    "\nSyntax:\n"
    "\tbreakpoint set (--t tid)? location\n"
    "\n";

/*
 * Regexes
 */
static const char *BREAKPOINT_DELETE_COMMAND_REGEX =
    "(?<ids>[\\d\\s]+)?";

static const char *BREAKPOINT_SET_COMMAND_REGEX =
    "(--t\\s+(?<tid>(0[xX])?[[:xdigit:]]+)\\s+)?(?<locations>[\\w+\\-*\\/\\$()]+)";

/*
 * Regex groups
 */
static const char *BREAKPOINT_DELETE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "ids" };

static const char *BREAKPOINT_SET_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "tid", "locations" };

#endif
