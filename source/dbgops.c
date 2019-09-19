#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "breakpoint.h"
#include "convvar.h"
#include "dbgops.h"
#include "debuggee.h"
#include "exception.h"
#include "linkedlist.h"
#include "memutils.h"
#include "ptrace.h"
#include "queue.h"
#include "servers.h"
#include "sigsupport.h"
#include "strext.h"
#include "thread.h"
#include "trace.h"
#include "watchpoint.h"

#include "symbol/dbgsymbol.h"
#include "symbol/sym.h"

void ops_printsiginfo(char **outbuffer){
    concat(outbuffer, "%-11s %-5s %-5s %-6s\n", "NAME", "PASS", "STOP", "NOTIFY");
    concat(outbuffer, "=========== ===== ===== ======\n");

    int signo = 0;

    while(signo++ < (NSIG - 1)){
        int notify, pass, stop;
        char *e = NULL;

        sigsettings(signo, &notify, &pass, &stop, 0, &e);

        free(e);

        char *sigstr = strdup(sys_signame[signo]);
        size_t sigstrlen = strlen(sigstr);

        for(int i=0; i<sigstrlen; i++)
            sigstr[i] = toupper(sigstr[i]);
        
        char *fullsig = NULL;
        concat(&fullsig, "SIG%s", sigstr);
        free(sigstr);

        const char *notify_str = notify ? "true" : "false";
        const char *pass_str = pass ? "true" : "false";
        const char *stop_str = stop ? "true" : "false";

        concat(outbuffer, "%-11s %-5s %-5s %-6s\n",
                fullsig, pass_str, stop_str, notify_str);

        free(fullsig);
    }
}

static void reply_to_all_exceptions(void){
    EXC_QUEUE_LOCK;

    if(NEED_REPLY){
        Request *r = dequeue(EXCEPTION_QUEUE);

        while(r){
            reply_to_exception(r, KERN_SUCCESS);
            free(r);
            r = dequeue(EXCEPTION_QUEUE);
        }

        NEED_REPLY = 0;
    }

    pthread_mutex_unlock(&EXCEPTION_QUEUE_MUTEX);
}

void ops_detach(int from_death, char **outbuffer){
    ops_suspend();

    munmap(DSCDATA, DSCSZ);

    breakpoint_delete_all();
    watchpoint_delete_all();

    TH_LOCKED_FOREACH(current){
        struct machthread *t = current->data;

        get_debug_state(t);
        t->debug_state.__mdscr_el1 = 0;
        set_debug_state(t);
    }
    TH_END_LOCKED_FOREACH;

    debuggee->restore_exception_ports();

    reply_to_all_exceptions();

    debuggee->deallocate_ports(outbuffer);

    /* Send SIGSTOP to set debuggee's process status to
     * SSTOP so we can detach. Calling ptrace with PT_THUPDATE
     * to handle Unix signals sets this status to SRUN, and ptrace 
     * bails if this status is SRUN. See bsd/kern/mach_process.c
     */
    if(!from_death){
        kill(debuggee->pid, SIGSTOP);
        int ret = ptrace(PT_DETACH, debuggee->pid, (caddr_t)1, 0);

        /* In some cases, it takes a while for the debuggee to notice the
         * SIGSTOP.
         */
        while(ret == -1){
            ret = ptrace(PT_DETACH, debuggee->pid, (caddr_t)1, 0);
            usleep(500);
        }

        kill(debuggee->pid, SIGCONT);
    }

    EXC_QUEUE_LOCK;
    queue_free(EXCEPTION_QUEUE);
    EXCEPTION_QUEUE = NULL;
    EXC_QUEUE_UNLOCK;

    BP_LOCK;
    linkedlist_free(debuggee->breakpoints);
    debuggee->breakpoints = NULL;
    BP_UNLOCK;

    WP_LOCK;
    linkedlist_free(debuggee->watchpoints);
    debuggee->watchpoints = NULL;
    WP_UNLOCK;

    TH_LOCK;
    linkedlist_free(debuggee->threads);
    debuggee->threads = NULL;
    TH_UNLOCK;

    concat(outbuffer, "Detached from %s (%d)\n",
            debuggee->debuggee_name, debuggee->pid);

    free(debuggee->debuggee_name);
    debuggee->debuggee_name = NULL;

    debuggee->num_breakpoints = 0;
    debuggee->num_watchpoints = 0;
    debuggee->pid = -1;

    void_convvar("$_");
    void_convvar("$__");
    void_convvar("$ASLR");

    destroy_all_symbol_entries();

    linkedlist_free(debuggee->symbols);
    debuggee->symbols = NULL;

    sym_end(debuggee->dwarfinfo);
    free(debuggee->dwarfinfo);
    debuggee->dwarfinfo = NULL;

    reset_unnamed_sym_cnt();

    ops_resume();
}

kern_return_t ops_resume(void){
    reply_to_all_exceptions();

    return debuggee->resume();
}

kern_return_t ops_suspend(void){
    return debuggee->suspend();
}

enum { BP, WP };

static void delete_invalid_bp_or_wp(int which, void *data, char **out){
    concat(out, "\n[The thread assigned to %s %d has gone"
            " away, deleting it]\n",
            which == BP ? "breakpoint" : "watchpoint",
            which == BP ?
            ((struct breakpoint *)data)->id :
            ((struct watchpoint *)data)->id
          );

    which == BP ?
        breakpoint_delete_specific(data) :
        watchpoint_delete_specific(data);
}

static void update_bp_or_wp_with_correct_threadinfo(int which,
        struct machthread *t, void *data, char **out){
    which == BP ?
        (((struct breakpoint *)data)->threadinfo.iosdbg_tid = t->ID) :
        (((struct watchpoint *)data)->threadinfo.iosdbg_tid = t->ID);
    which == BP ?
        (((struct breakpoint *)data)->threadinfo.pthread_tid = t->tid) :
        (((struct watchpoint *)data)->threadinfo.pthread_tid = t->tid);

    if(which == BP){
        struct breakpoint *b = data;

        free(b->threadinfo.tname);
        b->threadinfo.tname = NULL;
        concat(&b->threadinfo.tname, "%s", t->tname);
    }
    else{
        struct watchpoint *w = data;

        free(w->threadinfo.tname);
        w->threadinfo.tname = NULL;
        concat(&w->threadinfo.tname, "%s", t->tname);
    }

    concat(out, "\n[Corrected thread info for %s %d]\n",
            which == BP ? "breakpoint" : "watchpoint",
            which == BP ?
            ((struct breakpoint *)data)->id :
            ((struct watchpoint *)data)->id
          );
}

static void set_correct_thread_debug_state(int which, struct machthread *t,
        void *data){
    get_debug_state(t);

    if(which == BP){
        struct breakpoint *b = data;

        t->debug_state.__bcr[b->hw_bp_reg] = b->bcr;
        t->debug_state.__bvr[b->hw_bp_reg] = b->bvr;
    }
    else{
        struct watchpoint *w = data;

        t->debug_state.__wcr[w->hw_wp_reg] = w->wcr;
        t->debug_state.__wvr[w->hw_wp_reg] = w->wvr;
    }

    set_debug_state(t);
}

static void clear_wrong_thread_debug_state(int which, struct machthread *t,
        void *data){
    get_debug_state(t);

    if(which == BP){
        struct breakpoint *b = data;

        t->debug_state.__bcr[b->hw_bp_reg] = 0;
        t->debug_state.__bvr[b->hw_bp_reg] = 0;
    }
    else{
        struct watchpoint *w = data;

        t->debug_state.__wcr[w->hw_wp_reg] = 0;
        t->debug_state.__wvr[w->hw_wp_reg] = 0;
    }

    t->debug_state.__mdscr_el1 = 0;

    set_debug_state(t);
}

static void fetch_bp_or_wp_threads(int which, void *data,
        struct machthread **found, struct machthread **correct){
    struct machthread *found_thread = find_thread_from_ID(which == BP ? 
            ((struct breakpoint *)data)->threadinfo.iosdbg_tid :
            ((struct watchpoint *)data)->threadinfo.iosdbg_tid);
    struct machthread *correct_thread = NULL;

    if(!found_thread){
        found_thread = find_thread_from_TID(which == BP ?
            ((struct breakpoint *)data)->threadinfo.pthread_tid :
            ((struct watchpoint *)data)->threadinfo.pthread_tid);
        correct_thread = found_thread;
    }
    else{
        correct_thread = find_thread_from_TID(which == BP ?
            ((struct breakpoint *)data)->threadinfo.pthread_tid :
            ((struct watchpoint *)data)->threadinfo.pthread_tid);
    }

    *found = found_thread;
    *correct = correct_thread;
}

static void adjust_breakpoints(char **out){
    BP_LOCK;
    if(!debuggee->breakpoints){
        BP_UNLOCK;
        return;
    }
    BP_UNLOCK;

    BP_LOCKED_FOREACH(current){
        struct breakpoint *bp = current->data;

        if(bp->threadinfo.all || !bp->hw)
            continue;

        struct machthread *found = NULL, *correct = NULL;
        fetch_bp_or_wp_threads(BP, bp, &found, &correct);

        if(found && correct &&
                ((found->tid != bp->threadinfo.pthread_tid) ||
                 (found->ID != bp->threadinfo.iosdbg_tid))){
            clear_wrong_thread_debug_state(BP, found, bp);
            set_correct_thread_debug_state(BP, correct, bp);
            update_bp_or_wp_with_correct_threadinfo(BP, correct, bp, out);
        }
        else if(!found || !correct){
            delete_invalid_bp_or_wp(BP, bp, out);
        }
    }
    BP_END_LOCKED_FOREACH;
}

static void adjust_watchpoints(char **out){
    WP_LOCK;
    if(!debuggee->watchpoints){
        WP_UNLOCK;
        return;
    }
    WP_UNLOCK;

    WP_LOCKED_FOREACH(current){
        struct watchpoint *wp = current->data;

        if(wp->threadinfo.all)
            continue;

        struct machthread *found = NULL, *correct = NULL;
        fetch_bp_or_wp_threads(WP, wp, &found, &correct);

        if(found && correct &&
                ((found->tid != wp->threadinfo.pthread_tid) ||
                 (found->ID != wp->threadinfo.iosdbg_tid))){
            clear_wrong_thread_debug_state(WP, found, wp);
            set_correct_thread_debug_state(WP, correct, wp);
            update_bp_or_wp_with_correct_threadinfo(WP, correct, wp, out);
        }
        else if(!found || !correct){
            delete_invalid_bp_or_wp(WP, wp, out);
        }
    }
    WP_END_LOCKED_FOREACH;
}

static void adjust_bps_and_wps(char **out){
    adjust_breakpoints(out);
    adjust_watchpoints(out);
}

void ops_threadupdate(char **out){
    thread_act_port_array_t threads;
    mach_msg_type_number_t cnt;
    debuggee->get_threads(&threads, &cnt, out);

    if(!threads)
        return;

    update_thread_list(threads, cnt, out);

    struct machthread *focused = get_focused_thread();

    if(!focused){
        if(*out)
            concat(out, "\n");

        concat(out, "[Previously selected thread dead, selecting thread #1]\n");
        set_focused_thread(threads[0]);
        focused = get_focused_thread();
    }

    if(focused)
        update_all_thread_states(focused);

    adjust_bps_and_wps(out);

    kern_return_t ret = vm_deallocate(mach_task_self(),
            (vm_address_t)threads, cnt * sizeof(mach_port_t));

    if(ret){
        concat(out, "warning: vm_deallocate: %s\n", __func__,
                mach_error_string(ret));
    }
}
