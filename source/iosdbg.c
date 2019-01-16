#include "iosdbg.h"

// SIGINT handler
void interrupt(int show_prompt){
	if(debuggee->pid == -1)
		return;

	if(debuggee->interrupted)
		return;

	kern_return_t err = debuggee->suspend();

	if(err){
		printf("Cannot suspend: %s\n", mach_error_string(err));
		debuggee->interrupted = 0;

		return;
	}

	debuggee->interrupted = 1;

	printf("\n");

	debuggee->get_thread_state();

	memutils_disassemble_at_location(debuggee->thread_state.__pc, 0x4, DISAS_DONT_SHOW_ARROW_AT_LOCATION_PARAMETER);

	if(show_prompt)
		rl_printf(RL_REPROMPT, "%s stopped.\n", debuggee->debuggee_name);
}

void install_handlers(void){
	debuggee->find_slide = &find_slide;
	debuggee->restore_exception_ports = &restore_exception_ports;
	debuggee->resume = &resume;
	debuggee->setup_exception_handling = &setup_exception_handling;
	debuggee->deallocate_ports = &deallocate_ports;
	debuggee->suspend = &suspend;
	debuggee->update_threads = &update_threads;
	debuggee->get_debug_state = &get_debug_state;
	debuggee->set_debug_state = &set_debug_state;
	debuggee->get_thread_state = &get_thread_state;
	debuggee->set_thread_state = &set_thread_state;
	debuggee->get_neon_state = &get_neon_state;
	debuggee->set_neon_state = &set_neon_state;
}

int main(int argc, char **argv, const char **envp){
	if(getuid() && geteuid()){
		printf("iosdbg requires root to operate correctly\n");
		return 1;
	}
	
	debuggee = NULL;

	setup_initial_debuggee();
	install_handlers();

	rl_catch_signals = 0;

	signal(SIGINT, interrupt);

	char *line = NULL;
	char *prevline = NULL;
	
	while((line = readline(prompt)) != NULL){
		/* If the user hits enter, repeat the last command,
		 * and do not add to the command history if the length
		 * of line is 0.
		 */
		if(strlen(line) == 0 && prevline){
			line = realloc(line, strlen(prevline) + 1);
			strcpy(line, prevline);
		}
		else if(strlen(line) > 0 && (!prevline || (prevline && strcmp(line, prevline) != 0)))
			add_history(line);

		// update the debuggee's linkedlist of threads
		if(debuggee->pid != -1){
			thread_act_port_array_t threads;
			debuggee->update_threads(&threads);
			
			machthread_updatethreads(threads);

			struct machthread *focused = machthread_getfocused();

			// we have to set a focused thread first, so set it to the first thread
			if(!focused){
				printf("[Previously selected thread dead, selecting thread #1]\n\n");
				machthread_setfocused(threads[0]);
				focused = machthread_getfocused();
			}

			if(focused)
				machthread_updatestate(focused);
		}

		/* Make a copy of line in case the command function modifies it. */
		char *linecopy = malloc(strlen(line) + 1);
		strcpy(linecopy, line);
		
		execute_command(line);
		
		prevline = realloc(prevline, strlen(linecopy) + 1);
		strcpy(prevline, linecopy);

		free(linecopy);
		free(line);
	}

	return 0;
}
