#include "iosdbg.h"

void *death_server(void *arg){
	while(1){
		if(debuggee->pid != -1){
			mach_port_type_t type;
			kern_return_t err = mach_port_type(mach_task_self(), debuggee->task, &type);

			if(type == MACH_PORT_TYPE_DEAD_NAME){
				printf("\n[%d dead]\n", debuggee->pid);
				cmdfunc_detach(NULL, 1);
				rl_on_new_line();
				rl_redisplay();
			}
		}

		sleep(1);
	}
}

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
	debuggee->suspend = &suspend;
	debuggee->update_threads = &update_threads;
	debuggee->get_debug_state = &get_debug_state;
	debuggee->set_debug_state = &set_debug_state;
	debuggee->get_thread_state = &get_thread_state;
	debuggee->set_thread_state = &set_thread_state;
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

	// Until I can figure out how to correctly implement mach notifications, this will work fine.
	// Spawn a thread to check for the termination of the debuggee.
	pthread_t dt;
	pthread_create(&dt, NULL, death_server, NULL);

	while((line = readline("(iosdbg) ")) != NULL){
		// add the command to history
		if(strlen(line) > 0)
			add_history(line);

		// update the debuggee's linkedlist of threads
		if(debuggee->pid != -1){
			thread_act_port_array_t threads;
			debuggee->update_threads(&threads);
			
			machthread_updatethreads(threads);

			struct machthread *focused = machthread_getfocused();

			// we have to set a focused thread first, so set it to the first thread
			if(!focused){
				machthread_setfocused(threads[0]);
				focused = machthread_getfocused();
			}

			if(focused)
				machthread_updatestate(focused);
		}
		
		execute_command(line);
		free(line);
	}

	return 0;
}
