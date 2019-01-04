/*
Implementation for a breakpoint.
*/

#include "breakpoint.h"

// Create a new breakpoint.
struct breakpoint *breakpoint_new(unsigned long long location, int temporary){
	// invalid address
	//if(location + debuggee->aslr_slide < debuggee->aslr_slide + 0x100000000)
	//	return NULL;

	struct breakpoint *bp = malloc(sizeof(struct breakpoint));

	if(!bp)
		return NULL;

	bp->id = current_breakpoint_id++;
	bp->location = location /*+ debuggee->aslr_slide*/;
	
	int sz = 0x4;
	
	void *orig_instruction = malloc(sz);
	memutils_read_memory_at_location((void *)bp->location, orig_instruction, sz);

	bp->old_instruction = CFSwapInt32(memutils_buffer_to_number(orig_instruction, sz));
	
	free(orig_instruction);
	
	bp->hit_count = 0;
	bp->disabled = 0;

	bp->temporary = temporary;
	
	debuggee->num_breakpoints++;

	return bp;
}

// Set a breakpoint at address.
bp_error_t breakpoint_at_address(unsigned long long address, int temporary){
	struct breakpoint *bp = breakpoint_new(address, temporary);

	if(!bp)
		return BP_FAILURE;

	// write BRK #0 to the address we're breakpointing at
	memutils_write_memory_to_location(bp->location, CFSwapInt32(BRK));

	// add this breakpoint to the debuggee's linked list of breakpoints
	linkedlist_add(debuggee->breakpoints, bp);

	if(!temporary)
		printf("Breakpoint %d at %#llx\n", bp->id, bp->location);

	return BP_SUCCESS;
}

// I call this function when a breakpoint is hit.
void breakpoint_hit(struct breakpoint *bp){
	if(!bp)
		return;
	
	if(bp->temporary)
		breakpoint_delete(bp->id);
	else
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

			if(!current_breakpoint->temporary)
				printf("Breakpoint %d deleted\n", current_breakpoint->id);
			
			debuggee->num_breakpoints--;

			return BP_SUCCESS;
		}

		current = current->next;
	}

	return BP_FAILURE;
}

bp_error_t breakpoint_disable(int breakpoint_id){
	if(!debuggee->breakpoints->front)
		return BP_FAILURE;

	if(breakpoint_id == 0)
		return BP_FAILURE;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		if(current_breakpoint->id == breakpoint_id){
			memutils_write_memory_to_location(current_breakpoint->location, current_breakpoint->old_instruction);
			current_breakpoint->disabled = 1;
			return BP_SUCCESS;
		}

		current = current->next;
	}

	return BP_FAILURE;	
}

bp_error_t breakpoint_enable(int breakpoint_id){
	if(!debuggee->breakpoints->front)
		return BP_FAILURE;

	if(breakpoint_id == 0)
		return BP_FAILURE;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		if(current_breakpoint->id == breakpoint_id){
			memutils_write_memory_to_location(current_breakpoint->location, CFSwapInt32(BRK));
			current_breakpoint->disabled = 0;
			return BP_SUCCESS;
		}

		current = current->next;
	}

	return BP_FAILURE;	
}

int breakpoint_disabled(int breakpoint_id){
	if(!debuggee->breakpoints->front)
		return BP_FAILURE;

	if(breakpoint_id == 0)
		return BP_FAILURE;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		if(current_breakpoint->id == breakpoint_id)
			return current_breakpoint->disabled;

		current = current->next;
	}

	return 0;
}

// Delete every breakpoint
void breakpoint_delete_all(void){
	if(!debuggee->breakpoints)
		return;

	if(!debuggee->breakpoints->front)
		return;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		breakpoint_delete(current_breakpoint->id);

		current = current->next;
	}
}

void breakpoint_disable_all(void){
	if(!debuggee->breakpoints->front)
		return;
	
	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;
		memutils_write_memory_to_location(current_breakpoint->location, current_breakpoint->old_instruction);
		current = current->next;
	}
}

void breakpoint_enable_all(void){
	if(!debuggee->breakpoints->front)
		return;
	
	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;
		memutils_write_memory_to_location(current_breakpoint->location, CFSwapInt32(BRK));
		current = current->next;
	}
}
