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
#include "printutils.h"
#include "ptrace.h"
#include "queue.h"
#include "sigsupport.h"
#include "strext.h"
#include "thread.h"
#include "trace.h"
#include "watchpoint.h"

void ops_printsiginfo(void){
    printf("%-11s %-5s %-5s %-6s\n", "NAME", "PASS", "STOP", "NOTIFY");
    printf("=========== ===== ===== ======\n");

    int signo = 0;

    while(signo++ < (NSIG - 1)){
        int notify, pass, stop;
        char *e = NULL;

        sigsettings(signo, &notify, &pass, &stop, 0, &e);

        if(e){
            printf("error: %s\n", e);
            free(e);
        }

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

        printf("%-11s %-5s %-5s %-6s\n",
                fullsig, pass_str, stop_str, notify_str);

        free(fullsig);
    }
}

void ops_detach(int from_death){
    if(!from_death){
        pthread_mutex_lock(&EXCEPTION_SERVER_IS_DETACHING_MUTEX);

        debuggee->want_detach = 1;

        /* Boot exception_server out of mach_msg. SIGCONT was an arbitrary choice.
         * We have to do this first so this SIGCONT doesn't get handled.
         */
        kill(debuggee->pid, SIGCONT);

        pthread_mutex_unlock(&EXCEPTION_SERVER_IS_DETACHING_MUTEX);

        pthread_mutex_lock(&HAS_REPLIED_MUTEX);

        HAS_REPLIED_TO_LATEST_EXCEPTION = 1;
        
        /* If exception_server wasn't in mach_msg, we need to tell it to move
         * on and wait for EXCEPTION_SERVER_IS_DETACHING_COND to get signaled.
         */
        pthread_cond_signal(&MAIN_THREAD_CHANGED_REPLIED_VAR_COND);

        pthread_mutex_unlock(&HAS_REPLIED_MUTEX);

        pthread_mutex_lock(&EXCEPTION_SERVER_IS_DETACHING_MUTEX);

        /* Wait for exception_server to tell us it's ready to collect
         * the remaining exceptions.
         */
        pthread_cond_wait(&WAIT_TO_SIGNAL_EXCEPTION_SERVER_IS_DETACHING_COND,
                &EXCEPTION_SERVER_IS_DETACHING_MUTEX);

        /* Reply to the latest exception before we collect the rest. */
        void *request = dequeue(debuggee->exc_requests);

        if(request)
            reply_to_exception(request, KERN_SUCCESS);

        /* We're good to go, tell exception_server to collect the remaining
         * exceptions for the debuggee.
         */    
        pthread_cond_signal(&EXCEPTION_SERVER_IS_DETACHING_COND);

        /* Wait for exception_server to be done. */
        pthread_cond_wait(&IS_DONE_HANDLING_EXCEPTIONS_BEFORE_DETACH_COND,
                &EXCEPTION_SERVER_IS_DETACHING_MUTEX);

        debuggee->want_detach = 0;

        pthread_mutex_unlock(&EXCEPTION_SERVER_IS_DETACHING_MUTEX);
    }

    pthread_mutex_lock(&HAS_REPLIED_MUTEX);

    debuggee->suspend();
    debuggee->interrupted = 1;  

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

    /* Reply to any exceptions. */
    while(debuggee->pending_exceptions > 0){
        void *request = dequeue(debuggee->exc_requests);
        reply_to_exception(request, KERN_SUCCESS);
    }

    debuggee->deallocate_ports();
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

    debuggee->interrupted = 0;
    debuggee->last_hit_bkpt_ID = 0;
    debuggee->last_hit_wp_loc = 0;
    debuggee->last_hit_wp_PC = 0;
    debuggee->num_breakpoints = 0;
    debuggee->num_watchpoints = 0;
    debuggee->num_watchpoints = 0;
    debuggee->pid = -1;

    free(debuggee->debuggee_name);
    debuggee->debuggee_name = NULL;

    void_convvar("$_");
    void_convvar("$__");
    void_convvar("$ASLR");

    debuggee->resume();
    debuggee->interrupted = 0;

    pthread_mutex_unlock(&HAS_REPLIED_MUTEX);
}

void ops_resume(void){
    pthread_mutex_lock(&HAS_REPLIED_MUTEX);

    void *request = dequeue(debuggee->exc_requests);

    if(request){
        reply_to_exception(request, KERN_SUCCESS);
        HAS_REPLIED_TO_LATEST_EXCEPTION = 1;
    }

    /* Wake up the exception thread. */
    pthread_cond_signal(&MAIN_THREAD_CHANGED_REPLIED_VAR_COND);

    debuggee->resume();
    debuggee->interrupted = 0;

    pthread_mutex_unlock(&HAS_REPLIED_MUTEX);
}

void ops_threadupdate(void){
    thread_act_port_array_t threads;
    debuggee->get_threads(&threads);

    machthread_updatethreads(threads);

    struct machthread *focused = machthread_getfocused();

    if(!focused){
        printf("[Previously selected thread dead, selecting thread #1]\n\n");
        machthread_setfocused(threads[0]);
        focused = machthread_getfocused();
    }

    if(focused)
        machthread_updatestate(focused);
}
