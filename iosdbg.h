#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <mach/mach.h>
#include <errno.h>
#include <pthread/pthread.h>

#include "linenoise.h"
#include "mach_exc.h"

#include "linkedlist.h"

#define RESET "\033[0m"
#define GREEN "\033[0;32m"
#define YELLOW "\033[0;33m"
#define RED "\033[0;31m"

// implemented in iosdbg.c
void help();
void setup_initial_debuggee();
int resume();
int detach();
void resume_threads();
int suspend_threads();
int attach(pid_t);
void interrupt(int);
int show_general_registers();
int show_neon_registers();
void setup_exception_handling();

extern boolean_t mach_exc_server(mach_msg_header_t *InHeadP, mach_msg_header_t *OutHeadP);

/* Both unused. */
kern_return_t catch_mach_exception_raise_state(mach_port_t exception_port, exception_type_t exception, exception_data_t code, mach_msg_type_number_t code_count, int *flavor, thread_state_t in_state, mach_msg_type_number_t in_state_count, thread_state_t out_state, mach_msg_type_number_t *out_state_count){return KERN_FAILURE;}
kern_return_t catch_mach_exception_raise_state_identity(mach_port_t exception_port, mach_port_t thread, mach_port_t task, exception_type_t exception, exception_data_t code, mach_msg_type_number_t code_count, int *flavor, thread_state_t in_state, mach_msg_type_number_t in_state_count, thread_state_t out_state, mach_msg_type_number_t *out_state_count){return KERN_FAILURE;}		

void fail(const char *message, ...){
	va_list args;
	va_start(args, message);

	printf(RED"FATAL: "RESET);
	vprintf(message, args);

	va_end(args);

	exit(1);
}

void warn(const char *message, ...){
	va_list args;
	va_start(args, message);

	printf(YELLOW"NOTICE: "RESET);
	vprintf(message, args);

	va_end(args);
}

void milestone(const char *message, ...){
	va_list args;
	va_start(args, message);

	printf(GREEN"[*] "RESET);
	vprintf(message, args);

	va_end(args);
}

const char *get_exception_code(exception_type_t exception){
	switch(exception){
	case EXC_BAD_ACCESS:
		return "EXC_BAD_ACCESS";
	case EXC_BAD_INSTRUCTION:
		return "EXC_BAD_INSTRUCTION";
	case EXC_ARITHMETIC:
		return "EXC_ARITHMETIC";
	case EXC_EMULATION:
		return "EXC_EMULATION";
	case EXC_SOFTWARE:
		return "EXC_SOFTWARE";
	case EXC_BREAKPOINT:
		return "EXC_BREAKPOINT";
	case EXC_SYSCALL:
		return "EXC_SYSCALL";
	case EXC_MACH_SYSCALL:
		return "EXC_MACH_SYSCALL";
	case EXC_RPC_ALERT:
		return "EXC_RPC_ALERT";
	case EXC_CRASH:
		return "EXC_CRASH";
	case EXC_RESOURCE:
		return "EXC_RESOURCE";
	case EXC_GUARD:
		return "EXC_GUARD";
	case EXC_CORPSE_NOTIFY:
		return "EXC_CORPSE_NOTIFY";
	default:
		return "<Unknown Exception>";
	}
}

#define MAX_EXCEPTION_PORTS 16

struct original_exception_ports_t {
	mach_msg_type_number_t count;
	exception_mask_t masks[MAX_EXCEPTION_PORTS];
	exception_handler_t ports[MAX_EXCEPTION_PORTS];
	exception_behavior_t behaviors[MAX_EXCEPTION_PORTS];
	thread_state_flavor_t flavors[MAX_EXCEPTION_PORTS];
};

struct debuggee {
	mach_port_t task;
	pid_t pid;
	int interrupted;
	thread_act_port_array_t threads;
	mach_msg_type_number_t thread_count;
	mach_port_t exception_port;
	struct original_exception_ports_t original_exception_ports;
};

struct debuggee *debuggee;
