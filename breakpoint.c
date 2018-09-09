/*
Implementation for a breakpoint.
*/

#include "breakpoint.h"

// Create a new breakpoint.
struct breakpoint *breakpoint_new(unsigned long long location){
	struct breakpoint *bp = malloc(sizeof(struct breakpoint));

	if(!bp)
		return NULL;

	bp->id = current_breakpoint_id++;
	bp->location = location;

	// TODO: original instruction

	bp->hit_count = 0;

	return bp;
}