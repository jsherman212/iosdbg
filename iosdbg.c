#include "iosdbg.h"

int main(int argc, char **argv, const char **envp){
	setup_initial_debuggee();
	rl_catch_signals = 0;

	signal(SIGINT, interrupt);

	char *line = NULL;

	// Until I can figure out how to correctly implement mach notifications, this will work fine.
	// Spawn a thread to check for the termination of the debuggee.
	pthread_t dt;
	pthread_create(&dt, NULL, death_server, NULL);

	while((line = readline("(iosdbg) ")) != NULL){
		// add the command to history
		if(strlen(line) > 0)
			add_history(line);

		// update the debuggee's list of threads
		if(debuggee->pid != -1){
			kern_return_t err = task_threads(debuggee->task, &debuggee->threads, &debuggee->thread_count);

			if(err){
				printf("we couldn't update the list of threads for %d: %s\n", debuggee->pid, mach_error_string(err));
				continue;
			}
		}

		execute_command(line);

		free(line);
	}

	return 0;
}