#ifndef _TCMD_H_
#define _TCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_thread_list(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_thread_select(struct cmd_args_t *, int, char **);

static const char *THREAD_COMMAND_DOCUMENTATION =
    "'thread' describes the group of commmands which deal with "
    "managing debuggee threads.\n";

static const char *THREAD_LIST_COMMAND_DOCUMENTATION =
    "Inquire about existing threads of the debuggee.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tthread list\n"
    "\n";

static const char *THREAD_SELECT_COMMAND_DOCUMENTATION =
    "Select the thread to focus on while debugging.\n"
    "This command has one mandatory argument and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\ttid\n"
    "\t\tThe thread ID to focus on.\n"
    "\nSyntax:\n"
    "\tthread select tid\n"
    "\n";

/*
 * Regexes
 */
static const char *THREAD_SELECT_COMMAND_REGEX =
    "^\\s*(?<tid>\\d+)";

/*
 * Regex groups
 */
static const char *THREAD_SELECT_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "tid" };

#endif
