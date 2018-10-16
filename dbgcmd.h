#ifndef _DBGCMD_H_
#define _DBGCMD_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread/pthread.h>
#include <readline/readline.h>
#include <mach/mach.h>
#include "breakpoint.h"
#include "dbgutils.h" // For pid_of_program, suspend_threads
#include "defs.h" // For debuggee

extern boolean_t mach_exc_server(mach_msg_header_t *InHeadP, mach_msg_header_t *OutHeadP);

typedef int cmd_error_t;

#define CMD_SUCCESS (cmd_error_t)0;
#define CMD_FAILURE (cmd_error_t)1;

struct cmd_match_result_t {
	int num_matches;
	char *match;
	struct dbg_cmd_t *matched_cmd;
	char *matches;
	int ambigious;
	int perfect;
};

struct dbg_cmd_t {
	char *name;
	Function *function;
	char *desc;
};

cmd_error_t cmdfunc_attach(const char *, int);
cmd_error_t cmdfunc_aslr(const char *, int);
cmd_error_t cmdfunc_break(const char *, int);
cmd_error_t cmdfunc_continue(const char *, int);
cmd_error_t cmdfunc_delete(const char *, int);
cmd_error_t cmdfunc_detach(const char *, int);
cmd_error_t cmdfunc_help(const char *, int);
cmd_error_t cmdfunc_kill(const char *, int);
cmd_error_t cmdfunc_quit(const char *, int);
cmd_error_t cmdfunc_regsfloat(const char *, int);
cmd_error_t cmdfunc_regsgen(const char *, int);
cmd_error_t cmdfunc_set(const char *, int);

static struct dbg_cmd_t COMMANDS[] = {
	{ "attach", cmdfunc_attach, "Attach to a program with its PID or executable name." },
	{ "aslr", cmdfunc_aslr, "Show the ASLR slide." },
	{ "break", cmdfunc_break, "Set a breakpoint." },
	{ "continue", cmdfunc_continue, "Continue." },
	{ "delete", cmdfunc_delete, "Delete a breakpoint via its ID. Specify no ID to delete all breakpoints." },
	{ "detach", cmdfunc_detach, "Detach from the debuggee." },
	{ "help", cmdfunc_help, "Get help for a specific command." },
	{ "kill", cmdfunc_kill, "Kill the debuggee." },
	{ "quit", cmdfunc_quit, "Quit iosdbg." },
	{ "regs", NULL, NULL },
	{ "regs float", cmdfunc_regsfloat, "Show a floating point register." },
	{ "regs gen", cmdfunc_regsgen, "Show one or all general purpose registers." },
	{ "set", cmdfunc_set, "Set" },
};

cmd_error_t execute_command(char *);

// For visibility in iosdbg.c
void interrupt(int);

#endif