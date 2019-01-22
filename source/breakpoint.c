/*
Implementation for a breakpoint.
*/

#include "breakpoint.h"

/* Find an available hardware breakpoint register.*/
int find_ready_bp_reg(void){
	struct node_t *current = debuggee->breakpoints->front;
	
	/* Keep track of what hardware breakpoint registers are used
	 * in the breakpoints currently active.
	 */
	int *bp_map = malloc(sizeof(int) * debuggee->num_hw_bps);

	/* -1 means the hardware breakpoint register representing that spot
	 * in the array has not been used. 0 means the opposite.
	 */
	for(int i=0; i<debuggee->num_hw_bps; i++)
		bp_map[i] = -1;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		if(current_breakpoint->hw)
			bp_map[current_breakpoint->hw_bp_reg] = 0;

		current = current->next;
	}

	/* Now search bp_map for any empty spots. */
	for(int i=0; i<debuggee->num_hw_bps; i++){
		if(bp_map[i] != 0){
			free(bp_map);
			return i;
		}
	}

	free(bp_map);

	/* No available hardware watchpoint registers found. */
	return -1;
}

struct breakpoint *breakpoint_new(unsigned long location, int temporary, int single_step){
	if(location < 0x100000000)
		return NULL;

	struct breakpoint *bp = malloc(sizeof(struct breakpoint));

	if(!bp)
		return NULL;

	bp->hw = 0;
	bp->hw_bp_reg = -1;

	int available_bp_reg = find_ready_bp_reg();

	/* We have an available breakpoint register, use it. */
	if(available_bp_reg != -1){
		debuggee->get_debug_state();

		bp->hw = 1;
		bp->hw_bp_reg = available_bp_reg;

		/* Setup the DBGBCR<n>_EL1 register.
		 * We need the following criteria to correctly set up this breakpoint:
		 * 	- BT must be 0b0000 for an unlinked instruction address match, where
		 * 	  DBGBVR<n>_EL1 is the location of the breakpoint.
		 * 	- BAS must be 0b1111 to tell the machine to match the instruction
		 * 	  at DBGBVR<n>_EL1.
		 * 	- PMC must be 0b10 so these breakpoints generate debug events in EL0, 
		 * 	  where we are executing.
		 * 	- E must be 0b1 so this breakpoint is enabled.
		 */
		debuggee->debug_state.__bcr[available_bp_reg] = 
			BT |
			BAS |
			PMC |
			E;
		
		/* Bits[1:0] must be clear in DBGBVR<n>_EL1 or else the instruction is mis-aligned,
		 * so clear those bits in the location. */
		location &= ~0x3;

		/* Put the location in whichever DBGBVR<n>_EL1 is available. */
		debuggee->debug_state.__bvr[available_bp_reg] = location;
		debuggee->set_debug_state();
	}
	
	bp->location = location;

	int sz = 0x4;
	
	void *orig_instruction = malloc(sz);
	memutils_read_memory_at_location((void *)bp->location, orig_instruction, sz);

	bp->old_instruction = CFSwapInt32(memutils_buffer_to_number(orig_instruction, sz));
	
	free(orig_instruction);
	
	bp->hit_count = 0;
	bp->disabled = 0;

	bp->temporary = temporary;
	bp->ss = single_step;

	debuggee->num_breakpoints++;
	
	bp->id = current_breakpoint_id;
	
	if(!bp->temporary)
		current_breakpoint_id++;

	return bp;
}

// Set a breakpoint at address.
bp_error_t breakpoint_at_address(unsigned long address, int temporary, int single_step){
	struct breakpoint *bp = breakpoint_new(address, temporary, single_step);

	if(!bp)
		return BP_FAILURE;

	/* If we ran out of hardware breakpoints, set a software breakpoint
	 * by writing BRK #0 to bp->location.
	 */
	if(!bp->hw)
		memutils_write_memory_to_location(bp->location, CFSwapInt32(BRK));

	linkedlist_add(debuggee->breakpoints, bp);

	if(!temporary)
		printf("Breakpoint %d at %#lx\n", bp->id, bp->location);

	return BP_SUCCESS;
}

void breakpoint_hit(struct breakpoint *bp){
	if(!bp)
		return;

	if(bp->temporary)
		breakpoint_delete(bp->id);
	else
		bp->hit_count++;
}


void enable_hw_bp(struct breakpoint *bp){
	debuggee->get_debug_state();

	debuggee->debug_state.__bcr[bp->hw_bp_reg] = 
		BT | 
		BAS | 
		PMC | 
		E;

	debuggee->debug_state.__bvr[bp->hw_bp_reg] = (bp->location & ~0x3);

	debuggee->set_debug_state();
}

void disable_hw_bp(struct breakpoint *bp){
	debuggee->get_debug_state();
	debuggee->debug_state.__bcr[bp->hw_bp_reg] = 0;
	debuggee->set_debug_state();
}

/* Set whether or not a breakpoint is disabled or enabled,
 * and take action accordingly.
 */
void bp_set_state_internal(struct breakpoint *bp, int disabled){
	if(bp->hw){
		if(disabled)
			disable_hw_bp(bp);
		else
			enable_hw_bp(bp);
	}
	else{
		if(disabled)
			memutils_write_memory_to_location(bp->location, bp->old_instruction);	
		else
			memutils_write_memory_to_location(bp->location, CFSwapInt32(BRK));
	}

	bp->disabled = disabled;
}

void bp_delete_internal(struct breakpoint *bp){
	bp_set_state_internal(bp, BP_DISABLED);
	linkedlist_delete(debuggee->breakpoints, bp);
	
	debuggee->num_breakpoints--;
}

bp_error_t breakpoint_delete(int breakpoint_id){
	if(!debuggee->breakpoints->front)
		return BP_FAILURE;

	if(breakpoint_id == 0)
		return BP_FAILURE;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		if(current_breakpoint->id == breakpoint_id){
			bp_delete_internal(current_breakpoint);
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
			bp_set_state_internal(current_breakpoint, BP_DISABLED);
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
			bp_set_state_internal(current_breakpoint, BP_ENABLED);
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

void breakpoint_delete_all(void){
	if(!debuggee->breakpoints)
		return;

	if(!debuggee->breakpoints->front)
		return;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		bp_delete_internal(current_breakpoint);

		current = current->next;
	}
}

struct breakpoint *find_bp_with_address(unsigned long addr){
	if(!debuggee->breakpoints->front)
		return NULL;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;
		
		if(current_breakpoint->location == addr)
			return current_breakpoint;
		
		current = current->next;
	}

	return NULL;
}

void delete_ss_bps(void){
	if(!debuggee->breakpoints)
		return;

	if(!debuggee->breakpoints->front)
		return;

	struct node_t *current = debuggee->breakpoints->front;

	while(current){
		struct breakpoint *current_breakpoint = (struct breakpoint *)current->data;

		if(current_breakpoint->ss)
			bp_delete_internal(current_breakpoint);

		current = current->next;
	}
}
