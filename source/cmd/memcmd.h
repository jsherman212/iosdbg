#ifndef _MEMCMD_H_
#define _MEMCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_disassemble(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_examine(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_memoryfind(struct cmd_args_t *, int, char **);

static const char *DISASSEMBLE_COMMAND_DOCUMENTATION =
    "Disassemble debuggee memory. Include '--no-aslr' to keep ASLR from being added.\n"
    "This command has two mandatory arguments and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tThis expression will be evaluated and used as where iosdbg"
    " will start disassembling.\n"
    "\tcount\n"
    "\t\tHow many bytes iosdbg will disassemble.\n"
    "\nSyntax:\n"
    "\tdisassemble location count\n"
    "\n"
    "\nThis command has an alias: 'dis'\n"
    "\n";

static const char *EXAMINE_COMMAND_DOCUMENTATION =
    "View debuggee memory. Include '--no-aslr' to keep ASLR from being added.\n"
    "This command has two mandatory arguments and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tThis expression will be evaluted and used as where iosdbg"
    " will start dumping memory.\n"
    "\tcount\n"
    "\t\tHow many bytes iosdbg will dump.\n"
    "\nSyntax:\n"
    "\texamine location count\n"
    "\n"
    "\nThis command has an alias: 'x'\n"
    "\n";

static const char *MEMORY_COMMAND_DOCUMENTATION =
    "Memory command class.\n";

static const char *MEMORY_FIND_COMMAND_DOCUMENTATION =
    "Memory find documentation\n";

/*
 * Regexes
 */
static const char *DISASSEMBLE_COMMAND_REGEX =
    "(?<location>[\\w+\\-*\\/\\$()]+)\\s+(?<count>[\\w+\\-*\\/\\$()]+)";

static const char *EXAMINE_COMMAND_REGEX =
    "(?<location>[\\w+\\-*\\/\\$()]+)\\s+(?<count>[\\w+\\-*\\/\\$()]+)";

/*
 * Regex groups
 */
static const char *DISASSEMBLE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "location", "count" };

static const char *EXAMINE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "location", "count" };

#endif
