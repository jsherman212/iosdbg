#ifndef _DBGCMD_H_
#define _DBGCMD_H_

#include "breakpoint.h"
#include "watchpoint.h"
#include "defs.h"
#include "dbgutils.h"
#include "machthread.h"

typedef int cmd_error_t;

#define CMD_SUCCESS (cmd_error_t)0
#define CMD_FAILURE (cmd_error_t)1

struct cmd_match_result_t {
	int num_matches;
	char *match;
	struct dbg_cmd_t *matched_cmd;
	char *matches;
	int ambigious;
	int perfect;
};

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
cmd_error_t cmdfunc_threadlist(const char *, int);
cmd_error_t cmdfunc_threadselect(const char *, int);
cmd_error_t cmdfunc_watch(const char *, int);

cmd_error_t execute_command(char *);

static struct dbg_cmd_t COMMANDS[] = {
	{ "attach", NULL, cmdfunc_attach, "Attach to a program with its PID or executable name." },
	{ "aslr", NULL, cmdfunc_aslr, "Show the ASLR slide." },
	{ "backtrace", "bt", cmdfunc_backtrace, "Unwind the stack." },
	{ "break", "b", cmdfunc_break, "Set a breakpoint." },
	{ "continue", "c", cmdfunc_continue, "Resume debuggee execution." },
	{ "delete", "d", cmdfunc_delete, "Delete a breakpoint or a watchpoint via its ID. Syntax:\n\t(d|delete) <type> <id>\n\n\ttype: 'b' for breakpoint or 'w' for watchpoint.\n\tid: the id of the breakpoint or watchpoint you want to delete." },
	{ "detach", NULL, cmdfunc_detach, "Detach from the debuggee." },
	{ "disassemble", "disas", cmdfunc_disassemble, "Disassemble memory from the debuggee. Syntax:\n\tdisassemble <location> <numlines>.\n\n\tPass --no-aslr to keep ASLR from being added to the location." },
	{ "examine", "x", cmdfunc_examine, "Examine debuggee memory. Syntax:\n\t(examine|x) <location> <count>\n\n\tPass --no-aslr to keep ASLR from being added to the location." },
	{ "help", NULL, cmdfunc_help, "Get help for a specific command." },
	{ "kill", NULL, cmdfunc_kill, "Kill the debuggee." },
	{ "quit", "q", cmdfunc_quit, "Quit iosdbg." },
	{ "regs", NULL, NULL, NULL },
	{ "regs float", NULL, cmdfunc_regsfloat, "Show a floating point register." },
	{ "regs gen", NULL, cmdfunc_regsgen, "Show one or all general purpose registers." },
	{ "set", NULL, cmdfunc_set, "Set the value of memory or a configuration variable for the debugger. Syntax:\n\tset (*offset|variable)=value\n\n\tYou must prefix an offset with '*'.\n\n\tIf you want your value to be intepreted as hex, use '0x'.\n\n\tPass --no-aslr to prevent ASLR from being added." },
	{ "thread", NULL, NULL, NULL },
	{ "thread list", NULL, cmdfunc_threadlist, "List threads from the debuggee." },
	{ "thread select", NULL, cmdfunc_threadselect, "Select a thread to focus on." },
	{ "watch", "w", cmdfunc_watch, "Set a watchpoint. Syntax:\n\twatch <addr> <size>\n\n\taddr: the location to watch\n\tsize: the size, in bytes, of the data at location to watch" }
};

#endif
