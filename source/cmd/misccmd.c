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
#include "../printing.h"
#include "../procutils.h"
#include "../ptrace.h"
#include "../servers.h"
#include "../sigsupport.h"
#include "../strext.h"
#include "../tarrays.h"
#include "../thread.h"
#include "../trace.h"

int KEEP_CHECKING_FOR_PROCESS;

static pid_t parse_pid(char *pidstr, char **error){
    return is_number_fast(pidstr) ? (pid_t)strtol_err(pidstr, error)
        : pid_of_program(pidstr, error);
}

enum cmd_error_t cmdfunc_aslr(struct cmd_args_t *args, 
        int arg1, char **error){
    WriteMessageBuffer("%4s%#lx\n", "", debuggee->aslr_slide);
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_attach(struct cmd_args_t *args, 
        int arg1, char **error){
    char *waitfor = argcopy(args, ATTACH_COMMAND_REGEX_GROUPS[0]);
    char *target = argcopy(args, ATTACH_COMMAND_REGEX_GROUPS[1]);

    /* Check if the user wants to attach to something else while attached
     * to something.
     */
    if(debuggee->pid != -1){
        char ans = answer("Detach from %s and reattach to %s? (y/n) ", 
                debuggee->debuggee_name, target);

        if(ans == 'n'){
            if(waitfor)
                free(waitfor);

            free(target);

            return CMD_SUCCESS;
        }

        /* Detach from what we are attached to
         * and call this function again.
         */     
        cmdfunc_detach(NULL, 0, NULL);

        /* Re-construct the argument queue for the next call. */
        argins(args, ATTACH_COMMAND_REGEX_GROUPS[0], waitfor ? strdup(waitfor) : NULL);
        argins(args, ATTACH_COMMAND_REGEX_GROUPS[1], strdup(target));

        if(waitfor)
            free(waitfor);

        free(target);

        return cmdfunc_attach(args, 0, error);
    }

    pid_t target_pid;

    /* Check for '--waitfor', and if we have it,
     * constantly check if this process has launched.
     */
    if(waitfor){
        printf("Waiting for process '%s' to launch (Ctrl+C to stop)\n\n", 
                target);

        char *e = NULL;

        /* If we're waiting for something to launch, it will not exist,
         * so don't return on error.
         */
        target_pid = parse_pid(target, &e);
    
        KEEP_CHECKING_FOR_PROCESS = 1;

        while(target_pid == -1 && KEEP_CHECKING_FOR_PROCESS){
            target_pid = parse_pid(target, &e);

            free(e);
            e = NULL;

            usleep(400);
        }

        KEEP_CHECKING_FOR_PROCESS = 0;
    }
    else{
        target_pid = parse_pid(target, error);
    }

    if(*error || target_pid == -1){
        if(waitfor)
            free(waitfor);

        free(target);

        return CMD_FAILURE;
    }

    kern_return_t err = task_for_pid(mach_task_self(), 
            target_pid, &debuggee->task);

    if(err){
        if(waitfor)
            free(waitfor);

        concat(error, "couldn't get task port for %s (pid: %d): %s\n"
                "Did you forget to sign iosdbg with entitlements?\n"
                "Are you privileged enough to debug this process?",
                target, target_pid, mach_error_string(err));

        free(target);

        return CMD_FAILURE;
    }

    debuggee->aslr_slide = debuggee->find_slide();

    if(debuggee->aslr_slide == -1)
        WriteMessageBuffer("warning: couldn't find debuggee's ASLR slide\n");

    debuggee->pid = target_pid;

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
    else{
        debuggee->debuggee_name = strdup(target);
    }

    debuggee->exc_requests = queue_new();

    debuggee->breakpoints = linkedlist_new();
    debuggee->watchpoints = linkedlist_new();
    debuggee->threads = linkedlist_new();

    setup_servers();

    debuggee->num_breakpoints = 0;
    debuggee->num_watchpoints = 0;

    thread_act_port_array_t threads;
    debuggee->get_threads(&threads);
    
    resetmtid();
    
    machthread_updatethreads(threads);
    machthread_setfocused(threads[0]);

    struct machthread *focused = machthread_getfocused();
    get_thread_state(focused);

    debuggee->want_detach = 0;

    WriteMessageBuffer("Attached to %s (pid: %d), slide: %#lx.\n",
            debuggee->debuggee_name, debuggee->pid, debuggee->aslr_slide);

    /* Have Unix signals be sent as Mach exceptions. */
    ptrace(PT_ATTACHEXC, debuggee->pid, 0, 0);

    void_convvar("$_exitcode");
    void_convvar("$_exitsignal");

    char *aslr = NULL;
    concat(&aslr, "%#llx", debuggee->aslr_slide);

    char *e = NULL;
    set_convvar("$ASLR", aslr, &e);

    if(e){
        WriteMessageBuffer("warning: %s\n", e);
        free(e);
    }

    free(aslr);

    if(waitfor)
        free(target);

    free(target);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_backtrace(struct cmd_args_t *args, 
        int arg1, char **error){
    struct machthread *focused = machthread_getfocused();

    get_thread_state(focused);

    WriteMessageBuffer("  * frame #0: 0x%16.16llx\n", focused->thread_state.__pc);
    WriteMessageBuffer("    frame #1: 0x%16.16llx\n", focused->thread_state.__lr);

    /* There's a linked list of frame pointers. */
    struct frame_t {
        struct frame_t *next;
        unsigned long frame;
    };

    struct frame_t *current_frame = malloc(sizeof(struct frame_t));
    kern_return_t err = read_memory_at_location(
            (void *)focused->thread_state.__fp, current_frame, 
            sizeof(struct frame_t));
    
    if(err){
        concat(error, "backtrace failed: %s", mach_error_string(err));
        return CMD_FAILURE;
    }

    int frame_counter = 2;

    while(current_frame->next){
        WriteMessageBuffer("%4sframe #%d: 0x%16.16lx\n", "", frame_counter,
                current_frame->frame);

        read_memory_at_location((void *)current_frame->next, 
                (void *)current_frame, sizeof(struct frame_t)); 
        frame_counter++;
    }

    WriteMessageBuffer(" - cannot unwind past frame %d -\n", frame_counter - 1);

    free(current_frame);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_continue(struct cmd_args_t *args, 
        int arg1, char **error){
    //printf("%s: debuggee->suspended() %d\n", __func__,
      //      debuggee->suspended());
    if(!debuggee->suspended())
        return CMD_FAILURE;

    ops_resume();

    WriteMessageBuffer("Process %d resuming\n", debuggee->pid);

    /* Make output look nicer. */
    if(debuggee->currently_tracing){
        rl_already_prompted = 1;
        WriteMessageBuffer("\n");
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
        WriteMessageBuffer("Detached from %s (%d)\n", n, p);

    free(n);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_help(struct cmd_args_t *args, 
        int arg1, char **error){
    if(args->num_args == 0){
        show_all_top_level_cmds();
        return CMD_SUCCESS;
    }

    char *cmd = argcopy(args, HELP_COMMAND_REGEX_GROUPS[0]);
    documentation_for_cmdname(cmd, error);

    free(cmd);

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_interrupt(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->pid == -1)
        return CMD_FAILURE;

    for(struct node_t *current = debuggee->threads->front;
            current;
            current = current->next){
        struct machthread *t = current->data;

        get_debug_state(t);

        t->debug_state.__mdscr_el1 = 0;

        set_debug_state(t);
    }

    kill(debuggee->pid, SIGSTOP);

    return CMD_SUCCESS;
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

    int status;
    waitpid(debuggee->pid, &status, 0);

    ops_detach(0);

    sigsettings(SIGKILL, &notify_backup, &pass_backup, &stop_backup, 1, error);

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_quit(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->pid != -1)
        ops_detach(0);

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
    
    return CMD_QUIT;
}

enum cmd_error_t cmdfunc_stepi(struct cmd_args_t *args, 
        int arg1, char **error){
    /* Disable breakpoints when single stepping so we don't have to deal
     * with more exceptions being raised. Instead, just check if we're at
     * a breakpointed address every time we step.
     */
    breakpoint_disable_all();

    struct machthread *focused = machthread_getfocused();

    get_debug_state(focused);
    focused->debug_state.__mdscr_el1 |= 1;
    set_debug_state(focused);

    debuggee->is_single_stepping = 1;

    ops_resume();

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_trace(struct cmd_args_t *args, 
        int arg1, char **error){
    if(debuggee->tracing_disabled){
        concat(error, "tracing is not supported on this host");
        return CMD_FAILURE;
    }
    
    if(debuggee->currently_tracing){
        concat(error, "already tracing");
        return CMD_FAILURE;
    }

    start_trace();
    
    return CMD_SUCCESS;
}
