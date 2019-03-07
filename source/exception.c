#include <ctype.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>

#include "breakpoint.h"
#include "dbgcmd.h"
#include "defs.h"
#include "machthread.h"
#include "memutils.h"
#include "printutils.h"
#include "trace.h"
#include "watchpoint.h"

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
