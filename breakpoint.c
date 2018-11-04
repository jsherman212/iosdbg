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
	
	int sz = 0x4;
	
	void *orig_instruction = malloc(sz);
	memutils_read_memory_at_location((void *)bp->location, orig_instruction, sz);

	bp->old_instruction = CFSwapInt32(memutils_buffer_to_number(orig_instruction, sz));
	
	free(orig_instruction);
	
	bp->hit_count = 0;
	bp->disabled = 0;

	return bp;
}

// Set a breakpoint at address.
bp_error_t breakpoint_at_address(unsigned long long address){
	struct breakpoint *bp = breakpoint_new(address);

	if(!bp)
		return BP_FAILURE;

	// write BRK #1 to the address we're breakpointing at
	memutils_write_memory_to_location(bp->location, BRK);

	// add this breakpoint to the debuggee's linked list of breakpoints
	linkedlist_add(debuggee->breakpoints, bp);

	printf("Breakpoint %d at %#llx\n", bp->id, bp->location);

	return BP_SUCCESS;
}

// I call this function when a breakpoint is hit.
void breakpoint_hit(struct breakpoint *bp){
	if(!bp)
		return;

	// increment hit count
	bp->hit_count++;
}

// Deleting a breakpoint means restoring the original instruction.
// Return: BP_SUCCESS if the breakpoint was found and deleted, BP_FAILURE otherwise.
bp_error_t breakpoint_delete(int breakpoint_id){
	if(!debuggee->breakpoints->front)
		return BP_FAILURE;

	if(breakpoint_id == 0)
		return BP_FAILURE;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		if(current_breakpoint->id == breakpoint_id){
			memutils_write_memory_to_location(current_breakpoint->location, current_breakpoint->old_instruction);
			linkedlist_delete(debuggee->breakpoints, current_breakpoint);

			printf("Breakpoint %d deleted\n", current_breakpoint->id);

			return BP_SUCCESS;
		}

		current = current->next;
	}

	return BP_FAILURE;
}

// Delete every breakpoint
void breakpoint_delete_all(){
	if(!debuggee->breakpoints->front)
		return;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		breakpoint_delete(current_breakpoint->id);

		current = current->next;
	}
}
