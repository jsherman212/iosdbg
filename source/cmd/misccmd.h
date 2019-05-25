#ifndef _DBGCMD_H_
#define _DBGCMD_H_

#include "argparse.h"

/* When --waitfor is included as an argument for 'attach'. */
extern int KEEP_CHECKING_FOR_PROCESS;

enum cmd_error_t cmdfunc_aslr(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_attach(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_backtrace(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_continue(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_detach(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_help(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_interrupt(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_kill(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_quit(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_stepi(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_trace(struct cmd_args_t *, int, char **);

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
    "\n";

static const char *CONTINUE_COMMAND_DOCUMENTATION =
    "Resume debuggee execution.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tcontinue\n"
    "\n";

static const char *DETACH_COMMAND_DOCUMENTATION =
    "Detach from the debuggee.\n"
    "This command has no arguments.\n"
    "\nSyntax:\n"
    "\tdetach\n"
    "\n";

static const char *HELP_COMMAND_DOCUMENTATION =
    "Get help for a command.\n"
    "This command has no mandatory arguments and one optional argument.\n"
    "\nOptional arguments:\n"
    "\tcommand\n"
    "\t\tThe command you'll be shown documentation for.\n"
    "\t\tOmit this argument to be shown all top level commands.\n"
    "\nSyntax:\n"
    "\thelp command?\n"
    "\n";

static const char *INTERRUPT_COMMAND_DOCUMENTATION =
    "Interrupt the debuggee.\n"
    "This commmand has no arguments.\n"
    "\nSyntax:\n"
    "\tinterrupt\n"
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

/*
 * Regexes
 */
static const char *ATTACH_COMMAND_REGEX =
    "(?J)((?<waitfor>--waitfor)\\s+(\"(?<target>.*)\"|(?!.*\")(?<target>\\w+)))|^\\s*((\"(?<target>.*)\")|(?!.*\")(?<target>\\w+))";


static const char *HELP_COMMAND_REGEX =
    "(?<cmd>[\\w\\s]+)?";

/*
 * Regex groups
 */
static const char *ATTACH_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "waitfor", "target" };

static const char *HELP_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "cmd" };

#endif
