#ifndef _BREAKPOINT_H_
#define _BREAKPOINT_H_

#include <stdio.h>
#include <stdlib.h>
#include "memutils.h"
#include "linkedlist.h"

struct breakpoint {
	// Breakpoint ID.
	int id;

	// Location of the breakpoint.
	unsigned long long location;

	// The old instruction that we overwrote to cause the exception.
	unsigned long long old_instruction;

	// How many times this breakpoint has hit.
	int hit_count;

	// Whether or not this breakpoint is disabled.
	// Disabled does not mean deleted. It is still active, but it does not hit.
	int disabled;
};

typedef int bp_error_t;

#define BP_SUCCESS (bp_error_t)0;
#define BP_FAILURE (bp_error_t)1;

static int current_breakpoint_id = 1;

// BRK #1
static unsigned long long BRK = 0x200020D4;

struct breakpoint *breakpoint_new(unsigned long long);
bp_error_t breakpoint_at_address(unsigned long long);
void breakpoint_hit(struct breakpoint *);
bp_error_t breakpoint_delete(int);
void breakpoint_delete_all(void);

#endif