#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <mach/mach.h>
#include <errno.h>

#include "linenoise.h"

#define RESET "\033[0m"
#define GREEN "\033[0;32m"
#define YELLOW "\033[0;33m"
#define RED "\033[0;31m"			

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

struct debuggee {
	mach_port_t task;
	pid_t pid;
	int interrupted;
	thread_act_port_array_t threads;
	mach_msg_type_number_t thread_count;
	mach_port_t exception_port;
};

struct debuggee *debuggee;
