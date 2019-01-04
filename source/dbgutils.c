#include "dbgutils.h"

// pidof implementation
// Get the pid of a program based on the program name provided
// Return: pid on success, -1 on error
pid_t pid_of_program(char *progname){
	int err;
	struct kinfo_proc *result = NULL;

	static const int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };

	size_t length = 0;

	err = sysctl((int *)name, (sizeof(name) / sizeof(name[0])) - 1, NULL, &length, NULL, 0);
	
	if(err){
		printf("Couldn't get the size of our kinfo_proc buffer: %s\n", strerror(errno));
		return -1;
	}
	
	result = malloc(length);
	err = sysctl((int *)name, (sizeof(name) / sizeof(name[0])) - 1, result, &length, NULL, 0);
	
	if(err){
		printf("Second sysctl call failed: %s\n", strerror(errno));
		return -1;
	}

	int num_procs = length / sizeof(struct kinfo_proc);
	int matches = 0;
	char *matchstr = malloc(512);
	bzero(matchstr, 512);
	pid_t final_pid = -1;
	int maxnamelen = MAXCOMLEN + 1;

	for(int i=0; i<num_procs; i++){
		struct kinfo_proc *current = &result[i];
		
		if(current){
			pid_t pid = current->kp_proc.p_pid;
			char *pname = current->kp_proc.p_comm;
			int pnamelen = strlen(pname);
			int charstocompare = pnamelen < maxnamelen ? pnamelen : maxnamelen;

			if(strncmp(pname, progname, charstocompare) == 0){
				matches++;
				sprintf(matchstr, "%s PID %d: %s\n", matchstr, pid, pname);
				final_pid = pid;
			}
		}
	}

	free(result);
	
	if(matches > 1){
		printf("Multiple instances of '%s': \n%s\n", progname, matchstr);
		free(matchstr);
		return -1;
	}

	free(matchstr);

	if(matches == 0){
		printf("%s not found\n", progname);
		return -1;
	}

	if(matches == 1)
		return final_pid;
	
	return -1;
}

void *_exception_server(void *arg){
	while(1){
		// shut down this thread once we detach
		if(debuggee->pid == -1)
			pthread_exit(NULL);

		kern_return_t err = mach_msg_server_once(mach_exc_server, 4096, debuggee->exception_port, 0);

		if(err)
			printf("\nmach_msg_server_once: error: %s\n", mach_error_string(err));
	}

	return NULL;
}

// setup our exception related stuff
void setup_exceptions(void){
	debuggee->setup_exception_handling();

	// start the exception server
	pthread_t exception_server_thread;
	pthread_create(&exception_server_thread, NULL, _exception_server, NULL);
}

void *bp_manager_thread(void *arg){
	while(1){
		if(debuggee->num_breakpoints > 0 && debuggee->pid != -1){
			struct machthread *focused = machthread_getfocused();

			if(focused){
				arm_thread_state64_t thread_state;
				mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
				kern_return_t err = thread_get_state(focused->port, ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);

				if(err == KERN_SUCCESS){
					// we need a way to disable breakpoints once they hit or we'll be stuck on them forever
					if(debuggee->last_bkpt_PC != thread_state.__pc && breakpoint_disabled(debuggee->last_bkpt_ID)){
						// cease debuggee execution while we do this to prevent anything screwy
						task_suspend(debuggee->task);
						breakpoint_enable(debuggee->last_bkpt_ID);
						task_resume(debuggee->task);
					}
				}
			}
		}
	}

	return NULL;
}

void hexdump(const void* data, size_t size) {
    char ascii[17];
    size_t i, j;
    ascii[16] = '\0';
    for (i = 0; i < size; ++i) {
        printf("%02X ", ((unsigned char*)data)[i]);
        if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
            ascii[i % 16] = ((unsigned char*)data)[i];
        } else {
            ascii[i % 16] = '.';
        }
        if ((i+1) % 8 == 0 || i+1 == size) {
            printf(" ");
            if ((i+1) % 16 == 0) {
                printf("|  %s \n", ascii);
            } else if (i+1 == size) {
                ascii[(i+1) % 16] = '\0';
                if ((i+1) % 16 <= 8) {
                    printf(" ");
                }
                for (j = (i+1) % 16; j < 16; ++j) {
                    printf("   ");
                }
                printf("|  %s \n", ascii);
            }
        }
    }
}

void setup_initial_debuggee(){
	debuggee = malloc(sizeof(struct debuggee));

	// if we aren't attached to anything, debuggee's pid is -1
	debuggee->pid = -1;
	debuggee->interrupted = 0;
	debuggee->breakpoints = linkedlist_new();
	debuggee->watchpoints = linkedlist_new();
	debuggee->threads = linkedlist_new();

	debuggee->num_breakpoints = 0;
	debuggee->num_watchpoints = 0;

	// TODO get rid of this BS
	// this thread will manage the job of re-enabling breakpoints after they're hit
	pthread_t bp_manager;
	pthread_create(&bp_manager, NULL, bp_manager_thread, NULL);
}

const char *get_exception_code(exception_type_t exception){
	switch(exception){
	case EXC_BAD_ACCESS:
		return "EXC_BAD_ACCESS";
	case EXC_BAD_INSTRUCTION:
		return "EXC_BAD_INSTRUCTION";
	case EXC_ARITHMETIC:
		return "EXC_ARITHMETIC";
	case EXC_EMULATION:
		return "EXC_EMULATION";
	case EXC_SOFTWARE:
		return "EXC_SOFTWARE";
	case EXC_BREAKPOINT:
		return "EXC_BREAKPOINT";
	case EXC_SYSCALL:
		return "EXC_SYSCALL";
	case EXC_MACH_SYSCALL:
		return "EXC_MACH_SYSCALL";
	case EXC_RPC_ALERT:
		return "EXC_RPC_ALERT";
	case EXC_CRASH:
		return "EXC_CRASH";
	case EXC_RESOURCE:
		return "EXC_RESOURCE";
	case EXC_GUARD:
		return "EXC_GUARD";
	case EXC_CORPSE_NOTIFY:
		return "EXC_CORPSE_NOTIFY";
	default:
		return "<Unknown Exception>";
	}
}

// Exceptions are caught herei
// Breakpoints are handled here
kern_return_t catch_mach_exception_raise(
		mach_port_t exception_port,
		mach_port_t thread,
		mach_port_t task,
		exception_type_t exception,
		exception_data_t code,
		mach_msg_type_number_t code_count){
	
	// hardware watchpoints are out of the question
	// software watchpoints would halt the debuggee
	// the only option left would be to cause an exception and assume it was caused by a watchpoint we set
	if(/*!debuggee_was_interrupted && */exception == EXC_BAD_ACCESS && debuggee->num_watchpoints > 0){	
		task_suspend(debuggee->task);
		
		arm_thread_state64_t thread_state;
		mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
		thread_get_state(thread, ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);
		
		// record the PC of this exception and assume it came from a watchpoint
		debuggee->last_wp_PC = thread_state.__pc;

		// disable every watchpoint (set location protection back to VM_PROT_READ | VM_PROT_WRITE)
		// so this instruction can execute as normal
		watchpoint_disable_all();

		//printf("Temp breakpointing at thread_state.__pc + 0x4: %#llx\n", debuggee->last_wp_PC + 0x4);

		// we have no idea which watchpoint caused this exception
		// but if we set a temporary breakpoint at the next instruction, we can compare
		// watchpoint data from this exception with the one that's about to happen to figure that out
		// once this breakpoint hits, the value of whatever watchpoint caused this EXC_BAD_ACCESS
		// will have been updated
		breakpoint_at_address(debuggee->last_wp_PC + 0x4, BP_TEMP);

		//printf("Temp breakpointing at thread_state.__lr in case this function is only one instr: %#llx\n", thread_state.__lr);
		
		// temp breakpoint at LR also in case the function we're in is currently only one instruction
		//breakpoint_at_address(thread_state.__lr, BP_TEMP);

		task_resume(debuggee->task);

		return KERN_SUCCESS;
	}
	
	int debuggee_was_interrupted = debuggee->interrupted ? 1 : 0;

	if(!debuggee->interrupted){
		debuggee->suspend();
		debuggee->interrupted = 1;
	}
	
	if(!debuggee_was_interrupted && debuggee->num_breakpoints > 0){
		struct machthread *curfocused = machthread_getfocused();
		unsigned long long tid = get_tid_from_thread_port(thread);
		
		// give focus to the thread that caused this exception
		if(!curfocused || curfocused->port != thread){
			printf("[Switching to thread %#llx]\n", tid);
			machthread_setfocused(thread);
		}

		// get PC to check if we're at a breakpointed address
		arm_thread_state64_t thread_state;
		mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
		kern_return_t err = thread_get_state(thread, ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);

		char *tname = get_thread_name_from_thread_port(thread);
		
		if(debuggee->breakpoints->front){
			struct node_t *current = debuggee->breakpoints->front;

			while(current){
				unsigned long long location = ((struct breakpoint *)current->data)->location;

				struct breakpoint *hit = (struct breakpoint *)current->data;

				if(!hit->temporary && location == thread_state.__pc){
					debuggee->PC = thread_state.__pc;

					breakpoint_hit(hit);
					
					// disable this so we can continue execution
					breakpoint_disable(hit->id);	

					debuggee->last_bkpt_PC = thread_state.__pc;
					debuggee->last_bkpt_ID = hit->id;

					printf("\n * Thread %#llx, '%s': breakpoint %d at %#llx hit %d time(s). %#llx in debuggee.\n", tid, tname, hit->id, hit->location, hit->hit_count, thread_state.__pc);
					
					memutils_disassemble_at_location(location, 0x4, DISAS_DONT_SHOW_ARROW_AT_LOCATION_PARAMETER);
					
					rl_on_new_line();
					rl_forced_update_display();
					
					free(tname);
					
					return KERN_SUCCESS;
				}
				else if(hit->temporary){
					breakpoint_hit(hit);

					if(thread_state.__pc - 0x4 == debuggee->last_wp_PC){
						struct node_t *current_wp_node = debuggee->watchpoints->front;

						while(current_wp_node){
							struct watchpoint *current_watchpoint = (struct watchpoint *)current_wp_node->data;
							int sz = current_watchpoint->data_len;

							void *prev_data = malloc(sz);
							memcpy(prev_data, current_watchpoint->data, sz);
							
							memutils_read_memory_at_location((void *)current_watchpoint->location, current_watchpoint->data, current_watchpoint->data_len);

							if(memcmp(prev_data, current_watchpoint->data, sz) != 0){
								watchpoint_hit(current_watchpoint);

								printf("\nWatchpoint %d hit:\n\n", current_watchpoint->id);
								
								// figure out data type being watched
								// 1 = char
								// 2 = short
								// 4 = signed/unsigned int or float
								// 8 = unsigned long (long) or  double
								unsigned int sz = current_watchpoint->data_len;
								
								long long old_val = memutils_buffer_to_number(prev_data, sz);
								long long new_val = memutils_buffer_to_number(current_watchpoint->data, sz);

								if(sz == sizeof(char))
									printf("Old value: %c\nNew value: %c\n\n", (char)old_val, (char)new_val);
								else if(sz == sizeof(short) || sz == sizeof(int)){
									old_val = (int)CFSwapInt32(old_val);
									new_val = (int)CFSwapInt32(new_val);

									printf("Old value: %s%#x\nNew value: %s%#x\n\n", (int)old_val < 0 ? "-" : "", (int)old_val < 0 ? (int)-old_val : (int)old_val, (int)new_val < 0 ? "-" : "", (int)new_val < 0 ? (int)-new_val : (int)new_val);
								}
								else{
									old_val = (long)CFSwapInt64(old_val);
									new_val = (long)CFSwapInt64(new_val);
									
									printf("Old value: %s%#lx\nNew value: %s%#lx\n\n", (long)old_val < 0 ? "-" : "", (long)old_val < 0 ? (long)-old_val : (long)old_val, (long)new_val < 0 ? "-" : "", (long)new_val < 0 ? (long)-new_val : (long)new_val);
								}

								// show the arrow at the location the exception from enabling a watchpoint occured for a better visual
								memutils_disassemble_at_location(debuggee->last_wp_PC, 0x4, DISAS_SHOW_ARROW_AT_LOCATION_PARAMETER);

								rl_on_new_line();
								rl_forced_update_display();

								watchpoint_enable_all();
								
								free(prev_data);

								return KERN_SUCCESS;
							}
							current_wp_node = current_wp_node->next;
							
							free(prev_data);
						}
					}

					watchpoint_enable_all();

					task_resume(debuggee->task);
					debuggee->interrupted = 0;

					return KERN_SUCCESS;
				}

				current = current->next;
			}
		}
	
		printf("\n * Thread %#llx, '%s' received signal %d, %s. %#llx in debuggee.\n", tid, tname, exception, get_exception_code(exception), thread_state.__pc);
		rl_on_new_line();
		rl_forced_update_display();
		
		free(tname);
	}
	
	return KERN_SUCCESS;
}
