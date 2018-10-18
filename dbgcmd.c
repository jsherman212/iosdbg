/*
Implementation for every command.
*/

#include "dbgcmd.h"

cmd_error_t cmdfunc_attach(const char *args, int arg1){
	if(debuggee->pid != -1)
		return CMD_FAILURE;

	pid_t pid = pid_of_program((char *)args);

	if(pid == -1)
		return CMD_FAILURE;

	kern_return_t err = task_for_pid(mach_task_self(), pid, &debuggee->task);

	if(err){
		printf("attach: couldn't get task port for pid %d: %s\n", pid, mach_error_string(err));
		return CMD_FAILURE;
	}

	err = task_suspend(debuggee->task);

	if(err){
		printf("attach: task_suspend call failed: %s\n", mach_error_string(err));
		return CMD_FAILURE;
	}

	int result = suspend_threads();

	if(result != 0){
		printf("attach: couldn't suspend threads for %d while attaching, detaching...\n", debuggee->pid);

		cmdfunc_detach(NULL, 0);

		return CMD_FAILURE;
	}

	debuggee->pid = pid;
	debuggee->interrupted = 1;

	vm_region_basic_info_data_64_t info;
	vm_address_t address = 0;
	vm_size_t size;
	mach_port_t object_name;
	mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
	
	err = vm_region_64(debuggee->task, &address, &size, VM_REGION_BASIC_INFO, (vm_region_info_t)&info, &info_count, &object_name);

	if(err){
		printf("attach: vm_region_64: %s, detaching\n", mach_error_string(err));

		cmdfunc_detach(NULL, 0);

		return CMD_FAILURE;
	}

	debuggee->aslr_slide = address - 0x100000000;

	printf("Attached to %d, ASLR slide is 0x%llx. Do not worry about adding ASLR to addresses, it is already accounted for.\n", debuggee->pid, debuggee->aslr_slide);

	debuggee->breakpoints = linkedlist_new();

	// TODO maybe add a cool little thing like 0x000000018078cfd8 in debuggee

	setup_exception_handling();

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_aslr(const char *args, int arg1){
	printf("Debuggee ASLR slide: 0x%llx\n", debuggee->aslr_slide);
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_backtrace(const char *args, int arg1){
	

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_break(const char *args, int arg1){
	if(!args){
		printf("Location?\n");
		return CMD_FAILURE;
	}

	char *tok = strtok((char *)args, " ");

	while(tok){
		unsigned long long location = strtoul(tok, NULL, 16);
		breakpoint_at_address(location);

		tok = strtok(NULL, " ");
	}
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_continue(const char *args, int arg1){
	if(!debuggee->interrupted)
		return CMD_FAILURE;

	kern_return_t err = task_resume(debuggee->task);

	if(err){
		printf("resume: couldn't continue: %s\n", mach_error_string(err));
		return CMD_FAILURE;
	}

	resume_threads();

	debuggee->interrupted = 0;

	printf("Continuing.\n");

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_delete(const char *args, int arg1){
	if(!args){
		printf("We need a breakpoint ID\n");
		return CMD_FAILURE;
	}
	
	int error = breakpoint_delete(atoi(args));

	if(error){
		printf("Couldn't set breakpoint\n");
		return CMD_FAILURE;
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_detach(const char *args, int from_death){
	// delete all breakpoints on detach so the original instruction is written back to prevent a crash
	// TODO: instead of deleting them, maybe disable all of them and if we are attached to the same thing again re-enable them?
	breakpoint_delete_all();

	if(!from_death){
		// restore original exception ports
		for(mach_msg_type_number_t i=0; i<debuggee->original_exception_ports.count; i++){
			kern_return_t err = task_set_exception_ports(debuggee->task, debuggee->original_exception_ports.masks[i], debuggee->original_exception_ports.ports[i], debuggee->original_exception_ports.behaviors[i], debuggee->original_exception_ports.flavors[i]);
			
			if(err)
				printf("detach: task_set_exception_ports: %s, %d\n", mach_error_string(err), i);
		}

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

	printf("Detached from %d\n", debuggee->pid);

	debuggee->pid = -1;

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_regsfloat(const char *args, int arg1){
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

		kern_return_t err = thread_get_state(debuggee->threads[0], ARM_NEON_STATE64, (thread_state_t)&neon_state, &count);

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
		
			unsigned long long upper = neon_state.__v[reg_num] << 64;
			unsigned long long lower  = neon_state.__v[reg_num];
			
			

			printf("%llx %llx\n", upper, lower);

		}
		else if(reg_type == 'd'){
			// D registers, bottom 64 bits of each Q register
			IF.i = neon_state.__v[reg_num] >> 32;
			printf("D%d 				%f\n", reg_num, IF.f);
		}
		else if(reg_type == 's'){
			// S registers, bottom 32 bits of each Q register
			IF.i = neon_state.__v[reg_num] & 0xFFFFFFFF;
			printf("S%d 				%f (0x%x)\n", reg_num, IF.f, IF.i);
		}
	
		tok = strtok(NULL, " ");
	}

	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_regsgen(const char *args, int arg1){
	arm_thread_state64_t thread_state;
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;

	kern_return_t err = thread_get_state(debuggee->threads[0], ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);

	if(err){
		printf("show_general_registers: thread_get_state failed: %s\n", mach_error_string(err));
		return CMD_FAILURE;
	}
	
	// if there were no arguments, print every register
	if(!args){
		for(int i=0; i<29; i++)
			printf("X%d                 0x%llx\n", i, thread_state.__x[i]);
		
		printf("FP 				0x%llx\n", thread_state.__fp);
		printf("LR 				0x%llx\n", thread_state.__lr);
		printf("SP 				0x%llx\n", thread_state.__sp);
		printf("PC 				0x%llx\n", thread_state.__pc);
		printf("CPSR 				0x%x\n", thread_state.__cpsr);

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

		printf("X%d                 0x%llx\n", reg_num, thread_state.__x[reg_num]);

		tok = strtok(NULL, " ");
	}
	
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_kill(const char *args, int arg1){
	cmdfunc_detach(NULL, 0);
	kill(debuggee->pid, SIGKILL);
	return CMD_SUCCESS;
}

cmd_error_t cmdfunc_help(const char *args, int arg1){
	// it does not make sense for the command to be autocompleted here
	// so just search through the command table until we find the argument
	int num_cmds = sizeof(COMMANDS) / sizeof(struct dbg_cmd_t);
	int cur_cmd_idx = 0;

	while(cur_cmd_idx < num_cmds){
		struct dbg_cmd_t *cmd = &COMMANDS[cur_cmd_idx];
	
		// must not be an ambigious command
		if(strcmp(cmd->name, args) == 0 && cmd->function){
			printf("	%s\n", cmd->desc);
			return CMD_SUCCESS;
		}

		cur_cmd_idx++;
	}
	
	// not found
	return CMD_FAILURE;
}

cmd_error_t cmdfunc_quit(const char *args, int arg1){
	if(debuggee->pid != -1)
		cmdfunc_detach(NULL, 0);

	free(debuggee);
	exit(0);
}

cmd_error_t cmdfunc_set(const char *args, int arg1){
	printf("\nTODO\n");
	return CMD_SUCCESS;
}

// Given user input, autocomplete their command and find the arguments.
// If the command is valid, call the function pointer from the correct command struct.
cmd_error_t execute_command(char *user_command){
	int num_commands = sizeof(COMMANDS) / sizeof(struct dbg_cmd_t);

	// make a copy of the parameters so we don't modify them
	char *user_command_copy = malloc(1024);
	strcpy(user_command_copy, user_command);

	char *token = strtok(user_command_copy, " ");
	if(!token)
		return CMD_FAILURE;

	// this string will hold all the arguments for the command passed in
	// if it is still NULL by the time we exit the main loop, there were no commands
	char *cmd_args = NULL;

	char *piece = malloc(1024);
	strcpy(piece, token);

	struct cmd_match_result_t *current_result = malloc(sizeof(struct cmd_match_result_t));
	current_result->num_matches = 0;
	current_result->match = NULL;
	current_result->matched_cmd = NULL;
	current_result->matches = malloc(1024);
	current_result->ambigious = 0;
	current_result->perfect = 0;

	// will hold the best command we've found
	// this compensates for command arguments being included
	// in the command string we're testing for
	struct cmd_match_result_t *final_result = NULL;

	while(token){
		int cur_cmd_idx = 0;
		struct dbg_cmd_t *cmd = &COMMANDS[cur_cmd_idx];

		char *prev_piece = NULL;

		while(cur_cmd_idx < num_commands){
			int use_prev_piece = prev_piece != NULL;
			int piecelen = strlen(use_prev_piece ? prev_piece : piece);
			
			// check if the command is an alias
			// check when a command is shorter than another command
			if((cmd->function && strcmp(cmd->name, piece) == 0) || (cmd->alias && strcmp(cmd->alias, user_command_copy) == 0)){
				final_result = malloc(sizeof(struct cmd_match_result_t));

				final_result->num_matches = 1;
				final_result->match = malloc(1024);
				strcpy(final_result->match, cmd->name);
				final_result->matches = NULL;
				final_result->matched_cmd = cmd;
				final_result->perfect = 1;

				break;
			}

			if(strncmp(cmd->name, use_prev_piece ? prev_piece : piece, piecelen) == 0){
				current_result->num_matches++;

				// guaranteed ambigious command
				if(!cmd->function){
					if(current_result->match)
						current_result->match = NULL;
					
					// strlen(cmd->name) + ' ' + '\0'
					char *updated_piece = malloc(strlen(cmd->name) + 1 + 1);
					strcpy(updated_piece, cmd->name);
					strcat(updated_piece, " ");

					strcpy(piece, updated_piece);

					free(updated_piece);

					current_result->ambigious = 1;
				}
				else
					current_result->ambigious = current_result->num_matches > 1 ? 1 : 0;

				// tack on any other matches we've found
				if(cmd->function){
					strcat(current_result->matches, cmd->name);
					strcat(current_result->matches, ", ");
				}

				if(current_result->num_matches == 1){
					// strlen(cmd->name) + ' ' + '\0'
					char *updated_piece = malloc(strlen(cmd->name) + 1 + 1);
					
					// find the end of the current word in the command
					char *cmdname_copy = cmd->name;

					// advance to where piece leaves off
					cmdname_copy += strlen(piece);

					// find the nearest space after that
					char *space = strchr(cmdname_copy, ' ');

					if(space){
						// if we found a space, we need to know how many bytes of cmd->name to copy
						// because we aren't at the end of this command yet
						int byte_amount = space - cmdname_copy;

						strncpy(updated_piece, cmd->name, strlen(piece) + byte_amount);
					}
					else
						// otherwise, we've reached the end of the command
						// and it's safe to copy the entire thing
						strcpy(updated_piece, cmd->name);
					
					strcat(updated_piece, " ");

					// we need to check for ambiguity but we are modifing piece
					// make a backup and use this in the strncmp call when it is not NULL
					if(!prev_piece)
						prev_piece = malloc(1024);

					strcpy(prev_piece, piece);
					strcpy(piece, updated_piece);

					free(updated_piece);
					
					if(!current_result->match)
						current_result->match = malloc(1024);

					strcpy(current_result->match, cmd->name);
					strcpy(current_result->matches, current_result->match);
					strcat(current_result->matches, ", ");

					final_result = current_result;
					final_result->matched_cmd = cmd;
				}

				if(current_result->ambigious){
					if(current_result->match){
						free(current_result->match);
						current_result->match = NULL;
					}

					if(final_result)
						final_result->matched_cmd = NULL;
				}
			}

			cur_cmd_idx++;
			cmd = &COMMANDS[cur_cmd_idx];
		}

		cur_cmd_idx = 0;

		token = strtok(NULL, " ");

		if(token && (final_result && !final_result->perfect)){
			if(!current_result->ambigious)
				strcat(piece, token);
			else if(token && current_result->ambigious){
				// find the last space in the command string we're building
				char *lastspace = strrchr(piece, ' ');

				// append the next piece to the command string
				if(lastspace){
					char *updated_piece = malloc(strlen(piece) + strlen(token) + 1);
					strcpy(updated_piece, piece);
					strcat(updated_piece, token);
					strcpy(piece, updated_piece);

					free(updated_piece);
				}
			}
		}

		// once final_result->match is not NULL, we've found a match
		// this means we can assume anything `token` contains is an argument
		if(token && final_result && final_result->match){
			cmd_args = malloc(1024);
			strcat(cmd_args, token);
			strcat(cmd_args, " ");
		}

		current_result->num_matches = 0;
	}

	if(final_result){
		// trim the extra comma and space from the ambiguous commands string
		if(final_result->matches)
			final_result->matches[strlen(final_result->matches) - 2] = '\0';

		if(!final_result->match)
			printf("Ambigious command '%s': %s\n", user_command, final_result->matches);

		// do the same with the cmd_args string
		if(cmd_args && strlen(cmd_args) > 0)
			cmd_args[strlen(cmd_args) - 1] = '\0';

		if(final_result->matched_cmd && final_result->matched_cmd->function)
			final_result->matched_cmd->function(cmd_args, 0);
	}
	else
		printf("Unknown command '%s'\n", user_command);

	free(current_result->match);
	free(current_result->matches);
	free(current_result);
	free(piece);
	free(user_command_copy);

	return CMD_SUCCESS;
}
