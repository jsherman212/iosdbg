#ifndef _DBGCMD_H_
#define _DBGCMD_H_

#include <dlfcn.h>
#include "breakpoint.h"
#include "dbgutils.h"
#include "defs.h"
#include "expr.h"
#include "machthread.h"
#include "trace.h"
#include "watchpoint.h"

typedef int cmd_error_t;

#define CMD_SUCCESS (cmd_error_t)0
#define CMD_FAILURE (cmd_error_t)1

#define PT_DETACH   11
#define PT_SIGEXC   12
#define PT_ATTACHEXC    14
#define PT_THUPDATE 13

/* When --waitfor is included as an argument for 'attach'. */
extern int keep_checking_for_process;

struct dbg_cmd_t {
	const char *name;
	const char *alias;
	Function *function;
	const char *desc;
};

cmd_error_t cmdfunc_attach(const char *, int);
cmd_error_t cmdfunc_aslr(const char *, int);
cmd_error_t cmdfunc_backtrace(const char *, int);
cmd_error_t cmdfunc_break(const char *, int);
cmd_error_t cmdfunc_continue(const char *, int);
cmd_error_t cmdfunc_delete(const char *, int);
cmd_error_t cmdfunc_detach(const char *, int);
cmd_error_t cmdfunc_disassemble(const char *, int);
cmd_error_t cmdfunc_examine(const char *, int);
cmd_error_t cmdfunc_help(const char *, int);
cmd_error_t cmdfunc_kill(const char *, int);
cmd_error_t cmdfunc_quit(const char *, int);
cmd_error_t cmdfunc_regsfloat(const char *, int);
cmd_error_t cmdfunc_regsgen(const char *, int);
cmd_error_t cmdfunc_set(const char *, int);
cmd_error_t cmdfunc_stepi(const char *, int);
cmd_error_t cmdfunc_threadlist(const char *, int);
cmd_error_t cmdfunc_threadselect(const char *, int);
cmd_error_t cmdfunc_trace(const char *, int);
cmd_error_t cmdfunc_watch(const char *, int);

cmd_error_t execute_command(char *);

static struct dbg_cmd_t COMMANDS[] = {
	{ "aslr", NULL, cmdfunc_aslr, "Show the ASLR slide." },
	{ "attach", NULL, cmdfunc_attach, "Attach to a program with its PID or executable name. Syntax: attach <(PID|{--waitfor} progname)>\n\n\tInclude '--waitfor' to wait for the target process to launch." },
	{ "backtrace", "bt", cmdfunc_backtrace, "Unwind the stack." },
	{ "break", "b", cmdfunc_break, "Set a breakpoint.\n\n\tPass --no-aslr to keep ASLR from being added." },
	{ "continue", "c", cmdfunc_continue, "Resume debuggee execution." },
	{ "delete", "d", cmdfunc_delete, "Delete a breakpoint or a watchpoint via its ID. Syntax:\n\t(d|delete) <type> {id}\n\n\ttype: 'b' for breakpoint or 'w' for watchpoint.\n\tid: the *optional* id of the breakpoint or watchpoint you want to delete.\n\tIf you don't include it, you'll be given the option to delete all breakpoints or watchpoints." },
	{ "detach", NULL, cmdfunc_detach, "Detach from the debuggee." },
	{ "disassemble", "dis", cmdfunc_disassemble, "Disassemble memory from the debuggee. Syntax:\n\tdisassemble <location> <numlines>.\n\n\tPass --no-aslr to keep ASLR from being added to the location." },
	{ "examine", "x", cmdfunc_examine, "Examine debuggee memory. Syntax:\n\t(examine|x) (location|$register) <count>\n\n\tIf you want to view a register, prefix it with '$'. ASLR will not be accounted for.\n\n\tPass --no-aslr to keep ASLR from being added to the location." },
	{ "help", NULL, cmdfunc_help, "Get help for a specific command." },
	{ "kill", NULL, cmdfunc_kill, "Kill the debuggee." },
	{ "quit", "q", cmdfunc_quit, "Quit iosdbg." },
	{ "regs", NULL, NULL, NULL },
	{ "regs float", NULL, cmdfunc_regsfloat, "Show a floating point register. Syntax:\n\tregs float <reg1 reg2 ...>\n\n\tYou can list as many floating point registers as you want." },
	{ "regs gen", NULL, cmdfunc_regsgen, "Show general purpose registers. Syntax:\n\tregs gen {reg1 reg2 ...}\n\n\tThe argument is optional. All general purpose registers are dumped if there is no argument.\n\tYou can list as many general registers as you want." },
	{ "set", NULL, cmdfunc_set, "Modify debuggee memory, registers, or a configuration variable (TODO) for the debugger. Syntax:\n\tset (*offset|$register|variable)=value\n\n\tYou must prefix an offset with '*'.\n\tYou must prefix a register with '$'.\n\n\tIf you want to write to one of the 128 bit V registers, format value like this:\n\t\"{byte1 byte2 byte3 byte4 byte5 byte6 byte7 byte8 byte9 byte10 byte11 byte12 byte13 byte14 byte15 byte16}\".\n\n\tIf you want your value to be intepreted as hex, use '0x'.\n\n\tPass --no-aslr to prevent ASLR from being added." },
	{ "stepi", NULL, cmdfunc_stepi, "Step into the next instruction." },
	{ "thread", NULL, NULL, NULL },
	{ "thread list", NULL, cmdfunc_threadlist, "List threads from the debuggee." },
	{ "thread select", NULL, cmdfunc_threadselect, "Select a thread to focus on." },
	{ "trace", NULL, cmdfunc_trace, "Trace system calls, mach system calls, and mach messages from the debuggee. Press Ctrl+C to quit." },
	{ "watch", "w", cmdfunc_watch, "Set a watchpoint. Syntax:\n\twatch {type} <addr> <size>\n\n\ttype: what kind of access to <location> you want to watch for.\n\n\tvalid values: --r (read), --w (write), --rw (read/write).\n\n\tif no type is given, iosdbg defaults to --w.\n\n\taddr: the location to watch\n\tsize: the size, in bytes, of the data at location to watch" },
	{ "", NULL, NULL, ""}
};

#endif
