/*
Implementation for every command.
*/

#include "dbgcmd.h"

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

cmd_error_t cmdfunc_aslr(const char *args, int arg1){
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	printf("%#llx\n", debuggee->aslr_slide);
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_attach(const char *args, int arg1){
	if(!args){
		cmdfunc_help("attach", 0);
		return CMD_FAILURE;
	}

	if(strcmp(args, "iosdbg") == 0){
		printf("Not so fast\n");
		return CMD_FAILURE;
	}

	if(debuggee->pid != -1){
		char ans = answer("Detach from %s and reattach to %s? (y/n) ", debuggee->debuggee_name, args);

		if(ans == 'n')
			return CMD_SUCCESS;
		
		/* Detach from what we are attached to
		 * and call this function again.
		 */		
		cmdfunc_detach(NULL, 0);
		cmdfunc_attach(args, 0);

		return CMD_SUCCESS;
	}

	pid_t pid = pid_of_program((char *)args);

	if(pid == -1)
		return CMD_FAILURE;

	kern_return_t err = task_for_pid(mach_task_self(), pid, &debuggee->task);

	if(err){
		printf("attach: couldn't get task port for pid %d: %s\n", pid, mach_error_string(err));
		printf("Did you forget to sign iosdbg with entitlements?\n");
		return CMD_FAILURE;
	}
	
	err = debuggee->suspend();

	if(err){
		printf("attach: task_suspend call failed: %s\n", mach_error_string(err));
		return CMD_FAILURE;
	}
	
	debuggee->pid = pid;
	debuggee->interrupted = 1;
	
	debuggee->aslr_slide = debuggee->find_slide();
	
	debuggee->debuggee_name = malloc(strlen(args) + 1);
	memset(debuggee->debuggee_name, '\0', strlen(args) + 1);
	strcpy(debuggee->debuggee_name, args);
	
	printf("\nAttached to %s (pid: %d), slide: %#llx.\n", debuggee->debuggee_name, debuggee->pid, debuggee->aslr_slide);

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

	memutils_disassemble_at_location(debuggee->thread_state.__pc, 0x4, DISAS_DONT_SHOW_ARROW_AT_LOCATION_PARAMETER);

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_backtrace(const char *args, int arg1){
	if(debuggee->pid == -1)
		return CMD_FAILURE;
	
	// get FP register
	arm_thread_state64_t thread_state;
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;

	struct machthread *focused = machthread_getfocused();
	
	if(!focused){
		printf("We are not focused on any thread.\n");
		return CMD_FAILURE;
	}

	kern_return_t err = thread_get_state(focused->port, ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);

	if(err){
		printf("cmdfunc_backtrace: thread_get_state failed: %s\n", mach_error_string(err));
		return CMD_FAILURE;
	}

	// print frame 0, which is where we are currently at
	printf("  * frame #0: %#llx\n", thread_state.__pc);
	
	// frame 1 is what is in LR
	printf("     frame #1: %#llx\n", thread_state.__lr);

	int frame_counter = 2;

	// there's a linked list-like thing of frame pointers
	// so we can unwind the stack by following this linked list
	struct frame_t {
		struct frame_t *next;
		unsigned long long frame;
	};

	struct frame_t *current_frame = malloc(sizeof(struct frame_t));
	err = memutils_read_memory_at_location((void *)thread_state.__fp, current_frame, sizeof(struct frame_t));
	
	if(err){
		printf("Backtrace failed\n");
		return CMD_FAILURE;
	}

	while(current_frame->next){
		printf("     frame #%d: %#llx\n", frame_counter, current_frame->frame);

		memutils_read_memory_at_location((void *)current_frame->next, (void *)current_frame, sizeof(struct frame_t));	
		frame_counter++;
	}

	printf(" - cannot unwind past frame %d -\n", frame_counter);

	free(current_frame);

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_break(const char *args, int arg1){
	if(!args){
		cmdfunc_help("break", 0);
		return CMD_FAILURE;
	}

	if(debuggee->pid == -1)
		return CMD_FAILURE;

	char *tok = strtok((char *)args, " ");	
	
	long location = strtol(tok, NULL, 16);

	/* Check for --no-aslr. */
	tok = strtok(NULL, " ");
	
	if(tok && strcmp(tok, "--no-aslr") == 0){
		breakpoint_at_address(location, BP_NO_TEMP, BP_NO_SS);
		return CMD_SUCCESS;
	}

	breakpoint_at_address(location + debuggee->aslr_slide, BP_NO_TEMP, BP_NO_SS);

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_continue(const char *args, int arg1){
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	if(!debuggee->interrupted)
		return CMD_FAILURE;

	kern_return_t err = debuggee->resume();

	if(err){
		printf("continue: %s\n", mach_error_string(err));
		return CMD_FAILURE;
	}

	debuggee->interrupted = 0;

	rl_printf(RL_NO_REPROMPT, "Process %d resuming\n", debuggee->pid);

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_delete(const char *args, int arg1){
	if(debuggee->pid == -1)
		return CMD_FAILURE;
	
	if(!args){
		cmdfunc_help("delete", 0);
		return CMD_FAILURE;
	}

	char *tok = strtok((char *)args, " ");
	
	if(!tok){
		cmdfunc_help("delete", 0);
		return CMD_FAILURE;
	}
	
	char *type = malloc(strlen(tok) + 1);
	strcpy(type, tok);

	if(strcmp(type, "b") != 0 && strcmp(type, "w") != 0){
		cmdfunc_help("delete", 0);
		return CMD_FAILURE;
	}

	if(strcmp(type, "b") == 0 && debuggee->num_breakpoints == 0){
		printf("No breakpoints to delete\n");
		return CMD_SUCCESS;
	}

	if(strcmp(type, "w") == 0 && debuggee->num_watchpoints == 0){
		printf("No watchpoints to delete\n");
		return CMD_SUCCESS;
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

		return CMD_SUCCESS;
	}

	int id = atoi(tok);

	if(strcmp(type, "b") == 0){
		bp_error_t error = breakpoint_delete(id);

		if(error == BP_FAILURE){
			printf("Couldn't delete breakpoint\n");
			return CMD_FAILURE;
		}

		printf("Breakpoint %d deleted\n", id);
	}
	else if(strcmp(type, "w") == 0){
		wp_error_t error = watchpoint_delete(id);

		if(error == WP_FAILURE){
			printf("Couldn't delete watchpoint\n");
			return CMD_FAILURE;
		}

		printf("Watchpoint %d deleted\n", id);
	}
	else{
		cmdfunc_help("delete", 0);
		return CMD_FAILURE;
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_detach(const char *args, int from_death){
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	// delete all breakpoints on detach so the original instruction is written back to prevent a crash
	// TODO: instead of deleting them, maybe disable all of them and if we are attached to the same thing again re-enable them?
	breakpoint_delete_all();
	watchpoint_delete_all();

	/* Disable hardware single stepping. */
	debuggee->get_debug_state();
	debuggee->debug_state.__mdscr_el1 = 0;
	debuggee->set_debug_state();

	if(!from_death){
		debuggee->restore_exception_ports();

		if(debuggee->interrupted){
			cmd_error_t result = cmdfunc_continue(NULL, 0);

			if(result != CMD_SUCCESS){
				printf("detach: couldn't resume execution before we detach?\n");
				return CMD_FAILURE;
			}
		}
	}

	debuggee->interrupted = 0;

	linkedlist_free(debuggee->breakpoints);
	debuggee->breakpoints = NULL;

	linkedlist_free(debuggee->watchpoints);
	debuggee->watchpoints = NULL;

	linkedlist_free(debuggee->threads);
	debuggee->threads = NULL;

	if(!from_death)
		printf("Detached from %s (%d)\n", debuggee->debuggee_name, debuggee->pid);

	debuggee->pid = -1;
	debuggee->num_breakpoints = 0;
	debuggee->num_watchpoints = 0;

	free(debuggee->debuggee_name);
	debuggee->debuggee_name = NULL;
	
	debuggee->last_hit_bkpt_ID = 0;
	debuggee->last_hit_bkpt_hw = 0;
	
	debuggee->last_hit_wp_loc = 0;
	debuggee->last_hit_wp_PC = 0;

	debuggee->deallocate_ports();

	current_machthread_id = 1;

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_disassemble(const char *args, int arg1){
	if(!args){
		cmdfunc_help("disassemble", 0);
		return CMD_FAILURE;
	}

	char *tok = strtok((char *)args, " ");
	
	if(!tok){
		cmdfunc_help("disassemble", 0);
		return CMD_FAILURE;
	}

	// first thing is the location
	char *loc_str = malloc(strlen(tok) + 1);
	strcpy(loc_str, tok);

	unsigned long long location = strtoull(loc_str, NULL, 16);

	free(loc_str);

	// then the amount of instructions to disassemble
	tok = strtok(NULL, " ");

	if(!tok){
		cmdfunc_help("disassemble", 0);
		return CMD_FAILURE;
	}

	int base = 10;

	if(strstr(tok, "0x"))
		base = 16;
	
	int amount = strtol(tok, NULL, base);

	if(amount <= 0){
		cmdfunc_help("disassemble", 0);
		return CMD_FAILURE;
	}

	// finally, whether or not '--no-aslr' was given
	tok = strtok(NULL, " ");

	// if it is NULL, nothing is there, so add ASLR to the location
	if(!tok)
		location += debuggee->aslr_slide;

	kern_return_t err = memutils_disassemble_at_location(location, amount, DISAS_DONT_SHOW_ARROW_AT_LOCATION_PARAMETER);
	
	if(err){
		printf("Couldn't disassemble\n");
		return CMD_FAILURE;
	}
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_examine(const char *args, int arg1){
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	if(!args){
		cmdfunc_help("examine", 0);
		return CMD_FAILURE;
	}

	char *tok = strtok((char *)args, " ");
	
	if(!tok){
		cmdfunc_help("examine", 0);
		return CMD_FAILURE;
	}

	// first thing will be the location
	char *loc_str = malloc(strlen(tok) + 1);
	strcpy(loc_str, tok);
	
	int base = 10;

	if(strstr(loc_str, "0x"))
		base = 16;

	unsigned long long location = strtoull(loc_str, NULL, base);

	free(loc_str);

	// next thing will be however many bytes is wanted
	tok = strtok(NULL, " ");

	if(!tok){
		cmdfunc_help("examine", 0);
		return CMD_FAILURE;
	}

	char *amount_str = malloc(strlen(tok) + 1);
	strcpy(amount_str, tok);

	base = 10;

	if(strstr(amount_str, "0x"))
		base = 16;

	unsigned long amount = strtol(amount_str, NULL, base);

	free(amount_str);

	// check if --no-aslr was given
	tok = strtok(NULL, " ");

	kern_return_t ret;

	if(tok){
		if(strcmp(tok, "--no-aslr") == 0){
			ret = memutils_dump_memory_new(location, amount);
			
			if(ret)
				return CMD_FAILURE;
		}
		else{
			cmdfunc_help("examine", 0);
			return CMD_FAILURE;
		}

		return CMD_SUCCESS;
	}

	ret = memutils_dump_memory_new(location + debuggee->aslr_slide, amount);

	if(ret)
		return CMD_FAILURE;
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_help(const char *args, int arg1){
	if(!args){
		printf("Need the command\n");
		return CMD_FAILURE;
	}
	
	// it does not make sense for the command to be autocompleted here
	// so just search through the command table until we find the argument
	int num_cmds = sizeof(COMMANDS) / sizeof(struct dbg_cmd_t);
	int cur_cmd_idx = 0;

	while(cur_cmd_idx < num_cmds){
		struct dbg_cmd_t *cmd = &COMMANDS[cur_cmd_idx];
	
		// must not be an ambigious command
		if(strcmp(cmd->name, args) == 0 && cmd->function){
			printf("\t%s\n", cmd->desc);
			return CMD_SUCCESS;
		}

		cur_cmd_idx++;
	}
	
	// not found
	return CMD_FAILURE;
}

cmd_error_t cmdfunc_kill(const char *args, int arg1){
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	if(!debuggee->debuggee_name)
		return CMD_FAILURE;

	char *saved_name = malloc(strlen(debuggee->debuggee_name) + 1);
	strcpy(saved_name, debuggee->debuggee_name);

	cmdfunc_detach(NULL, 0);
	
	// the kill system call panics all my devices
	pid_t p;
	char *argv[] = {"killall", "-9", saved_name, NULL};
	int status = posix_spawnp(&p, "killall", NULL, NULL, (char * const *)argv, NULL);
	
	free(saved_name);

	if(status == 0)
		waitpid(p, &status, 0);
	else{
		printf("posix_spawnp failed\n");
		return CMD_FAILURE;
	}
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_quit(const char *args, int arg1){
	if(debuggee->pid != -1)
		cmdfunc_detach(NULL, 0);

	free(debuggee);
	exit(0);
}

cmd_error_t cmdfunc_regsfloat(const char *args, int arg1){
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	if(!args){
		printf("Register?\n");
		return CMD_FAILURE;
	}

	// Iterate through and show all the registers the user asked for
	char *tok = strtok((char *)args, " ");

	while(tok){
		char reg_type = tok[0];
		// move up a byte for the register number
		tok++;
		int reg_num = atoi(tok);

		if(reg_num < 0 || reg_num > 31)
			continue;

		arm_neon_state64_t neon_state;
		mach_msg_type_number_t count = ARM_NEON_STATE64_COUNT;
		
		struct machthread *focused = machthread_getfocused();

		if(!focused){
			printf("We are not focused on any thread.\n");
			return CMD_FAILURE;
		}

		kern_return_t err = thread_get_state(focused->port, ARM_NEON_STATE64, (thread_state_t)&neon_state, &count);

		if(err){
			printf("show_neon_registers: thread_get_state failed: %s\n", mach_error_string(err));
			return CMD_FAILURE;
		}

		union intfloat {
			int i;
			float f;
		} IF;

		if(reg_type == 'v'){
			// TODO figure this out...
			// print each byte in this 128 bit integer (16)
		
			//void *upper = neon_state.__v[reg_num] >> 64;
			//void *lower = neon_state.__v[reg_num] << 64;
			
			//memutils_dump_memory_from_location(upper, 8, 8, 16);
			//memutils_dump_memory_from_location(lower, 8, 8, 16);	

		//	printf("%llx %llx\n", upper, lower);

		}
		else if(reg_type == 'd'){
			// D registers, bottom 64 bits of each Q register
			IF.i = neon_state.__v[reg_num] >> 32;
			printf("D%d 				%f\n", reg_num, IF.f);
		}
		else if(reg_type == 's'){
			// S registers, bottom 32 bits of each Q register
			IF.i = neon_state.__v[reg_num] & 0xFFFFFFFF;
			printf("S%d\t\t\t%f (0x%x)\n", reg_num, IF.f, IF.i);
		}
	
		tok = strtok(NULL, " ");
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_regsgen(const char *args, int arg1){
	if(debuggee->pid == -1)
		return CMD_FAILURE;
	
	arm_thread_state64_t thread_state;
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
	
	struct machthread *focused = machthread_getfocused();

	if(!focused){
		printf("We are not focused on any thread.\n");
		return CMD_FAILURE;
	}

	kern_return_t err = thread_get_state(focused->port, ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);

	if(err){
		printf("Failed\n");
		return CMD_FAILURE;
	}
	
	// if there were no arguments, print every register
	if(!args){
		for(int i=0; i<29; i++)
			printf("X%d\t\t\t%#llx\n", i, thread_state.__x[i]);
		
		printf("FP\t\t\t%#llx\n", thread_state.__fp);
		printf("LR\t\t\t%#llx\n", thread_state.__lr);
		printf("SP\t\t\t%#llx\n", thread_state.__sp);
		printf("PC\t\t\t%#llx\n", thread_state.__pc);
		printf("CPSR\t\t\t%#x\n", thread_state.__cpsr);

		return CMD_SUCCESS;
	}

	// otherwise, print every register they asked for
	char *tok = strtok((char *)args, " ");

	while(tok){
		if(tok[0] != 'x'){
			tok = strtok(NULL, " ");
			continue;
		}

		// move up one byte to get to the "register number"
		tok++;
		int reg_num = atoi(tok);
		
		if(reg_num < 0 || reg_num > 29){
			tok = strtok(NULL, " ");
			continue;
		}

		printf("x%d\t\t\t%#llx\n", reg_num, thread_state.__x[reg_num]);

		tok = strtok(NULL, " ");
	}
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_set(const char *args, int arg1){
	if(!args){
		cmdfunc_help("set", 0);
		return CMD_FAILURE;
	}
	
	// check for offset
	if(args[0] == '*'){
		// move past the '*'
		args++;
		args = strtok((char *)args, " ");
	
		// get the location, an equals sign follows it
		char *location_str = malloc(64);
		char *equals = strchr(args, '=');

		if(!equals){
			printf("\t * No new value\n\n");
			cmdfunc_help("set", 0);
			return CMD_FAILURE;
		}

		strncpy(location_str, args, equals - args);

		char *zero_x = strstr(location_str, "0x");
		if(!zero_x){
			printf("\t * Need '0x' before location\n\n");
			cmdfunc_help("set", 0);
			return CMD_FAILURE;
		}

		// TODO allow math on the location and the value

		int base = 16;
		unsigned long long location = strtoll(location_str, NULL, base);

		// find out what they want the location set to
		char *value_str = malloc(64);

		// equals + 1 to get past the actual equals sign
		strcpy(value_str, equals + 1);
		
		if(strlen(value_str) == 0){
			printf("Need a value\n");
			return CMD_FAILURE;
		}

		// see how they want their new value interpreted
		int value_base = 16;

		// no "0x", so base 10
		if(!strstr(value_str, "0x"))
			value_base = 10;

		unsigned long long value = strtoll(value_str, NULL, value_base);

		location += debuggee->aslr_slide;

		args = strtok(NULL, " ");
		if(args && strstr(args, "--no-aslr"))
			location -= debuggee->aslr_slide;

		kern_return_t result = memutils_write_memory_to_location((vm_address_t)location, (vm_offset_t)value);
		
		if(result){
			printf("Error: %s\n", mach_error_string(result));
			return CMD_FAILURE;
		}
	}

	// if they're not modifing an offset, they're setting a config variable
	// to be implemented
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_stepi(const char *args, int arg1){
	if(!debuggee->interrupted){
		printf("Debuggee must be suspended\n");
		return CMD_FAILURE;
	}

	debuggee->get_debug_state();
	debuggee->debug_state.__mdscr_el1 |= 1;
	debuggee->set_debug_state();
	
	debuggee->want_single_step = 1;

	debuggee->resume();
	debuggee->interrupted = 0;

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_threadlist(const char *args, int arg1){
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

cmd_error_t cmdfunc_threadselect(const char *args, int arg1){
	if(!args)
		return CMD_FAILURE;
	
	if(debuggee->pid == -1)
		return CMD_FAILURE;

	if(!debuggee->threads)
		return CMD_FAILURE;

	if(!debuggee->threads->front)
		return CMD_FAILURE;

	int thread_id = atoi(args);

	if(thread_id < 1 || thread_id > debuggee->thread_count){
		printf("Out of bounds, must be in between [1, %d]\n", debuggee->thread_count);
		printf("Threads:\n");
		cmdfunc_threadlist(NULL, 0);
		return CMD_FAILURE;
	}

	int result = machthread_setfocusgivenindex(thread_id);
	
	if(result){
		printf("Failed");
		return CMD_FAILURE;
	}

	printf("Selected thread %d\n", thread_id);
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_watch(const char *args, int arg1){
	if(!args){
		cmdfunc_help("watch", 0);
		return CMD_FAILURE;
	}
	
	char *tok = strtok((char *)args, " ");

	if(!tok){
		cmdfunc_help("watch", 0);
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
			cmdfunc_help("watch", 0);
			return CMD_FAILURE;
		}
		
		tok = strtok(NULL, " ");

		if(!tok){
			cmdfunc_help("watch", 0);
			return CMD_FAILURE;
		}
	}

	long location = strtol(tok, NULL, 16);
	
	tok = strtok(NULL, " ");
	
	if(!tok){
		cmdfunc_help("watch", 0);
		return CMD_FAILURE;
	}

	/* Base does not matter since watchpoint_at_address
	 * bails if data_len > sizeof(long)
	 */
	int data_len = strtol(tok, NULL, 16);

	watchpoint_at_address(location, data_len, LSC);

	return CMD_SUCCESS;
}

cmd_error_t execute_command(char *input){
	/* Trim any whitespace at the beginning and the end of the command. */
	while(isspace(*input))
		input++;

	if(!input)
		return CMD_FAILURE;

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
	Function *finalfunc = NULL;

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
		
			finalfunc(args, 0);

			free(usercmd);
			
			return CMD_SUCCESS;
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
				
				printf("Ambiguous command '%s': %s\n", input, possible_cmds);

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

		printf("Ambiguous command '%s': %s\n", input, possible_cmds);
		
		return CMD_FAILURE;
	}

	free(possible_cmds);

	if(!finalfunc){
		printf("Unknown command '%s'\n", input);
		return CMD_FAILURE;
	}

	/* If we've found a good command, call its function.
	 * At this point, anything token contains is an argument. */
	if(!token)
		return finalfunc(NULL, 0);

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

	cmd_error_t result = finalfunc(args, 0);

	free(args);
	free(usercmd);
	
	return result;
}
