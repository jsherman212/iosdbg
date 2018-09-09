#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pthread/pthread.h>
#include "linenoise.h"
#include "mach_exc.h"
#include "linkedlist.h"
#include "breakpoint.h"
#include "memutils.h"
#include "defs.h"

#define RESET "\033[0m"
#define GREEN "\033[0;32m"
#define YELLOW "\033[0;33m"
#define RED "\033[0;31m"

// Implemented in iosdbg.c
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