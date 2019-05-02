#ifndef _WPCMD_H_
#define _WPCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_watchpoint_delete(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_watchpoint_list(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_watchpoint_set(struct cmd_args_t *, int, char **);

static const char *WATCHPOINT_COMMAND_DOCUMENTATION =
    "'watchpoint' describes the group of commands which deal with watchpoints.\n";

static const char *WATCHPOINT_DELETE_COMMAND_DOCUMENTATION =
    "Delete a watchpoint. TODO fill in\n";

static const char *WATCHPOINT_LIST_COMMAND_DOCUMENTATION =
    "Watchpoint list\n";

static const char *WATCHPOINT_SET_COMMAND_DOCUMENTATION =
    "Set a watchpoint. ASLR is never accounted for.\n"
    "This command has two mandatory arguments and one optional argument.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tThis expression will be evaluated and interpreted as\n"
    "\t\t the watchpoint's location.\n"
    "\tsize\n"
    "\t\tThe size of the data to watch.\n"
    "\nOptional arguments:\n"
    "\ttype\n"
    "\t\tThe type of the watchpoint. Acceptable values are:\n"
    "\t\t\t'--r'  (read)\n"
    "\t\t\t'--w'  (write)\n"
    "\t\t\t'--rw' (read/write)\n"
    "\t\tIf this argument is omitted, iosdbg assumes --w.\n"
    "\nSyntax:\n"
    "\twatch type? location size\n"
    "\n";

/*
 * Regexes
 */
static const char *WATCHPOINT_DELETE_COMMAND_REGEX =
    "(?<ids>\\d+)";

static const char *WATCHPOINT_SET_COMMAND_REGEX =
    "(?(?=--[rw])(?<type>--[rw]{1,2}))\\s*"
    "(?<location>[\\w+\\-*\\/\\$()]+)\\s+(?<size>(0[xX])?\\d+)";

/*
 * Regex groups
 */
static const char *WATCHPOINT_DELETE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "ids" };

static const char *WATCHPOINT_SET_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "type", "location", "size" };

#endif
