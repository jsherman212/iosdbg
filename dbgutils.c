#include "dbgutils.h"

// pidof implementation
// Get the pid of a program based on the program name provided
// Return: pid on success, -1 on error
pid_t pid_of_program(char *progname){
	FILE *mypidof = fopen("temppidof", "w");

	if(mypidof){
		// dump a bash script to get the pid of what we want to attach to in the file
		// given a name of a binary, print the PID(s) in a string separated by commas
		fprintf(mypidof, "#!/bin/sh\nps axc | awk \"{if (\\$5==\\\"%s\\\") print \\$1\\\",\\\"}\"|tr '\n' ' '", progname);
		fflush(mypidof);
		fclose(mypidof);
		
		pid_t chmod_pid;
		char *chmod_argv[] = {"chmod", "+x", "temppidof", NULL};

		int chmod_status = posix_spawnp(&chmod_pid, "chmod", NULL, NULL, (char * const *)chmod_argv, NULL);

		if(chmod_status){
			printf("Couldn't spawn chmod?\n");
			return -1;
		}

		waitpid(chmod_pid, &chmod_status, 0);

		FILE *pidofreader = popen("./temppidof 2>&1", "r");

		if(pidofreader){
			char program_pid[256];
			fgets(program_pid, sizeof(program_pid), pidofreader);

			pclose(pidofreader);

			// we don't need this anymore
			remove("./temppidof");

			char *finalpid = malloc(64);

			// count the number of commas
			// if there's more than one comma, we have two instances of the same process
			int num_commas = 0;
			int len = strlen(program_pid);

			for(int i=0; i<len; i++){
				if(program_pid[i] == ',')
					num_commas++;
				else if(num_commas == 0)
					// at the same time we can start to construct the PID string without the comma
					sprintf(finalpid, "%s%c", finalpid, program_pid[i]);
			}

			pid_t pid;

			if(num_commas == 1){
				pid = atoi(finalpid);
				free(finalpid);
				return pid;
			}
			else if(num_commas > 1){
				printf("There is more than one instance of %s. Aborting. PIDs: %s\n", progname, program_pid);
				free(finalpid);
				return -1;
			}
			else if(num_commas == 0){
				printf("%s not found\n", progname);
				free(finalpid);
				return -1;
			}
		}
		else
			return -1;
	}
	else
		return -1;

	return -1;
}

void *death_server(void *arg){
	while(1){
		if(debuggee->pid != -1){
			mach_port_type_t type;
			kern_return_t err = mach_port_type(mach_task_self(), debuggee->task, &type);

			if(err)
				printf("death_server: mach_port_type failed: %s\n", mach_error_string(err));

			if(type == MACH_PORT_TYPE_DEAD_NAME){
				printf("\n %d dead? Detaching...\n", debuggee->pid);
				cmdfunc_detach(NULL, 1);
			}
		}

		sleep(1);
	}
}

int suspend_threads(){
	kern_return_t err = task_threads(debuggee->task, &debuggee->threads, &debuggee->thread_count);

	if(err){
		printf("suspend_threads: couldn't get the list of threads for %d: %s\n", debuggee->pid, mach_error_string(err));
		return -1;
	}

	for(int i=0; i<debuggee->thread_count; i++)
		thread_suspend(debuggee->threads[i]);

	return 0;
}

void resume_threads(){
	for(int i=0; i<debuggee->thread_count; i++)
		thread_resume(debuggee->threads[i]);
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

void *exception_server(void *arg){
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
void setup_exception_handling(){
	// make an exception port for the debuggee
	kern_return_t err = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &debuggee->exception_port);

	if(err){
		printf("setup_exception_handling: mach_port_allocate failed: %s\n", mach_error_string(err));
		return;
	}

	// be able to send messages on that exception port
	err = mach_port_insert_right(mach_task_self(), debuggee->exception_port, debuggee->exception_port, MACH_MSG_TYPE_MAKE_SEND);

	if(err){
		printf("setup_exception_handling: mach_port_insert_right failed: %s\n", mach_error_string(err));
		return;
	}

	mach_port_t port_set;

	err = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &port_set);

	if(err){
		printf("setup_exception_handling: mach_port_allocate failed: %s\n", mach_error_string(err));
		return;
	}

	err = mach_port_move_member(mach_task_self(), debuggee->exception_port, port_set);

	if(err){
		printf("setup_exception_handling: mach_port_move_member failed: %s\n", mach_error_string(err));
		return;
	}

	// allocate port to notify us of termination
	err = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &debuggee->death_port);

	if(err){
		printf("setup_exception_handling: mach_port_allocate failed allocating termination notification port: %s\n", mach_error_string(err));
		return;
	}

	err = mach_port_move_member(mach_task_self(), debuggee->death_port, port_set);

	if(err){
		printf("setup_exception_handling: mach_port_move_member failed: %s\n", mach_error_string(err));
		return;
	}
	
	mach_port_t p;
	err = mach_port_request_notification(mach_task_self(), debuggee->task, MACH_NOTIFY_DEAD_NAME, 0, debuggee->death_port, MACH_MSG_TYPE_MAKE_SEND_ONCE, &p);

	if(err){
		printf("setup_exception_handling: mach_port_request_notification failed: %s\n", mach_error_string(err));
		return;
	}

	// save the old exception ports
	err = task_get_exception_ports(debuggee->task, EXC_MASK_ALL, debuggee->original_exception_ports.masks, &debuggee->original_exception_ports.count, debuggee->original_exception_ports.ports, debuggee->original_exception_ports.behaviors, debuggee->original_exception_ports.flavors);

	if(err){
		printf("setup_exception_handling: task_get_exception_ports failed: %s\n", mach_error_string(err));
		return;
	}

	// add the ability to get exceptions on the debuggee exception port
	// OR EXCEPTION_DEFAULT with MACH_EXCEPTION_CODES so 64-bit safe exception messages will be provided 
	err = task_set_exception_ports(debuggee->task, EXC_MASK_ALL, debuggee->exception_port, EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, THREAD_STATE_NONE);

	if(err){
		printf("setup_exception_handling: task_set_exception_ports failed: %s\n", mach_error_string(err));
		return;
	}

	// start the exception server
	pthread_t exception_server_thread;
	pthread_create(&exception_server_thread, NULL, exception_server, NULL);
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
		printf("setup_initial_debuggee: couldn't allocate memory for breakpoint linked list.\n");
		exit(1);
	}
}

// SIGINT handler
void interrupt(int x1){
	if(debuggee->pid == -1)
		return;

	if(debuggee->interrupted)
		return;

	// TODO: Provide a nice way of showing the client they interrupted the debuggee that doesn't screw up the limenoise prompt
	kern_return_t err = task_suspend(debuggee->task);

	if(err){
		printf("Cannot interrupt: %s\n", mach_error_string(err));
		debuggee->interrupted = 0;

		return;
	}

	int result = suspend_threads();

	if(result){
		printf("Couldn't suspend threads for %d during interrupt\n", debuggee->pid);
		debuggee->interrupted = 0;

		return;
	}

	debuggee->interrupted = 1;

	printf("\n%d suspended\n", debuggee->pid);
	
	// fake iosdbg prompt
	printf("(iosdbg) ");
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

	// interrupt debuggee
	interrupt(0);

	// get PC to check if we're at a breakpointed address
	arm_thread_state64_t thread_state;
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;

	kern_return_t err = thread_get_state(debuggee->threads[0], ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);

	// what to print out to the client when an exception is hit
	if(debuggee->breakpoints->front){
		struct node_t *current = debuggee->breakpoints->front;

		while(current){
			unsigned long long location = ((struct breakpoint *)current->data)->location;

			if(location == thread_state.__pc){
				struct breakpoint *hit = (struct breakpoint *)current->data;

				breakpoint_hit(hit);

				printf("\n * Thread %#x: breakpoint %d at %#llx hit %d time(s). %#llx in debuggee.\n", thread, hit->id, hit->location, hit->hit_count, thread_state.__pc);

				return KERN_SUCCESS;
			}

			current = current->next;
		}
	}

	printf("\n * Thread %#x received signal %d, %s. %#llx in debuggee.\n", thread, exception, get_exception_code(exception), thread_state.__pc);

	return KERN_SUCCESS;
}
