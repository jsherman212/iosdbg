/*
 * This file contains functions that could be needed anywhere.
 * The idea is to not have to manually call a "cmdfunc" when
 * we need certain functionality elsewhere.
 */

#include <ctype.h>
#include <dlfcn.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>

#include "breakpoint.h"
#include "convvar.h"
#include "defs.h"
#include "dbgops.h"
#include "exception.h"
#include "linkedlist.h"
#include "sigsupport.h"
#include "watchpoint.h"

void ops_printsiginfo(void){
    printf("%-11s %-5s %-5s %-6s\n", "NAME", "PASS", "STOP", "NOTIFY");
    printf("=========== ===== ===== ======\n");

    /* Signals start at one. */
    int signo = 0;

    while(signo++ < NSIG){
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
        asprintf(&fullsig, "SIG%s", sigstr);
        free(sigstr);

        const char *notify_str = notify ? "true" : "false";
        const char *pass_str = pass ? "true" : "false";
        const char *stop_str = stop ? "true" : "false";

        printf("%-11s %-5s %-5s %-6s\n",
                fullsig, pass_str, notify_str, stop_str);

        free(fullsig);
    }
}

void ops_detach(int from_death){
    debuggee->want_detach = 1;

    void_convvar("$_");
    void_convvar("$__");

    breakpoint_delete_all();
    watchpoint_delete_all();

    /* Disable hardware single stepping. */
    debuggee->get_debug_state();
    debuggee->debug_state.__mdscr_el1 = 0;
    debuggee->set_debug_state();

    ops_resume();

    /* Send SIGSTOP to set debuggee's process status to
     * SSTOP so we can detach. Calling ptrace with PT_THUPDATE
     * to handle Unix signals sets this status to SRUN, and ptrace 
     * bails if this status is SRUN. See bsd/kern/mach_process.c
     */
    if(!from_death){
        kill(debuggee->pid, SIGSTOP);
        ptrace(PT_DETACH, debuggee->pid, 0, 0);
        kill(debuggee->pid, SIGCONT);
    }

    debuggee->deallocate_ports();
    debuggee->restore_exception_ports();

    linkedlist_free(debuggee->breakpoints);
    linkedlist_free(debuggee->watchpoints);
    linkedlist_free(debuggee->threads);

    debuggee->breakpoints = NULL;
    debuggee->watchpoints = NULL;
    debuggee->threads = NULL;

    debuggee->interrupted = 0;
    debuggee->last_hit_bkpt_ID = 0;
    debuggee->last_hit_wp_loc = 0;
    debuggee->last_hit_wp_PC = 0;
    debuggee->num_breakpoints = 0;
    debuggee->num_watchpoints = 0;
    debuggee->pid = -1;

    free(debuggee->debuggee_name);
    debuggee->debuggee_name = NULL;

    debuggee->want_detach = 0;
}

void ops_resume(void){
    reply_to_exception(debuggee->exc_request, KERN_SUCCESS);
    debuggee->resume();
    debuggee->interrupted = 0;
}
