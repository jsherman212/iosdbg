/*
Implementation for a breakpoint.
*/

#include "breakpoint.h"

// Create a new breakpoint.
struct breakpoint *breakpoint_new(unsigned long long location){
	// invalid address
	if(location + debuggee->aslr_slide < debuggee->aslr_slide + 0x100000000)
		return NULL;

	struct breakpoint *bp = malloc(sizeof(struct breakpoint));

	if(!bp)
		return NULL;

	bp->id = current_breakpoint_id++;
	bp->location = location + debuggee->aslr_slide;
	
	unsigned char *orig_instruction = malloc(4);
	memutils_read_memory_at_location(bp->location, orig_instruction, 4);

	bp->old_instruction = memutils_buffer_to_number(orig_instruction, 4);
	bp->hit_count = 0;

	return bp;
}

// Set a breakpoint at address.
int breakpoint_at_address(unsigned long long address){
	struct breakpoint *bp = breakpoint_new(address);

	if(!bp)
		return 1;

	// write BRK #1 to the address we're breakpointing at
	memutils_write_memory_to_location(bp->location, BRK);

	// add this breakpoint to the debuggee's linked list of breakpoints
	linkedlist_add(debuggee->breakpoints, bp);

	return 0;
}

// I call this function when a breakpoint is hit.
void breakpoint_hit(struct breakpoint *bp){
	if(!bp)
		return;

	// increment hit count
	bp->hit_count++;
}

// Deleting a breakpoint means restoring the original instruction.
// Return: 0 if the breakpoint was found and deleted, 1 otherwise.
int breakpoint_delete(int breakpoint_id){
	if(!debuggee->breakpoints->front)
		return 1;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		printf("current_breakpoint old instruction %llx\n", current_breakpoint->old_instruction);

		if(current_breakpoint->id == breakpoint_id){
			memutils_write_memory_to_location(current_breakpoint->location, current_breakpoint->old_instruction);
			linkedlist_delete(debuggee->breakpoints, current_breakpoint);
			return 0;
		}

		current = current->next;
	}

	return 1;
}