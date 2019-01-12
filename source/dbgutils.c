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
	memset(matchstr, '\0', 512);
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

void setup_initial_debuggee(void){
	debuggee = malloc(sizeof(struct debuggee));

	// if we aren't attached to anything, debuggee's pid is -1
	debuggee->pid = -1;
	debuggee->interrupted = 0;
	debuggee->breakpoints = linkedlist_new();
	debuggee->watchpoints = linkedlist_new();
	debuggee->threads = linkedlist_new();

	debuggee->num_breakpoints = 0;
	debuggee->num_watchpoints = 0;

	debuggee->last_hit_bkpt_ID = 0;
	debuggee->last_hit_bkpt_hw = 0;

	size_t len = sizeof(int);

	sysctlbyname("hw.optional.breakpoint", &debuggee->num_hw_bps, &len, NULL, 0);
	
	len = sizeof(int);

	sysctlbyname("hw.optional.watchpoint", &debuggee->num_hw_wps, &len, NULL, 0);
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

kern_return_t catch_mach_exception_raise(
		mach_port_t exception_port,
		mach_port_t thread,
		mach_port_t task,
		exception_type_t exception,
		exception_data_t code,
		mach_msg_type_number_t code_count){
	debuggee->suspend();
	debuggee->interrupted = 1;
	
	struct machthread *curfocused = machthread_getfocused();
	
	unsigned long long tid = get_tid_from_thread_port(thread);
	
	// give focus to the thread that caused this exception
	if(!curfocused || curfocused->port != thread){
		printf("[Switching to thread %#llx]\n", tid);
		machthread_setfocused(thread);
	}
	
	debuggee->get_thread_state();

	if(exception != EXC_BREAKPOINT)
		return KERN_SUCCESS;
	
	/* exception_type_t is typedef'ed as int *, which is
	 * not enough space to hold the data break address.*/
	long elem0 = ((long *)code)[0];
	long break_loc = ((long *)code)[1];
	
	if(elem0 == EXC_ARM_BREAKPOINT){
		if(break_loc == 0){
			if(!debuggee->last_hit_bkpt_hw)
				breakpoint_enable(debuggee->last_hit_bkpt_ID);	
			
			debuggee->resume();
			debuggee->interrupted = 0;
			
			return KERN_SUCCESS;
		}

		struct breakpoint *hit = find_bp_with_address(break_loc);
		
		if(!hit){
			printf("Could not find hit breakpoint\n");			
			return KERN_SUCCESS;
		}
		
		breakpoint_hit(hit);

		/* Record the ID and type of this breakpoint so we can
		 * enable it later if it's a software breakpoint.
		 */
		debuggee->last_hit_bkpt_ID = hit->id;
		debuggee->last_hit_bkpt_hw = hit->hw;
		
		char *tname = get_thread_name_from_thread_port(thread);

		printf("\n * Thread %#llx: '%s': breakpoint %d at %#lx hit %d time(s). %#llx in debuggee.\n", tid, tname, hit->id, hit->location, hit->hit_count, debuggee->thread_state.__pc);
		
		free(tname);

		if(!hit->hw)
			breakpoint_disable(hit->id);

		memutils_disassemble_at_location(hit->location, 0x4, DISAS_DONT_SHOW_ARROW_AT_LOCATION_PARAMETER);
		
		/* Enable hardware single stepping so we can get past this instruction. */
		debuggee->get_debug_state();
		debuggee->debug_state.__mdscr_el1 |= 1;
		debuggee->set_debug_state();

		rl_on_new_line();
		rl_forced_update_display();

		return KERN_SUCCESS;
	}
	else if(elem0 == EXC_ARM_DA_DEBUG){
		printf("Hardware watchpoint hit at %#lx, read/write occured at %#llx\n", break_loc, debuggee->thread_state.__pc);
	}
	else
		printf("some other thing?\n");

	return KERN_SUCCESS;
}
