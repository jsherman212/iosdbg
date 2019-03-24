#include <dlfcn.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <readline/readline.h>

#include "breakpoint.h"
#include "convvar.h"
#include "dbgcmd.h"         /* Includes argparse.h */
#include "dbgutils.h"
#include "exception.h"      /* Includes defs.h */
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

pid_t parse_pid(char *pidstr, char **err){
    return is_number(pidstr) ? (pid_t)strtol_err(pidstr, err) 
        : pid_of_program(pidstr, err);
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

#define NUM_CMDS 25
struct dbg_cmd_t COMMANDS[NUM_CMDS];

cmd_error_t help_internal(char *cmd_name){
    int num_cmds = sizeof(COMMANDS) / sizeof(struct dbg_cmd_t);
    int cur_cmd_idx = 0;

    while(cur_cmd_idx < num_cmds){
        struct dbg_cmd_t *cmd = &COMMANDS[cur_cmd_idx];
    
        /* Must not be an ambiguous command. */
        if(strcmp(cmd->name, cmd_name) == 0 && cmd->function){
            printf("%s", cmd->desc);
            return CMD_SUCCESS;
        }

        cur_cmd_idx++;
    }

    return CMD_FAILURE;
}

cmd_error_t cmdfunc_aslr(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "not attached to anything");
        return CMD_FAILURE;
    }

    printf("%7s%#llx\n", "", debuggee->aslr_slide);
    
    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_attach(struct cmd_args_t *args, 
        int arg1, char **error){
    if(!args){
        asprintf(error, "need target");
        return CMD_FAILURE;
    }

    /* First argument could either be '--waitfor' or what the user
     * wants to attach to.
     */
    char *firstarg = argnext(args);

    if(!firstarg){
        asprintf(error, "need target");
        return CMD_FAILURE;
    }

    int waitfor = strcmp(firstarg, "--waitfor") == 0;

    /* Check if the user wants to attach to something else while attached
     * to something.
     */
    if(debuggee->pid != -1){
        /* If we got '--waitfor' as the first argument, whatever the user
         * wants to attach to will be next.
         */
        char *target = firstarg;

        if(waitfor)
            target = argnext(args);

        if(!target){
            asprintf(error, "need target");
            return CMD_FAILURE;
        }

        char ans = answer("Detach from %s and reattach to %s? (y/n) ", 
                debuggee->debuggee_name, target);

        if(ans == 'n')
            return CMD_SUCCESS;

        if(strcmp(target, "0") == 0){
            asprintf(error, "no kernel debugging");
            return CMD_FAILURE;
        }

        /* Detach from what we are attached to
         * and call this function again.
         */     
        cmdfunc_detach(NULL, 0, error);

        /* Re-construct the argument queue for the next call. */
        enqueue(args->argqueue, firstarg);
        enqueue(args->argqueue, target);

        return cmdfunc_attach(args, 0, error);
    }

    char *target = firstarg;
    
    if(waitfor)
        target = argnext(args);

    if(!target){
        asprintf(error, "need target");
        return CMD_FAILURE;
    }

    if(strcmp(target, "iosdbg") == 0){
        asprintf(error, "cannot attach to myself");
        return CMD_FAILURE;
    }

    pid_t target_pid;

    /* Check for '--waitfor', and if we have it,
     * constantly check if this process has launched.
     */
    if(waitfor){
        if(is_number(target)){
            asprintf(error, "cannot wait for PIDs");
            return CMD_FAILURE;
        }

        printf("Waiting for process '%s' to launch (Ctrl+C to stop)\n\n", 
                target);

        /* If we're waiting for something to launch, it will not exist,
         * so don't return on error.
         */
        target_pid = parse_pid(target, error);
        keep_checking_for_process = 1;

        while(target_pid == -1 && keep_checking_for_process){
            target_pid = parse_pid(target, error);

            *error = NULL;

            usleep(400);
        }

        keep_checking_for_process = 0;
    }
    else
        target_pid = parse_pid(target, error);

    if(*error)
        return CMD_FAILURE;

    if(target_pid == 0){
        asprintf(error, "no kernel debugging");
        return CMD_FAILURE;
    }

    if(target_pid == -1)
        return CMD_FAILURE;

    kern_return_t err = task_for_pid(mach_task_self(), 
            target_pid, &debuggee->task);

    if(err){
        asprintf(error, "couldn't get task port for %s (pid: %d): %s\n"
                "Did you forget to sign iosdbg with entitlements?", 
                target, target_pid, mach_error_string(err));
        return CMD_FAILURE;
    }

    debuggee->pid = target_pid;
    debuggee->aslr_slide = debuggee->find_slide();
    
    if(is_number(target)){
        char *name = progname_from_pid(debuggee->pid, error);

        if(*error)
            return CMD_FAILURE;

        debuggee->debuggee_name = strdup(name);

        free(name);
    }
    else
        debuggee->debuggee_name = strdup(target);

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

    printf("Attached to %s (pid: %d), slide: %#llx.\n",
            debuggee->debuggee_name, debuggee->pid, debuggee->aslr_slide);

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

cmd_error_t cmdfunc_backtrace(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "not attached to anything");
        return CMD_FAILURE;
    }
    
    debuggee->get_thread_state();

    printf("  * frame #0: 0x%16.16llx\n", debuggee->thread_state.__pc);
    printf("    frame #1: 0x%16.16llx\n", debuggee->thread_state.__lr);

    int frame_counter = 2;

    /* There's a linked list of frame pointers. */
    struct frame_t {
        struct frame_t *next;
        unsigned long long frame;
    };

    struct frame_t *current_frame = malloc(sizeof(struct frame_t));
    kern_return_t err = read_memory_at_location(
            (void *)debuggee->thread_state.__fp, current_frame, 
            sizeof(struct frame_t));
    
    if(err){
        asprintf(error, "backtrace failed: %s", mach_error_string(err));
        return CMD_FAILURE;
    }

    while(current_frame->next){
        printf("    frame #%d: 0x%16.16llx\n", frame_counter, 
                current_frame->frame);

        read_memory_at_location((void *)current_frame->next, 
                (void *)current_frame, sizeof(struct frame_t)); 
        frame_counter++;
    }

    printf(" - cannot unwind past frame %d -\n", frame_counter - 1);

    free(current_frame);

    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_break(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "not attached to anything");
        return CMD_FAILURE;
    }

    if(!args){
        help_internal("break");
        return CMD_FAILURE;
    }

    char *location_str = argnext(args);
    
    if(!location_str){
        asprintf(error, "need location");
        return CMD_FAILURE;
    }

    long location = parse_expr(location_str, error);

    if(*error){
        asprintf(error, "expression evaluation failed: %s", *error);
        return CMD_FAILURE;
    }

    if(args->add_aslr)
        location += debuggee->aslr_slide;

    return breakpoint_at_address(location, BP_NO_TEMP, error);    
}

cmd_error_t cmdfunc_continue(struct cmd_args_t *args, 
        int do_not_print_msg, char **error){
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

cmd_error_t cmdfunc_delete(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->pid == -1)
        return CMD_FAILURE;
    
    if(!args){
        help_internal("delete");
        return CMD_FAILURE;
    }

    char *type = argnext(args);

    if(!type){
        asprintf(error, "need type");
        return CMD_FAILURE;
    }

    if(strcmp(type, "b") != 0 && strcmp(type, "w") != 0){
        asprintf(error, "unknown type '%s'", type);
        return CMD_FAILURE;
    }

    if(strcmp(type, "b") == 0 && debuggee->num_breakpoints == 0){
        asprintf(error, "no breakpoints to delete");
        return CMD_FAILURE;
    }

    if(strcmp(type, "w") == 0 && debuggee->num_watchpoints == 0){
        asprintf(error, "no watchpoints to delete");
        return CMD_FAILURE;
    }
    
    char *delete_id_str = argnext(args);

    /* If there's nothing after type, give the user
     * an option to delete all.
     */
    if(!delete_id_str){
        const char *target = strcmp(type, "b") == 0 ? 
            "breakpoints" : "watchpoints";
        
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

    int delete_id = (int)strtol_err(delete_id_str, error);

    if(*error)
        return CMD_FAILURE;

    if(strcmp(type, "b") == 0){
        bp_error_t err = breakpoint_delete(delete_id);

        if(err == BP_FAILURE){
            asprintf(error, "couldn't delete breakpoint %d", delete_id);
            return CMD_FAILURE;
        }

        printf("Breakpoint %d deleted\n", delete_id);
    }
    else if(strcmp(type, "w") == 0){
        wp_error_t err = watchpoint_delete(delete_id);

        if(err == WP_FAILURE){
            asprintf(error, "couldn't delete watchpoint %d", delete_id);
            return CMD_FAILURE;
        }

        printf("Watchpoint %d deleted\n", delete_id);
    }

    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_detach(struct cmd_args_t *args, 
        int from_death, char **error){
    if(debuggee->pid == -1)
        return CMD_FAILURE;

    if(!debuggee->tracing_disabled)
        stop_trace();
    
    debuggee->want_detach = 1;

    void_convvar("$_");
    void_convvar("$__");

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
        int status = posix_spawnp(&p, "kill", NULL, NULL, 
                (char * const *)stop_argv, NULL);
            
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
        status = posix_spawnp(&p, "kill", NULL, NULL, 
                (char * const *)cont_argv, NULL);

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
    
    debuggee->last_hit_wp_loc = 0;
    debuggee->last_hit_wp_PC = 0;

    debuggee->deallocate_ports();

    if(!from_death)
        printf("Detached from %s (%d)\n", debuggee->debuggee_name, debuggee->pid);

    debuggee->pid = -1;

    free(debuggee->debuggee_name);
    debuggee->debuggee_name = NULL;

    debuggee->want_detach = 0;

    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_disassemble(struct cmd_args_t *args, 
        int arg1, char **error){
    if(!args){
        help_internal("disassemble");
        return CMD_FAILURE;
    }

    char *location_str = argnext(args);
    
    if(!location_str){
        asprintf(error, "need location");
        return CMD_FAILURE;
    }

    long location = parse_expr(location_str, error);

    if(*error)
        return CMD_FAILURE;

    /* Get the amount of instructions to disassemble. */
    char *amount_str = argnext(args);

    if(!amount_str){
        asprintf(error, "need amount");
        return CMD_FAILURE;
    }

    int amount = (int)strtol_err(amount_str, error);

    if(*error)
        return CMD_FAILURE;

    if(amount <= 0){
        asprintf(error, "bad amount %d", amount);
        return CMD_FAILURE;
    }

    if(args->add_aslr)
        location += debuggee->aslr_slide;

    kern_return_t err = disassemble_at_location(location, amount);

    if(err){
        asprintf(error, "could not disassemble from %#lx to %#lx: %s", 
                location, location + amount, mach_error_string(err));
        return CMD_FAILURE;
    }
    
    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_examine(struct cmd_args_t *args, 
        int arg1, char **error){
    if(!args){
        help_internal("examine");
        return CMD_FAILURE;
    }

    if(debuggee->pid == -1)
        return CMD_FAILURE;

    char *location_str = argnext(args);
    
    if(!location_str){
        asprintf(error, "need location");
        return CMD_FAILURE;
    }

    long location = parse_expr(location_str, error);

    if(*error)
        return CMD_FAILURE;

    /* Next, however many bytes are wanted. */
    char *size = argnext(args);

    if(!size){
        asprintf(error, "need size");
        return CMD_FAILURE;
    }
    
    int amount = (int)strtol_err(size, error);

    if(*error)
        return CMD_FAILURE;
    
    if(amount < 0){
        asprintf(error, "negative amount");
        return CMD_FAILURE;
    }

    if(args->add_aslr)
        location += debuggee->aslr_slide;

    kern_return_t err = dump_memory(location, amount);

    if(err){
        asprintf(error, "could not dump memory from %#lx to %#lx: %s", 
                location, location + amount, mach_error_string(err));
        return CMD_FAILURE;
    }

    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_help(struct cmd_args_t *args, 
        int arg1, char **error){
    if(!args){
        asprintf(error, "need command");
        return CMD_FAILURE;
    }

    char *cmd = argnext(args);

    while(cmd){
        help_internal(cmd);
        cmd = argnext(args);
    }
    
    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_kill(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->pid == -1)
        return CMD_FAILURE;

    if(!debuggee->debuggee_name)
        return CMD_FAILURE;

    char ans = answer("Do you really want to kill %s? (y/n) ", 
            debuggee->debuggee_name);

    if(ans == 'n')
        return CMD_SUCCESS;

    char *saved_name = strdup(debuggee->debuggee_name);

    cmdfunc_detach(NULL, 0, error);
    
    pid_t p;
    char *argv[] = {"killall", "-9", saved_name, NULL};
    int status = posix_spawnp(&p, "killall", NULL, NULL, 
            (char * const *)argv, NULL);
    
    free(saved_name);

    if(status == 0)
        waitpid(p, &status, 0);
    else{
        asprintf(error, "could not kill debuggee");
        return CMD_FAILURE;
    }
    
    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_quit(struct cmd_args_t *args, 
        int arg1, char **error){
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

cmd_error_t cmdfunc_regsfloat(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->pid == -1)
        return CMD_FAILURE;

    if(args && args->num_args == 0){
        asprintf(error, "need a register");
        return CMD_FAILURE;
    }

    /* If the user wants a quadword register,
     * the max string length would be 87.
     */
    const int sz = 90;

    char *regstr = malloc(sz);

    /* Iterate through and show all the registers the user asked for. */
    char *curreg = argnext(args);

    while(curreg){
        debuggee->get_neon_state();

        if(strcmp(curreg, "fpsr") == 0){
            curreg = argnext(args);
            printf("%10s = 0x%8.8x\n", "fpsr", debuggee->neon_state.__fpsr);
            continue;
        }
        else if(strcmp(curreg, "fpcr") == 0){
            curreg = argnext(args);
            printf("%10s = 0x%8.8x\n", "fpcr", debuggee->neon_state.__fpcr);
            continue;
        }
        
        memset(regstr, '\0', sz);

        char reg_type = tolower(curreg[0]);
        
        /* Move up a byte for the register number. */
        memmove(curreg, curreg + 1, strlen(curreg));

        int reg_num = (int)strtol_err(curreg, error);

        if(*error){
            free(regstr);
            return CMD_FAILURE;
        }

        int good_reg_num = (reg_num >= 0 && reg_num <= 31);
        int good_reg_type = ((reg_type == 'q' || reg_type == 'v') 
                || reg_type == 'd' || reg_type == 's');

        if(!good_reg_num || !good_reg_type){
            printf("%8sInvalid register\n", "");

            curreg = argnext(args);
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

            sprintf(regstr, "%s0x%02x}", regstr, 
                    *(uint8_t *)(hi_data + (sizeof(long) - 1)));

            free(hi);
            free(lo);
        }
        /* Doubleword */
        else if(reg_type == 'd')
            sprintf(regstr, "d%d = %.15g", reg_num, 
                    *(double *)&debuggee->neon_state.__v[reg_num]);
        /* Word */
        else if(reg_type == 's')
            sprintf(regstr, "s%d = %g", reg_num, 
                    *(float *)&debuggee->neon_state.__v[reg_num]);

        /* Figure out how many bytes the register takes up in the string. */
        char *space = strchr(regstr, ' ');
        int bytes = space - regstr;

        int add = 8 - bytes;
        
        printf("%*s\n", (int)(strlen(regstr) + add), regstr);
        
        curreg = argnext(args);
    }

    free(regstr);

    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_regsgen(struct cmd_args_t *args, 
        int arg1, char **error){
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

            printf("%10s = 0x%16.16llx\n", regstr, 
                    debuggee->thread_state.__x[i]);

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
    char *curreg = argnext(args);

    while(curreg){
        char reg_type = tolower(curreg[0]);

        if(reg_type != 'x' && reg_type != 'w'){
            char *curreg_cpy = strdup(curreg);
            size_t curreg_cpy_len = strlen(curreg_cpy);

            /* We need to be able to free it. */
            for(int i=0; i<curreg_cpy_len; i++)
                curreg_cpy[i] = tolower(curreg_cpy[i]);

            if(strcmp(curreg_cpy, "fp") == 0)
                printf("%8s = 0x%16.16llx\n", "fp", debuggee->thread_state.__fp);
            else if(strcmp(curreg_cpy, "lr") == 0)
                printf("%8s = 0x%16.16llx\n", "lr", debuggee->thread_state.__lr);
            else if(strcmp(curreg_cpy, "sp") == 0)
                printf("%8s = 0x%16.16llx\n", "sp", debuggee->thread_state.__sp);
            else if(strcmp(curreg_cpy, "pc") == 0)
                printf("%8s = 0x%16.16llx\n", "pc", debuggee->thread_state.__pc);
            else if(strcmp(curreg_cpy, "cpsr") == 0)
                printf("%8s = 0x%8.8x\n", "cpsr", debuggee->thread_state.__cpsr);
            else
                printf("Invalid register\n");

            free(curreg_cpy);

            curreg = argnext(args);
            continue;
        }

        /* Move up one byte to get to the "register number". */
        memmove(curreg, curreg + 1, strlen(curreg));

        int reg_num = (int)strtol_err(curreg, error);

        if(*error)
            return CMD_FAILURE;
        
        if(reg_num < 0 || reg_num > 29){
            curreg = argnext(args);
            continue;
        }

        char *regstr = malloc(sz);
        memset(regstr, '\0', sz);

        sprintf(regstr, "%c%d", reg_type, reg_num);

        if(reg_type == 'x')
            printf("%8s = 0x%16.16llx\n", regstr, 
                    (long long)debuggee->thread_state.__x[reg_num]);
        else
            printf("%8s = 0x%8.8x\n", regstr, 
                    (int)debuggee->thread_state.__x[reg_num]);

        free(regstr);

        curreg = argnext(args);
    }
    
    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_set(struct cmd_args_t *args, 
        int arg1, char **error){
    if(!args){
        help_internal("set");
        return CMD_FAILURE;
    }

    char *specifier_str = argnext(args);

    if(!specifier_str){
        help_internal("set");
        return CMD_FAILURE;
    }

    char specifier = specifier_str[0];

    char *target_str = argnext(args);

    if(!target_str){
        help_internal("set");
        return CMD_FAILURE;
    }
    
    char *value_str = argnext(args);

    if(!value_str){
        help_internal("set");
        return CMD_FAILURE;
    }

    /* If we are writing to an offset, we've done everything needed. */
    if(specifier == '*'){
        long value = parse_expr(value_str, error);

        if(*error){
            asprintf(error, "expression evaluation failed: %s", *error);
            return CMD_FAILURE;
        }

        long location = parse_expr(target_str, error);

        if(*error){
            asprintf(error, "expression evaluation failed: %s", *error);
            return CMD_FAILURE;
        }

        if(args->add_aslr)
            location += debuggee->aslr_slide;
        
        kern_return_t err = write_memory_to_location(
                (vm_address_t)location, (vm_offset_t)value);

        if(err){
            asprintf(error, "could not write to %#lx: %s", location, 
                    mach_error_string(err));
            return CMD_FAILURE;
        }

        return CMD_SUCCESS;
    }
    /* Convenience variable or register. */
    else if(specifier == '$'){
        /* To tell whether or not the user wants to set a 
         * convenience variable, we can pass a string to the
         * error parameter. convvar_set will bail and initialize `e`
         * if the name is a system register. However, there's no
         * need to notify the user if this occurs. Otherwise,
         * the user meant to set a convenience variable and we can
         * return after it is updated.
         */
        char *e = NULL;

        /* target_str doesn't include the '$'. */
        char *var;
        asprintf(&var, "$%s", target_str);
        set_convvar(var, value_str, &e);
        
        free(var);

        if(!e)
            return CMD_SUCCESS;

        /* Put this check here so the user and set convenience variables
         * without being attached to anything.
         */
        if(debuggee->pid == -1){
            help_internal("set");
            return CMD_FAILURE;
        }

        for(int i=0; i<strlen(target_str); i++)
            target_str[i] = tolower(target_str[i]);

        char reg_type = target_str[0];
        int reg_num = strtol(target_str + 1, NULL, 10);

        int gpr = reg_type == 'x' || reg_type == 'w';
        int fpr = (reg_type == 'q' || reg_type == 'v') || 
                reg_type == 'd' || reg_type == 's';
        int quadword = fpr && (reg_type == 'q' || reg_type == 'v');

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

        if(fpr && !quadword && *error)
            return CMD_FAILURE;

        double valuedf = strtod_err(value_str, error);

        if(fpr && !quadword && *error)
            return CMD_FAILURE;

        /* Take care of any special registers. */
        if(strcmp(target_str, "fp") == 0)
            debuggee->thread_state.__fp = valuellx;
        else if(strcmp(target_str, "lr") == 0)
            debuggee->thread_state.__lr = valuellx;
        else if(strcmp(target_str, "sp") == 0)
            debuggee->thread_state.__sp = valuellx;
        else if(strcmp(target_str, "pc") == 0)
            debuggee->thread_state.__pc = valuellx;
        else if(strcmp(target_str, "cpsr") == 0)
            debuggee->thread_state.__cpsr = valued;
        else if(strcmp(target_str, "fpsr") == 0)
            debuggee->neon_state.__fpsr = valued;
        else if(strcmp(target_str, "fpcr") == 0)
            debuggee->neon_state.__fpcr = valued;
        else{
            if(!good_reg_num || !good_reg_type){
                asprintf(error, "bad register '%s'", target_str);
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
                    if(value_str[0] != '{' || 
                            value_str[strlen(value_str) - 1] != '}'){
                        asprintf(error, "bad value '%s'", value_str);
                        return CMD_FAILURE;
                    }

                    if(strlen(value_str) == 2){
                        asprintf(error, "bad value '%s'", value_str);
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
                                                
                        unsigned int byte = 
                            (unsigned int)strtol(curbyte, NULL, 0);

                        if(i < sizeof(long)){
                            lo_str = realloc(lo_str, strlen(lo_str) +
                                    strlen(curbyte) + 3);
                            sprintf(lo_str, "%s%02x", lo_str, byte);
                        }
                        else{
                            hi_str = realloc(hi_str, strlen(hi_str) + 
                                    strlen(curbyte) + 3);
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

        debuggee->set_thread_state();
        debuggee->set_neon_state();
    }

    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_show(struct cmd_args_t *args, 
        int arg1, char **error){
    if(!args){
        show_all_cvars();
        return CMD_SUCCESS;
    }

    /* All arguments will be convenience variables. */
    char *cur_convvar = argnext(args);

    while(cur_convvar){
        p_convvar(cur_convvar);
        cur_convvar = argnext(args);
    }

    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_stepi(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->pid == -1)
        return CMD_FAILURE;
    
    if(debuggee->pending_messages > 0)
        reply_to_exception(debuggee->exc_request, KERN_SUCCESS);

    /* Disable breakpoints when single stepping so we don't have to deal
     * with more exceptions being raised. Instead, just check if we're at
     * a breakpointed address every time we step.
     */
    breakpoint_disable_all();

    debuggee->get_debug_state();
    debuggee->debug_state.__mdscr_el1 |= 1;
    debuggee->set_debug_state();

    debuggee->is_single_stepping = 1;

    rl_already_prompted = 1;

    debuggee->resume();
    debuggee->interrupted = 0;

    if(*error)
        return CMD_FAILURE;

    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_threadlist(struct cmd_args_t *args, 
        int arg1, char **error){
    if(!debuggee->threads)
        return CMD_FAILURE;
    
    if(!debuggee->threads->front)
        return CMD_FAILURE;
    
    struct node_t *current = debuggee->threads->front;

    while(current){
        struct machthread *t = current->data;

        printf("\t%sthread #%d, tid = %#llx, name = '%s', where = %#llx\n", 
                t->focused ? "* " : "", t->ID, t->tid, t->tname, 
                t->thread_state.__pc);
        
        current = current->next;
    }

    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_threadselect(struct cmd_args_t *args, 
        int arg1, char **error){
    if(!args)
        return CMD_FAILURE;
    
    if(debuggee->pid == -1)
        return CMD_FAILURE;

    if(!debuggee->threads)
        return CMD_FAILURE;

    if(!debuggee->threads->front)
        return CMD_FAILURE;

    /* Current argument: the ID of the thread the user wants to focus on. */
    char *curarg = argnext(args);
    
    if(!curarg){
        asprintf(error, "need thread ID");
        return CMD_FAILURE;
    }

    int thread_id = (int)strtol_err(curarg, error);

    if(*error)
        return CMD_FAILURE;

    if(thread_id < 1 || thread_id > debuggee->thread_count){
        asprintf(error, "out of bounds, must be in [1, %d]", 
                debuggee->thread_count);
        return CMD_FAILURE;
    }

    int result = machthread_setfocusgivenindex(thread_id);
    
    if(result){
        asprintf(error, "could not set focused thread to thread %d", thread_id);
        return CMD_FAILURE;
    }

    printf("Selected thread #%d\n", thread_id);
    
    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_trace(struct cmd_args_t *args, 
        int arg1, char **error){
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

cmd_error_t cmdfunc_unset(struct cmd_args_t *args, 
        int arg1, char **error){
    if(!args)
        return CMD_FAILURE;

    if(args->num_args == 0){
        asprintf(error, "need a convenience variable");
        return CMD_FAILURE;
    }

    /* Arguments will consist of convenience variables. */
    char *cur_convvar = argnext(args);

    while(cur_convvar){
        void_convvar(cur_convvar);
        cur_convvar = argnext(args);
    }

    return CMD_SUCCESS;
}

cmd_error_t cmdfunc_watch(struct cmd_args_t *args, 
        int arg1, char **error){
    if(!args)
        return CMD_FAILURE;

    if(debuggee->pid == -1)
        return CMD_FAILURE;

    /* Current argument: watchpoint type or location. */
    char *curarg = argnext(args);

    if(!curarg){
        help_internal("watch");
        return CMD_FAILURE;
    }

    int LSC = WP_WRITE;

    /* Check if the user specified a watchpoint type. If they didn't,
     * this watchpoint will match on reads and writes.
     */
    if(!strstr(curarg, "0x")){
        if(strcmp(curarg, "--r") == 0)
            LSC = WP_READ;
        else if(strcmp(curarg, "--w") == 0)
            LSC = WP_WRITE;
        else if(strcmp(curarg, "--rw") == 0)
            LSC = WP_READ_WRITE;
        else{
            help_internal("watch");
            return CMD_FAILURE;
        }

        /* If we had a type before the location, we need to get the next
         * argument. After that, current argument is the location to watch.
         */
        curarg = argnext(args);

        /* We need the location after type. */
        if(!curarg){
            help_internal("watch");
            return CMD_FAILURE;
        }
    }

    long location = parse_expr(curarg, error);

    if(*error){
        asprintf(error, "expression evaluation failed: %s\n", *error);
        return CMD_FAILURE;
    }

    /* Current argument: size of data we're watching. */
    curarg = argnext(args);
    
    if(!curarg){
        help_internal("watch");
        return CMD_FAILURE;
    }

    int data_len = (int)strtol_err(curarg, error);

    if(*error)
        return CMD_FAILURE;

    return watchpoint_at_address(location, data_len, LSC, error);
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
    
    char *usercmd = strdup(input);

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
    cmd_error_t (*finalfunc)(struct cmd_args_t *, int, char **) = NULL;

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
                args = input + tokenlen + 1;

            struct cmd_args_t *parsed_args = parse_args(args,
                    curcmd->rinfo.argregex,
                    curcmd->rinfo.groupnames,
                    curcmd->rinfo.num_groups,
                    curcmd->rinfo.unk_num_args,
                    errstr);

            if(*errstr){
                argfree(parsed_args);
                free(usercmd);
                return CMD_FAILURE;
            }
        
            cmd_error_t result = finalfunc(parsed_args, 0, errstr);

            argfree(parsed_args);
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

    struct dbg_cmd_t *matchedcmd = NULL;

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
                matchedcmd = cmd;
                finalfunc = matchedcmd->function;
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
                
                asprintf(errstr, "ambiguous command '%s': %s", input, 
                        possible_cmds);

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
                matchedcmd = prevcmd;
                finalfunc = matchedcmd->function;
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
     * At this point, anything token contains is an argument. 
     */
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

    struct cmd_args_t *parsed_args = parse_args(args,
            matchedcmd->rinfo.argregex,
            matchedcmd->rinfo.groupnames,
            matchedcmd->rinfo.num_groups,
            matchedcmd->rinfo.unk_num_args,
            errstr);

    if(*errstr){
        argfree(parsed_args);
        free(args);
        free(usercmd);
        return CMD_FAILURE;
    }

    cmd_error_t result = finalfunc(parsed_args, 0, errstr);

    argfree(parsed_args);
    free(args);
    free(usercmd);

    return result;
}

/* This function eliminates the messy nature of initializing structs
 * within an array.
 */
void initialize_commands(void){
    int cmdidx = 0;

    #define ADD_CMD(cmd) COMMANDS[cmdidx++] = cmd

    struct dbg_cmd_t aslr = {
        "aslr", NULL, 
            "Show debuggee's ASLR slide.\n"
            "This command has no arguments.\n"
            "\nSyntax:\n"
            "\taslr\n"
            "\n",
        {
            "",
            0,
            0,
            {}
        },
        cmdfunc_aslr
    };

    ADD_CMD(aslr);

    struct dbg_cmd_t attach = {
        "attach", NULL,
            "Attach to a program via its PID or name.\n"
            "To attach upon launch, give '--waitfor' before the program's name.\n"
            "This command has one mandatory argument and one optional argument.\n"
            "\nMandatory arguments:\n"
            "\ttarget\n"
            "\t\tWhat you want to attach to. It can be a PID or a program name.\n"
            "\n"
            "\nOptional arguments:\n"
            "\t--waitfor\n"
            "\t\t'--waitfor' tells iosdbg to wait for process launch.\n"
            "\nSyntax:\n"
            "\tattach --waitfor? target\n"
            "\n",
        {
            "(?<waitfor>--waitfor)?\\s?\"?(?<target>[^\"]+)\"?",
            2,
            0,
            { "waitfor", "target" }
        },
        cmdfunc_attach
    };

    ADD_CMD(attach);
    
    struct dbg_cmd_t backtrace = {
        "backtrace", "bt",
            "Print a backtrace of the entire stack. All stack frames are printed.\n"
            "This command has no arguments.\n"
            "\nSyntax:\n"
            "\tbacktrace\n"
            "\n"
            "\nThis command has an alias: 'bt'\n"
            "\n",
        {
            "",
            0,
            0,
            {}
        },
        cmdfunc_backtrace
    };

    ADD_CMD(backtrace);

    struct dbg_cmd_t breakpoint = {
        "break", "b",
            "Set a breakpoint. Include '--no-aslr' to keep ASLR from being added.\n"
            "This command has one mandatory argument and no optional arguments.\n"
            "\nMandatory arguments:\n"
            "\tlocation\n"
            "\t\tThis expression will be evaluated and used as the location for the breakpoint.\n"
            "\t\tThis command accepts an arbitrary amount of this argument,"
            " allowing you to set multiple breakpoints.\n"
            "\nSyntax:\n"
            "\tbreak location\n"
            "\n"
            "\nThis command has an alias: 'b'\n"
            "\n",
        {
            "(?<args>[\\w+\\-*\\/\\$()]+)",
            1,
            1,
            { "args" }
        },
        cmdfunc_break
    };

    ADD_CMD(breakpoint);

    struct dbg_cmd_t cont = {
        "continue", "c",
            "Resume debuggee execution.\n"
            "This command has no arguments.\n"
            "\nSyntax:\n"
            "\tcontinue\n"
            "\n"
            "\nThis command has an alias: 'c'\n"
            "\n",
        {
            "",
            0,
            0,
            {}
        },
        cmdfunc_continue
    };

    ADD_CMD(cont);

    struct dbg_cmd_t delete = {
        "delete", "d",
            "Delete breakpoints or watchpoints.\n"
            "This command has one mandatory argument and one optional argument.\n"
            "\nMandatory arguments:\n"
            "\ttype\n"
            "\t\tWhat you want to delete. This can be 'b' for breakpoint"
            " or 'w' for watchpoint.\n"
            "\nOptional arguments:\n"
            "\tid\n"
            "\t\tThe id of the breakpoint or watchpoint you want to delete.\n"
            "\t\tThis command accepts an arbitrary amount of this argument,"
            " allowing you to delete multiple breakpoints or watchpoints.\n"
            "\t\tOmit this argument for the option to delete all breakpoints"
            " or watchpoints.\n"
            "\nSyntax:\n"
            "\tdelete type id?\n"
            "\n"
            "\nThis command has an alias: 'd'\n"
            "\n",
        {
            "(?<type>b|w)(\\s+)?(?<ids>[-\\d\\s]+)?",
            2,
            1,
            { "type", "ids" }
        },
        cmdfunc_delete
    };

    ADD_CMD(delete);

    struct dbg_cmd_t detach = {
        "detach", NULL,
            "Detach from the debuggee.\n"
            "This command has no arguments.\n"
            "\nSyntax:\n"
            "\tdetach\n"
            "\n",
        {
            "",
            0,
            0,
            {}
        },
        cmdfunc_detach
    };

    ADD_CMD(detach);

    struct dbg_cmd_t disassemble = {
        "disassemble", "dis",
            "Disassemble debuggee memory. Include '--no-aslr' to keep ASLR from being added.\n"
            "This command has two mandatory arguments and no optional arguments.\n"
            "\nMandatory arguments:\n"
            "\tlocation\n"
            "\t\tThis expression will be evaluated and used as where iosdbg"
            " will start disassembling.\n"
            "\tcount\n"
            "\t\tHow many bytes iosdbg will disassemble.\n"
            "\nSyntax:\n"
            "\tdisassemble location count\n"
            "\n"
            "\nThis command has an alias: 'dis'\n"
            "\n",
        {
            "(?<args>[\\w+\\-*\\/\\$()]+)",
            1,
            1,
            { "args" }
        },
        cmdfunc_disassemble
    };

    ADD_CMD(disassemble);

    struct dbg_cmd_t examine = {
        "examine", "x",
            "View debuggee memory. Include '--no-aslr' to keep ASLR from being added.\n"
            "This command has two mandatory arguments and no optional arguments.\n"
            "\nMandatory arguments:\n"
            "\tlocation\n"
            "\t\tThis expression will be evaluted and used as where iosdbg"
            " will start dumping memory.\n"
            "\tcount\n"
            "\t\tHow many bytes iosdbg will dump.\n"
            "\nSyntax:\n"
            "\texamine location count\n"
            "\n"
            "\nThis command has an alias: 'x'\n"
            "\n",
        {
            "(?<args>[\\w+\\-*\\/\\$()]+)",
            1,
            1,
            { "args" }
        },
        cmdfunc_examine
    };

    ADD_CMD(examine);

    struct dbg_cmd_t help = {
        "help", NULL,
            "Get help for a command.\n"
            "This command has one mandatory argument and no optional arguments.\n"
            "\nMandatory arguments:\n"
            "\tcommand\n"
            "\t\tThe description of the command.\n"
            "\nSyntax:\n"
            "\thelp command\n"
            "\n",
        {
            "(?J)^\"(?<cmd>[\\w\\s]+)\"|^(?<cmd>(?![\\w\\s]+\")\\w+)",
            1,
            0,
            { "cmd" }
        },
        cmdfunc_help
    };

    ADD_CMD(help);

    struct dbg_cmd_t kill = {
        "kill", NULL,
            "Kill the debuggee.\n"
            "This command has no arguments.\n"
            "\nSyntax:\n"
            "\tkill\n"
            "\n",
        {
            "",
            0,
            0,
            {}
        },
        cmdfunc_kill
    };

    ADD_CMD(kill);

    struct dbg_cmd_t quit = {
        "quit", "q",
            "Quit iosdbg.\n"
            "This command has no arguments.\n"
            "\nSyntax:\n"
            "\tquit\n"
            "\n"
            "\nThis command has an alias: 'q'\n"
            "\n",
        {
            "",
            0,
            0,
            {}
        },
        cmdfunc_quit
    };

    ADD_CMD(quit);

    struct dbg_cmd_t regs = {
        "regs", NULL, NULL,
        {
            "",
            0,
            0,
            {}
        },
        NULL
    };

    ADD_CMD(regs);

    struct dbg_cmd_t regsfloat = {
        "regs float", NULL,
            "Show floating point registers.\n"
            "This command has one mandatory argument and no optional arguments.\n"
            "\nMandatory arguments:\n"
            "\treg\n"
            "\t\tThe floating point register.\n"
            "\t\tThis command accepts an arbitrary amount of this argument,"
            " allowing you to view many floating point registers at once.\n"
            "\nSyntax:\n"
            "\tregs float reg\n"
            "\n",
        {
            "\\b(?<reg>[qvdsQVDS]{1}\\d{1,2}|(fpsr|FPSR|fpcr|FPCR)+)\\b",
            1,
            1,
            { "reg" }
        },
        cmdfunc_regsfloat
    };

    ADD_CMD(regsfloat);

    struct dbg_cmd_t regsgen = {
        "regs gen", NULL,
            "Show general purpose registers.\n"
            "This command has no mandatory arguments and one optional argument.\n"
            "\nOptional arguments:\n"
            "\treg\n"
            "\t\tThe general purpose register.\n"
            "\t\tThis command accepts an arbitrary amount of this argument,"
            " allowing you to view many general purpose registers at once.\n"
            "\t\tOmit this argument to see every general purpose register.\n"
            "\nSyntax:\n"
            "\tregs gen reg\n"
            "\n",
        {
            "\\b(?<reg>[xwXW]{1}\\d{1,2}|(fp|FP|lr|LR|sp|SP|pc|PC|cpsr|CPSR)+)\\b",
            1,
            1,
            { "reg" }
        },
        cmdfunc_regsgen
    };

    ADD_CMD(regsgen);

    struct dbg_cmd_t set = {
        "set", NULL,
            "Modify debuggee memory, registers, or iosdbg convenience variables.\n"
            "Include '--no-aslr' to keep ASLR from being added.\n"
            "This command has two mandatory arguments and no optional arguments.\n"
            "\nMandatory arguments:\n"
            "\ttarget\n"
            "\t\tThis expression will be evaluated and interpreted as what"
            " will be changed.\n"
            "\t\tPrefix locations in memory with '*'.\n"
            "\t\tPrefix registers and convenience variables with '$'.\n"
            "\tvalue\n"
            "\t\tWhat `target` will be changed to.\n"
            "\t\tThis is an expression only when writing to memory. (prefix = '*')\n"
            "\t\tIf you want to modify one of the 128 bit V registers,"
            " format value as follows:\n"
            "\t\t\"{byte1 byte2 byte3 byte4 byte5 byte6 byte7 byte8 byte9 byte10 "
            "byte11 byte12 byte13 byte14 byte15 byte16}\".\n"
            "\t\tBytes do not have to be in hex.\n"
            "\t\tConvenience variables can hold integers, floating point values,"
            " and strings.\n"
            "\t\tWhen setting a convenience variable, include '.' for a floating"
            " point value\n"
            "\t\t and quotes for strings.\n"
            "\t\tConvenience variables must be prefixed with '$'.\n"
            "\nSyntax:\n"
            "\tset target=value\n"
            "\n",
        {
            "(?<type>[*$]{1})(?<target>[\\w\\d+\\-*\\/$()]+)\\s*"
                "="
                "\\s*(?<value>(\\{.*\\})|(\\\".*\\\")|((?!\")[.\\-\\w\\d+\\-*\\/$()]+))",
            3,
            0,
            { "type", "target", "value" }
        },
        cmdfunc_set
    };

    ADD_CMD(set);

    struct dbg_cmd_t show = {
        "show", NULL,
            "Display convenience variables.\n"
            "This command has no mandatory arguments and one optional argument.\n"
            "\nOptional arguments:\n"
            "\tvar\n"
            "\t\tWhat variable to display.\n"
            "\t\tThis command accepts an arbitrary amount of this argument,\n"
            "\t\t allowing you to display many convenience variables at once.\n"
            "\t\tOmit this argument to display every convenience variable.\n"
            "\nSyntax:\n"
            "\tshow var\n"
            "\n",
        {
            "(?<var>\\$[\\w\\d+\\-*\\/$()]+)",
            1,
            1,
            { "var" }
        },
        cmdfunc_show
    };

    ADD_CMD(show);

    struct dbg_cmd_t stepi = {
        "stepi", NULL,
            "Step into the next machine instruction.\n"
            "This command has no arguments.\n"
            "\nSyntax:\n"
            "\tstepi\n"
            "\n",
        {
            "",
            0,
            0,
            {}
        },
        cmdfunc_stepi
    };

    ADD_CMD(stepi);

    struct dbg_cmd_t thread = {
        "thread", NULL, NULL,
        {
            "",
            0,
            0,
            {}
        },
        NULL
    };

    ADD_CMD(thread);

    struct dbg_cmd_t threadlist = {
        "thread list", NULL,
            "Inquire about existing threads of the debuggee.\n"
            "This command has no arguments.\n"
            "\nSyntax:\n"
            "\tthread list\n"
            "\n",
        {
            "",
            0,
            0,
            {}
        },
        cmdfunc_threadlist
    };

    ADD_CMD(threadlist);

    struct dbg_cmd_t threadselect = {
        "thread select", NULL,
            "Select the thread to focus on while debugging.\n"
            "This command has one mandatory argument and no optional arguments.\n"
            "\nMandatory arguments:\n"
            "\ttid\n"
            "\t\tThe thread ID to focus on.\n"
            "\nSyntax:\n"
            "\tthread select tid\n"
            "\n",
        {
            "^\\s*(?<tid>\\d+)",
            1,
            0,
            { "tid" }
        },
        cmdfunc_threadselect
    };

    ADD_CMD(threadselect);

    struct dbg_cmd_t trace = {
        "trace", NULL,
            "This command provides similar functionality to strace through"
            " the kdebug interface.\n"
            "This command has no arguments.\n"
            "\nSyntax:\n"
            "\ttrace\n"
            "\n",
        {
            "",
            0,
            0,
            {}
        },
        cmdfunc_trace
    };

    ADD_CMD(trace);

    struct dbg_cmd_t unset = {
        "unset", NULL,
            "Revert the value of a convenience variable to 'void'.\n"
            "This command has one mandatory argument and no optional arguments.\n"
            "\nMandatory arguments:\n"
            "\tvar\n"
            "\t\tThe convenience variable to modify.\n"
            "\t\tThis command accepts an arbitrary amount of this argument,\n"
            "\t\t allowing you to modify many convenience variables at once.\n"
            "\nSyntax:\n"
            "\tunset var\n"
            "\n",
        {
            "(?<var>\\$\\w+)",
            1,
            1,
            { "var" }
        },
        cmdfunc_unset
    };

    ADD_CMD(unset);
    
    struct dbg_cmd_t watch = {
        "watch", "w",
            "Set a watchpoint. ASLR is never accounted for.\n"
            "This command has two mandatory arguments and one optional argument.\n"
            "\nMandatory arguments:\n"
            "\tlocation\n"
            "\t\tThis expression will be evaluated and interpreted as\n"
            "\t\t the watchpoint's location.\n"
            "\tsize\n"
            "\t\tThe size of the data to watch.\n"
            "\nOptional arguments:\n"
            "\ttype\n"
            "\t\tThe type of the watchpoint. Acceptable values are:\n"
            "\t\t\t'--r'  (read)\n"
            "\t\t\t'--w'  (write)\n"
            "\t\t\t'--rw' (read/write)\n"
            "\t\tIf this argument is omitted, iosdbg assumes --w.\n"
            "\nSyntax:\n"
            "\twatch type? location size\n"
            "\n"
            "\nThis command has an alias: 'w'\n"
            "\n",
        {
            "^(?<type>--[rw]{1,2})?\\s*(?<location>[\\w+\\-*\\/\\$()]+)\\s+"
                "(?<size>(0[xX])?\\d+)",
            3,
            0,
            { "type", "location", "size" }
        },
        cmdfunc_watch
    };

    ADD_CMD(watch);

    struct dbg_cmd_t terminator = {
        "", NULL, "",
        {
            "",
            0,
            0,
            {}
        },
        NULL
    };

    ADD_CMD(terminator);
}
