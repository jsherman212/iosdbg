#ifndef _VARIABLECMD_H_
#define _VARIABLECMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_variable_print(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_variable_set(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_variable_unset(struct cmd_args_t *, int, char **);

static const char *VARIABLE_COMMAND_DOCUMENTATION =
    "'var' describes the group of commands which deal with convenience variables.\n";

static const char *VARIABLE_PRINT_COMMAND_DOCUMENTATION =
    "Show convenience variables.\n"
    "This command has no mandatory arguments and one optional argument.\n"
    "\nOptional arguments:\n"
    "\tvar\n"
    "\t\tThe convenience variable.\n"
    "\t\tIf this argument is omitted, every variable is shown.\n"
    "\nSyntax\n"
    "\tvar print var?\n"
    "\n";

static const char *VARIABLE_SET_COMMAND_DOCUMENTATION =
    "Create a new convenience variable or set the value of an existing one.\n"
    "This command has two mandatory arguments and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tvar\n"
    "\t\tThe convenience variable.\n"
    "\t\tIt must start with '$'.\n"
    "\tvalue\n"
    "\t\tWhat you're setting `var` to.\n"
    "\nSyntax:\n"
    "\tvariable set var value\n"
    "\n";

static const char *VARIABLE_UNSET_COMMAND_DOCUMENTATION =
    "Revert the value of an existing convenience variable to void.\n"
    "The variable is not deleted.\n"
    "This commmand has one mandatory argument and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tvar\n"
    "\t\tThe convenience variable.\n"
    "\nSyntax:\n"
    "\tvariable unset var\n"
    "\n";

/*
 * Regexes
 */
static const char *VARIABLE_PRINT_COMMAND_REGEX =
    "(?<var>\\$[\\w\\d+\\-*\\/$()]+)?";

static const char *VARIABLE_SET_COMMAND_REGEX =
    "(?J)(?<var>\\$[\\w\\d+\\-*\\/$()]+)\\s+"
    "((?=\")(?<value>\".*\")|(?<value>[.\\-\\w\\d+\\-*\\/$()]+))";

static const char *VARIABLE_UNSET_COMMAND_REGEX =
    "(?<var>\\$[\\w\\d+\\-*\\/$()]+)";

/*
 * Regex groups
 */
static const char *VARIABLE_PRINT_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "var" };

static const char *VARIABLE_SET_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "var", "value" };

static const char *VARIABLE_UNSET_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "var" };

#endif
