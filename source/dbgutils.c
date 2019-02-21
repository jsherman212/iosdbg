#include <dlfcn.h>
#include <errno.h>
#include <pthread/pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>

#include <readline/readline.h>

#include "breakpoint.h"
#include "convvar.h"
#include "defs.h"
#include "dbgcmd.h"
#include "dbgutils.h"
#include "linkedlist.h"
#include "machthread.h"
#include "memutils.h"
#include "printutils.h"
#include "trace.h"
#include "watchpoint.h"

/* Both unused. */
kern_return_t catch_mach_exception_raise_state(mach_port_t exception_port, exception_type_t exception, exception_data_t code, mach_msg_type_number_t code_count, int *flavor, thread_state_t in_state, mach_msg_type_number_t in_state_count, thread_state_t out_state, mach_msg_type_number_t *out_state_count){return KERN_FAILURE;}
kern_return_t catch_mach_exception_raise_state_identity(mach_port_t exception_port, mach_port_t thread, mach_port_t task, exception_type_t exception, exception_data_t code, mach_msg_type_number_t code_count, int *flavor, thread_state_t in_state, mach_msg_type_number_t in_state_count, thread_state_t out_state, mach_msg_type_number_t *out_state_count){return KERN_FAILURE;}

extern boolean_t mach_exc_server(mach_msg_header_t *InHeadP, mach_msg_header_t *OutHeadP);

struct kinfo_proc *fill_kinfo_proc_buffer(size_t *length){
	int err;
	struct kinfo_proc *result = NULL;

	static const int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };

	*length = 0;

	err = sysctl((int *)name, (sizeof(name) / sizeof(name[0])) - 1, NULL, length, NULL, 0);
	
	if(err){
		printf("Couldn't get the size of our kinfo_proc buffer: %s\n", strerror(errno));
		return NULL;
	}
	
	result = malloc(*length);
	err = sysctl((int *)name, (sizeof(name) / sizeof(name[0])) - 1, result, length, NULL, 0);
	
	if(err){
		printf("Second sysctl call failed: %s\n", strerror(errno));
		return NULL;
	}

	return result;
}

pid_t pid_of_program(char *progname, char **errorstring){
	size_t length;

	struct kinfo_proc *result = fill_kinfo_proc_buffer(&length);

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
			char p_stat = current->kp_proc.p_stat;
			int pnamelen = strlen(pname);
			int charstocompare = pnamelen < maxnamelen ? pnamelen : maxnamelen;

			if(strncmp(pname, progname, charstocompare) == 0 && p_stat != SZOMB){
				matches++;
				sprintf(matchstr, "%s PID %d: %s\n", matchstr, pid, pname);
				final_pid = pid;
			}
		}
	}

	free(result);
	
	if(matches > 1){
		asprintf(errorstring, "multiple instances of '%s': \n%s", progname, matchstr);
		free(matchstr);
		return -1;
	}

	free(matchstr);

	if(matches == 0){
		asprintf(errorstring, "could not find a process named '%s'", progname);
		return -1;
	}

	if(matches == 1)
		return final_pid;
	
	return -1;
}

char *progname_from_pid(pid_t pid){
	size_t length;

	struct kinfo_proc *result = fill_kinfo_proc_buffer(&length);

	int num_procs = length / sizeof(struct kinfo_proc);

	for(int i=0; i<num_procs; i++){
		struct kinfo_proc *current = &result[i];

		if(current){
			if(current->kp_proc.p_pid == pid)
				return strdup(current->kp_proc.p_comm);
		}
	}

	return NULL;
}

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

void setup_initial_debuggee(void){
	debuggee = malloc(sizeof(struct debuggee));

	/* If we aren't attached to anything, debuggee's pid is -1. */
	debuggee->pid = -1;
	debuggee->interrupted = 0;
	debuggee->breakpoints = linkedlist_new();
	debuggee->watchpoints = linkedlist_new();
	debuggee->threads = linkedlist_new();

	debuggee->num_breakpoints = 0;
	debuggee->num_watchpoints = 0;

	debuggee->last_hit_bkpt_ID = 0;
	debuggee->last_hit_bkpt_hw = 0;

	debuggee->is_single_stepping = 0;
	debuggee->want_single_step = 0;

	debuggee->want_detach = 0;

	debuggee->last_unix_signal = -1;
	debuggee->soft_signal_exc = 0;

	debuggee->tracing_disabled = 0;
	debuggee->currently_tracing = 0;

	/* Figure out how many hardware breakpoints/watchpoints are supported. */
	size_t len = sizeof(int);

	sysctlbyname("hw.optional.breakpoint", &debuggee->num_hw_bps, &len, NULL, 0);
	
	len = sizeof(int);

	sysctlbyname("hw.optional.watchpoint", &debuggee->num_hw_wps, &len, NULL, 0);

	/* Create some iosdbg managed convenience variables. */
	char *error = NULL;

	set_convvar("$_", "", &error);
	set_convvar("$__", "", &error);
	set_convvar("$_exitcode", "", &error);
	set_convvar("$_exitsignal", "", &error);

	/* The user can set this so iosdbg never adds ASLR. */
	set_convvar("$NO_ASLR_OVERRIDE", "", &error);
}

const char *get_exception_name(exception_type_t exception){
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

void describe_hit_watchpoint(void *prev_data, void *cur_data, unsigned int sz){
	long long old_val = memutils_buffer_to_number(prev_data, sz);
	long long new_val = memutils_buffer_to_number(cur_data, sz);

	/* I'd like output in hex, but %x specifies unsigned int, and data could be negative.
	 * This is a hacky workaround.
	 */
	if(sz == sizeof(char))
		printf("Old value: %s%#x\nNew value: %s%#x\n\n", (char)old_val < 0 ? "-" : "", (char)old_val < 0 ? (char)-old_val : (char)old_val, (char)new_val < 0 ? "-" : "", (char)new_val < 0 ? (char)-new_val : (char)new_val);
	else if(sz == sizeof(short) || sz == sizeof(int)){
		old_val = (int)CFSwapInt32(old_val);
		new_val = (int)CFSwapInt32(new_val);

		printf("Old value: %s%#x\nNew value: %s%#x\n\n", (int)old_val < 0 ? "-" : "", (int)old_val < 0 ? (int)-old_val : (int)old_val, (int)new_val < 0 ? "-" : "", (int)new_val < 0 ? (int)-new_val : (int)new_val);
	}
	else{
		old_val = (long)CFSwapInt64(old_val);
		new_val = (long)CFSwapInt64(new_val);

		printf("Old value: %s%#lx\nNew value: %s%#lx\n\n", (long)old_val < 0 ? "-" : "", (long)old_val < 0 ? (long)-old_val : (long)old_val, (long)new_val < 0 ? "-" : "", (long)new_val < 0 ? (long)-new_val : (long)new_val);
	}
}

void describe_hit_breakpoint(unsigned long long tid, char *tname, struct breakpoint *hit){
	printf("\n * Thread %#llx: '%s': breakpoint %d at %#lx hit %d time(s).\n", tid, tname, hit->id, hit->location, hit->hit_count);
}

void clear_signal(mach_port_t thread){
	void *h = dlopen(0, RTLD_GLOBAL | RTLD_NOW);
	int (*ptrace)(int, pid_t, caddr_t, int) = dlsym(h, "ptrace");

	ptrace(PT_THUPDATE, debuggee->pid, (caddr_t)(unsigned long long)thread, 0);

	dlclose(h);
}

kern_return_t catch_mach_exception_raise(
		mach_port_t exception_port,
		mach_port_t thread,
		mach_port_t task,
		exception_type_t exception,
		exception_data_t _code,
		mach_msg_type_number_t code_count){
	/* Finish printing everything while tracing so
	 * we don't get caught in the middle of it.
	 */
	wait_for_trace();

	if(debuggee->task != task)
		return KERN_FAILURE;

	/* If this is called two times in a row, make sure
	 * to not increment the suspend count for our task
	 * more than once.
	 */
	if(!debuggee->interrupted){
		debuggee->suspend();
		debuggee->interrupted = 1;
	}

	debuggee->soft_signal_exc = 0;

	struct machthread *curfocused = machthread_getfocused();
	
	unsigned long long tid = get_tid_from_thread_port(thread);

	/* exception_data_t is typedef'ed as int *, which is
	 * not enough space to hold the data break address.
	 */
	long code = ((long *)_code)[0];
	long subcode = ((long *)_code)[1];

	/* Like LLDB, ignore signals when we aren't on the
	 * main thread and single stepping. 
	 */
	if(debuggee->is_single_stepping && exception == EXC_SOFTWARE && code == EXC_SOFT_SIGNAL){
		struct machthread *main = machthread_find(1);

		if(main && main->port != curfocused->port){
			rl_clear_visible_line();
			safe_reprompt();
			clear_signal(thread);

			return KERN_SUCCESS;
		}
	}

	/* Give focus to the thread that caused this exception. */
	if(!curfocused || curfocused->port != thread){
		printf("[Switching to thread %#llx]\n", tid);
		machthread_setfocused(thread);
	}

	debuggee->get_thread_state();

	if(exception == EXC_BREAKPOINT && code == EXC_ARM_BREAKPOINT){
		char *tname = get_thread_name_from_thread_port(thread);
		
		/* Hardware single step exception. */
		if(subcode == 0){
			/* When the exception caused by the single step happens,
			 * we can re-enable the breakpoint last it if it was software,
			 * or do nothing if it was hardware.
			 */
			if(!debuggee->last_hit_bkpt_hw)
				breakpoint_enable(debuggee->last_hit_bkpt_ID);
			
			if(debuggee->is_single_stepping && debuggee->want_single_step){
				/* Assume the user only wants to single step once
				 * so we don't have to deal with finding every place
				 * to reset this variable.
				 */
				debuggee->want_single_step = 0;

				/*debuggee->get_debug_state();
				debuggee->debug_state.__mdscr_el1 |= 1;
				debuggee->set_debug_state();
				*/
				/* Check if we a hit a breakpoint while single stepping,
				 * and if we do, report it.
				 */
				struct breakpoint *hit = find_bp_with_address(debuggee->thread_state.__pc);

				if(hit && !hit->ss){
					breakpoint_hit(hit);
					describe_hit_breakpoint(tid, tname, hit);
				}
				else
					printf("\n");

				free(tname);

				disassemble_at_location(debuggee->thread_state.__pc, 0x4);
				
				safe_reprompt();

				return KERN_SUCCESS;
			}
			else{
				debuggee->is_single_stepping = 0;

				debuggee->get_debug_state();
				debuggee->debug_state.__mdscr_el1 = 0;
				debuggee->set_debug_state();
			}

			/* Wait until we are not on a breakpoint to start single stepping. */
			struct breakpoint *bp = find_bp_with_address(debuggee->thread_state.__pc);

			if(!bp && !debuggee->is_single_stepping && debuggee->want_single_step){
				char *e;
				breakpoint_at_address(debuggee->thread_state.__pc, BP_TEMP, BP_SS, &e);
				
				debuggee->is_single_stepping = 1;
			}

			/* After we let the CPU single step, the instruction 
			 * at the last watchpoint executes, so we can check
			 * if there was a change in the data being watched.
			 */
			struct watchpoint *hit = find_wp_with_address(debuggee->last_hit_wp_loc);

			/* If nothing was found, continue as normal. */
			if(!hit){
				debuggee->resume();
				debuggee->interrupted = 0;
				
				return KERN_SUCCESS;
			}

			unsigned int sz = hit->data_len;
			
			/* Save previous data for comparision. */
			void *prev_data = malloc(sz);
			memcpy(prev_data, hit->data, sz);
			
			memutils_read_memory_at_location((void *)hit->location, hit->data, sz);
			
			/* Nothing has changed. However, I would like for a
			 * watchpoint to report even if this is the case if 
			 * the WP_READ bit is set in hit->LSC.
			 */
			if(memcmp(prev_data, hit->data, sz) == 0 && (hit->LSC & WP_READ)){
				free(prev_data);

				debuggee->resume();
				debuggee->interrupted = 0;

				return KERN_SUCCESS;
			}

			printf("\nWatchpoint %d hit:\n\n", hit->id);

			describe_hit_watchpoint(prev_data, hit->data, sz);
			disassemble_at_location(debuggee->last_hit_wp_PC + 4, 0x4);

			free(prev_data);
			
			debuggee->last_hit_wp_loc = 0;
			debuggee->last_hit_wp_PC = 0;
			
			safe_reprompt();

			free(tname);

			return KERN_SUCCESS;
		}

		struct breakpoint *hit = find_bp_with_address(subcode);

		if(!hit){
			printf("Could not find hit breakpoint? Please open an issue on github\n");
			return KERN_FAILURE;
		}
		
		/* Enable hardware single stepping so we can get past this instruction. */
		debuggee->get_debug_state();
		debuggee->debug_state.__mdscr_el1 |= 1;
		debuggee->set_debug_state();

		/* Record the ID and type of this breakpoint so we can
		 * enable it later if it's a software breakpoint.
		 */
		if(!hit->ss)
			debuggee->last_hit_bkpt_ID = hit->id;

		debuggee->last_hit_bkpt_hw = hit->hw;

		/* If we're single stepping and we encounter a
		 * software breakpoint, an exception will be
		 * thrown no matter what. This results in stepping
		 * the same instruction twice, so just disable it
		 * and re-enable it when the single step exception occurs.
		 */
		if(!hit->hw && !hit->ss && debuggee->is_single_stepping){
			breakpoint_disable(hit->id);

			debuggee->resume();
			debuggee->interrupted = 0;

			return KERN_SUCCESS;
		}

		breakpoint_hit(hit);

		if(!debuggee->is_single_stepping)
			describe_hit_breakpoint(tid, tname, hit);
		
		free(tname);

		/* Disable this software breakpoint to prevent
		 * an exception from being thrown over and over.
		 */
		if(!hit->hw)
			breakpoint_disable(hit->id);

		/* Fix up the output if a single step breakpoint hit. */
		if(hit->ss)
			printf("\n");

		disassemble_at_location(hit->location, 0x4);
			
		safe_reprompt();

		return KERN_SUCCESS;
	}
	else if(code == EXC_ARM_DA_DEBUG){
		/* A watchpoint hit. Log anything we need to compare data when
		 * the single step exception occurs.
		 */
		debuggee->last_hit_wp_loc = subcode;
		debuggee->last_hit_wp_PC = debuggee->thread_state.__pc;
		
		/* Enable hardware single stepping so we can get past this watchpoint. */
		debuggee->get_debug_state();
		debuggee->debug_state.__mdscr_el1 |= 1;
		debuggee->set_debug_state();

		debuggee->resume();
		debuggee->interrupted = 0;

		return KERN_SUCCESS;
	}

	/* Some other exception occured. */
	char *tname = get_thread_name_from_thread_port(thread);

	char *whathappened;

	asprintf(&whathappened, "\n * Thread %#llx, '%s' received signal ", tid, tname);

	/* A Unix signal was caught. */
	if(exception == EXC_SOFTWARE && code == EXC_SOFT_SIGNAL){
		/* If we sent SIGSTOP to correct the debuggee's process
		 * state, ignore it and resume execution.
		 */
		if(debuggee->want_detach){
			free(whathappened);

			debuggee->resume();
			debuggee->interrupted = 0;

			return KERN_SUCCESS;
		}

		debuggee->last_unix_signal = subcode;
		debuggee->soft_signal_exc = 1;

		char *sigstr = strdup(sys_signame[subcode]);
		size_t sigstrlen = strlen(sigstr);

		for(int i=0; i<sigstrlen; i++)
			sigstr[i] = toupper(sigstr[i]);

		asprintf(&whathappened, "%s%ld, SIG%s. ", whathappened, subcode, sigstr);

		free(sigstr);
		
		/* Ignore SIGINT and SIGTRAP. */
		if(subcode == SIGINT || subcode == SIGTRAP)
			clear_signal(thread);
	}
	else
		asprintf(&whathappened, "%s%d, %s. ", whathappened, exception, get_exception_name(exception));

	asprintf(&whathappened, "%s%#llx in debuggee.\n", whathappened, debuggee->thread_state.__pc);

	printf("%s", whathappened);

	free(whathappened);
	free(tname);

	disassemble_at_location(debuggee->thread_state.__pc, 0x4);

	rl_already_prompted = 0;

	safe_reprompt();

	return KERN_SUCCESS;
}
