#include <dlfcn.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <readline/readline.h>

#include "breakpoint.h"
#include "convvar.h"
#include "dbgcmd.h"
#include "dbgutils.h"
#include "exception.h"		/* Includes defs.h */
#include "expr.h"
#include "linkedlist.h"
#include "machthread.h"
#include "memutils.h"
#include "procutils.h"
#include "servers.h"
#include "trace.h"
#include "watchpoint.h"

int keep_checking_for_process;

/* Allow the user to answer whatever
 * question is given.
 */
char answer(const char *question, ...){
	va_list args;
	va_start(args, question);

	vprintf(question, args);

	va_end(args);
	
	char *answer = NULL;
	size_t len;
	
	getline(&answer, &len, stdin);
	answer[strlen(answer) - 1] = '\0';
	
	/* Allow the user to hit enter as another way
	 * of saying yes.
	 */
	if(strlen(answer) == 0)
		return 'y';

	char ret = tolower(answer[0]);

	while(ret != 'y' && ret != 'n'){
		va_list args;
		va_start(args, question);

		vprintf(question, args);

		va_end(args);

		free(answer);
		answer = NULL;

		getline(&answer, &len, stdin);
		answer[strlen(answer) - 1] = '\0';
		
		ret = tolower(answer[0]);
	}
	
	free(answer);

	return ret;
}

int is_number(char *str){
	size_t len = strlen(str);

	for(int i=0; i<len; i++){
		if(!isdigit(str[i]))
			return 0;
	}

	return 1;
}

pid_t parse_pid(char *pidstr, char **err){
	return is_number(pidstr) ? strtol(pidstr, NULL, 10) : pid_of_program(pidstr, err);
}

int wants_add_aslr(char *str){
	/* This convenience variable allows the user to tell iosdbg
	 * to ignore ASLR no matter what.
	 */
	char *error;
	char *no_aslr_override = convvar_strval("$NO_ASLR_OVERRIDE", &error);

	if(no_aslr_override && strcmp(no_aslr_override, "void") != 0)
		return 0;

	if(!str)
		return 1;

	return strnstr(str, "--no-aslr", strlen(str)) == NULL;
}

long strtol_err(char *str, char **error){
	if(!str){
		asprintf(error, "NULL argument `str`");
		return -1;
	}

	char *endptr = NULL;
	long result = strtol(str, &endptr, 0);

	if(endptr && *endptr != '\0'){
		asprintf(error, "invalid number '%s'", str);
		return -1;
	}

	return result;
}

double strtod_err(char *str, char **error){
	if(!str){
		asprintf(error, "NULL argument `str`");
		return -1.0;
	}

	char *endptr = NULL;
	double result = strtod(str, &endptr);

	if(endptr && *endptr != '\0'){
		asprintf(error, "invalid number '%s'", str);
		return -1.0;
	}

	return result;
}

cmd_error_t cmdfunc_aslr(char *args, int arg1, char **error){
	if(debuggee->pid == -1){
		asprintf(error, "not attached to anything");
		return CMD_FAILURE;
	}

	printf("%7s%#llx\n", "", debuggee->aslr_slide);
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_attach(char *args, int arg1, char **error){
	if(!args){
		asprintf(error, "no target");
		return CMD_FAILURE;
	}

	if(strcmp(args, "iosdbg") == 0){
		asprintf(error, "cannot attach to myself");
		return CMD_FAILURE;
	}

	if(debuggee->pid != -1){
		char *target = NULL;

		if(strstr(args, "--waitfor"))
			target = args + strlen("--waitfor ");

		char ans = answer("Detach from %s and reattach to %s? (y/n) ", debuggee->debuggee_name, !target ? args : target);

		if(ans == 'n')
			return CMD_SUCCESS;
		
		/* Detach from what we are attached to
		 * and call this function again.
		 */		
		cmdfunc_detach(NULL, 0, error);
		cmdfunc_attach(args, 0, error);

		return CMD_SUCCESS;
	}

	pid_t pid;
	char *piderr = NULL;

	char *target = NULL;

	/* Constantly check if this process has been launched. */
	if(strstr(args, "--waitfor")){
		target = args + strlen("--waitfor ");

		if(strlen(target) == 0){
			asprintf(error, "no target given");
			cmdfunc_help("attach", 0, error);
			return CMD_FAILURE;
		}

		if(is_number(target)){
			asprintf(error, "cannot wait for PIDs");
			return CMD_FAILURE;
		}

		if(strcmp(target, "iosdbg") == 0){
			asprintf(error, "cannot attach to myself");
			return CMD_FAILURE;
		}

		printf("Waiting for process '%s' to launch (Ctrl+C to stop)\n\n", target);

		pid = parse_pid(target, &piderr);

		if(piderr)
			free(piderr);
		
		piderr = NULL;

		keep_checking_for_process = 1;

		while(pid == -1 && keep_checking_for_process){
			pid = parse_pid(target, &piderr);

			if(piderr)
				free(piderr);

			piderr = NULL;

			usleep(400);
		}

		keep_checking_for_process = 0;
	}
	else{
		target = args;
		pid = parse_pid(args, &piderr);
	}
	
	if(pid == -1){
		if(piderr){
			asprintf(error, "%s", piderr);
			free(piderr);
		}
		else
			asprintf(error, "PID not found");

		return CMD_FAILURE;
	}

	if(pid == 0){
		asprintf(error, "no kernel debugging");
		return CMD_FAILURE;
	}

	kern_return_t err = task_for_pid(mach_task_self(), pid, &debuggee->task);

	if(err){
		asprintf(error, "couldn't get task port for %s (pid: %d): %s\n"
				"Did you forget to sign iosdbg with entitlements?", args, pid, mach_error_string(err));
		return CMD_FAILURE;
	}

	debuggee->pid = pid;
	debuggee->aslr_slide = debuggee->find_slide();
	
	if(is_number(target)){
		char *name = progname_from_pid(debuggee->pid, error);

		if(*error)
			return CMD_FAILURE;

		if(!name){
			asprintf(error, "could not get the debuggee's name");
			cmdfunc_detach(NULL, 0, error);
			return CMD_FAILURE;
		}

		debuggee->debuggee_name = malloc(strlen(name) + 1);
		strcpy(debuggee->debuggee_name, name);

		free(name);
	}
	else{
		debuggee->debuggee_name = malloc(strlen(target) + 1);
		strcpy(debuggee->debuggee_name, target);
	}

	debuggee->breakpoints = linkedlist_new();
	debuggee->watchpoints = linkedlist_new();
	debuggee->threads = linkedlist_new();

	setup_servers();

	debuggee->num_breakpoints = 0;
	debuggee->num_watchpoints = 0;

	thread_act_port_array_t threads;
	debuggee->update_threads(&threads);
	
	resetmtid();
	
	machthread_updatethreads(threads);
	machthread_setfocused(threads[0]);

	debuggee->get_thread_state();

	debuggee->want_detach = 0;

	printf("Attached to %s (pid: %d), slide: %#llx.\n", debuggee->debuggee_name, debuggee->pid, debuggee->aslr_slide);

	/* ptrace.h is unavailable on iOS. */
	void *h = dlopen(0, RTLD_GLOBAL | RTLD_NOW);
	int (*ptrace)(int, pid_t, caddr_t, int) = dlsym(h, "ptrace");

	/* Have Unix signals be sent as Mach exceptions. */
	ptrace(PT_ATTACHEXC, debuggee->pid, 0, 0);
	ptrace(PT_SIGEXC, debuggee->pid, 0, 0);
	
	dlclose(h);

	/* Since SIGSTOP is going to be caught right after
	 * this function, don't reprint the (iosdbg) prompt.
	 */
	rl_already_prompted = 1;

	void_convvar("$_exitcode");
	void_convvar("$_exitsignal");

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_backtrace(char *args, int arg1, char **error){
	if(debuggee->pid == -1){
		asprintf(error, "not attached to anything");
		return CMD_FAILURE;
	}
	
	debuggee->get_thread_state();

	// print frame 0, which is where we are currently at
	printf("  * frame #0: 0x%16.16llx\n", debuggee->thread_state.__pc);
	
	// frame 1 is what is in LR
	printf("    frame #1: 0x%16.16llx\n", debuggee->thread_state.__lr);

	int frame_counter = 2;

	// there's a linked list-like thing of frame pointers
	// so we can unwind the stack by following this linked list
	struct frame_t {
		struct frame_t *next;
		unsigned long long frame;
	};

	struct frame_t *current_frame = malloc(sizeof(struct frame_t));
	kern_return_t err = memutils_read_memory_at_location((void *)debuggee->thread_state.__fp, current_frame, sizeof(struct frame_t));
	
	if(err){
		asprintf(error, "backtrace failed: %s", mach_error_string(err));
		return CMD_FAILURE;
	}

	while(current_frame->next){
		printf("    frame #%d: 0x%16.16llx\n", frame_counter, current_frame->frame);

		memutils_read_memory_at_location((void *)current_frame->next, (void *)current_frame, sizeof(struct frame_t));	
		frame_counter++;
	}

	printf(" - cannot unwind past frame %d -\n", frame_counter - 1);

	free(current_frame);

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_break(char *args, int arg1, char **error){
	if(!args){
		asprintf(error, "missing argument");
		cmdfunc_help("break", 0, error);
		return CMD_FAILURE;
	}

	if(debuggee->pid == -1){
		asprintf(error, "not attached to anything");
		return CMD_FAILURE;
	}

	char *tok = strtok(args, " ");	

	long location = parse_expr(tok, error);

	if(*error){
		asprintf(error, "expression evaluation failed: %s", *error);
		return CMD_FAILURE;
	}

	/* Check for --no-aslr. */
	tok = strtok(NULL, " ");
	
	int add_aslr = wants_add_aslr(tok);

	if(add_aslr)
		location += debuggee->aslr_slide;
	
	bp_error_t result = breakpoint_at_address(location, BP_NO_TEMP, BP_NO_SS, error);

	if(result != BP_SUCCESS)
		return CMD_FAILURE;

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_continue(char *args, int do_not_print_msg, char **error){
	if(debuggee->pid == -1)
		return CMD_FAILURE;
	
	if(!debuggee->interrupted)
		return CMD_FAILURE;
	
	if(debuggee->pending_messages > 0)
		reply_to_exception(debuggee->exc_request, KERN_SUCCESS);

	kern_return_t err = debuggee->resume();

	if(err){
		asprintf(error, "cannot resume: %s", mach_error_string(err));
		return CMD_FAILURE;
	}

	debuggee->interrupted = 0;

	if(!do_not_print_msg)
		printf("Process %d resuming\n", debuggee->pid);

	/* Make output look nicer. */
	if(debuggee->currently_tracing){
		rl_already_prompted = 1;
		printf("\n");
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_delete(char *args, int arg1, char **error){
	if(debuggee->pid == -1)
		return CMD_FAILURE;
	
	if(!args){
		cmdfunc_help("delete", 0, error);
		return CMD_FAILURE;
	}

	char *tok = strtok(args, " ");
	
	if(!tok){
		cmdfunc_help("delete", 0, error);
		return CMD_FAILURE;
	}

	char *type = strdup(tok);

	if(strcmp(type, "b") != 0 && strcmp(type, "w") != 0){
		cmdfunc_help("delete", 0, error);
		free(type);
		return CMD_FAILURE;
	}

	if(strcmp(type, "b") == 0 && debuggee->num_breakpoints == 0){
		asprintf(error, "no breakpoints to delete");
		free(type);
		return CMD_FAILURE;
	}

	if(strcmp(type, "w") == 0 && debuggee->num_watchpoints == 0){
		asprintf(error, "no watchpoints to delete");
		free(type);
		return CMD_FAILURE;
	}
	
	tok = strtok(NULL, " ");

	/* If there's nothing after type, give the user
	 * an option to delete all.
	 */
	if(!tok){
		const char *target = strcmp(type, "b") == 0 ? "breakpoints" : "watchpoints";
		
		char ans = answer("Delete all %s? (y/n) ", target);

		if(ans == 'n'){
			printf("Nothing deleted.\n");
			free(type);
			return CMD_SUCCESS;
		}

		void (*delete_func)(void) = 
			strcmp(target, "breakpoints") == 0 ? 
			&breakpoint_delete_all :
			&watchpoint_delete_all;

		int num_deleted = strcmp(target, "breakpoints") == 0 ?
			debuggee->num_breakpoints :
			debuggee->num_watchpoints;

		delete_func();

		printf("All %s removed. (%d %s)\n", target, num_deleted, target);

		free(type);

		return CMD_SUCCESS;
	}

	int id = (int)strtol_err(tok, error);

	if(*error){
		free(type);
		return CMD_FAILURE;
	}

	if(strcmp(type, "b") == 0){
		free(type);

		bp_error_t err = breakpoint_delete(id);

		if(err == BP_FAILURE){
			asprintf(error, "couldn't delete breakpoint %d", id);
			return CMD_FAILURE;
		}

		printf("Breakpoint %d deleted\n", id);
	}
	else if(strcmp(type, "w") == 0){
		free(type);
		
		wp_error_t err = watchpoint_delete(id);

		if(err == WP_FAILURE){
			asprintf(error, "couldn't delete watchpoint %d", id);
			return CMD_FAILURE;
		}

		printf("Watchpoint %d deleted\n", id);
	}
	else{
		cmdfunc_help("delete", 0, error);
		return CMD_FAILURE;
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_detach(char *args, int from_death, char **error){
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	if(!debuggee->tracing_disabled)
		stop_trace();
	
	debuggee->want_detach = 1;

	void_convvar("$_");
	void_convvar("$__");

	// delete all breakpoints on detach so the original instruction is written back to prevent a crash
	// TODO: instead of deleting them, maybe disable all of them and if we are attached to the same thing again re-enable them?
	breakpoint_delete_all();
	watchpoint_delete_all();

	/* Disable hardware single stepping. */
	debuggee->get_debug_state();
	debuggee->debug_state.__mdscr_el1 = 0;
	debuggee->set_debug_state();

	cmdfunc_continue(NULL, 1, error);

	/* Send SIGSTOP to set debuggee's process status to
	 * SSTOP so we can detach. Calling ptrace with PT_THUPDATE
	 * to handle Unix signals sets this status to SRUN, and ptrace 
	 * bails if this status is SRUN. See bsd/kern/mach_process.c
	 */
	if(!from_death){
		char *pidstr;
		asprintf(&pidstr, "%d", debuggee->pid);

		pid_t p;
		char *stop_argv[] = {"kill", "-STOP", pidstr, NULL};
		int status = posix_spawnp(&p, "kill", NULL, NULL, (char * const *)stop_argv, NULL);
			
		if(status == 0)
			waitpid(p, &status, 0);
		else{
			asprintf(error, "posix_spawnp for SIGSTOP failed");
			return CMD_FAILURE;
		}

		void *h = dlopen(0, RTLD_GLOBAL | RTLD_NOW);
		int (*ptrace)(int, pid_t, caddr_t, int) = dlsym(h, "ptrace");
		
		ptrace(PT_DETACH, debuggee->pid, 0, 0);

		dlclose(h);

		/* Send SIGCONT so the process is running again. */
		char *cont_argv[] = {"kill", "-CONT", pidstr, NULL};
		status = posix_spawnp(&p, "kill", NULL, NULL, (char * const *)cont_argv, NULL);

		if(status == 0)
			waitpid(p, &status, 0);
		else{
			asprintf(error, "posix_spawnp for SIGCONT failed");
			return CMD_FAILURE;
		}

		free(pidstr);
	}

	debuggee->restore_exception_ports();

	debuggee->interrupted = 0;

	linkedlist_free(debuggee->breakpoints);
	debuggee->breakpoints = NULL;

	linkedlist_free(debuggee->watchpoints);
	debuggee->watchpoints = NULL;

	linkedlist_free(debuggee->threads);
	debuggee->threads = NULL;

	debuggee->num_breakpoints = 0;
	debuggee->num_watchpoints = 0;

	debuggee->last_hit_bkpt_ID = 0;
	debuggee->last_hit_bkpt_hw = 0;
	
	debuggee->last_hit_wp_loc = 0;
	debuggee->last_hit_wp_PC = 0;

	debuggee->deallocate_ports();

	if(!from_death)
		printf("Detached from %s (%d)\n", debuggee->debuggee_name, debuggee->pid);

	debuggee->pid = -1;

	free(debuggee->debuggee_name);
	debuggee->debuggee_name = NULL;

	debuggee->want_detach = 0;
	
	debuggee->last_unix_signal = -1;
	debuggee->soft_signal_exc = 0;

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_disassemble(char *args, int arg1, char **error){
	if(!args){
		cmdfunc_help("disassemble", 0, error);
		return CMD_FAILURE;
	}

	char *tok = strtok(args, " ");
	
	if(!tok){
		cmdfunc_help("disassemble", 0, error);
		return CMD_FAILURE;
	}

	long location = parse_expr(tok, error);

	if(*error)
		return CMD_FAILURE;

	/* Get the amount of instructions to disassemble. */
	tok = strtok(NULL, " ");

	if(!tok){
		cmdfunc_help("disassemble", 0, error);
		return CMD_FAILURE;
	}

	*error = NULL;
	int amount = (int)strtol_err(tok, error);

	if(*error)
		return CMD_FAILURE;

	if(amount <= 0){
		cmdfunc_help("disassemble", 0, error);
		return CMD_FAILURE;
	}

	tok = strtok(NULL, " ");

	int add_aslr = wants_add_aslr(tok);

	if(add_aslr)
		location += debuggee->aslr_slide;

	kern_return_t err = disassemble_at_location(location, amount);

	if(err){
		asprintf(error, "could not disassemble from %#lx to %#lx: %s", location, location + amount, mach_error_string(err));
		return CMD_FAILURE;
	}
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_examine(char *args, int arg1, char **error){
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	if(!args){
		cmdfunc_help("examine", 0, error);
		return CMD_FAILURE;
	}

	char *tok = strtok(args, " ");
	
	if(!tok){
		cmdfunc_help("examine", 0, error);
		return CMD_FAILURE;
	}

	long location = parse_expr(tok, error);

	if(*error)
		return CMD_FAILURE;

	/* Next, however many bytes are wanted. */
	tok = strtok(NULL, " ");

	if(!tok){
		cmdfunc_help("examine", 0, error);
		return CMD_FAILURE;
	}
	
	int amount = (int)strtol_err(tok, error);

	if(*error)
		return CMD_FAILURE;
	
	if(amount < 0){
		cmdfunc_help("examine", 0, error);
		return CMD_FAILURE;
	}

	tok = strtok(NULL, " ");

	int add_aslr = wants_add_aslr(tok);

	if(add_aslr)
		location += debuggee->aslr_slide;

	kern_return_t err = memutils_dump_memory(location, amount);

	if(err){
		asprintf(error, "could not dump memory from %#lx to %#lx: %s", location, location + amount, mach_error_string(err));
		return CMD_FAILURE;
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_help(char *args, int arg1, char **error){
	if(!args){
		asprintf(error, "need command");
		return CMD_FAILURE;
	}

	int num_cmds = sizeof(COMMANDS) / sizeof(struct dbg_cmd_t);
	int cur_cmd_idx = 0;

	while(cur_cmd_idx < num_cmds){
		struct dbg_cmd_t *cmd = &COMMANDS[cur_cmd_idx];
	
		/* Must not be an ambiguous command. */
		if(strcmp(cmd->name, args) == 0 && cmd->function){
			printf("\t%s\n", cmd->desc);
			return CMD_SUCCESS;
		}

		cur_cmd_idx++;
	}
	
	return CMD_FAILURE;
}

cmd_error_t cmdfunc_kill(char *args, int arg1, char **error){
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	if(!debuggee->debuggee_name)
		return CMD_FAILURE;

	char ans = answer("Do you really want to kill %s? (y/n) ", debuggee->debuggee_name);

	if(ans == 'n')
		return CMD_SUCCESS;

	char *saved_name = malloc(strlen(debuggee->debuggee_name) + 1);
	strcpy(saved_name, debuggee->debuggee_name);

	cmdfunc_detach(NULL, 0, error);
	
	pid_t p;
	char *argv[] = {"killall", "-9", saved_name, NULL};
	int status = posix_spawnp(&p, "killall", NULL, NULL, (char * const *)argv, NULL);
	
	free(saved_name);

	if(status == 0)
		waitpid(p, &status, 0);
	else{
		asprintf(error, "could not kill debuggee");
		return CMD_FAILURE;
	}
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_quit(char *args, int arg1, char **error){
	cmdfunc_detach(NULL, 0, error);

	if(*error)
		return CMD_FAILURE;

	/* Free the arrays made from the trace.codes file. */
	if(!debuggee->tracing_disabled){
		stop_trace();
		
		for(int i=0; i<bsd_syscalls_arr_len; i++){
			if(bsd_syscalls[i])
				free(bsd_syscalls[i]);
		}

		free(bsd_syscalls);

		for(int i=0; i<mach_traps_arr_len; i++){
			if(mach_traps[i])
				free(mach_traps[i]);
		}

		free(mach_traps);

		for(int i=0; i<mach_messages_arr_len; i++){
			if(mach_messages[i])
				free(mach_messages[i]);
		}

		free(mach_messages);
	}

	free(debuggee);
	
	exit(0);
}

cmd_error_t cmdfunc_regsfloat(char *args, int arg1, char **error){
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	if(!args){
		cmdfunc_help("regs float", 0, error);
		return CMD_FAILURE;
	}

	/* If the user wants a quadword register,
	 * the max string length would be 87.
	 */
	const int sz = 90;

	char *regstr = malloc(sz);

	/* Iterate through and show all the registers the user asked for. */
	char *tok = strtok(args, " ");

	while(tok){
		debuggee->get_neon_state();

		if(strcmp(tok, "fpsr") == 0){
			tok = strtok(NULL, " ");
			printf("%10s = 0x%8.8x\n", "fpsr", debuggee->neon_state.__fpsr);
			continue;
		}
		else if(strcmp(tok, "fpcr") == 0){
			tok = strtok(NULL, " ");
			printf("%10s = 0x%8.8x\n", "fpcr", debuggee->neon_state.__fpcr);
			continue;
		}
		
		memset(regstr, '\0', sz);

		char reg_type = tolower(tok[0]);
		
		/* Move up a byte for the register number. */
		tok++;

		int reg_num = (int)strtol_err(tok, error);

		if(*error){
			free(regstr);
			return CMD_FAILURE;
		}

		int good_reg_num = (reg_num >= 0 && reg_num <= 31);
		int good_reg_type = ((reg_type == 'q' || reg_type == 'v') || reg_type == 'd' || reg_type == 's');

		if(!good_reg_num || !good_reg_type){
			printf("%8sInvalid register\n", "");

			tok = strtok(NULL, " ");
			continue;
		}
		/* Quadword */
		else if(reg_type == 'q' || reg_type == 'v'){
			long *hi = malloc(sizeof(long));
			long *lo = malloc(sizeof(long));
			
			*hi = debuggee->neon_state.__v[reg_num] >> 64;
			*lo = debuggee->neon_state.__v[reg_num];
			
			void *hi_data = (uint8_t *)hi;
			void *lo_data = (uint8_t *)lo;

			sprintf(regstr, "v%d = {", reg_num);

			for(int i=0; i<sizeof(long); i++)
				sprintf(regstr, "%s0x%02x ", regstr, *(uint8_t *)(lo_data + i));
			
			for(int i=0; i<sizeof(long) - 1; i++)
				sprintf(regstr, "%s0x%02x ", regstr, *(uint8_t *)(hi_data + i));

			sprintf(regstr, "%s0x%02x}", regstr, *(uint8_t *)(hi_data + (sizeof(long) - 1)));

			free(hi);
			free(lo);
		}
		/* Doubleword */
		else if(reg_type == 'd')
			sprintf(regstr, "d%d = %.15g", reg_num, *(double *)&debuggee->neon_state.__v[reg_num]);
		/* Word */
		else if(reg_type == 's')
			sprintf(regstr, "s%d = %g", reg_num, *(float *)&debuggee->neon_state.__v[reg_num]);

		/* Figure out how many bytes the register takes up in the string. */
		char *space = strchr(regstr, ' ');
		int bytes = space - regstr;

		int add = 8 - bytes;
		
		printf("%*s\n", (int)(strlen(regstr) + add), regstr);
		
		tok = strtok(NULL, " ");
	}

	free(regstr);

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_regsgen(char *args, int arg1, char **error){
	if(debuggee->pid == -1)
		return CMD_FAILURE;
	
	debuggee->get_thread_state();

	const int sz = 8;

	/* If there were no arguments, print every register. */
	if(!args){
		for(int i=0; i<29; i++){		
			char *regstr = malloc(sz);
			memset(regstr, '\0', sz);

			sprintf(regstr, "x%d", i);

			printf("%10s = 0x%16.16llx\n", regstr, debuggee->thread_state.__x[i]);

			free(regstr);
		}
		
		printf("%10s = 0x%16.16llx\n", "fp", debuggee->thread_state.__fp);
		printf("%10s = 0x%16.16llx\n", "lr", debuggee->thread_state.__lr);
		printf("%10s = 0x%16.16llx\n", "sp", debuggee->thread_state.__sp);
		printf("%10s = 0x%16.16llx\n", "pc", debuggee->thread_state.__pc);
		printf("%10s = 0x%8.8x\n", "cpsr", debuggee->thread_state.__cpsr);

		return CMD_SUCCESS;
	}

	/* Otherwise, print every register they asked for. */
	char *tok = strtok(args, " ");

	while(tok){
		char reg_type = tolower(tok[0]);

		if(reg_type != 'x' && reg_type != 'w'){
			char *tokcpy = strdup(tok);

			/* We need to be able to free it. */
			for(int i=0; i<strlen(tokcpy); i++)
				tokcpy[i] = tolower(tokcpy[i]);

			if(strcmp(tokcpy, "fp") == 0)
				printf("%8s = 0x%16.16llx\n", "fp", debuggee->thread_state.__fp);
			else if(strcmp(tokcpy, "lr") == 0)
				printf("%8s = 0x%16.16llx\n", "lr", debuggee->thread_state.__lr);
			else if(strcmp(tokcpy, "sp") == 0)
				printf("%8s = 0x%16.16llx\n", "sp", debuggee->thread_state.__sp);
			else if(strcmp(tokcpy, "pc") == 0)
				printf("%8s = 0x%16.16llx\n", "pc", debuggee->thread_state.__pc);
			else if(strcmp(tokcpy, "cpsr") == 0)
				printf("%8s = 0x%8.8x\n", "cpsr", debuggee->thread_state.__cpsr);
			else
				printf("Invalid register\n");

			free(tokcpy);

			tok = strtok(NULL, " ");
			continue;
		}

		/* Move up one byte to get to the "register number". */
		tok++;

		int reg_num = (int)strtol_err(tok, error);

		if(*error)
			return CMD_FAILURE;
		
		if(reg_num < 0 || reg_num > 29){
			tok = strtok(NULL, " ");
			continue;
		}

		char *regstr = malloc(sz);
		memset(regstr, '\0', sz);

		sprintf(regstr, "%c%d", reg_type, reg_num);

		if(reg_type == 'x')
			printf("%8s = 0x%16.16llx\n", regstr, (long long)debuggee->thread_state.__x[reg_num]);
		else
			printf("%8s = 0x%8.8x\n", regstr, (int)debuggee->thread_state.__x[reg_num]);

		free(regstr);

		tok = strtok(NULL, " ");
	}
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_set(char *args, int arg1, char **error){
	if(!args){
		cmdfunc_help("set", 0, error);
		return CMD_FAILURE;
	}

	char specifier = args[0];

	char *argcpy = strdup(args);
	char *equals = strchr(argcpy, '=');

	if(!equals){
		free(argcpy);
		cmdfunc_help("set", 0, error);
		return CMD_FAILURE;
	}

	int cpylen = equals - argcpy;

	char *target = strndup(argcpy, cpylen);
	char *value_str = strdup(equals + 1);

	if(strlen(value_str) == 0){
		free(argcpy);
		free(target);
		free(value_str);

		cmdfunc_help("set", 0, error);
		return CMD_FAILURE;
	}

	char *nextspace = strchr(value_str, ' ');

	if(nextspace)
		value_str[nextspace - value_str] = '\0';

	/* If we are writing to an offset, we've done everything needed. */
	if(specifier == '*'){
		long value = strtol_err(value_str, error);

		free(value_str);

		if(*error)
			return CMD_FAILURE;

		long location = parse_expr(target + 1, error);

		if(*error){
			asprintf(error, "expression evaluation failed: %s", *error);
			return CMD_FAILURE;
		}

		/* Check for no ASLR. */
		char *tok = strtok(argcpy, " ");
		
		tok = strtok(NULL, " ");

		int wants_aslr = wants_add_aslr(tok);

		if(wants_aslr)
			location += debuggee->aslr_slide;
		
		free(argcpy);

		kern_return_t err = memutils_write_memory_to_location((vm_address_t)location, (vm_offset_t)value);

		if(err){
			asprintf(error, "could not write to %#lx: %s", location, mach_error_string(err));
			return CMD_FAILURE;
		}

		return CMD_SUCCESS;
	}
	/* Convenience variable or register. */
	else if(specifier == '$'){
		free(argcpy);

		/* To tell whether or not the user wants to set a 
		 * convenience variable, we can pass a string to the
		 * error parameter. convvar_set will bail and initialize `e`
		 * if the name is a system register. However, there's no
		 * need to notify the user if this occurs. Otherwise,
		 * the user meant to set a convenience variable and we can
		 * return after it is updated.
		 */
		char *e = NULL;
		set_convvar(target, value_str, &e);
		
		if(!e){
			free(target);
			free(value_str);

			return CMD_SUCCESS;
		}

		/* Put this check here so the user and set convenience variables
		 * without being attached to anything.
		 */
		if(debuggee->pid == -1){
			cmdfunc_help("set", 0, error);
			return CMD_FAILURE;
		}

		memmove(target, target + 1, strlen(target));

		for(int i=0; i<strlen(target); i++)
			target[i] = tolower(target[i]);

		char reg_type = target[0];
		char *reg_num_s = malloc(strlen(target + 1) + 1);
		strcpy(reg_num_s, target + 1);

		int reg_num = strtol(reg_num_s, NULL, 10);

		free(reg_num_s);

		int gpr = reg_type == 'x' || reg_type == 'w';
		int fpr = (reg_type == 'q' || reg_type == 'v') || 
				reg_type == 'd' || reg_type == 's';

		int good_reg_num = (reg_num >= 0 && reg_num <= 31);
		int good_reg_type = gpr || fpr;

		debuggee->get_thread_state();
		debuggee->get_neon_state();

		/* Various representations of our value string. */
		int valued = (int)strtol_err(value_str, error);

		if(gpr && *error)
			return CMD_FAILURE;

		long valuellx = strtol_err(value_str, error);

		if(gpr && *error)
			return CMD_FAILURE;

		/* The functions above will have set error
		 * if we have a floating point value, so
		 * clear it.
		 */
		*error = NULL;

		float valuef = (float)strtod_err(value_str, error);

		if(fpr && *error)
			return CMD_FAILURE;

		double valuedf = strtod_err(value_str, error);

		if(fpr && *error)
			return CMD_FAILURE;

		/* Take care of any special registers. */
		if(strcmp(target, "fp") == 0)
			debuggee->thread_state.__fp = valuellx;
		else if(strcmp(target, "lr") == 0)
			debuggee->thread_state.__lr = valuellx;
		else if(strcmp(target, "sp") == 0)
			debuggee->thread_state.__sp = valuellx;
		else if(strcmp(target, "pc") == 0)
			debuggee->thread_state.__pc = valuellx;
		else if(strcmp(target, "cpsr") == 0)
			debuggee->thread_state.__cpsr = valued;
		else if(strcmp(target, "fpsr") == 0)
			debuggee->neon_state.__fpsr = valued;
		else if(strcmp(target, "fpcr") == 0)
			debuggee->neon_state.__fpcr = valued;
		else{
			if(!good_reg_num || !good_reg_type){
				cmdfunc_help("set", 0, error);
				return CMD_FAILURE;
			}

			if(gpr){
				if(reg_type == 'x')
					debuggee->thread_state.__x[reg_num] = valuellx;
				else{
					debuggee->thread_state.__x[reg_num] &= ~0xFFFFFFFFULL;
					debuggee->thread_state.__x[reg_num] |= valued;
				}
			}
			else{
				if(reg_type == 'q' || reg_type == 'v'){
					if(value_str[0] != '{' || value_str[strlen(value_str) - 1] != '}'){
						cmdfunc_help("set", 0, error);
						return CMD_FAILURE;
					}

					if(strlen(value_str) == 2){
						cmdfunc_help("set", 0, error);
						return CMD_FAILURE;
					}
					
					/* Remove the brackets. */
					value_str[strlen(value_str) - 1] = '\0';
					memmove(value_str, value_str + 1, strlen(value_str));

					size_t value_str_len = strlen(value_str);

					char *hi_str = malloc(value_str_len + 1);
					char *lo_str = malloc(value_str_len + 1);

					memset(hi_str, '\0', value_str_len);
					memset(lo_str, '\0', value_str_len);

					for(int i=0; i<sizeof(long)*2; i++){
						char *space = strrchr(value_str, ' ');
						char *curbyte = NULL;

						if(space){
							curbyte = strdup(space + 1);
							
							/* Truncate what we've already processed. */
							space[0] = '\0';
						}
						else
							curbyte = strdup(value_str);
						
						unsigned int byte = strtol(curbyte, NULL, 0);

						if(i < sizeof(long)){
							lo_str = realloc(lo_str, strlen(lo_str) + strlen(curbyte) + 3);
							sprintf(lo_str, "%s%02x", lo_str, byte);
						}
						else{
							hi_str = realloc(hi_str, strlen(hi_str) + strlen(curbyte) + 3);
							sprintf(hi_str, "%s%02x", hi_str, byte);
						}

						free(curbyte);
					}

					long hi = strtoul(hi_str, NULL, 16);
					long lo = strtoul(lo_str, NULL, 16);

					/* Since this is a 128 bit "number", we have to split it
					 * up into two 64 bit pointers to correctly modify it.
					 */
					long *H = (long *)(&debuggee->neon_state.__v[reg_num]);
					long *L = (long *)(&debuggee->neon_state.__v[reg_num]) + 1;

					*H = hi;
					*L = lo;

					free(hi_str);
					free(lo_str);
				}
				else if(reg_type == 'd')
					debuggee->neon_state.__v[reg_num] = *(long *)&valuedf;
				else
					debuggee->neon_state.__v[reg_num] = *(int *)&valuef;
			}
		}

		free(value_str);

		debuggee->set_thread_state();
		debuggee->set_neon_state();
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_show(char *args, int arg1, char **error){
	if(!args){
		show_all_cvars();
		return CMD_SUCCESS;
	}

	char *tok = strtok(args, " ");

	while(tok){
		p_convvar(tok);
		tok = strtok(NULL, " ");
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_stepi(char *args, int arg1, char **error){
	if(debuggee->pid == -1)
		return CMD_FAILURE;
	
	if(debuggee->pending_messages > 0)
		reply_to_exception(debuggee->exc_request, KERN_SUCCESS);

	debuggee->get_debug_state();
	debuggee->debug_state.__mdscr_el1 |= 1;
	debuggee->set_debug_state();
	
	debuggee->want_single_step = 1;
	debuggee->is_single_stepping = 1;

	debuggee->resume();
	debuggee->interrupted = 0;

	if(*error)
		return CMD_FAILURE;

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_threadlist(char *args, int arg1, char **error){
	if(!debuggee)
		return CMD_FAILURE;

	if(!debuggee->threads)
		return CMD_FAILURE;
	
	if(!debuggee->threads->front)
		return CMD_FAILURE;
	
	struct node_t *current = debuggee->threads->front;

	while(current){
		struct machthread *t = current->data;

		printf("\t%sthread #%d, tid = %#llx, name = '%s', where = %#llx\n", t->focused ? "* " : "", t->ID, t->tid, t->tname, t->thread_state.__pc);
		
		current = current->next;
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_threadselect(char *args, int arg1, char **error){
	if(!args)
		return CMD_FAILURE;
	
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	if(!debuggee->threads)
		return CMD_FAILURE;

	if(!debuggee->threads->front)
		return CMD_FAILURE;

	int thread_id = (int)strtol_err((char *)args, error);

	if(*error)
		return CMD_FAILURE;

	if(thread_id < 1 || thread_id > debuggee->thread_count){
		asprintf(error, "out of bounds, must be in [1, %d]", debuggee->thread_count);
		return CMD_FAILURE;
	}

	/* Delete any single step breakpoints belonging to
	 * the other thread.
	 */
	if(debuggee->is_single_stepping){
		delete_ss_bps();

		debuggee->is_single_stepping = 0;

		debuggee->get_debug_state();
		debuggee->debug_state.__mdscr_el1 = 0;
		debuggee->set_debug_state();
	}

	int result = machthread_setfocusgivenindex(thread_id);
	
	if(result){
		asprintf(error, "could not set focused thread to thread %d", thread_id);
		return CMD_FAILURE;
	}

	printf("Selected thread #%d\n", thread_id);
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_trace(char *args, int arg1, char **error){
	if(debuggee->tracing_disabled){
		asprintf(error, "tracing is not supported on this host");
		return CMD_FAILURE;
	}
	
	if(debuggee->currently_tracing){
		asprintf(error, "already tracing");
		return CMD_FAILURE;
	}

	start_trace();
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_unset(char *args, int arg1, char **error){
	if(!args){
		asprintf(error, "need a convenience variable");
		return CMD_FAILURE;
	}
	
	char *tok = strtok(args, " ");

	while(tok){
		void_convvar(tok);
		tok = strtok(NULL, " ");
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_watch(char *args, int arg1, char **error){
	if(!args){
		cmdfunc_help("watch", 0, error);
		return CMD_FAILURE;
	}
	
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	char *tok = strtok(args, " ");

	if(!tok){
		cmdfunc_help("watch", 0, error);
		return CMD_FAILURE;
	}

	int LSC = WP_WRITE;

	/* Check if the user specified a watchpoint type. If they didn't,
	 * this watchpoint will match on reads and writes.
	 */
	if(!strstr(tok, "0x")){
		if(strcmp(tok, "--r") == 0)
			LSC = WP_READ;
		else if(strcmp(tok, "--w") == 0)
			LSC = WP_WRITE;
		else if(strcmp(tok, "--rw") == 0)
			LSC = WP_READ_WRITE;
		else{
			cmdfunc_help("watch", 0, error);
			return CMD_FAILURE;
		}
		
		tok = strtok(NULL, " ");

		if(!tok){
			cmdfunc_help("watch", 0, error);
			return CMD_FAILURE;
		}
	}

	long location = parse_expr(tok, error);

	if(*error){
		asprintf(error, "expression evaluation failed: %s\n", *error);
		return CMD_FAILURE;
	}

	tok = strtok(NULL, " ");
	
	if(!tok){
		cmdfunc_help("watch", 0, error);
		return CMD_FAILURE;
	}

	/* Base does not matter since watchpoint_at_address
	 * bails if data_len > sizeof(long)
	 */
	int data_len = (int)strtol_err(tok, error);

	if(*error)
		return CMD_FAILURE;

	wp_error_t err = watchpoint_at_address(location, data_len, LSC, error);

	if(err != WP_SUCCESS)
		return CMD_FAILURE;

	return CMD_SUCCESS;
}

cmd_error_t execute_command(char *input, char **errstr){
	if(!input)
		return CMD_FAILURE;

	/* Trim any whitespace at the beginning and the end of the command. */
	while(isspace(*input))
		input++;

	const char *end = input + (strlen(input) - 1);

	while(end > input && isspace(*end))
		end--;

	input[(end - input) + 1] = '\0';
	
	char *usercmd = malloc(strlen(input) + 1);
	strcpy(usercmd, input);

	char *token = strtok(usercmd, " ");

	/* If there's nothing here, bail out. */
	if(!token){
		free(usercmd);
		return CMD_FAILURE;
	}
	
	/* This is the what function we will call when
	 * we figure out what command the user wants.
	 * If this is still NULL by the end of this function,
	 * no suitable command was found.
	 */
	cmd_error_t (*finalfunc)(char *, int, char **) = NULL;

	int numcmds = sizeof(COMMANDS) / sizeof(struct dbg_cmd_t);
	
	/* First, check if the command is an alias. */
	for(int i=0; i<numcmds; i++){
		struct dbg_cmd_t *curcmd = &COMMANDS[i];

		/* If there is an alias, there's nothing left to do
		 * but to initialize finalfunc and call it.
		 */
		if(curcmd->alias && strcmp(curcmd->alias, token) == 0){
			finalfunc = curcmd->function;
			
			/* Anything after token will be an argument
			 * for this command. Add one to get past the
			 * space.
			 */
			/* Assume there we no arguments entered for safety. */
			char *args = NULL;

			/* If there were, initialize args with them, and add
			 * one to get past the space.
			 */
			size_t tokenlen = strlen(token);

			if(strlen(input) > tokenlen)
				args = (char *)input + tokenlen + 1;
		
			cmd_error_t result = finalfunc(args, 0, errstr);

			free(usercmd);
			
			return result;
		}
	}

	const int init_buf_sz = 16;
	
	/* This is what will hold the possible commands
	 * that the user's input could be.
	 */
	char *possible_cmds = malloc(init_buf_sz);
	memset(possible_cmds, '\0', init_buf_sz);

	/* This is what the final command will be. */
	char *finalcmd = malloc(init_buf_sz);
	memset(finalcmd, '\0', init_buf_sz);
	
	/* If it was not an alias, do the command matching. */
	int idx = 0;
	
	int ambiguous = 0;
	int guaranteed = 0;
	int num_matches = 0;
	
	/* The command will be parsed and appended to finalcmd when
	 * there is no ambiguity.
	 */
	char *potcmd = malloc(init_buf_sz);
	memset(potcmd, '\0', init_buf_sz);

	/* Save the previous command to match a function with a non-guaranteed
	 * ambiguous command.
	 */
	struct dbg_cmd_t *prevcmd = NULL;

	size_t pclen;

	while(token){
		struct dbg_cmd_t *cmd = &COMMANDS[idx];

		char *tempbuf = NULL;

		/* Check if this command is guaranteed to be ambiguous. If it
		 * is, tack it on and start back at the beginning.
		 */
		if(strlen(finalcmd) == 0 && !cmd->function && 
				strncmp(cmd->name, token, strlen(token)) == 0){
			/* Since this is guaranteed to be ambiguous, we don't know
			 * how many bytes the resulting command will take up.
			 */
			potcmd = realloc(potcmd, init_buf_sz + 64);
			sprintf(potcmd, "%s", cmd->name);

			finalcmd = realloc(finalcmd, strlen(cmd->name) + 1 + 1);
			sprintf(finalcmd, "%s", cmd->name);
			
			tempbuf = malloc(strlen(finalcmd) + 1);
			strcpy(tempbuf, finalcmd);
			
			guaranteed = 1;
		}
		else{
			/* Append token to finalcmd in a temporary buffer 
			 * so we can test for equality in cmd->name.
			 */
			if(guaranteed){
				tempbuf = malloc(strlen(finalcmd) + strlen(token) + 1 + 1);
				sprintf(tempbuf, "%s %s", finalcmd, token);
			}
			else{
				tempbuf = malloc(strlen(finalcmd) + strlen(token) + 1);
				sprintf(tempbuf, "%s%s", finalcmd, token);
			}
		}
		
		pclen = strlen(possible_cmds);
		
		/* Since tempbuf holds what we have so far + our token, now is a
		 * good time to check for ambiguity for guaranteed ambiguous commands,
		 * as well as if we have a match.
		 */
		if(guaranteed){
			int idxcpy = idx;
			struct dbg_cmd_t *cmdcpy = &COMMANDS[idxcpy];
			
			char *substr = strstr(cmdcpy->name, tempbuf);

			if(substr){
				/* Reset variables when we start a new ambiguity check. */
				num_matches = 0;
				memset(possible_cmds, '\0', strlen(possible_cmds));
			}

			while(substr){
				num_matches++;

				pclen = strlen(possible_cmds);
				size_t cpylen = strlen(cmdcpy->name);

				if(cmdcpy->function){
					size_t bufsz = pclen + cpylen + 2 + 1;
					
					possible_cmds = realloc(possible_cmds, bufsz);
					strcat(possible_cmds, cmdcpy->name);
					strcat(possible_cmds, ", ");
				}

				cmdcpy = &COMMANDS[++idxcpy];
				substr = strstr(cmdcpy->name, tempbuf);
			}

			/* We found a matching command, save its function. */
			if(num_matches == 1){
				finalfunc = cmd->function;
				token = strtok(NULL, " ");
				break;
			}
		}

		/* Check for any matches. If there are matches, update a few variables
		 * so we can use them to see if we should append any part of `potcmd`
		 * when there aren't.
		*/
		if(cmd->function && strncmp(cmd->name, tempbuf, strlen(tempbuf)) == 0){
			/* We have different ways of checking for ambiguity for guaranteed
			 * ambiguous commands and regular commands.
			 */
			if(!guaranteed){
				num_matches++;
				ambiguous = num_matches > 1;
			}
			
			size_t cmdlen = strlen(cmd->name);
			
			/* Keep track of possiblities from ambiguous input. */	
			if(!guaranteed){
				possible_cmds = realloc(possible_cmds, pclen + cmdlen + 3);
				
				strcat(possible_cmds, cmd->name);
				strcat(possible_cmds, ", ");
			}

			/* Keep track of any potential matches. */
			potcmd = realloc(potcmd, strlen(cmd->name) + 1);
			strcpy(potcmd, cmd->name);
			
			/* If this command is guaranteed ambiguous, it will have more
			 * than one word. Tack the next word of this command onto
			 * finalcmd.
			 */
			if(guaranteed){
				char *potcmd_copy = potcmd;
				potcmd_copy += strlen(finalcmd) + 1;

				char *space = strchr(potcmd_copy, ' ');

				int bytes = space ? space - potcmd_copy : strlen(potcmd_copy);
				potcmd_copy[bytes] = '\0';

				finalcmd = realloc(finalcmd, strlen(finalcmd) + 1 + bytes + 1);
				sprintf(finalcmd, "%s %s", finalcmd, potcmd_copy);
				
				token = strtok(NULL, " ");
				
				continue;
			}
		}
		else{
			if(ambiguous){
				/* Chop off the ", " at the end of this string. */
				possible_cmds[pclen - 2] = '\0';
				
				asprintf(errstr, "ambiguous command '%s': %s", input, possible_cmds);

				free(usercmd);
				free(possible_cmds);
				free(finalcmd);
				free(potcmd);

				return CMD_FAILURE;
			}

			finalcmd = realloc(finalcmd, strlen(potcmd) + 1);
			strcpy(finalcmd, potcmd);
			
			/* We found a matching command, save its function. */
			if(!guaranteed && strlen(finalcmd) > 0){
				finalfunc = prevcmd->function;
				token = strtok(NULL, " ");
				break;
			}
		}

		free(tempbuf);

		prevcmd = cmd;

		idx++;
		
		if(idx == numcmds){
			token = strtok(NULL, " ");
			idx = 0;
		}
	}

	free(finalcmd);
	free(potcmd);

	pclen = strlen(possible_cmds);

	/* We already handle ambiguity for commands that are not guaranteed
	 * to be ambiguous in the loop.
	 */
	if(num_matches > 1){
		if(pclen > 2)
			possible_cmds[pclen - 2] = '\0';

		asprintf(errstr, "ambiguous command '%s': %s", input, possible_cmds);
		
		return CMD_FAILURE;
	}

	free(possible_cmds);

	if(!finalfunc){
		asprintf(errstr, "unknown command '%s'", input);
		return CMD_FAILURE;
	}

	/* If we've found a good command, call its function.
	 * At this point, anything token contains is an argument. */
	if(!token)
		return finalfunc(NULL, 0, errstr);

	char *args = malloc(init_buf_sz);
	memset(args, '\0', init_buf_sz);

	while(token){
		args = realloc(args, strlen(args) + strlen(token) + 1 + 1);
		strcat(args, token);
		strcat(args, " ");
		
		token = strtok(NULL, " ");
	}

	/* Remove the trailing space from args. */
	args[strlen(args) - 1] = '\0';

	cmd_error_t result = finalfunc(args, 0, errstr);

	free(args);
	free(usercmd);
	
	return result;
}
