/*
An iOS debugger I'm making for fun and to learn.
Don't expect this to ever be as sophisticated as GDB or LLDB.
*/

// TODO: add a threads field in the debuggee struct and update it every time the client executes a new command

#include "iosdbg.h"

int resume_threads();

// print every command with a description
void help(){
	printf("attach 						attach to a program with its PID\n");
	printf("aslr						show the ASLR slide (TODO)\n");
	printf("clear						clear screen\n");
	printf("continue					resume debuggee execution\n");
	printf("detach						detach from the current program\n");
	printf("help						show this message\n");
	printf("quit						quit iosdbg\n");
	printf("regs <gen, float>			show registers (float: TODO)\n");

}

void setup_initial_debuggee(){
	debuggee = malloc(sizeof(struct debuggee));

	// if we aren't attached to anything, debuggee's pid is -1
	debuggee->pid = -1;
	debuggee->interrupted = 0;
}

// resume the debuggee's execution
// Returns: 0 on success, -1 on fail
int resume(){
	if(!debuggee->interrupted)
		return -1;

	kern_return_t err = task_resume(debuggee->task);

	if(err){
		warn("resume: couldn't continue: %s\n", mach_error_string(err));
		return -1;
	}

	int result = resume_threads();

	if(result != 0){
		warn("resume: couldn't resume threads\n");
		return -1;
	}

	debuggee->interrupted = 0;

	return 0;
}

// try and detach from the debuggee
// Returns: 0 on success, -1 on fail
int detach(){
	if(debuggee->interrupted){
		int result = resume();

		if(result != 0){
			warn("detach: couldn't resume execution before we detach?\n");
			return -1;
		}
	}

	debuggee->interrupted = 0;

	printf("detached from %d\n", debuggee->pid);

	debuggee->pid = -1;

	return 0;
}

// try and resume every thread in the debuggee
// Return: 0 on success, -1 on fail
int resume_threads(){
	thread_act_port_array_t threads;
	mach_msg_type_number_t thread_count;

	kern_return_t err = task_threads(debuggee->task, &threads, &thread_count);

	if(err){
		warn("resume_threads: couldn't get the list of threads for %d: %s\n", debuggee->pid, mach_error_string(err));
		return -1;
	}

	for(int i=0; i<thread_count; i++)
		thread_resume(threads[i]);

	return 0;
}

// try and suspend every thread in the debuggee
// Return: 0 on success, -1 on fail
int suspend_threads(){
	thread_act_port_array_t threads;
	mach_msg_type_number_t thread_count;

	kern_return_t err = task_threads(debuggee->task, &threads, &thread_count);

	if(err){
		warn("suspend_threads: couldn't get the list of threads for %d: %s\n", debuggee->pid, mach_error_string(err));
		return -1;
	}

	for(int i=0; i<thread_count; i++)
		thread_suspend(threads[i]);

	return 0;
}

// try and attach to the debuggee
// Returns: 0 on success, -1 on fail
int attach(pid_t pid){
	kern_return_t err = task_for_pid(mach_task_self(), pid, &debuggee->task);

	if(err){
		warn("attach: couldn't get task port for pid %d: %s\n", pid, mach_error_string(err));
		return -1;
	}

	err = task_suspend(debuggee->task);

	if(err){
		warn("attach: task_suspend call failed: %s\n", mach_error_string(err));
		return -1;
	}

	int result = suspend_threads();

	if(result != 0){
		warn("attach: couldn't suspend threads for %d while attaching, detaching...\n", debuggee->pid);

		detach();

		return -1;
	}

	debuggee->pid = pid;
	debuggee->interrupted = 1;

	printf("attached to %d\n", debuggee->pid);

	return 0;
}

// SIGINT handler
void interrupt(int x1){
	if(debuggee->interrupted)
		return;

	// TODO: Provide a nice way of showing the client they interrupted the debuggee that doesn't screw up the limenoise prompt

	kern_return_t err = task_suspend(debuggee->task);

	if(err){
		warn("cannot interrupt: %s\n", mach_error_string(err));
		debuggee->interrupted = 0;

		return;
	}

	int result = suspend_threads();

	if(result != 0){
		warn("couldn't suspend threads for %d during interrupt\n", debuggee->pid);
		debuggee->interrupted = 0;

		return;
	}

	debuggee->interrupted = 1;
}

// show general registers of thread 0
// Return: 0 on success, -1 on fail
int show_general_registers(){
	// check if debuggee is interrupted?

	thread_act_port_array_t threads;
	mach_msg_type_number_t thread_count;

	kern_return_t err = task_threads(debuggee->task, &threads, &thread_count);

	if(err){
		warn("show_general_registers: couldn't get the list of threads for %d: %s\n", debuggee->pid, mach_error_string(err));
		return -1;
	}

	arm_thread_state64_t thread_state;
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;

	err = thread_get_state(threads[0], ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);

	if(err){
		warn("show_general_registers: thread_get_state failed: %s\n", mach_error_string(err));
		return -1;
	}

	// print general purpose registers
	for(int i=0; i<29; i++)
		printf("X%d 					0x%llx\n", i, thread_state.__x[i]);

	// print the other ones
	printf("FP 					0x%llx\n", thread_state.__fp);
	printf("LR 					0x%llx\n", thread_state.__lr);
	printf("SP 					0x%llx\n", thread_state.__sp);
	printf("PC 					0x%llx\n", thread_state.__pc);
	printf("CPSR 					0x%x\n", thread_state.__cpsr);

	return 0;
}

int main(int argc, char **argv, const char **envp){
	setup_initial_debuggee();

	signal(SIGINT, interrupt);

	char *line = NULL;

	while((line = linenoise("(iosdbg) ")) != NULL){
		// add the command to history
		linenoiseHistoryAdd(line);

		if(strcmp(line, "quit") == 0){
			if(debuggee->pid != -1)
				detach();

			free(debuggee);
			exit(0);
		}
		else if(strcmp(line, "continue") == 0)
			resume();
		else if(strcmp(line, "clear") == 0)
			linenoiseClearScreen();
		else if(strcmp(line, "help") == 0)
			help();
		else if(strcmp(line, "detach") == 0)
			detach();
		else if(strstr(line, "attach")){
			if(debuggee->pid != -1){
				warn("Already attached to %d\n", debuggee->pid);
				continue;
			}

			char *tok = strtok(line, " ");
			pid_t potential_target_pid;
			
			while(tok){
				potential_target_pid = atoi(tok);
				tok = strtok(NULL, " ");
			}

			if(potential_target_pid == 0)
				warn("we cannot attach to the kernel!\n");
			else if(potential_target_pid == getpid())
				warn("do not try and debug me!\n");
			else{
				int result = attach(potential_target_pid);

				if(result != 0)
					warn("Couldn't attach to %d\n", potential_target_pid);
			}
		}
		else if(strstr(line, "regs")){
			char *reg_type = malloc(1024);
			char *tok = strtok(line, " ");

			while(tok){
				strcpy(reg_type, tok);
				tok = strtok(NULL, " ");
			}

			if(strcmp(reg_type, "gen") != 0 && strcmp(reg_type, "float") != 0){
				warn("Bad argument. try `gen` or `float`\n");
				free(reg_type);
				continue;
			}

			if(strcmp(reg_type, "float") == 0){
				warn("TODO\n");
				free(reg_type);
				continue;
			}
			else
				show_general_registers();

			free(reg_type);
		}
		else
			printf("Invalid command.\n");

		free(line);
	}

	return 0;
}