#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <readline/readline.h>

#include "documentation.h"
#include "misccmd.h"

#include "../breakpoint.h"
#include "../convvar.h"
#include "../dbgio.h"
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
#include "../watchpoint.h"

#include "../symbol/dbgsymbol.h"
#include "../symbol/image.h"
#include "../symbol/sym.h"

int KEEP_CHECKING_FOR_PROCESS;

static pid_t parse_pid(char *pidstr, char **error){
    return is_number_fast(pidstr) ? (pid_t)strtol_err(pidstr, error)
        : pid_of_program(pidstr, error);
}

enum cmd_error_t cmdfunc_aslr(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    concat(outbuffer, "%4s%#lx\n", "", debuggee->aslr_slide);
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_attach(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    char *waitfor = argcopy(args, ATTACH_COMMAND_REGEX_GROUPS[0]);
    char *target = argcopy(args, ATTACH_COMMAND_REGEX_GROUPS[1]);

    /* Check if the user wants to attach to something else while attached
     * to something.
     */
    if(debuggee->pid != -1){
        char ans = answer("Detach from %s and reattach to %s? (y/n) ", 
                debuggee->debuggee_name, target);

        if(ans == 'n'){
            free(waitfor);
            free(target);

            return CMD_SUCCESS;
        }

        /* Detach from what we are attached to
         * and call this function again.
         */
        if(*outbuffer){
            free(*outbuffer);
            *outbuffer = NULL;
        }

        cmdfunc_detach(NULL, 0, outbuffer, NULL);

        /* Re-construct the argument queue for the next call. */
        argins(args, ATTACH_COMMAND_REGEX_GROUPS[0], waitfor ? strdup(waitfor) : NULL);
        argins(args, ATTACH_COMMAND_REGEX_GROUPS[1], strdup(target));

        free(waitfor);
        free(target);

        return cmdfunc_attach(args, 0, outbuffer, error);
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
        free(waitfor);
        free(target);

        return CMD_FAILURE;
    }

    kern_return_t err = task_for_pid(mach_task_self(), 
            target_pid, &debuggee->task);

    if(err){
        concat(error, "couldn't get task port for %s (pid: %d): %s\n"
                "Did you forget to sign iosdbg with entitlements?\n"
                "Are you privileged enough to debug this process?",
                target, target_pid, mach_error_string(err));

        free(waitfor);
        free(target);

        return CMD_FAILURE;
    }

    debuggee->aslr_slide = debuggee->find_slide();

    if(debuggee->aslr_slide == -1)
        concat(outbuffer, "warning: couldn't find debuggee's ASLR slide\n");

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

    BP_LOCK;
    debuggee->breakpoints = linkedlist_new();
    BP_UNLOCK;

    WP_LOCK;
    debuggee->watchpoints = linkedlist_new();
    WP_UNLOCK;

    TH_LOCK;
    debuggee->threads = linkedlist_new();
    TH_UNLOCK;
    
    debuggee->num_breakpoints = 0;
    debuggee->num_watchpoints = 0;

    thread_act_port_array_t threads;
    mach_msg_type_number_t cnt;
    debuggee->get_threads(&threads, &cnt, outbuffer);

    debuggee->thread_count = cnt;
    
    resetmtid();
    
    update_thread_list(threads, debuggee->thread_count, outbuffer);
    set_focused_thread(threads[0]);

    struct machthread *focused = get_focused_thread();
    get_thread_state(focused);

    setup_servers(outbuffer);

    concat(outbuffer, "Attached to %s (pid: %d), slide: %#lx.\n",
            debuggee->debuggee_name, debuggee->pid, debuggee->aslr_slide);

    /* Have Unix signals be sent as Mach exceptions. */
    ptrace(PT_ATTACHEXC, debuggee->pid, 0, 0);

    void_convvar("$_exitcode");
    void_convvar("$_exitsignal");

    char *aslr = NULL;
    concat(&aslr, "%#llx", debuggee->aslr_slide);

    char *e = NULL;
    set_convvar("$ASLR", aslr, &e);

    if(e)
        concat(outbuffer, "warning: %s\n", e);

    free(e);
    free(aslr);
    free(target);

    // XXX no hardcode on master
    const char *dscp = "/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64";

    const char *dscerrmsg = "warning: could not properly examine"
        " the debuggee's dyld_all_image_infos structure, symbolication"
        " will be minimal.\n";

    struct stat st = {0};
    stat(dscp, &st);

    int dscfd = open(dscp, O_RDONLY, 0);

    if(dscfd == -1){
        concat(outbuffer, "%s", dscerrmsg);
        return CMD_SUCCESS;
    }

    void *dscdata = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, dscfd, 0);

    if(initialize_debuggee_dyld_all_image_infos(dscdata))
        concat(outbuffer, "%s", dscerrmsg);

    munmap(dscdata, st.st_size);
    close(dscfd);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_backtrace(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    if(!debuggee->suspended()){
        concat(error, "debuggee must be suspended");
        return CMD_FAILURE;
    }

    struct machthread *focused = get_focused_thread();

    get_thread_state(focused);
    
    char *pc_srcfile = NULL, *pc_srcfunc = NULL, *lr_srcfile = NULL,
         *lr_srcfunc = NULL;
    uint64_t pc_srcfileline = 0, lr_srcfileline = 0;
    void *root_die = NULL;

    int has_debug_info = debuggee->has_debug_info();
        
    concat(outbuffer, "  * frame #0: 0x%16.16llx", focused->thread_state.__pc);

    char *imgname = NULL, *symname = NULL;
    unsigned int symdist = 0;

    if(get_symbol_info_from_address(debuggee->symbols,
            focused->thread_state.__pc, &imgname, &symname, &symdist)){
        concat(outbuffer, "\n");
    }
    else{
        concat(outbuffer, " %s`%s + %#lx\n", imgname, symname, symdist);
    }

    free(imgname);
    free(symname);
    imgname = NULL;
    symname = NULL;
    symdist = 0;

    /*
    if(!has_debug_info || sym_get_line_info_from_pc(debuggee->dwarfinfo,
                focused->thread_state.__pc - debuggee->aslr_slide,
                &pc_srcfile, &pc_srcfunc, &pc_srcfileline, &root_die, NULL)){
        concat(outbuffer, "\n");
    }
    else{
        concat(outbuffer, " %s at %s:%lld\n", pc_srcfunc, pc_srcfile,
                pc_srcfileline);
        free(pc_srcfile);
        free(pc_srcfunc);
    }
    */

    concat(outbuffer, "    frame #1: 0x%16.16llx", focused->thread_state.__lr);

    if(get_symbol_info_from_address(debuggee->symbols,
            focused->thread_state.__lr, &imgname, &symname, &symdist)){
        concat(outbuffer, "\n");
    }
    else{
        concat(outbuffer, " %s`%s + %#lx\n", imgname, symname, symdist);
    }

    free(imgname);
    free(symname);
    imgname = NULL;
    symname = NULL;
    symdist = 0;
    /*
    if(!has_debug_info || sym_get_line_info_from_pc(debuggee->dwarfinfo,
                focused->thread_state.__lr - debuggee->aslr_slide,
                &lr_srcfile, &lr_srcfunc, &lr_srcfileline, &root_die, NULL)){
        concat(outbuffer, "\n");
    }
    else{
        concat(outbuffer, " %s at %s:%lld\n", lr_srcfunc, lr_srcfile,
                lr_srcfileline);
        free(lr_srcfile);
        free(lr_srcfunc);
    }*/

    struct frame_t {
        struct frame_t *next;
        unsigned long frame;
    };

    struct frame_t *current_frame = malloc(sizeof(struct frame_t));
    kern_return_t err = read_memory_at_location(focused->thread_state.__fp,
            current_frame, sizeof(struct frame_t));
    
    if(err){
        concat(error, "backtrace failed: %s", mach_error_string(err));
        return CMD_FAILURE;
    }

    int frame_counter = 2;

    while(current_frame->next){
        concat(outbuffer, "%4sframe #%d: 0x%16.16lx", "", frame_counter,
                current_frame->frame);

        char *imgname = NULL, *symname = NULL;
        unsigned int symdist = 0;

        if(get_symbol_info_from_address(debuggee->symbols,
                    current_frame->frame, &imgname, &symname, &symdist)){
            concat(outbuffer, "\n");
        }
        else{
            concat(outbuffer, " %s`%s + %#lx\n", imgname, symname, symdist);
        }

        free(imgname);
        free(symname);

        read_memory_at_location((uintptr_t)current_frame->next, 
                (void *)current_frame, sizeof(struct frame_t)); 
        frame_counter++;
    }

    concat(outbuffer, " - cannot unwind past frame %d -\n", frame_counter - 1);

    free(current_frame);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_continue(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    if(!debuggee->suspended())
        return CMD_FAILURE;

    breakpoint_delete_all_specific(BP_COND_STEPPING);

    ops_resume();

    concat(outbuffer, "Process %d resuming\n", debuggee->pid);

    /* Make output look nicer. */
    if(debuggee->currently_tracing){
        rl_already_prompted = 1;
        concat(outbuffer, "\n");
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_detach(struct cmd_args_t *args, 
        int from_death, char **outbuffer, char **error){
    if(!debuggee->tracing_disabled)
        stop_trace();

    ops_detach(from_death, outbuffer);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_evaluate(struct cmd_args_t *args, 
        int from_death, char **outbuffer, char **error){
    char *expr = argcopy(args, EVALUATE_COMMAND_REGEX_GROUPS[0]);

    static int cnt = 0;

    while(expr){
        char *e = NULL;
        long result = eval_expr(expr, &e);

        if(e){
            concat(outbuffer, "could not evaluate expr %d: %s\n", cnt, e);
            free(e);
        }
        else{
            concat(outbuffer, "$%d = %ld\n", cnt, result);

            char *name = NULL;
            char *value = NULL;

            concat(&name, "$%d", cnt);
            concat(&value, "%ld", result);

            char *e = NULL;

            set_convvar(name, value, &e);

            free(e);
            free(name);
            free(value);

            cnt++;
        }

        free(expr);
        expr = argcopy(args, EVALUATE_COMMAND_REGEX_GROUPS[0]);
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_help(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    if(args->num_args == 0){
        show_all_top_level_cmds(outbuffer);
        return CMD_SUCCESS;
    }

    char *cmd = argcopy(args, HELP_COMMAND_REGEX_GROUPS[0]);
    documentation_for_cmdname(cmd, outbuffer, error);

    free(cmd);

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_interrupt(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    if(debuggee->pid == -1)
        return CMD_FAILURE;

    TH_LOCKED_FOREACH(current){
        struct machthread *t = current->data;

        get_debug_state(t);
        t->debug_state.__mdscr_el1 = 0;
        set_debug_state(t);
    }
    TH_END_LOCKED_FOREACH;

    kill(debuggee->pid, SIGSTOP);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_kill(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
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

    pid_t p = debuggee->pid;

    ops_detach(0, outbuffer);

    kill(p, SIGKILL);

    int status;
    waitpid(p, &status, 0);

    sigsettings(SIGKILL, &notify_backup, &pass_backup, &stop_backup, 1, error);

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_quit(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    if(debuggee->pid != -1)
        ops_detach(0, outbuffer);

    /* Free the arrays made from the trace.codes file. */
    if(!debuggee->tracing_disabled){
        stop_trace();
        
        for(int i=0; i<bsd_syscalls_arr_len; i++)
            free(bsd_syscalls[i]);

        free(bsd_syscalls);

        for(int i=0; i<mach_traps_arr_len; i++)
            free(mach_traps[i]);

        free(mach_traps);

        for(int i=0; i<mach_traps2_arr_len; i++)
            free(mach_traps2[i]);

        free(mach_traps2);
    }

    free(debuggee);
    
    return CMD_QUIT;
}

enum cmd_error_t cmdfunc_trace(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
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
