/*
Hold important definitions for things being used everywhere.
*/

#include <mach/mach.h>

#define MAX_EXCEPTION_PORTS 16

struct original_exception_ports_t {
	mach_msg_type_number_t count;
	exception_mask_t masks[MAX_EXCEPTION_PORTS];
	exception_handler_t ports[MAX_EXCEPTION_PORTS];
	exception_behavior_t behaviors[MAX_EXCEPTION_PORTS];
	thread_state_flavor_t flavors[MAX_EXCEPTION_PORTS];
};

#pragma once
struct debuggee {
	// Task port to the debuggee.
	mach_port_t task;

	// PID of the debuggee.
	pid_t pid;

	// Whether execution has been suspended or not.
	int interrupted;

	// The list of threads for the debuggee.
	thread_act_port_array_t threads;

	// Count of threads for the debuggee.
	mach_msg_type_number_t thread_count;

	// Port to get exceptions from the debuggee.
	mach_port_t exception_port;

	// Saved exception ports from the debuggee.
	struct original_exception_ports_t original_exception_ports;

	// List of breakpoints on the debuggee.
	struct linkedlist *breakpoints;

	// The debuggee's ASLR slide.
	unsigned long long aslr_slide;
};

struct debuggee *debuggee = NULL;