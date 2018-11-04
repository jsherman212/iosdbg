#ifndef _DBGCMD_H_
#define _DBGCMD_H_

#include "breakpoint.h"
#include "dbgutils.h"
#include "defs.h"

extern boolean_t mach_exc_server(mach_msg_header_t *InHeadP, mach_msg_header_t *OutHeadP);

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
cmd_error_t cmdfunc_examine(const char *, int);
cmd_error_t cmdfunc_help(const char *, int);
cmd_error_t cmdfunc_kill(const char *, int);
cmd_error_t cmdfunc_quit(const char *, int);
cmd_error_t cmdfunc_regsfloat(const char *, int);
cmd_error_t cmdfunc_regsgen(const char *, int);
cmd_error_t cmdfunc_set(const char *, int);

cmd_error_t execute_command(char *);

static struct dbg_cmd_t COMMANDS[] = {
	{ "attach", NULL, cmdfunc_attach, "Attach to a program with its PID or executable name." },
	{ "aslr", NULL, cmdfunc_aslr, "Show the ASLR slide." },
	{ "backtrace", "bt", cmdfunc_backtrace, "Unwind until we cannot unwind further." },
	{ "break", "b", cmdfunc_break, "Set a breakpoint." },
	{ "continue", "c", cmdfunc_continue, "Continue." },
	{ "delete", "d", cmdfunc_delete, "Delete a breakpoint via its ID. Specify no ID to delete all breakpoints." },
	{ "detach", NULL, cmdfunc_detach, "Detach from the debuggee." },
	{ "examine", "x", cmdfunc_examine, "Examine memory at a location. Syntax:\n\t  (examine|x) <amount>/(optional size)<format> <location>\n\tSizes:\n\t  b: bytes\n\t  h: halfwords (two bytes)\n\t  w: words (four bytes, default)\n\t  g: giant words (eight bytes)\n\t  e: enormous word (16 bytes per line)\n\tFormats:\n\t  i: integer\n\t  x: hexadecimal\n\n\tIf you want your amount interpreted as hex, use '0x'.\n\tPass --no-aslr to prevent ASLR from being added." },
	{ "help", NULL, cmdfunc_help, "Get help for a specific command." },
	{ "kill", NULL, cmdfunc_kill, "Kill the debuggee." },
	{ "quit", "q", cmdfunc_quit, "Quit iosdbg." },
	{ "regs", NULL, NULL, NULL },
	{ "regs float", NULL, cmdfunc_regsfloat, "Show a floating point register." },
	{ "regs gen", NULL, cmdfunc_regsgen, "Show one or all general purpose registers." },
	{ "set", NULL, cmdfunc_set, "Set the value of memory or a configuration variable for the debugger. Syntax:\n\tset (*offset|variable)=value\n\n\tYou must prefix an offset with '*'.\n\n\tIf you want your value to be intepreted as hex, use '0x'.\n\n\tPass --no-aslr to prevent ASLR from being added." },
};

#endif
