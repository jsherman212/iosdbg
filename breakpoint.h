#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "defs.h"
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
};

static int current_breakpoint_id = 1;

// BRK #1
static unsigned long long BRK = 0x200020D4;

struct breakpoint *breakpoint_new(unsigned long long);
int breakpoint_at_address(unsigned long long);
void breakpoint_hit(struct breakpoint *);
int breakpoint_delete(int);
void breakpoint_delete_all();