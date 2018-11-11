#include "machthread.h"

struct machthread *machthread_new(mach_port_t thread_port){
	struct machthread *mt = malloc(sizeof(struct machthread));

	mt->port = thread_port;
	mt->focused = 0;
	mt->tid = get_tid_from_thread_port(mt->port);

	memset(mt->tname, '\0', sizeof(mt->tname));
	char *tname = get_thread_name_from_thread_port(mt->port);
	
	if(tname){
		strcpy(mt->tname, tname);

		free(tname);
	}
	else{
		const char *n = "";
		strcpy(mt->tname, (char *)n);
	}
	
	machthread_updatestate(mt);	
	
	mt->ID = current_machthread_id++;

	return mt;	
}

struct machthread *machthread_fromport(mach_port_t thread_port){
	if(thread_port == MACH_PORT_NULL)
		return NULL;

	if(!debuggee->threads)
		return NULL;

	struct node_t *current = debuggee->threads->front;

	while(current){
		struct machthread *t = current->data;

		if(t->port == thread_port)
			return t;

		current = current->next;
	}

	// not found
	return NULL;
}

struct machthread *machthread_find(int ID){
	if(!debuggee->threads)
		return NULL;

	struct node_t *current = debuggee->threads->front;

	while(current){
		struct machthread *t = current->data;

		if(t->ID == ID)
			return t;

		current = current->next;
	}

	// not found
	return NULL;
}

struct machthread *machthread_getfocused(void){
	if(!debuggee->threads)
		return NULL;

	struct node_t *current = debuggee->threads->front;

	while(current){
		struct machthread *t = current->data;

		if(t->focused)
			return t;

		current = current->next;
	}
	
	// we shouldn't reach here...
	return NULL;
}

int machthread_setfocusgivenindex(int focus_index){
	// we print out the thread list starting at 1
	focus_index--;
	int counter = 0;

	struct node_t *current = debuggee->threads->front;
	struct machthread *newfocus = NULL;

	while(current && counter <= focus_index){
		newfocus = current->data;
		current = current->next;

		counter++;
	}

	if(!newfocus)
		return -1;

	machthread_setfocused(newfocus->port);

	return 0;
}

void machthread_reassignall(void){
	if(!debuggee->threads)
		return;

	current_machthread_id = 1;
	struct node_t *current = debuggee->threads->front;

	while(current){
		struct machthread *t = current->data;
		t->ID = current_machthread_id++;
		current = current->next;
	}
}

void machthread_updatestate(struct machthread *mt){
	if(!mt)
		return;

	if(mt->port == MACH_PORT_NULL)
		return;
	
	arm_thread_state64_t thread_state;
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;

	kern_return_t kret = thread_get_state(mt->port, ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);

	// if there was a problem, delete this thread
	if(kret){
		if(mt->focused)
			// switching will happen after machthread_updatethreads returns
			printf("[Switching to thread 1, %llx, '%s']\n", ((struct machthread *)debuggee->threads->front->data)->tid, ((struct machthread *)debuggee->threads->front->data)->tname);
		
		linkedlist_delete(debuggee->threads, mt);
		return;
	}

	mt->thread_state = thread_state;
}

void machthread_updatethreads(thread_act_port_array_t threads){
	if(!debuggee->threads)
		return;

	// check if no threads are in this linked list, this is a special case
	// after we intitially populate the linked list, add and remove from it
	if(!debuggee->threads->front){
		for(int i=0; i<debuggee->thread_count; i++){
			struct machthread *add = machthread_new(threads[i]);

			linkedlist_add(debuggee->threads, add);
		}

		return;
	}

	// add any new threads
	// yes, this is terrible
	for(int i=0; i<debuggee->thread_count; i++){
		struct node_t *current = debuggee->threads->front;
		int already_present = 0;

		while(current){
			struct machthread *t = current->data;

			if(t->port == threads[i]){
				already_present = 1;
				break;
			}

			current = current->next;
		}
		
		if(!already_present){
			struct machthread *add = machthread_new(threads[i]);
			linkedlist_add(debuggee->threads, add);
		}
	}

	struct node_t *current = debuggee->threads->front;

	while(current){
		struct machthread *t = current->data;
		
		if(t->port == MACH_PORT_NULL)
			linkedlist_delete(debuggee->threads, t);
		else
			machthread_updatestate(t);

		current = current->next;
	}
	
	machthread_reassignall();
}

void machthread_setfocused(mach_port_t thread_port){
	if(thread_port == MACH_PORT_NULL)
		return;

	struct machthread *prevfocus = machthread_getfocused();
	struct machthread *newfocus = machthread_fromport(thread_port);
	
	if(!newfocus)
		return;

	newfocus->focused = 1;
	
	if(prevfocus)
		prevfocus->focused = 0;
}

void machthread_free(struct machthread *mt){
	//current_machthread_id = 1;

	free(mt);
}

// Caller is responsible for freeing this
char *get_thread_name_from_thread_port(mach_port_t thread_port){
	if(thread_port == MACH_PORT_NULL)
		return NULL;

	thread_extended_info_data_t exinfo;
	mach_msg_type_number_t count = THREAD_EXTENDED_INFO_COUNT;

	kern_return_t kret = thread_info(thread_port, THREAD_EXTENDED_INFO, (thread_info_t)&exinfo, &count);

	if(kret)
		return NULL;

	return strdup(exinfo.pth_name);
}

kern_return_t get_tid_from_thread_port(mach_port_t thread_port){
	if(thread_port == MACH_PORT_NULL)
		return KERN_FAILURE;

	thread_identifier_info_data_t ident;
	mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;

	kern_return_t kret = thread_info(thread_port, THREAD_IDENTIFIER_INFO, (thread_info_t)&ident, &count);

	if(kret)
		return kret;

	return ident.thread_id;
}
