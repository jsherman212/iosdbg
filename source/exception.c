#include <ctype.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>

#include "breakpoint.h"
#include "dbgcmd.h"
#include "dbgops.h"
#include "exception.h"      /* Includes defs.h */
#include "machthread.h"
#include "memutils.h"
#include "printutils.h"
#include "sigsupport.h"
#include "strext.h"
#include "trace.h"
#include "watchpoint.h"

int JUST_HIT_WATCHPOINT;
int JUST_HIT_BREAKPOINT;
int JUST_HIT_SW_BREAKPOINT;

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

static void set_single_step(int enabled){
    debuggee->get_debug_state();

    if(enabled)
        debuggee->debug_state.__mdscr_el1 |= 1;
    else
        debuggee->debug_state.__mdscr_el1 = 0;

    debuggee->set_debug_state();
}

static void describe_hit_watchpoint(void *prev_data, void *cur_data, 
        unsigned int sz){
    long old_val = *(long *)prev_data;
    long new_val = *(long *)cur_data;

    /* I'd like output in hex, but %x specifies unsigned int, 
     * and data could be negative. This is a hacky workaround.
     */
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

    /* This should never happen... but just in case? */
    if(!hit)
        return;

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

static void resume_after_exception(void){
    reply_to_exception(debuggee->exc_request, KERN_SUCCESS);
    ops_resume();

    if(debuggee->currently_tracing){
        rl_already_prompted = 1;
        printf("\n");
    }
}

static void handle_single_step(void){
    /* Re-enable all the breakpoints we disabled while performing the
     * single step. This function is called when the CPU raises the software
     * step exception after the single step occurs.
     */
    breakpoint_enable_all();

    if(JUST_HIT_BREAKPOINT){
        if(JUST_HIT_SW_BREAKPOINT){
            breakpoint_enable(debuggee->last_hit_bkpt_ID);
            JUST_HIT_SW_BREAKPOINT = 0;
        }

        /* If we caused a software step exception to get past a breakpoint,
         * just continue as normal. Otherwise, if we manually single step
         * right after a breakpoint hit, just print the disassembly.
         */
        if(!debuggee->is_single_stepping)
            resume_after_exception();
        else
            disassemble_at_location(debuggee->thread_state.__pc, 4);

        JUST_HIT_BREAKPOINT = 0;

        debuggee->is_single_stepping = 0;

        return;
    }

    putchar('\n');

    disassemble_at_location(debuggee->thread_state.__pc, 4);
    set_single_step(0);

    debuggee->is_single_stepping = 0;
}

static void handle_hit_breakpoint(long subcode, char **desc){
    struct breakpoint *hit = find_bp_with_address(subcode);

    if(!hit){
        printf("Could not find hit breakpoint (this shouldn't happen)\n");
        return;
    }

    breakpoint_hit(hit);

    concat(desc, " breakpoint %d at %lx hit %d time(s).\n",
            hit->id, hit->location, hit->hit_count);

    if(!hit->hw){
        JUST_HIT_SW_BREAKPOINT = 1;
        breakpoint_disable(hit->id);
    }

    debuggee->last_hit_bkpt_ID = hit->id;
}

void handle_exception(Request *request){
    /* When an exception occurs, there is a left over (iosdbg) prompt,
     * and this gets rid of it.
     */
    rl_clear_visible_line();
    rl_already_prompted = 0;

    if(!request){
        printf("NULL request (shouldn't happen)\n");
        return;
    }

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

    /* Give focus to whatever caused this exception. */
    struct machthread *focused = machthread_getfocused();

    if(!focused || focused->port != thread){
        printf("\n[Switching to thread %#llx]\n", 
                (unsigned long long)get_tid_from_thread_port(thread));
        machthread_setfocused(thread);
    }

    debuggee->get_thread_state();

    unsigned long long tid = get_tid_from_thread_port(thread);
    char *tname = get_thread_name_from_thread_port(thread);

    char *desc = NULL;
    asprintf(&desc, "\n * Thread %#llx", tid);

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
            concat(&desc, "%#llx in debuggee.\n", debuggee->thread_state.__pc);
        else{
            concat(&desc, "Resuming execution.");
            ops_resume();
        }
        
        /* Don't print any of this if we're detaching. */
        if(notify && !debuggee->want_detach){
            printf("%s", desc);

            if(stop)
                disassemble_at_location(debuggee->thread_state.__pc, 4);
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
        JUST_HIT_WATCHPOINT = 1;

        debuggee->last_hit_wp_loc = subcode;
        debuggee->last_hit_wp_PC = debuggee->thread_state.__pc;

        set_single_step(1);
        
        /* Continue execution so the software step exception occurs. */
        resume_after_exception();

        free(tname);
    }
    /* A hardware/software breakpoint hit, or the software step
     * exception has occured.
     */
    else if(exception == EXC_BREAKPOINT && code == EXC_ARM_BREAKPOINT){
        if(subcode == 0){
            if(JUST_HIT_WATCHPOINT){
                handle_hit_watchpoint();

                JUST_HIT_WATCHPOINT = 0;
                
                safe_reprompt();

                free(tname);

                return;
            }
            /* If we single step over where a breakpoint is set,
             * we should report it and count it as hit.
             */ 
            struct breakpoint *hit = find_bp_with_address(
                    debuggee->thread_state.__pc);

            if(debuggee->is_single_stepping && hit){
                breakpoint_hit(hit);

                concat(&desc, ": '%s': breakpoint %d at %#lx hit %d time(s).\n",
                        tname, hit->id, hit->location, hit->hit_count);

                printf("%s", desc);
            }
    
            handle_single_step();

            safe_reprompt();

            free(desc);
            free(tname);

            return;
        }
        
        JUST_HIT_BREAKPOINT = 1;

        concat(&desc, ": '%s':", tname);
        handle_hit_breakpoint(subcode, &desc);

        printf("%s", desc);

        disassemble_at_location(debuggee->thread_state.__pc, 4);

        free(desc);
        free(tname);

        set_single_step(1);
            
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

    debuggee->pending_messages--;
}
