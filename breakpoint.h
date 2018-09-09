#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "defs.h"
#include "memutils.h"

struct breakpoint {
	// Breakpoint ID.
	int id;

	// Location of the breakpoint.
	unsigned long long location;

	// The old instruction that we overwrote to cause the exception.
	unsigned long long old_instruction;

	// How many times this breakpoint has hit.
	int hit_count;
};

static int current_breakpoint_id = 0;

struct breakpoint *breakpoint_new(unsigned long long);
int breakpoint_at_address(unsigned long long);