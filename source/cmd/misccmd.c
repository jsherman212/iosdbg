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
#include "../interaction.h"
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

        if(ans == 'n'){
            if(waitfor)
                free(firstarg);

            free(target);

            return CMD_SUCCESS;
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

    if(*error || target_pid == -1){
        if(waitfor)
            free(firstarg);

        free(target);

        return CMD_FAILURE;
    }

    kern_return_t err = task_for_pid(mach_task_self(), 
            target_pid, &debuggee->task);

    if(err){
        if(waitfor)
            free(firstarg);

        asprintf(error, "couldn't get task port for %s (pid: %d): %s\n"
                "Did you forget to sign iosdbg with entitlements?\n"
                "Are you privileged enough to debug this process?",
                target, target_pid, mach_error_string(err));

        free(target);

        return CMD_FAILURE;
    }

    debuggee->pid = target_pid;
    debuggee->aslr_slide = debuggee->find_slide();
    
    if(is_number_fast(target)){
        char *name = progname_from_pid(debuggee->pid, error);

        if(*error){
            /* We already checked for a pid with waitfor. */
            free(target);

            return CMD_FAILURE;
        }

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

    char *aslr = NULL;
    asprintf(&aslr, "%#llx", debuggee->aslr_slide);

    set_convvar("$ASLR", aslr, error);

    if(*error){
        printf("warning: %s\n", *error);
        free(*error);
        *error = NULL;
    }

    free(aslr);

    if(waitfor)
        free(firstarg);

    free(target);

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
    if(args->num_args == 0){
        show_all_top_level_cmds();
        return CMD_SUCCESS;
    }

    char *cmd = argnext(args);
    documentation_for_cmdname(cmd, error);

    free(cmd);

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
