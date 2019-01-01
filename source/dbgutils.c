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

void setup_initial_debuggee(){
	debuggee = NULL;

	debuggee = malloc(sizeof(struct debuggee));

	if(!debuggee){
		printf("setup_initial_debuggee: malloc returned NULL, please restart iosdbg.\n");
		exit(1);
	}

	// if we aren't attached to anything, debuggee's pid is -1
	debuggee->pid = -1;
	debuggee->interrupted = 0;
	debuggee->breakpoints = linkedlist_new();

	if(!debuggee->breakpoints){
		printf("Couldn't allocate memory for breakpoint linked list.\n");
		exit(1);
	}

	debuggee->threads = linkedlist_new();

	if(!debuggee->threads){
		printf("Couldn't allocate memory for thread linked list.\n");
		exit(1);
	}
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

// Exceptions are caught here
// Breakpoints are handled here
kern_return_t catch_mach_exception_raise(
		mach_port_t exception_port,
		mach_port_t thread,
		mach_port_t task,
		exception_type_t exception,
		exception_data_t code,
		mach_msg_type_number_t code_count){
	// The kernel calls this function faster than we can flip debuggee->interrupted
	// That results in breakpoints hitting 1 - 20 additional times
	int debuggee_was_interrupted = debuggee->interrupted ? 1 : 0;

	if(!debuggee->interrupted){
		debuggee->suspend();
		debuggee->interrupted = 1;
	}

	if(!debuggee_was_interrupted){
		// Temporarily disable all breakpoints so the machine can execute the next instruction
		// They are re-enabled when the user continues
		// Safer than manually incrementing PC
		breakpoint_disable_all();
		
		// get PC to check if we're at a breakpointed address
		arm_thread_state64_t thread_state;
		mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
		
		struct machthread *curfocused = machthread_getfocused();
		
		unsigned long long tid = get_tid_from_thread_port(thread);
		
		// give focus to the thread that caused this exception
		if(!curfocused || curfocused->port != thread){
			printf("[Switching to thread %#llx]\n", tid);
			machthread_setfocused(thread);
		}

		kern_return_t err = thread_get_state(thread, ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);

		char *tname = get_thread_name_from_thread_port(thread);
			
		if(debuggee->breakpoints->front && !debuggee_was_interrupted){
			struct node_t *current = debuggee->breakpoints->front;

			while(current){
				unsigned long long location = ((struct breakpoint *)current->data)->location;

				if(location == thread_state.__pc){
					struct breakpoint *hit = (struct breakpoint *)current->data;

					breakpoint_hit(hit);

					printf("\n * Thread %#llx, '%s': breakpoint %d at %#llx hit %d time(s). %#llx in debuggee.\n", tid, tname, hit->id, hit->location, hit->hit_count, thread_state.__pc);
					
					rl_on_new_line();
					
					return KERN_SUCCESS;
				}

				current = current->next;
			}
		}
		
		printf("\n * Thread %#llx, '%s' received signal %d, %s. %#llx in debuggee.\n", tid, tname, exception, get_exception_code(exception), thread_state.__pc);
		rl_on_new_line();

		free(tname);
	}

	return KERN_SUCCESS;
}
