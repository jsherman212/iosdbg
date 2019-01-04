#include "watchpoint.h"

struct watchpoint *watchpoint_new(unsigned long long location, unsigned int data_len){
	if(data_len == 0)
		return NULL;

	if(data_len > sizeof(unsigned long long))
		return NULL;
	
	kern_return_t result = memutils_valid_location(location);
	
	if(result)
		return NULL;
	
	struct watchpoint *wp = malloc(sizeof(struct watchpoint));

	wp->id = current_watchpoint_id++;
	wp->location = location;	
	wp->hit_count = 0;
	
	wp->data_len = data_len;
	wp->data = malloc(wp->data_len);

	result = memutils_read_memory_at_location(wp->location, wp->data, wp->data_len);

	// make this memory location read only
	vm_protect(debuggee->task, location, data_len, 0, VM_PROT_READ);
	//printf("%s\n", mach_error_string(result));

	if(result)
		return NULL;
	

	return wp;
}

wp_error_t watchpoint_at_address(unsigned long long location, unsigned int data_len){
	struct watchpoint *wp = watchpoint_new(location, data_len);

	if(!wp){
		printf("Could not set watchpoint\n");
		return WP_FAILURE;
	}
	
	linkedlist_add(debuggee->watchpoints, wp);

	printf("Watchpoint %d at %#llx\n", wp->id, wp->location);
	
	debuggee->num_watchpoints++;

	return WP_SUCCESS;
}

void watchpoint_hit(struct watchpoint *wp){
	if(!wp)
		return;

	wp->hit_count++;
}

wp_error_t watchpoint_delete(int wp_id){
	if(!debuggee->watchpoints->front)
		return WP_FAILURE;

	if(wp_id == 0)
		return WP_FAILURE;

	struct node_t *current = debuggee->watchpoints->front;

	while(current){
		struct watchpoint *current_watchpoint = (struct watchpoint *)current->data;

		if(current_watchpoint->id == wp_id){
			linkedlist_delete(debuggee->watchpoints, current_watchpoint);
			vm_protect(debuggee->task, current_watchpoint->location, current_watchpoint->data_len, 0, VM_PROT_READ | VM_PROT_WRITE);

			printf("Watchpoint %d deleted\n", wp_id);

			debuggee->num_watchpoints--;

			return WP_SUCCESS;
		}

		current = current->next;
	}

	return WP_FAILURE;
}

wp_error_t watchpoint_set_state(int wp_id, int state){
	if(!debuggee->watchpoints->front)
		return WP_FAILURE;

	if(wp_id == 0)
		return WP_FAILURE;

	if(state != WP_DISABLE && state != WP_ENABLE)
		return WP_FAILURE;

	struct node_t *current = debuggee->watchpoints->front;

	while(current){
		struct watchpoint *current_watchpoint = (struct watchpoint *)current->data;

		if(current_watchpoint->id == wp_id){
			if(state == WP_DISABLE)
				vm_protect(debuggee->task, current_watchpoint->location, current_watchpoint->data_len, 0, VM_PROT_READ | VM_PROT_WRITE);
			else
				vm_protect(debuggee->task, current_watchpoint->location, current_watchpoint->data_len, 0, VM_PROT_READ);
			
			return WP_SUCCESS;
		}

		current = current->next;
	}

	return WP_FAILURE;	
}

void watchpoint_enable_all(void){
	if(!debuggee->watchpoints->front)
		return;
	
	struct node_t *current = debuggee->watchpoints->front;

	while(current){
		struct watchpoint *current_watchpoint = (struct watchpoint *)current->data;
		vm_protect(debuggee->task, current_watchpoint->location, current_watchpoint->data_len, 0, VM_PROT_READ);

		current = current->next;
	}
}

void watchpoint_disable_all(void){
	if(!debuggee->watchpoints->front)
		return;
	
	struct node_t *current = debuggee->watchpoints->front;

	while(current){
		struct watchpoint *current_watchpoint = (struct watchpoint *)current->data;
		vm_protect(debuggee->task, current_watchpoint->location, current_watchpoint->data_len, 0, VM_PROT_READ | VM_PROT_WRITE);

		current = current->next;
	}
}

void watchpoint_delete_all(void){
	if(!debuggee->watchpoints->front)
		return;

	struct node_t *current = debuggee->watchpoints->front;

	while(current){
		struct watchpoint *current_watchpoint = (struct watchpoint *)current->data;

		vm_protect(debuggee->task, current_watchpoint->location, current_watchpoint->data_len, 0, VM_PROT_READ | VM_PROT_WRITE);
		linkedlist_delete(debuggee->watchpoints, current_watchpoint);

		current = current->next;
	}

}
