#ifndef _CMD_H_
#define _CMD_H_

#define MAX_GROUPS 4
#define NUM_TOP_LEVEL_COMMANDS 21

#include "argparse.h"
#include "defs.h"

struct regexinfo {
    char *argregex;

    /* How many different capture groups argregex has. */
    int num_groups;

    /* If there is an unknown amount of arguments. For example, the user
     * can set multiple breakpoints or delete multiple convenience variables
     * with one command. In these cases, the group with an unknown amount of
     * results has to be the last item in groupnames.
     */
    int unk_num_args;

    /* Names of the groups, in the order they're expected in the command. */
    char *groupnames[MAX_GROUPS];
};

struct dbg_cmd_t {
    char *name;
    char *alias;
    char *documentation;
    
    struct regexinfo rinfo;

    /*
     * The array of subcmds can be thought as a tree,
     * which can be traversed level-order via queues.
     */
    struct dbg_cmd_t **subcmds;

    int level;
    int parentcmd;

    enum cmd_error_t (*cmd_function)(struct cmd_args_t *, int, char **);
    void (*audit_function)(struct cmd_args_t *, char **);
};

struct matchedcmdinfo_t {
    char *args;
    struct regexinfo rinfo;
    struct dbg_cmd_t *cmd;
    enum cmd_error_t (*cmd_function)(struct cmd_args_t *, int, char **);
    void (*audit_function)(struct cmd_args_t *, char **);
};

struct dbg_cmd_t *COMMANDS[NUM_TOP_LEVEL_COMMANDS];

/*
 * Command documentation will be kept here for readability elsewhere.
 */
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

static const char *BREAKPOINT_COMMAND_DOCUMENTATION =
    "Set a breakpoint. Include '--no-aslr' to keep ASLR from being added.\n"
    "This command has one mandatory argument and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tThis expression will be evaluated and used as the location for the breakpoint.\n"
    "\t\tThis command accepts an arbitrary amount of this argument,"
    " allowing you to set multiple breakpoints.\n"
    "\nSyntax:\n"
    "\tbreak location\n"
    "\n"
    "\nThis command has an alias: 'b'\n"
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

static const char *SIGNAL_COMMAND_DOCUMENTATION =
    "'signal' describes the group of commands which deal with signals.\n";

static const char *SIGNAL_HANDLE_COMMAND_DOCUMENTATION =
    "Change how iosdbg will handle signals sent from the OS to the debuggee.\n"
    "This command has no mandatory arguments and four optional arguments:\n"
    "\nOptional arguments:\n"
    "\tsignals\n"
    "\t\tThe signal(s) you're updating.\n"
    "\tnotify\n"
    "\t\tWhether or not iosdbg notifies you if the signal has been received.\n"
    "\tpass\n"
    "\t\tWhether or not the signal will be passed to the debuggee.\n"
    "\tstop\n"
    "\t\tWhether or not the debuggee will be stopped upon receiving this signal.\n"
    "\nIf none of these arguments are given, the current policy is shown.\n"
    "\nSyntax:\n"
    "\tsignal handle signals --notify <boolean> --pass <boolean> --stop <boolean>\n"
    "\n";

static const char *STEPI_COMMAND_DOCUMENTATION =
    "Step into the next machine instruction.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tstepi\n"
    "\n";

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

static const char *WATCH_COMMAND_DOCUMENTATION =
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
    "\n"
    "\nThis command has an alias: 'w'\n"
    "\n";

/*
 * Regex for each command used to match arguments.
 */
static const char *NO_ARGUMENT_REGEX = "";

static const char *ATTACH_COMMAND_REGEX =
    "(?J)((?<waitfor>--waitfor)\\s+(\"(?<target>.*)\"|(?!.*\")(?<target>\\w+)))|^\\s*((\"(?<target>.*)\")|(?!.*\")(?<target>\\w+))";

static const char *BREAKPOINT_COMMAND_REGEX =
    "(?<args>[\\w+\\-*\\/\\$()]+)";

static const char *DELETE_COMMAND_REGEX =
    "(?<type>b|w)(\\s+)?(?<ids>[-\\d\\s]+)?";

static const char *DISASSEMBLE_COMMAND_REGEX =
    "(?<location>[\\w+\\-*\\/\\$()]+)\\s+(?<count>[\\w+\\-*\\/\\$()]+)";

static const char *EXAMINE_COMMAND_REGEX =
    "(?<location>[\\w+\\-*\\/\\$()]+)\\s+(?<count>[\\w+\\-*\\/\\$()]+)";

static const char *HELP_COMMAND_REGEX =
    "(?J)^\"(?<cmd>[\\w\\s]+)\"|^(?<cmd>(?![\\w\\s]+\")\\w+)";

static const char *REGS_FLOAT_COMMAND_REGEX =
    "\\b(?<reg>[qvdsQVDS]{1}\\d{1,2}|(fpsr|FPSR|fpcr|FPCR)+)\\b";

static const char *REGS_GEN_COMMAND_REGEX =
    "(?<reg>(\\b([xwXW]{1}\\d{1,2}|(fp|FP|lr|LR|sp|SP|pc|PC|cpsr|CPSR)+)\\b))|(?=$)";

static const char *SET_COMMAND_REGEX =
    "(?<type>[*$]{1})(?<target>[\\w\\d+\\-*\\/$()]+)\\s*"
    "="
    "\\s*(?<value>(\\{.*\\})|(\\\".*\\\")|((?!\")[.\\-\\w\\d+\\-*\\/$()]+))";

static const char *SHOW_COMMAND_REGEX =
    "(?<var>\\$[\\w\\d+\\-*\\/$()]+)?";

static const char *SIGNAL_HANDLE_COMMAND_REGEX =
    "(^(?<signals>[\\w\\s]+[^--])\\s+"
    "--?(n(otify)?)\\s+(?<notify>0|1|(true|false)\\b)\\s+"
    "--?(p(ass)?)\\s+(?<pass>0|1|(true|false)\\b)\\s+"
    "--?(s(top)?)\\s+(?<stop>0|1|(true|false)\\b))?";

static const char *THREAD_SELECT_COMMAND_REGEX =
    "^\\s*(?<tid>\\d+)";

static const char *UNSET_COMMAND_REGEX =
    "(?<var>\\$\\w+)";

static const char *WATCH_COMMAND_REGEX =
    "(?(?=--[rw])(?<type>--[rw]{1,2}))\\s*"
    "(?<location>[\\w+\\-*\\/\\$()]+)\\s+(?<size>(0[xX])?\\d+)";
  
/*
 * Names for groups within command regex.
 */
static const char *NO_GROUPS[MAX_GROUPS] = {};

static const char *ATTACH_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "waitfor", "target" };

static const char *BREAKPOINT_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "args" };

static const char *DELETE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "type", "ids" };

static const char *DISASSEMBLE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "location", "count" };

static const char *EXAMINE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "location", "count" };

static const char *HELP_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "cmd" };

static const char *REGS_FLOAT_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "reg" };

static const char *REGS_GEN_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "reg" };

static const char *SET_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "type", "target", "value" };

static const char *SHOW_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "var" };

static const char *SIGNAL_HANDLE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "signals", "notify", "pass", "stop" };

static const char *THREAD_SELECT_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "tid" };

static const char *UNSET_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "var" };

static const char *WATCH_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "type", "location", "size" };

#endif
