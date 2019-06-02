#ifndef _WPCMD_H_
#define _WPCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_watchpoint_delete(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_watchpoint_list(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_watchpoint_set(struct cmd_args_t *, int, char **);

static const char *WATCHPOINT_COMMAND_DOCUMENTATION =
    "'watchpoint' describes the group of commands which deal with watchpoints.\n";

static const char *WATCHPOINT_DELETE_COMMAND_DOCUMENTATION =
    "Delete watchpoints.\n"
    "This command has no mandatory arguments and one optional argument.\n"
    "\nOptional arguments:\n"
    "\tid\n"
    "\t\tThe ID of the watchpoint you want to delete.\n"
    "\t\tThis command accepts an arbitrary amount of this argument.\n"
    "\t\tOmit this argument to delete all watchpoints.\n"
    "\nSyntax:\n"
    "\twatchpoint delete id?\n"
    "\n";

static const char *WATCHPOINT_LIST_COMMAND_DOCUMENTATION =
    "List watchpoints.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\twatchpoint list\n"
    "\n";

static const char *WATCHPOINT_SET_COMMAND_DOCUMENTATION =
    "Set a watchpoint.\n"
    "This command has two mandatory arguments and one optional argument.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tThis expression will be used as the watchpoint's location.\n"
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
    "\twatchpoint set type? location size\n"
    "\n";

/*
 * Regexes
 */
static const char *WATCHPOINT_DELETE_COMMAND_REGEX =
    "(?<ids>[\\d\\s]+)?";

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
