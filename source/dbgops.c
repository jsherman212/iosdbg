#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "breakpoint.h"
#include "convvar.h"
#include "dbgops.h"
#include "debuggee.h"
#include "exception.h"
#include "linkedlist.h"
#include "memutils.h"
#include "printing.h"
#include "ptrace.h"
#include "queue.h"
#include "servers.h"
#include "sigsupport.h"
#include "strext.h"
#include "thread.h"
#include "trace.h"
#include "watchpoint.h"

void ops_printsiginfo(char **outbuffer){
    concat(outbuffer, "%-11s %-5s %-5s %-6s\n", "NAME", "PASS", "STOP", "NOTIFY");
    concat(outbuffer, "=========== ===== ===== ======\n");

    int signo = 0;

    while(signo++ < (NSIG - 1)){
        int notify, pass, stop;
        char *e = NULL;

        sigsettings(signo, &notify, &pass, &stop, 0, &e);

        if(e)
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

void ops_detach(int from_death, char **outbuffer){
    ops_suspend();

    pthread_cancel(exception_server_thread);
    pthread_cancel(death_server_thread);
    pthread_cancel(tmon_thread);

    breakpoint_delete_all();
    watchpoint_delete_all();

    /* Disable hardware single stepping on all threads. */
    for(struct node_t *current = debuggee->threads->front;
            current;
            current = current->next){
        struct machthread *t = current->data;

        get_debug_state(t);
        t->debug_state.__mdscr_el1 = 0;
        set_debug_state(t);
    }

    void *request = dequeue(debuggee->exc_requests);
    
    /* Reply to any exceptions. */
    while(request){
        reply_to_exception(request, KERN_SUCCESS);
        free(request);
        request = dequeue(debuggee->exc_requests);
    }

    debuggee->deallocate_ports(outbuffer);
    debuggee->restore_exception_ports();

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
            ret = ptrace(PT_DETACH, debuggee->pid, 0, 0);
            usleep(500);
        }

        kill(debuggee->pid, SIGCONT);
    }

    queue_free(debuggee->exc_requests);
    debuggee->exc_requests = NULL;

    linkedlist_free(debuggee->breakpoints);
    linkedlist_free(debuggee->threads);
    linkedlist_free(debuggee->watchpoints);

    debuggee->breakpoints = NULL;
    debuggee->threads = NULL;
    debuggee->watchpoints = NULL;

    debuggee->num_breakpoints = 0;
    debuggee->num_watchpoints = 0;
    debuggee->pid = -1;

    free(debuggee->debuggee_name);
    debuggee->debuggee_name = NULL;

    void_convvar("$_");
    void_convvar("$__");
    void_convvar("$ASLR");

    ops_resume();
}

kern_return_t ops_resume(void){
    return debuggee->resume();
}

kern_return_t ops_suspend(void){
    return debuggee->suspend();
}

void ops_threadupdate(char **out){
    thread_act_port_array_t threads;
    debuggee->get_threads(&threads, out);

    //printf("%s: threads %p\n", __func__, threads);

    if(!threads)
        return;

    update_thread_list(threads, out);

    struct machthread *focused = get_focused_thread();

    if(!focused){
        if(*out)
            concat(out, "\n");

        concat(out, "[Previously selected thread dead, selecting thread #1]\n\n");
        set_focused_thread(threads[0]);
        focused = get_focused_thread();
    }

    if(focused)
        update_all_thread_states(focused);

    // XXX create functions for the following later

    /* Adjust thread specific breakpoints. */
    for(struct node_t *current = debuggee->breakpoints->front;
            current;
            current = current->next){
        struct breakpoint *bp = current->data;

        if(bp->threadinfo.all || !bp->hw)
            continue;

        struct machthread *found_thread = find_thread_from_ID(bp->threadinfo.iosdbg_tid);

        /* If that didn't work, let's see if we can find our thread
         * from its pthread TID.
         */
        
        if(!found_thread){
        /*    concat(out, "%s: searching for thread based off of iosdbg"
                   " TID didn't work, trying pthread TID\n", __func__);
           */
            found_thread = find_thread_from_TID(bp->threadinfo.pthread_tid);
        }
        /*
        concat(out, "%s: bp %d: iosdbg tid %d pthread tid %#llx."
                " Found thread iosdbg tid %d thread->tid %#llx\n",
                __func__, bp->id, bp->threadinfo.iosdbg_tid,
                bp->threadinfo.pthread_tid,
                found_thread?found_thread->ID:-1,found_thread?found_thread->tid:-1);
        */
        struct machthread *correct_thread = find_thread_from_TID(bp->threadinfo.pthread_tid);

        if(found_thread && correct_thread &&
                ((found_thread->tid != bp->threadinfo.pthread_tid) ||
                (found_thread->ID != bp->threadinfo.iosdbg_tid))){
            get_debug_state(found_thread);

            found_thread->debug_state.__bcr[bp->hw_bp_reg] = 0;
            found_thread->debug_state.__bvr[bp->hw_bp_reg] = 0;
            found_thread->debug_state.__mdscr_el1 = 0;

            set_debug_state(found_thread);
            
            //printf( "%s: correct_thread: %p\n", __func__, correct_thread);

            get_debug_state(correct_thread);

            correct_thread->debug_state.__bcr[bp->hw_bp_reg] = bp->bcr;
            correct_thread->debug_state.__bvr[bp->hw_bp_reg] = bp->bvr;

            set_debug_state(correct_thread);

            bp->threadinfo.iosdbg_tid = correct_thread->ID;
            bp->threadinfo.pthread_tid = correct_thread->tid;

            concat(out, "[Corrected thread info for breakpoint %d]\n", bp->id);
        }
        else if(!found_thread || !correct_thread){
            /*
            concat(out, "%s: thread for breakpoint %d doesn't exist,"
                    " we searched for iosdbg id %#x, pthread tid %#llx\n",
                    __func__,
                    bp->id, bp->threadinfo.iosdbg_tid, bp->threadinfo.pthread_tid);
                    */
            concat(out, "\n[The thread assigned to breakpoint %d has gone"
                    " away, deleting it]\n", bp->id);
            breakpoint_delete(bp->id, NULL);
        }
    }

    /* Adjust thread specific watchpoints. */
    for(struct node_t *current = debuggee->watchpoints->front;
            current;
            current = current->next){
        struct watchpoint *wp = current->data;

        if(wp->threadinfo.all)
            continue;

        struct machthread *thread = find_thread_from_ID(wp->threadinfo.iosdbg_tid);

        if(thread->tid != wp->threadinfo.pthread_tid){
            get_debug_state(thread);

            thread->debug_state.__wcr[wp->hw_wp_reg] = 0;
            thread->debug_state.__wvr[wp->hw_wp_reg] = 0;

            set_debug_state(thread);
            
            struct machthread *correct = find_thread_from_TID(wp->threadinfo.pthread_tid);

            if(!correct){
                concat(out, "\n[The thread assigned to watchpoint %d has gone"
                        " away, deleting it]\n", wp->id);
                breakpoint_delete(wp->id, NULL);
                continue;
            }

            get_debug_state(correct);

            correct->debug_state.__wcr[wp->hw_wp_reg] = wp->wcr;
            correct->debug_state.__wvr[wp->hw_wp_reg] = wp->wvr;

            set_debug_state(correct);

            wp->threadinfo.iosdbg_tid = correct->ID;
            wp->threadinfo.pthread_tid = correct->tid;

            concat(out, "\n[Corrected thread info for watchpoint %d]\n", wp->id);
        }
    }
}
