#include <errno.h>
#include <mach/mach.h>
#include <pthread/pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/event.h>
#include <unistd.h>

#include "convvar.h"
#include "dbgcmd.h"
#include "defs.h"
#include "printutils.h"
#include "trace.h"

/* Both unused. */
kern_return_t catch_mach_exception_raise_state(mach_port_t exception_port, exception_type_t exception, exception_data_t code, mach_msg_type_number_t code_count, int *flavor, thread_state_t in_state, mach_msg_type_number_t in_state_count, thread_state_t out_state, mach_msg_type_number_t *out_state_count){return KERN_FAILURE;}
kern_return_t catch_mach_exception_raise_state_identity(mach_port_t exception_port, mach_port_t thread, mach_port_t task, exception_type_t exception, exception_data_t code, mach_msg_type_number_t code_count, int *flavor, thread_state_t in_state, mach_msg_type_number_t in_state_count, thread_state_t out_state, mach_msg_type_number_t *out_state_count){return KERN_FAILURE;}

extern boolean_t mach_exc_server(mach_msg_header_t *InHeadP, mach_msg_header_t *OutHeadP);
void *exception_server(void *arg){
	while(1){
		if(debuggee->pid == -1)
			pthread_exit(NULL);

		struct msg req;
		kern_return_t err = mach_msg(&req.head, MACH_RCV_MSG, 0, sizeof(req), debuggee->exception_port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

		if(err)
			printf("exception_server: %s\n", mach_error_string(err));

		boolean_t parsed = mach_exc_server(&req.head, &debuggee->exc_rpl.head);

		if(!parsed)
			printf("exception_server: our request could not be parsed\n");

		/* If a fatal UNIX signal is encountered and we reply
		 * to this exception right away, the debuggee will be killed
		 * before the user can inspect it.
		 * Solution: if we had this kind of exception, reply to it
		 * when the user wants to continue.
		 */
		if(!debuggee->soft_signal_exc){
			err = mach_msg(&debuggee->exc_rpl.head, MACH_SEND_MSG, debuggee->exc_rpl.head.msgh_size, 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

			if(err)
				printf("exception_server: %s\n", mach_error_string(err));
		}
	}

	return NULL;
}

void *death_server(void *arg){
	int kqid = *(int *)arg;

	while(1){
		struct kevent death_event;

		/* Provide a struct for the kernel to write to if any changes occur. */
		int changes = kevent(kqid, NULL, 0, &death_event, 1, NULL);

		/* Don't report if we detached earlier. */
		if(debuggee->pid == -1){
			free(arg);
			pthread_exit(NULL);
		}

		if(changes < 0){
			printf("kevent: %s\n", strerror(errno));
			free(arg);
			pthread_exit(NULL);
		}
		
		wait_for_trace();
		
		/* Figure out how the debuggee exited. */
		int status;
		waitpid(debuggee->pid, &status, 0);

		char *error = NULL;

		if(WIFEXITED(status)){
			int wexitstatus = WEXITSTATUS(status);
			printf("\n[%s (%d) exited normally (status = 0x%8.8x)]\n", debuggee->debuggee_name, debuggee->pid, wexitstatus);

			char *wexitstatusstr;
			asprintf(&wexitstatusstr, "%#x", wexitstatus);

			void_convvar("$_exitsignal");
			set_convvar("$_exitcode", wexitstatusstr, &error);

			desc_auto_convvar_error_if_needed("$_exitcode", error);

			free(wexitstatusstr);
		}
		else if(WIFSIGNALED(status)){
			int wtermsig = WTERMSIG(status);
			printf("\n[%s (%d) terminated due to signal %d]\n", debuggee->debuggee_name, debuggee->pid, wtermsig);

			char *wtermsigstr;
			asprintf(&wtermsigstr, "%#x", wtermsig);

			void_convvar("$_exitcode");
			set_convvar("$_exitsignal", wtermsigstr, &error);

			desc_auto_convvar_error_if_needed("$_exitsignal", error);

			free(wtermsigstr);
		}

		free(arg);
		
		error = NULL;
		cmdfunc_detach(NULL, 1, &error);
		
		if(error)
			printf("could not detach: %s\n", error);

		close(kqid);
		safe_reprompt();
		pthread_exit(NULL);
	}

	return NULL;
}

void setup_servers(void){
	debuggee->setup_exception_handling();

	/* Start the exception server. */
	pthread_t exception_server_thread;
	pthread_create(&exception_server_thread, NULL, exception_server, NULL);

	int kqid = kqueue();

	if(kqid == -1){
		printf("Could not create kernel event queue\n");
		return;
	}

	struct kevent kev;

	EV_SET(&kev, debuggee->pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, NULL);
	
	/* Tell the kernel to add this event to the monitored list. */
	kevent(kqid, &kev, 1, NULL, 0, NULL);

	int *intptr = malloc(sizeof(int));
	*intptr = kqid;

	/* Check if the debuggee dies. */
	pthread_t death_server_thread;
	pthread_create(&death_server_thread, NULL, death_server, intptr);
}
