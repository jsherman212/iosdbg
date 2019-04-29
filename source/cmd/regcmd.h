#ifndef _REGCMD_H_
#define _REGCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_regsfloat(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_regsgen(struct cmd_args_t *, int, char **);

static const char *REGS_COMMAND_DOCUMENTATION =
    "'regs' describes the group of commands which deal with register viewing.\n";

static const char *REGS_FLOAT_COMMAND_DOCUMENTATION =
    "Show floating point registers.\n"
    "This command has one mandatory argument and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\treg\n"
    "\t\tThe floating point register.\n"
    "\t\tThis command accepts an arbitrary amount of this argument,"
    " allowing you to view many floating point registers at once.\n"
    "\nSyntax:\n"
    "\tregs float reg\n"
    "\n";

static const char *REGS_GEN_COMMAND_DOCUMENTATION =
    "Show general purpose registers.\n"
    "This command has no mandatory arguments and one optional argument.\n"
    "\nOptional arguments:\n"
    "\treg\n"
    "\t\tThe general purpose register.\n"
    "\t\tThis command accepts an arbitrary amount of this argument,"
    " allowing you to view many general purpose registers at once.\n"
    "\t\tOmit this argument to see every general purpose register.\n"
    "\nSyntax:\n"
    "\tregs gen reg\n"
    "\n";

/*
 * Regexes
 */
static const char *REGS_FLOAT_COMMAND_REGEX =
    "\\b(?<reg>[qvdsQVDS]{1}\\d{1,2}|(fpsr|FPSR|fpcr|FPCR)+)\\b";

static const char *REGS_GEN_COMMAND_REGEX =
    "(?<reg>(\\b([xwXW]{1}\\d{1,2}|(fp|FP|lr|LR|sp|SP|pc|PC|cpsr|CPSR)+)\\b))|(?=$)";

/*
 * Regex groups
 */
static const char *REGS_FLOAT_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "reg" };

static const char *REGS_GEN_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "reg" };

#endif
