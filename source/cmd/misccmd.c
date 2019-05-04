#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <readline/readline.h>

#include "documentation.h"
#include "misccmd.h"

#include "../breakpoint.h"
#include "../convvar.h"
#include "../dbgops.h"
#include "../debuggee.h"
#include "../exception.h"
#include "../expr.h"
#include "../linkedlist.h"
#include "../memutils.h"
#include "../procutils.h"
#include "../ptrace.h"
#include "../servers.h"
#include "../sigsupport.h"
#include "../strext.h"
#include "../tarrays.h"
#include "../thread.h"
#include "../trace.h"

int keep_checking_for_process;

/* Allow the user to answer whatever
 * question is given.
 */
static char answer(const char *question, ...){
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

static pid_t parse_pid(char *pidstr, char **error){
    return is_number_fast(pidstr) ? (pid_t)strtol_err(pidstr, error)
        : pid_of_program(pidstr, error);
}

enum cmd_error_t cmdfunc_aslr(struct cmd_args_t *args, 
        int arg1, char **error){
    printf("%7s%#llx\n", "", debuggee->aslr_slide);
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_attach(struct cmd_args_t *args, 
        int arg1, char **error){
    /* First argument could either be '--waitfor' or what the user
     * wants to attach to.
     */
    char *firstarg = argnext(args);
    int waitfor = strcmp(firstarg, "--waitfor") == 0;

    /* If we got '--waitfor' as the first argument, whatever the user
     * wants to attach to will be next.
     */
    char *target = firstarg;

    if(waitfor)
        target = argnext(args);

    /* Check if the user wants to attach to something else while attached
     * to something.
     */
    if(debuggee->pid != -1){
        char ans = answer("Detach from %s and reattach to %s? (y/n) ", 
                debuggee->debuggee_name, target);

        if(ans == 'n')
            return CMD_SUCCESS;

        /* Detach from what we are attached to
         * and call this function again.
         */     
        cmdfunc_detach(NULL, 0, error);

        /* Re-construct the argument queue for the next call. */
        enqueue(args->argqueue, firstarg);
        enqueue(args->argqueue, target);

        return cmdfunc_attach(args, 0, error);
    }

    pid_t target_pid;

    /* Check for '--waitfor', and if we have it,
     * constantly check if this process has launched.
     */
    if(waitfor){
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

    if(*error || target_pid == -1)
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
    
    if(is_number_fast(target)){
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

    /* Have Unix signals be sent as Mach exceptions. */
    ptrace(PT_ATTACHEXC, debuggee->pid, 0, 0);
    ptrace(PT_SIGEXC, debuggee->pid, 0, 0);

    void_convvar("$_exitcode");
    void_convvar("$_exitsignal");

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_backtrace(struct cmd_args_t *args, 
        int arg1, char **error){
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

enum cmd_error_t cmdfunc_continue(struct cmd_args_t *args, 
        int do_not_print_msg, char **error){
    if(!debuggee->interrupted)
        return CMD_FAILURE;

    ops_resume();

    if(!do_not_print_msg)
        printf("Process %d resuming\n", debuggee->pid);

    /* Make output look nicer. */
    if(debuggee->currently_tracing){
        rl_already_prompted = 1;
        printf("\n");
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_detach(struct cmd_args_t *args, 
        int from_death, char **error){
    if(!debuggee->tracing_disabled)
        stop_trace();
    
    char *n = strdup(debuggee->debuggee_name);
    pid_t p = debuggee->pid;

    ops_detach(from_death);

    if(!from_death)
        printf("Detached from %s (%d)\n", n, p);

    free(n);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_help(struct cmd_args_t *args, 
        int arg1, char **error){
    char *cmd = argnext(args);
    documentation_for_cmdname(cmd, error);

    return (*error) ? CMD_FAILURE : CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_kill(struct cmd_args_t *args, 
        int arg1, char **error){
    char ans = answer("Do you really want to kill %s? (y/n) ", 
            debuggee->debuggee_name);

    if(ans == 'n')
        return CMD_SUCCESS;

    /* Don't notify the user that the debuggee has received SIGKILL
     * if they wanted to kill it.
     */
    int notify_backup, pass_backup, stop_backup;
    sigsettings(SIGKILL, &notify_backup, &pass_backup, &stop_backup, 0, error);

    if(*error)
        return CMD_FAILURE;

    int notify = 0, pass = 1, stop = 0;
    sigsettings(SIGKILL, &notify, &pass, &stop, 1, error);

    if(*error)
        return CMD_FAILURE;

    kill(debuggee->pid, SIGKILL);

    /* We're gonna eventually detach, so wait until that happens before
     * we revert the settings back for SIGKILL.
     */
    while(debuggee->pid != -1);

    sigsettings(SIGKILL, &notify_backup, &pass_backup, &stop_backup, 1, error);

    return (*error) ? CMD_FAILURE : CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_quit(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->pid != -1)
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

enum cmd_error_t cmdfunc_set(struct cmd_args_t *args, 
        int arg1, char **error){
    char *specifier_str = argnext(args);
    char *target_str = argnext(args);
    char *value_str = argnext(args);

    char specifier = *specifier_str;

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

        /* Put this check here so the user can set convenience variables
         * without being attached to anything.
         */
        if(debuggee->pid == -1)
            return CMD_FAILURE;

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

        float valuef = (float)strtold_err(value_str, error);

        if(fpr && !quadword && *error)
            return CMD_FAILURE;

        double valuedf = strtold_err(value_str, error);

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
                            concat(&lo_str, "%02x", byte);
                        }
                        else{
                            hi_str = realloc(hi_str, strlen(hi_str) + 
                                    strlen(curbyte) + 3);
                            concat(&hi_str,  "%02x", byte);
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

enum cmd_error_t cmdfunc_show(struct cmd_args_t *args, 
        int arg1, char **error){
    if(args->num_args == 0){
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

enum cmd_error_t cmdfunc_stepi(struct cmd_args_t *args, 
        int arg1, char **error){
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
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_trace(struct cmd_args_t *args, 
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

enum cmd_error_t cmdfunc_unset(struct cmd_args_t *args, 
        int arg1, char **error){
    /* Arguments will consist of convenience variables. */
    char *cur_convvar = argnext(args);

    while(cur_convvar){
        void_convvar(cur_convvar);
        cur_convvar = argnext(args);
    }

    return CMD_SUCCESS;
}
