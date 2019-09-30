#ifndef _SYMCMD_H_
#define _SYMCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_symbols_add(struct cmd_args *, int, char **, char **);

static const char *SYMBOLS_COMMAND_DOCUMENTATION =
    "'symbols' describes the group of commands which deal with DWARF"
    " file processing.\n";

static const char *SYMBOLS_ADD_COMMAND_DOCUMENTATION =
    "Specify a DWARF file for source level debugging. C language only.\n"
    "This command has one mandatory arguments and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tpath\n"
    "\t\tThe path to the DWARF file.\n"
    "\nSyntax:\n"
    "\tsymbols add path\n"
    "\n";

/*
 * Regexes
 */
static const char *SYMBOLS_ADD_COMMAND_REGEX =
    "(?<path>[\\.\\/\\w\\s]+)";

/*
 * Regex groups
 */
static const char *SYMBOLS_ADD_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "path" };

#endif
