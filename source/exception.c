#include <ctype.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>

#include "breakpoint.h"
#include "dbgops.h"
#include "debuggee.h"
#include "exception.h"
#include "linkedlist.h"
#include "memutils.h"
#include "printutils.h"
#include "ptrace.h"
#include "sigsupport.h"
#include "strext.h"
#include "thread.h"
#include "trace.h"
#include "watchpoint.h"

#include "cmd/misccmd.h"

/*static int JUST_HIT_WATCHPOINT;
static int JUST_HIT_BREAKPOINT;
static int JUST_HIT_SW_BREAKPOINT;
*/

static const char *exc_str(exception_type_t exception){
    switch(exception){
        case EXC_BAD_ACCESS:
            return "EXC_BAD_ACCESS";
        case EXC_BAD_INSTRUCTION:
            return "EXC_BAD_INSTRUCTION";
        case EXC_ARITHMETIC:
            return "EXC_ARITHMETIC";
        case EXC_EMULATION:
            return "EXC_EMULATION";
        case EXC_SOFTWARE:
            return "EXC_SOFTWARE";
        case EXC_BREAKPOINT:
            return "EXC_BREAKPOINT";
        case EXC_SYSCALL:
            return "EXC_SYSCALL";
        case EXC_MACH_SYSCALL:
            return "EXC_MACH_SYSCALL";
        case EXC_RPC_ALERT:
            return "EXC_RPC_ALERT";
        case EXC_CRASH:
            return "EXC_CRASH";
        case EXC_RESOURCE:
            return "EXC_RESOURCE";
        case EXC_GUARD:
            return "EXC_GUARD";
        case EXC_CORPSE_NOTIFY:
            return "EXC_CORPSE_NOTIFY";
        default:
            return "<Unknown Exception>";
    }
}

static void set_single_step(struct machthread *t, int enabled){
   //printf("*****%s: setting single step for '%s' (tid %#llx) enabled? %d\n",
   //        __func__, t->tname, t->tid, enabled);

    
    get_debug_state(t);

    if(enabled)
        t->debug_state.__mdscr_el1 |= 1;
    else
        t->debug_state.__mdscr_el1 = 0;

    set_debug_state(t);
    
/*    for(struct node_t *current = debuggee->threads->front;
            current;
            current = current->next){
        struct machthread *t = current->data;

        if(t->just_hit_breakpoint){
            get_debug_state(t);

            if(enabled)
                t->debug_state.__mdscr_el1 |= 1;
            else
                t->debug_state.__mdscr_el1 = 0;

            set_debug_state(t);
        }
    }*/
}

static void describe_hit_watchpoint(void *prev_data, void *cur_data, 
        unsigned int sz){
    long old_val = *(long *)prev_data;
    long new_val = *(long *)cur_data;

    if(sz == sizeof(char)){
        printf("Old value: %s%#x\nNew value: %s%#x\n\n", 
                (char)old_val < 0 ? "-" : "", 
                (char)old_val < 0 ? (char)-old_val : (char)old_val, 
                (char)new_val < 0 ? "-" : "", 
                (char)new_val < 0 ? (char)-new_val : (char)new_val);
    }
    else if(sz == sizeof(short)){
        printf("Old value: %s%#x\nNew value: %s%#x\n\n", 
                (short)old_val < 0 ? "-" : "", 
                (short)old_val < 0 ? (short)-old_val : (short)old_val, 
                (short)new_val < 0 ? "-" : "", 
                (short)new_val < 0 ? (short)-new_val : (short)new_val);

    }
    else if(sz == sizeof(int)){
        printf("Old value: %s%#x\nNew value: %s%#x\n\n", 
                (int)old_val < 0 ? "-" : "", 
                (int)old_val < 0 ? (int)-old_val : (int)old_val, 
                (int)new_val < 0 ? "-" : "", 
                (int)new_val < 0 ? (int)-new_val : (int)new_val);
    }
    else{
        printf("Old value: %s%#lx\nNew value: %s%#lx\n\n", 
                (long)old_val < 0 ? "-" : "", 
                (long)old_val < 0 ? (long)-old_val : (long)old_val, 
                (long)new_val < 0 ? "-" : "", 
                (long)new_val < 0 ? (long)-new_val : (long)new_val);
    }
}

static void handle_soft_signal(mach_port_t thread, long subcode, char **desc,
        int notify, int pass, int stop){
    if(debuggee->want_detach){
        ops_resume();
        return;
    }

    char *sigstr = strdup(sys_signame[subcode]);
    size_t sigstrlen = strlen(sigstr);

    for(int i=0; i<sigstrlen; i++)
        sigstr[i] = toupper(sigstr[i]);

    concat(desc, "%ld, SIG%s. ", subcode, sigstr);

    free(sigstr);

    /* If we're passing signals, don't clear them. */
    if(pass)
        return;

    ptrace(PT_THUPDATE, debuggee->pid, (caddr_t)(unsigned long long)thread, 0);
}

static void handle_hit_watchpoint(void){
    struct watchpoint *hit = find_wp_with_address(debuggee->last_hit_wp_loc);

    watchpoint_hit(hit);

    unsigned int sz = hit->data_len;

    /* Save previous data for comparison. */
    void *prev_data = malloc(sz);
    memcpy(prev_data, hit->data, sz);

    read_memory_at_location((void *)hit->location, hit->data, sz);
    
    printf("\nWatchpoint %d hit:\n\n", hit->id);

    describe_hit_watchpoint(prev_data, hit->data, sz);
    disassemble_at_location(debuggee->last_hit_wp_PC + 4, 4);

    free(prev_data);
    
    debuggee->last_hit_wp_loc = 0;
    debuggee->last_hit_wp_PC = 0;
}
extern pthread_mutex_t HAS_REPLIED_MUTEX;

extern pthread_cond_t MAIN_THREAD_CHANGED_REPLIED_VAR_COND;
extern pthread_cond_t EXC_SERVER_CHANGED_REPLIED_VAR_COND;

extern int HAS_REPLIED_TO_LATEST_EXCEPTION;
static void resume_after_exception(Request *request){
//    reply_to_exception(request, KERN_SUCCESS);
    //dequeue(debuggee->exc_requests);
    
    /* This will reply to the latest exception. */
    //pthread_mutex_unlock(&HAS_REPLIED_MUTEX);
    //HAS_REPLIED_TO_LATEST_EXCEPTION = 0;
    //ops_resume();
    //pthread_mutex_lock(&HAS_REPLIED_MUTEX);

    /*void *req = dequeue(debuggee->exc_requests);
    //void *req = debuggee->exc_requests->front->data;
    //printf("%s: dequeueing request %p\n", __func__, req);

    if(req){
        printf("%d. *****START REPLYING\n", debuggee->exc_num);
        reply_to_exception(req, KERN_SUCCESS);
        printf("%d. *****END REPLYING\n", debuggee->exc_num);

        //req = dequeue(debuggee->exc_requests);
        //debuggee->pending_messages--;
       // linkedlist_delete(debuggee->exc_requests, req);
        //if(debuggee->exc_requests->front)
         //   req = debuggee->exc_requests->front->data;
    }
    debuggee->resume();
    debuggee->interrupted = 0;
    */

    pthread_cond_signal(&EXC_SERVER_CHANGED_REPLIED_VAR_COND);
    pthread_mutex_unlock(&HAS_REPLIED_MUTEX);

    ops_resume();

    pthread_mutex_lock(&HAS_REPLIED_MUTEX);

    if(debuggee->currently_tracing){
        rl_already_prompted = 1;
        putchar('\n');
    }
}

static void handle_single_step(struct machthread *t, Request *request){
    /* Re-enable all the breakpoints we disabled while performing the
     * single step. This function is called when the CPU raises the software
     * step exception after the single step occurs.
     */
    //breakpoint_enable_all();

    //printf("*****%s: JUST_HIT_BREAKPOINT %d\n", __func__, JUST_HIT_BREAKPOINT);

    //printf("*****%s: thread '%s' (tid %#llx) just hit a breakpoint? %d\n",
      //      __func__, t->tname, t->tid, t->just_hit_breakpoint);

    // XXX if debuggee->is_single_stepping enable all breakpoints?

    //if(JUST_HIT_BREAKPOINT){
    if(t->just_hit_breakpoint){
        //breakpoint_enable(debuggee->last_hit_bkpt_ID, NULL);
        //if(JUST_HIT_SW_BREAKPOINT){
        if(t->just_hit_sw_breakpoint){
#if 0
            printf("%s: we just hit a sw bp (id %d), re-enabling...\n",
                    __func__, debuggee->last_hit_bkpt_ID);
#endif
            breakpoint_enable(debuggee->last_hit_bkpt_ID, NULL);
         //   JUST_HIT_SW_BREAKPOINT = 0;
            t->just_hit_sw_breakpoint = 0;
        }

        get_thread_state(t);

        /* If we caused a software step exception to get past a breakpoint,
         * just continue as normal. Otherwise, if we manually single step
         * right after a breakpoint hit, just print the disassembly.
         */
        if(!debuggee->is_single_stepping){
     //       printf("*****%s: resuming after software step exception for '%s' tid %#llx\n",
       //             __func__, t->tname, t->tid);
            set_single_step(t, 0);
            resume_after_exception(request);
        }
        else{
            disassemble_at_location(t->thread_state.__pc, 4);
        }

        //JUST_HIT_BREAKPOINT = 0;
        t->just_hit_breakpoint = 0;

        debuggee->is_single_stepping = 0;

        return;
    }

    putchar('\n');
    disassemble_at_location(t->thread_state.__pc, 4);
    set_single_step(t, 0);

    debuggee->is_single_stepping = 0;
}

static void handle_hit_breakpoint(struct machthread *t,
        long subcode, char **desc){
    struct breakpoint *hit = find_bp_with_address(subcode);

    breakpoint_hit(hit);

    concat(desc, " breakpoint %d at %#lx hit %d time(s).\n",
            hit->id, hit->location, hit->hit_count);

    if(!hit->hw){
        //JUST_HIT_SW_BREAKPOINT = 1;
        t->just_hit_sw_breakpoint = 1;
#if 0
        printf("%s: we just hit a sw bp (id %d), disabling for the single step...\n",
                __func__, hit->id);
#endif
        breakpoint_disable(hit->id, NULL);
    }
    //breakpoint_disable(hit->id, NULL);

    debuggee->last_hit_bkpt_ID = hit->id;
}

void handle_exception(Request *request){
    /* When an exception occurs, there is a left over (iosdbg) prompt,
     * and this gets rid of it.
     */
    rl_clear_visible_line();
    rl_already_prompted = 0;

    /* Finish printing everything while tracing so
     * we don't get caught in the middle of it.
     */
    wait_for_trace();

    if(!debuggee->interrupted){
        debuggee->suspend();
        debuggee->interrupted = 1;
    }

    mach_port_t task = request->task.name;
    mach_port_t thread = request->thread.name;
    exception_type_t exception = request->exception;
    const char *exc = exc_str(exception);
    long code = ((long *)request->code)[0];
    long subcode = ((long *)request->code)[1];

    printf("Exception for task %x (debuggee task %x), thread %x, code %ld subcode %ld, exc '%s'\n",
            task, debuggee->task, thread, code, subcode, exc);

    /* Give focus to whatever caused this exception. */
    struct machthread *focused = machthread_getfocused();

    if(!focused || focused->port != thread){
//        printf("\n[Switching to thread %#llx]\n", 
  //              (unsigned long long)get_tid_from_thread_port(thread));
        machthread_setfocused(thread);
        focused = machthread_getfocused();
    }

    get_thread_state(focused);

    unsigned long tid = get_tid_from_thread_port(thread);
    char *tname = get_thread_name_from_thread_port(thread);

    char *desc = NULL;
    concat(&desc, "\n * Thread #%d (tid = %#llx)", focused->ID, tid);

    /* A number of things could have happened to cause an exception:
     *      - hardware breakpoint
     *      - hardware watchpoint
     *      - software breakpoint
     *      - software single step exception
     *      - Unix soft signal
     */
    /* Unix soft signal. */
    if(exception == EXC_SOFTWARE && code == EXC_SOFT_SIGNAL){
        int notify, pass, stop;
        char *error = NULL;

        sigsettings(subcode, &notify, &pass, &stop, 0, &error);

        if(error){
            printf("error: %s\n", error);
            free(error);
        }
        
        concat(&desc, ", '%s' received signal ", tname);
        handle_soft_signal(thread, subcode, &desc, notify, pass, stop);
        
        if(stop)
            concat(&desc, "%#llx in debuggee.\n", focused->thread_state.__pc);
        else{
            concat(&desc, "Resuming execution.");
            ops_resume();
        }
        
        /* Don't print any of this if we're detaching. */
        if(notify && !debuggee->want_detach){
            printf("%s", desc);

            if(stop)
                disassemble_at_location(focused->thread_state.__pc, 4);
        }

        free(tname);
        free(desc);

        safe_reprompt();
    }
    /* A hardware watchpoint hit. However, we need to single step in 
     * order for the CPU to execute the instruction at this address
     * so the value actually changes.
     */
    else if(code == EXC_ARM_DA_DEBUG){
        //JUST_HIT_WATCHPOINT = 1;
        focused->just_hit_watchpoint = 1;

        debuggee->last_hit_wp_loc = subcode;
        debuggee->last_hit_wp_PC = focused->thread_state.__pc;

        set_single_step(focused, 1);
        
        /* Continue execution so the software step exception occurs. */
        resume_after_exception(request);

        free(tname);
    }
    /* A hardware/software breakpoint hit, or the software step
     * exception has occured.
     */
    else if(exception == EXC_BREAKPOINT && code == EXC_ARM_BREAKPOINT){
        if(subcode == 0){
            //if(JUST_HIT_WATCHPOINT){
            if(focused->just_hit_watchpoint){
                handle_hit_watchpoint();

                //JUST_HIT_WATCHPOINT = 0;
                focused->just_hit_watchpoint = 0;
                
                safe_reprompt();

                free(tname);

                return;
            }

            /* If we single step over where a breakpoint is set,
             * we should report it and count it as hit.
             */ 
            struct breakpoint *hit = find_bp_with_address(
                    focused->thread_state.__pc);

            if(debuggee->is_single_stepping && hit){
                breakpoint_hit(hit);

                concat(&desc, ": '%s': breakpoint %d at %#lx hit %d time(s).\n",
                        tname, hit->id, hit->location, hit->hit_count);

                printf("%s", desc);
            }

        //    printf("*****%s: about to handle software step exception for '%s' tid %#llx\n",
          //          __func__, focused->tname, focused->tid);
    
            handle_single_step(focused, request);

            safe_reprompt();

            free(desc);
            free(tname);

            return;
        }
        
        //JUST_HIT_BREAKPOINT = 1;
        focused->just_hit_breakpoint = 1;

        concat(&desc, ": '%s':", tname);
        handle_hit_breakpoint(focused, subcode, &desc);

        printf("%s", desc);

        free(desc);
        free(tname);

        disassemble_at_location(focused->thread_state.__pc, 4);
        set_single_step(focused, 1);
        
        safe_reprompt();
    }
}

void reply_to_exception(Request *req, kern_return_t retcode){
    Reply reply;
    
    mach_msg_header_t *rpl_head = &reply.Head;

    /* This is from mach_excServer.c. */
    rpl_head->msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(
                req->Head.msgh_bits), 0);
    rpl_head->msgh_remote_port = req->Head.msgh_remote_port;
    rpl_head->msgh_size = (mach_msg_size_t)sizeof(mig_reply_error_t);
    rpl_head->msgh_local_port = MACH_PORT_NULL;
    rpl_head->msgh_id = req->Head.msgh_id + 100;
    rpl_head->msgh_reserved = 0;

    reply.NDR = req->NDR;
    reply.RetCode = retcode;

    mach_msg(&reply.Head,
            MACH_SEND_MSG,
            reply.Head.msgh_size,
            0,
            MACH_PORT_NULL,
            MACH_MSG_TIMEOUT_NONE,
            MACH_PORT_NULL);

    debuggee->pending_exceptions--;
}
