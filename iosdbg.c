/*
An iOS debugger I'm making for fun and to learn.
Don't expect this to ever be as sophisticated as GDB or LLDB.
*/

#include "iosdbg.h"

// print every command with a description
void help(){
	printf("attach 						attach to a program with its PID\n");
	printf("aslr						show the ASLR slide (TODO)\n");
	printf("clear						clear screen\n");
	printf("continue					resume debuggee execution\n");
	printf("detach						detach from the current program\n");
	printf("help						show this message\n");
	printf("quit						quit iosdbg\n");
	printf("regs <gen, float>			show general purpose registers (TODO)\n");

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
	kern_return_t err = task_resume(debuggee->port);

	if(err){
		warn("Couldn't continue: %s\n", mach_error_string(err));
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
			warn("Couldn't resume the execution before we detach?\n");
			return -1;
		}
	}

	debuggee->interrupted = 0;

	printf("detached from %d\n", debuggee->pid);

	debuggee->pid = -1;

	return 0;
}

// try and attach to the debuggee
// Returns: 0 on success, -1 on fail
int attach(pid_t pid){
	kern_return_t err = task_for_pid(mach_task_self(), pid, &debuggee->port);

	if(err){
		warn("Couldn't get task port for pid %d: %s\n", pid, mach_error_string(err));
		return -1;
	}

	err = task_suspend(debuggee->port);

	if(err){
		warn("task_suspend call failed: %s\n", mach_error_string(err));
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

	kern_return_t err = task_suspend(debuggee->port);

	if(err){
		warn("cannot interrupt: %s\n", mach_error_string(err));
		debuggee->interrupted = 0;

		return;
	}

	debuggee->interrupted = 1;
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
		else
			printf("Invalid command.\n");

		free(line);
	}

	return 0;
}