#ifndef _DBGCMD_H_
#define _DBGCMD_H_

#include "argparse.h"

/* When --waitfor is included as an argument for 'attach'. */
extern int keep_checking_for_process;

enum cmd_error_t cmdfunc_aslr(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_attach(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_backtrace(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_continue(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_delete(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_detach(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_disassemble(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_examine(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_help(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_kill(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_quit(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_set(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_show(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_stepi(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_trace(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_unset(struct cmd_args_t *, int, char **);

static const char *ASLR_COMMAND_DOCUMENTATION = 
    "Show debuggee's ASLR slide.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\taslr\n"
    "\n";

static const char *ATTACH_COMMAND_DOCUMENTATION =
    "Attach to a program via its PID or name.\n"
    "To attach upon launch, give '--waitfor' before the program's name.\n"
    "This command has one mandatory argument and one optional argument.\n"
    "\nMandatory arguments:\n"
    "\ttarget\n"
    "\t\tWhat you want to attach to. It can be a PID or a program name.\n"
    "\n"
    "\nOptional arguments:\n"
    "\t--waitfor\n"
    "\t\t'--waitfor' tells iosdbg to wait for process launch.\n"
    "\nSyntax:\n"
    "\tattach --waitfor? target\n"
    "\n";

static const char *BACKTRACE_COMMAND_DOCUMENTATION =
    "Print a backtrace of the entire stack. All stack frames are printed.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tbacktrace\n"
    "\n"
    "\nThis command has an alias: 'bt'\n"
    "\n";

static const char *CONTINUE_COMMAND_DOCUMENTATION =
    "Resume debuggee execution.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tcontinue\n"
    "\n"
    "\nThis command has an alias: 'c'\n"
    "\n";

static const char *DELETE_COMMAND_DOCUMENTATION =
    "Delete breakpoints or watchpoints.\n"
    "This command has one mandatory argument and one optional argument.\n"
    "\nMandatory arguments:\n"
    "\ttype\n"
    "\t\tWhat you want to delete. This can be 'b' for breakpoint"
    " or 'w' for watchpoint.\n"
    "\nOptional arguments:\n"
    "\tid\n"
    "\t\tThe id of the breakpoint or watchpoint you want to delete.\n"
    "\t\tThis command accepts an arbitrary amount of this argument,"
    " allowing you to delete multiple breakpoints or watchpoints.\n"
    "\t\tOmit this argument for the option to delete all breakpoints"
    " or watchpoints.\n"
    "\nSyntax:\n"
    "\tdelete type id?\n"
    "\n"
    "\nThis command has an alias: 'd'\n"
    "\n";

static const char *DETACH_COMMAND_DOCUMENTATION =
    "Detach from the debuggee.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tdetach\n"
    "\n";

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

static const char *HELP_COMMAND_DOCUMENTATION =
    "Get help for a command.\n"
    "This command has one mandatory argument and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tcommand\n"
    "\t\tThe command you'll be shown documentation for.\n"
    "\nSyntax:\n"
    "\thelp command\n"
    "\n";

static const char *KILL_COMMAND_DOCUMENTATION =
    "Kill the debuggee.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tkill\n"
    "\n";

static const char *QUIT_COMMAND_DOCUMENTATION =
    "Quit iosdbg.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tquit\n"
    "\n"
    "\nThis command has an alias: 'q'\n"
    "\n";

static const char *SET_COMMAND_DOCUMENTATION =
    "Modify debuggee memory, registers, or iosdbg convenience variables.\n"
    "Include '--no-aslr' to keep ASLR from being added.\n"
    "This command has two mandatory arguments and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\ttarget\n"
    "\t\tThis expression will be evaluated and interpreted as what"
    " will be changed.\n"
    "\t\tPrefix locations in memory with '*'.\n"
    "\t\tPrefix registers and convenience variables with '$'.\n"
    "\tvalue\n"
    "\t\tWhat `target` will be changed to.\n"
    "\t\tThis is an expression only when writing to memory. (prefix = '*')\n"
    "\t\tIf you want to modify one of the 128 bit V registers,"
    " format value as follows:\n"
    "\t\t\"{byte1 byte2 byte3 byte4 byte5 byte6 byte7 byte8 byte9 byte10 "
    "byte11 byte12 byte13 byte14 byte15 byte16}\".\n"
    "\t\tBytes do not have to be in hex.\n"
    "\t\tConvenience variables can hold integers, floating point values,"
    " and strings.\n"
    "\t\tWhen setting a convenience variable, include '.' for a floating"
    " point value\n"
    "\t\t and quotes for strings.\n"
    "\t\tConvenience variables must be prefixed with '$'.\n"
    "\nSyntax:\n"
    "\tset target=value\n"
    "\n";

static const char *SHOW_COMMAND_DOCUMENTATION =
    "Display convenience variables.\n"
    "This command has no mandatory arguments and one optional argument.\n"
    "\nOptional arguments:\n"
    "\tvar\n"
    "\t\tWhat variable to display.\n"
    "\t\tThis command accepts an arbitrary amount of this argument,\n"
    "\t\t allowing you to display many convenience variables at once.\n"
    "\t\tOmit this argument to display every convenience variable.\n"
    "\nSyntax:\n"
    "\tshow var\n"
    "\n";

static const char *STEPI_COMMAND_DOCUMENTATION =
    "Step into the next machine instruction.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tstepi\n"
    "\n";

static const char *TRACE_COMMAND_DOCUMENTATION =
    "This command provides similar functionality to strace through"
    " the kdebug interface.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\ttrace\n"
    "\n";

static const char *UNSET_COMMAND_DOCUMENTATION =
    "Revert the value of a convenience variable to 'void'.\n"
    "This command has one mandatory argument and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tvar\n"
    "\t\tThe convenience variable to modify.\n"
    "\t\tThis command accepts an arbitrary amount of this argument,\n"
    "\t\t allowing you to modify many convenience variables at once.\n"
    "\nSyntax:\n"
    "\tunset var\n"
    "\n";

/*
 * Regexes
 */
static const char *ATTACH_COMMAND_REGEX =
    "(?J)((?<waitfor>--waitfor)\\s+(\"(?<target>.*)\"|(?!.*\")(?<target>\\w+)))|^\\s*((\"(?<target>.*)\")|(?!.*\")(?<target>\\w+))";

static const char *DELETE_COMMAND_REGEX =
    "(?<type>b|w)(\\s+)?(?<ids>[-\\d\\s]+)?";

static const char *DISASSEMBLE_COMMAND_REGEX =
    "(?<location>[\\w+\\-*\\/\\$()]+)\\s+(?<count>[\\w+\\-*\\/\\$()]+)";

static const char *EXAMINE_COMMAND_REGEX =
    "(?<location>[\\w+\\-*\\/\\$()]+)\\s+(?<count>[\\w+\\-*\\/\\$()]+)";

static const char *HELP_COMMAND_REGEX =
    "(?J)^\"(?<cmd>[\\w\\s]+)\"|^(?<cmd>(?![\\w\\s]+\")\\w+)";

static const char *SET_COMMAND_REGEX =
    "(?<type>[*$]{1})(?<target>[\\w\\d+\\-*\\/$()]+)\\s*"
    "="
    "\\s*(?<value>(\\{.*\\})|(\\\".*\\\")|((?!\")[.\\-\\w\\d+\\-*\\/$()]+))";

static const char *SHOW_COMMAND_REGEX =
    "(?<var>\\$[\\w\\d+\\-*\\/$()]+)?";

static const char *UNSET_COMMAND_REGEX =
    "(?<var>\\$\\w+)";

/*
 * Regex groups
 */
static const char *ATTACH_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "waitfor", "target" };

static const char *DELETE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "type", "ids" };

static const char *DISASSEMBLE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "location", "count" };

static const char *EXAMINE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "location", "count" };

static const char *HELP_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "cmd" };

static const char *SET_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "type", "target", "value" };

static const char *SHOW_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "var" };

static const char *UNSET_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "var" };

#endif
