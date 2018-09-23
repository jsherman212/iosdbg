/*
An iOS debugger I'm making for fun and to learn.
Don't expect this to ever be as sophisticated as GDB or LLDB.
*/

#include "iosdbg.h"

// print every command with a description
void help(){
	printf("attach 										attach to a program with its PID or executable name\n");
	printf("aslr										show the ASLR slide\n");
	printf("break <addr>								set a breakpoint at addr\n");
	printf("clear										clear screen\n");
	printf("continue									resume debuggee execution\n");
	printf("delete <breakpoint id>						delete breakpoint with <breakpoint id>\n");
	printf("detach										detach from the current program\n");
	printf("help										show this message\n");
	printf("kill										kill the debuggee\n");
	printf("quit										quit iosdbg\n");
	printf("regs <gen, float> <optional register>		show registers or specific register (float: TODO)\n");
	printf("set <gen, float> reg <register>	<value>		set value for given register (TODO)\n");
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

extern boolean_t mach_exc_server(mach_msg_header_t *InHeadP, mach_msg_header_t *OutHeadP);

/* Both unused. */
kern_return_t catch_mach_exception_raise_state(mach_port_t exception_port, exception_type_t exception, exception_data_t code, mach_msg_type_number_t code_count, int *flavor, thread_state_t in_state, mach_msg_type_number_t in_state_count, thread_state_t out_state, mach_msg_type_number_t *out_state_count){return KERN_FAILURE;}
kern_return_t catch_mach_exception_raise_state_identity(mach_port_t exception_port, mach_port_t thread, mach_port_t task, exception_type_t exception, exception_data_t code, mach_msg_type_number_t code_count, int *flavor, thread_state_t in_state, mach_msg_type_number_t in_state_count, thread_state_t out_state, mach_msg_type_number_t *out_state_count){return KERN_FAILURE;}

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
	char *exception_string = malloc(1024);

	if(debuggee->breakpoints->front){
		struct node_t *current = debuggee->breakpoints->front;

		while(current){
			unsigned long long location = ((struct breakpoint *)current->data)->location;

			if(location == thread_state.__pc){
				struct breakpoint *hit = (struct breakpoint *)current->data;

				breakpoint_hit(hit);

				sprintf(exception_string, "\r\n * Thread %x: breakpoint %d at 0x%llx hit %d time(s). 0x%llx in debuggee.\r\n", thread, hit->id, hit->location, hit->hit_count, thread_state.__pc);
				printf("%s", exception_string);
				printf("\r\r(iosdbg) ");

				free(exception_string);

				return KERN_SUCCESS;
			}

			current = current->next;
		}
	}

	sprintf(exception_string, "\r\n * Thread %x received signal %d, %s. 0x%llx in debuggee.\r\n", thread, exception, get_exception_code(exception), thread_state.__pc);
	printf("%s", exception_string);
	printf("\r\r(iosdbg) ");

	free(exception_string);

	return KERN_SUCCESS;
}

void *exception_server(void *arg){
	while(1){
		// shut down this thread once we detach
		if(debuggee->pid == -1)
			pthread_exit(NULL);

		kern_return_t err = mach_msg_server_once(mach_exc_server, 4096, debuggee->exception_port, 0);

		if(err)
			printf("\r\nmach_msg_server_once: error: %s\r\n", mach_error_string(err));
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

// SIGINT handler
void interrupt(int x1){
	if(debuggee->interrupted)
		return;

	// TODO: Provide a nice way of showing the client they interrupted the debuggee that doesn't screw up the limenoise prompt

	kern_return_t err = task_suspend(debuggee->task);

	if(err){
		printf("cannot interrupt: %s\n", mach_error_string(err));
		debuggee->interrupted = 0;

		return;
	}

	int result = suspend_threads();

	if(result != 0){
		printf("couldn't suspend threads for %d during interrupt\n", debuggee->pid);
		debuggee->interrupted = 0;

		return;
	}

	debuggee->interrupted = 1;
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

// try and attach to the debuggee, and get its ASLR slide
// Returns: 0 on success, -1 on fail
int attach(pid_t pid){
	kern_return_t err = task_for_pid(mach_task_self(), pid, &debuggee->task);

	if(err){
		printf("attach: couldn't get task port for pid %d: %s\n", pid, mach_error_string(err));
		return -1;
	}

	err = task_suspend(debuggee->task);

	if(err){
		printf("attach: task_suspend call failed: %s\n", mach_error_string(err));
		return -1;
	}

	int result = suspend_threads();

	if(result != 0){
		printf("attach: couldn't suspend threads for %d while attaching, detaching...\n", debuggee->pid);

		detach();

		return -1;
	}

	debuggee->pid = pid;
	debuggee->interrupted = 1;

	vm_region_basic_info_data_64_t info;
	vm_address_t address = 0;
	vm_size_t size;
	mach_port_t object_name;
	mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
	
	err = vm_region_64(debuggee->task, &address, &size, VM_REGION_BASIC_INFO, (vm_region_info_t)&info, &info_count, &object_name);

	if(err){
		printf("attach: vm_region_64: %s\n", mach_error_string(err));

		detach();

		return -1;
	}

	debuggee->aslr_slide = address - 0x100000000;

	printf("Attached to %d, ASLR slide is 0x%llx. Do not worry about adding ASLR to addresses, it is already accounted for.\n", debuggee->pid, debuggee->aslr_slide);

	debuggee->breakpoints = linkedlist_new();

	return 0;
}

// resume the debuggee's execution
// Returns: 0 on success, -1 on fail
int resume(){
	if(!debuggee->interrupted)
		return -1;

	kern_return_t err = task_resume(debuggee->task);

	if(err){
		printf("resume: couldn't continue: %s\n", mach_error_string(err));
		return -1;
	}

	resume_threads();

	debuggee->interrupted = 0;

	printf("Continuing.\n");

	return 0;
}

// try and resume every thread in the debuggee
void resume_threads(){
	for(int i=0; i<debuggee->thread_count; i++)
		thread_resume(debuggee->threads[i]);
}

// try and suspend every thread in the debuggee
// Return: 0 on success, -1 on fail
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

// show general registers of thread 0
// Return: 0 on success, -1 on fail
int show_general_registers(int specific_register){
	if(specific_register < -1){
		printf("Bad register number %d\n", specific_register);
		return -1;
	}

	arm_thread_state64_t thread_state;
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;

	kern_return_t err = thread_get_state(debuggee->threads[0], ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);

	if(err){
		printf("show_general_registers: thread_get_state failed: %s\n", mach_error_string(err));
		return -1;
	}

	if(specific_register != -1){
		printf("X%d 				0x%llx\n", specific_register, thread_state.__x[specific_register]);
		return 0;
	}

	// print general purpose registers
	for(int i=0; i<29; i++)
		printf("X%d 				0x%llx\n", i, thread_state.__x[i]);

	// print the other ones
	printf("FP 				0x%llx\n", thread_state.__fp);
	printf("LR 				0x%llx\n", thread_state.__lr);
	printf("SP 				0x%llx\n", thread_state.__sp);
	printf("PC 				0x%llx\n", thread_state.__pc);
	printf("CPSR 				0x%x\n", thread_state.__cpsr);

	return 0;
}

// show a floating point register from thread 0
// Parameters: reg_type is the type of floating point register, reg_num is the register to show
// Return: 0 on success, -1 on fail
int show_neon_register(char reg_type, int reg_num){
	arm_neon_state64_t neon_state;
	mach_msg_type_number_t count = ARM_NEON_STATE64_COUNT;

	kern_return_t err = thread_get_state(debuggee->threads[0], ARM_NEON_STATE64, (thread_state_t)&neon_state, &count);

	if(err){
		printf("show_neon_registers: thread_get_state failed: %s\n", mach_error_string(err));
		return -1;
	}

	union intfloat {
		int i;
		float f;
	} IF;

	if(reg_type == 'v'){
		// TODO figure this out...
		// print each byte in this 128 bit integer (16)
		void *v = neon_state.__v[reg_num];

		printf("{ ");
		for(int i=0; i<16; i++)
			printf("0x%x ", *(unsigned char *)(v + i));

		printf("}\n");
	}
	if(reg_type == 'd'){
		// D registers, bottom 64 bits of each Q register
		IF.i = neon_state.__v[reg_num]/* & 0xFFFF*/;
		printf("D%d 				%f\n", reg_num, IF.f);
	}
	else if(reg_type == 's'){
		// S registers, bottom 32 bits of each Q register
		IF.i = neon_state.__v[reg_num] & 0xFFFFFFFF;
		printf("S%d 				%f\n", reg_num, IF.f);
	}
	else
		printf("Support for %c registers will be added soon\n", reg_type);

	return 0;
}

int set_breakpoint(unsigned long long location){
	return breakpoint_at_address(location);
}

int delete_breakpoint(int breakpoint_id){
	if(breakpoint_id == 0){
		printf("We need a breakpoint ID\n");
		return -1;
	}

	return breakpoint_delete(breakpoint_id);
}

// try and detach from the debuggee
// Returns: 0 on success, -1 on fail
int detach(){
	// delete all breakpoints on detach so the original instruction is written back to prevent a crash
	// TODO: instead of deleting them, maybe disable all of them and if we are attached to the same thing again re-enable them?
	breakpoint_delete_all();

	// restore original exception ports
	for(mach_msg_type_number_t i=0; i<debuggee->original_exception_ports.count; i++){
		kern_return_t err = task_set_exception_ports(debuggee->task, debuggee->original_exception_ports.masks[i], debuggee->original_exception_ports.ports[i], debuggee->original_exception_ports.behaviors[i], debuggee->original_exception_ports.flavors[i]);
		
		if(err)
			printf("detach: task_set_exception_ports: %s, %d\n", mach_error_string(err), i);
	}

	if(debuggee->interrupted){
		int result = resume();

		if(result != 0){
			printf("detach: couldn't resume execution before we detach?\n");
			return -1;
		}
	}

	debuggee->interrupted = 0;

	linkedlist_free(debuggee->breakpoints);
	debuggee->breakpoints = NULL;

	printf("Detached from %d\n", debuggee->pid);

	debuggee->pid = -1;

	return 0;
}

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

		system("chmod +x temppidof");

		FILE *pidofreader = popen("./temppidof 2>&1", "r");

		if(pidofreader){
			char program_pid[256];
			fgets(program_pid, sizeof(program_pid), pidofreader);

			pclose(pidofreader);

			// we don't need this anymore
			remove("./temppidof");

			char *finalpid = malloc(1024);

			// count the number of commas
			// if there's more than one comma, we have two instances of the same process
			int num_commas = 0;

			for(int i=0; i<strlen(program_pid); i++){
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

int main(int argc, char **argv, const char **envp){
	setup_initial_debuggee();

	signal(SIGINT, interrupt);

	char *line = NULL;

	while((line = linenoise("(iosdbg) ")) != NULL){
		// add the command to history
		linenoiseHistoryAdd(line);

		// update the debuggee's list of threads
		if(debuggee->pid != -1){
			kern_return_t err = task_threads(debuggee->task, &debuggee->threads, &debuggee->thread_count);

			if(err){
				printf("we couldn't update the list of threads for %d: %s\n", debuggee->pid, mach_error_string(err));
				continue;
			}
		}

		if(strcmp(line, "quit") == 0 || strcmp(line, "q") == 0){
			if(debuggee->pid != -1)
				detach();

			free(debuggee);
			exit(0);
		}
		else if(strcmp(line, "aslr") == 0 && debuggee->pid != -1)
			printf("Debuggee ASLR slide: 0x%llx\n", debuggee->aslr_slide);
		else if(strcmp(line, "continue") == 0 || strcmp(line, "c") == 0)
			resume();
		else if(strcmp(line, "clear") == 0)
			linenoiseClearScreen();
		else if(strcmp(line, "help") == 0)
			help();
		// TODO: THIS CRASHES SPRINGBOARD????
		else if(strcmp(line, "kill") == 0 && debuggee->pid != -1){
			detach();
			kill(debuggee->pid, SIGKILL);
		}
		else if(strcmp(line, "detach") == 0)
			detach();
		else if(strstr(line, "attach")){
			if(debuggee->pid != -1){
				printf("Already attached to %d\n", debuggee->pid);
				continue;
			}

			char *tok = strtok(line, " ");
			char *potential_target_pid_string = malloc(1024);

			while(tok){
				strcpy(potential_target_pid_string, tok);
				tok = strtok(NULL, " ");
			}

			// if it is 0, we got a binary name to attach to
			pid_t potential_target_pid = atoi(potential_target_pid_string);

			if(potential_target_pid == 0){
				pid_t prog_pid = pid_of_program(potential_target_pid_string);

				// pid_of_program failed
				if(prog_pid == -1)
					printf("Couldn't attach to %s\n", potential_target_pid_string);
				else{
					int result = attach(prog_pid);

					if(result != 0)
						printf("Couldn't attach to %d\n", potential_target_pid);
					else
						setup_exception_handling();
				}
			}
			else if(potential_target_pid == getpid())
				printf("Do not try and debug me!\n");
			else{
				int result = attach(potential_target_pid);

				if(result != 0)
					printf("Couldn't attach to %d\n", potential_target_pid);
				else
					setup_exception_handling();
			}
		}
		else if(strstr(line, "regs") && debuggee->pid != -1){
			char *reg_type = malloc(1024);
			char *tok = strtok(line, " ");

			tok = strtok(NULL, " ");
			strcpy(reg_type, tok);

			if(strcmp(reg_type, "float") == 0){
				char *specific_register = strtok(NULL, " ");

				if(specific_register){
					char reg_type = *specific_register;
					specific_register++;
					int reg_num = atoi(specific_register);

					if(reg_num < 0 || reg_num > 31){
						printf("Bad register number %d\n", reg_num);
						continue;
					}

					show_neon_register(tolower(reg_type), reg_num);
				}
				else
					printf("Need a register\n");
			}
			else if(strcmp(reg_type, "gen") == 0){
				// check to see if the client wants a specific register
				char *specific_register = strtok(NULL, " ");

				if(specific_register){
					// check if the client actually requested a general purpose register
					if(tolower(specific_register[0]) != 'x'){
						printf("That is not a general purpose register\n");
						continue;
					}

					// parse register string for register number
					// register number should be right after the "register letter" for lack of a better term
					specific_register++;

					int reg_num = atoi(specific_register);

					if(reg_num < 0 || reg_num > 34){
						printf("Bad register number %d\n", reg_num);
						continue;
					}

					show_general_registers(reg_num);
				}
				else
					// show every register
					show_general_registers(-1);
			}
			else{
				printf("Bad argument. try `gen` or `float`\n");
				free(reg_type);
				continue;
			}

			free(reg_type);
		}
		else if(strstr(line, "break") && debuggee->pid != -1){
			char *tok = strtok(line, " ");
			unsigned long long potential_breakpoint_location = 0x0;

			while(tok){
				potential_breakpoint_location = strtoul(tok, NULL, 16);
				tok = strtok(NULL, " ");
			}

			int result = set_breakpoint(potential_breakpoint_location);

			if(result != 0)
				printf("couldn't set breakpoint at 0x%llx\n", potential_breakpoint_location);
		}
		else if(strstr(line, "delete") && debuggee->pid != -1){
			char *tok = strtok(line, " ");
			int breakpoint_to_delete_id = -1;

			while(tok){
				breakpoint_to_delete_id = atoi(tok);
				tok = strtok(NULL, " ");
			}

			int result = delete_breakpoint(breakpoint_to_delete_id);

			if(result != 0)
				printf("Couldn't delete breakpoint %d\n", breakpoint_to_delete_id);
		}
		else
			printf("Invalid command.\n");

		free(line);
	}

	return 0;
}