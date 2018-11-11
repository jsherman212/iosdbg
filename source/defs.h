/*
Hold important definitions for things being used everywhere.
*/

#ifndef _DEFS_H_
#define _DEFS_H_

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <mach/mach.h>
#include <pthread/pthread.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "printutils.h" // rl_printf

// General errors.
typedef int gen_error_t;

#define GEN_SUCCESS (gen_error_t)0
#define GEN_FAILURE (gen_error_t)1

#define MAX_EXCEPTION_PORTS 16

struct original_exception_ports_t {
	mach_msg_type_number_t count;
	exception_mask_t masks[MAX_EXCEPTION_PORTS];
	exception_handler_t ports[MAX_EXCEPTION_PORTS];
	exception_behavior_t behaviors[MAX_EXCEPTION_PORTS];
	thread_state_flavor_t flavors[MAX_EXCEPTION_PORTS];
};

struct debuggee {
	// Task port to the debuggee.
	mach_port_t task;

	// PID of the debuggee.
	pid_t pid;

	// Port to notify us upon debuggee's termination.
	mach_port_t death_port;

	// Whether execution has been suspended or not.
	int interrupted;
	
	// Count of threads for the debuggee.
	mach_msg_type_number_t thread_count;

	// Port to get exceptions from the debuggee.
	mach_port_t exception_port;

	// Saved exception ports from the debuggee.
	struct original_exception_ports_t original_exception_ports;

	// List of breakpoints on the debuggee.
	struct linkedlist *breakpoints;

	// List of threads on the debuggee.
	struct linkedlist *threads;

	// The debuggee's ASLR slide.
	unsigned long long aslr_slide;

	// The function pointer to task_suspend
	kern_return_t (*suspend)();

	// The function pointer to task_resume
	kern_return_t (*resume)();
};

struct debuggee *debuggee;

#endif
