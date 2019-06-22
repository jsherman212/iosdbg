#ifndef _REGCMD_H_
#define _REGCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_register_view(struct cmd_args_t *, int, char **, char **);
enum cmd_error_t cmdfunc_register_write(struct cmd_args_t *, int, char **, char **);

static const char *REGISTER_COMMAND_DOCUMENTATION =
    "'register' describes the group of commands which deal with registers.\n";

static const char *REGISTER_VIEW_COMMAND_DOCUMENTATION =
    "Show one or more debuggee registers.\n"
    "This command has no mandatory arguments and one optional argument.\n"
    "\nOptional arguments:\n"
    "\treg\n"
    "\t\tThe general purpose or floating point register.\n"
    "\t\tThis command accepts an arbitrary amount of this argument.\n"
    "\t\tOmit this argument to see every general purpose register.\n"
    "\nSyntax:\n"
    "\tregister view reg?\n"
    "\n";

static const char *REGISTER_WRITE_COMMAND_DOCUMENTATION =
    "Write to debuggee registers.\n"
    "This command has two mandatory arguments and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\treg\n"
    "\t\tThe register you're modifying.\n"
    "\tvalue\n"
    "\t\tThe value you're going to set `reg` equal to.\n"
    "\t\tIf you want to modify one of the 128 bit V registers,"
    " format value as follows:\n"
    "\t\t\"{byte1 byte2 byte3 byte4 byte5 byte6 byte7 byte8 byte9 byte10 "
    "byte11 byte12 byte13 byte14 byte15 byte16}\".\n"
    "\t\tBytes do not have to be in hex.\n"
    "\nSyntax:\n"
    "\tregister write reg value\n"
    "\n";

/*
 * Regexes
 */
static const char *REGISTER_VIEW_COMMAND_REGEX =
    "(?<reg>[^\\s]+)";

static const char *REGISTER_WRITE_COMMAND_REGEX =
    "^(?<reg>[\\$\\w\\d]+)\\s+(?<value>(\\{.*\\})|(-?(0[xX])?[\\d\\.]+))";

/*
 * Regex groups
 */
static const char *REGISTER_VIEW_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "reg" };

static const char *REGISTER_WRITE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "reg", "value" };

#endif
