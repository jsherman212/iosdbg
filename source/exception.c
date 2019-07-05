#include <ctype.h>
#include <mach/mach.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "breakpoint.h"
#include "debuggee.h"
#include "exception.h"
#include "linkedlist.h"
#include "memutils.h"
#include "ptrace.h"
#include "sigsupport.h"
#include "strext.h"
#include "thread.h"
#include "trace.h"
#include "watchpoint.h"

#include "disas/branch.h"

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

static inline void enable_single_step(struct machthread *t){
    get_debug_state(t);
    t->debug_state.__mdscr_el1 |= 1;
    set_debug_state(t);
}

static void describe_hit_watchpoint(void *prev_data, void *cur_data,
        unsigned int sz, char **desc){
    long old_val = *(long *)prev_data;
    long new_val = *(long *)cur_data;

    if(sz == sizeof(char)){
        concat(desc, "Old value: %s%#x\nNew value: %s%#x\n\n", 
                (char)old_val < 0 ? "-" : "", 
                (char)old_val < 0 ? (char)-old_val : (char)old_val, 
                (char)new_val < 0 ? "-" : "", 
                (char)new_val < 0 ? (char)-new_val : (char)new_val);
    }
    else if(sz == sizeof(short)){
        concat(desc, "Old value: %s%#x\nNew value: %s%#x\n\n", 
                (short)old_val < 0 ? "-" : "", 
                (short)old_val < 0 ? (short)-old_val : (short)old_val, 
                (short)new_val < 0 ? "-" : "", 
                (short)new_val < 0 ? (short)-new_val : (short)new_val);

    }
    else if(sz == sizeof(int)){
        concat(desc, "Old value: %s%#x\nNew value: %s%#x\n\n", 
                (int)old_val < 0 ? "-" : "", 
                (int)old_val < 0 ? (int)-old_val : (int)old_val, 
                (int)new_val < 0 ? "-" : "", 
                (int)new_val < 0 ? (int)-new_val : (int)new_val);
    }
    else{
        concat(desc, "Old value: %s%#lx\nNew value: %s%#lx\n\n", 
                (long)old_val < 0 ? "-" : "", 
                (long)old_val < 0 ? (long)-old_val : (long)old_val, 
                (long)new_val < 0 ? "-" : "", 
                (long)new_val < 0 ? (long)-new_val : (long)new_val);
    }
}

static void handle_soft_signal(mach_port_t thread, long subcode, char **desc,
        int notify, int pass, int stop){
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

static void handle_hit_watchpoint(struct machthread *t, int *should_auto_resume,
        int *should_print, char **desc){
    struct watchpoint *hit = find_wp_with_address(t->last_hit_wp_loc);

    if(!hit){
        /* should auto resume, should not print */
        *should_print = 0;
        return;
    }

    watchpoint_hit(hit);

    unsigned int sz = hit->data_len;

    /* Save previous data for comparison. */
    void *prev_data = malloc(sz);
    memcpy(prev_data, hit->data, sz);

    read_memory_at_location((void *)hit->user_location, hit->data, sz);
    
    concat(desc, ": '%s': watchpoint %d at %#lx hit %d time(s).\n\n",
            t->tname, hit->id, hit->user_location, hit->hit_count);

    describe_hit_watchpoint(prev_data, hit->data, sz, desc);
    disassemble_at_location(t->last_hit_wp_PC + 4, 4, desc);

    free(prev_data);
    
    t->last_hit_wp_loc = 0;
    t->last_hit_wp_PC = 0;

    /* should print, should not auto resume */
    *should_auto_resume = 0;
}

pthread_mutex_t SET_SS_BP_LOCK_MUTEX = PTHREAD_MUTEX_INITIALIZER;

static void handle_single_step(struct machthread *t, int *should_auto_resume,
        int *should_print, char **desc){
    /* Re-enable all the breakpoints we disabled while performing the
     * single step. This function is called when the CPU raises the software
     * step exception after the single step occurs.
     */
  //  if(t->stepconfig.is_stepping)
    //    breakpoint_enable_all();

    printf("%s: t->just_hit_breakpoint %d t->stepconfig.just_hit_ss_breakpoint %d\n",
            __func__, t->just_hit_breakpoint, t->stepconfig.just_hit_ss_breakpoint);

    if(t->stepconfig.just_hit_ss_breakpoint){
        printf("%s: will auto resume and not print cuz ss bp was hit. Returning\n",
                __func__);
        *should_print = 0;
        return;
    }

    if(t->just_hit_breakpoint){
        if(t->just_hit_sw_breakpoint){
            breakpoint_enable(t->last_hit_bkpt_ID, NULL);
            t->just_hit_sw_breakpoint = 0;
        }

        /* If we caused a software step exception to get past a breakpoint,
         * just continue as normal. Otherwise, if we manually single step
         * right after a breakpoint hit, just print the disassembly.
         */
        if(!t->stepconfig.is_stepping){
            /* should not print, should auto resume */
            *should_print = 0;

            /*
            if(t->stepconfig.step_kind == INST_STEP_OVER){
                if(t->stepconfig.just_hit_ss_breakpoint){
                    printf("*****%s: not auto resuming, just hit ss bp\n",
                            __func__);
                    *should_auto_resume = 0;

                    t->stepconfig.just_hit_ss_breakpoint = 0;
                }
            }
                */
            
        }
        else{
            /* should print, should not auto resume */
            *should_auto_resume = 0;
            concat(desc, "\n");
            disassemble_at_location(t->thread_state.__pc, 4, desc);
        }

        t->just_hit_breakpoint = 0;
        t->stepconfig.is_stepping = 0;

        return;
    }

    *should_auto_resume = 0;
    concat(desc, "\n");
    disassemble_at_location(t->thread_state.__pc, 4, desc);
    t->stepconfig.is_stepping = 0;
}

static void handle_hit_breakpoint(struct machthread *t,
        int *should_auto_resume, int *should_print, long subcode,
        int *need_single_step_now, char **desc){
    struct breakpoint *hit = find_bp_with_cond(subcode, BP_COND_NORMAL);
    struct breakpoint *step = find_bp_with_cond(subcode, BP_COND_STEPPING);

    printf("%s: hit %p step %p\n", __func__, hit, step);

    if(!hit && !step){
        *should_print = 0;
        return;
    }

    // XXX fake single step output for step inst-over when we come back to
    // saved LR
    // XXX XXX shouldn't need to check step->for_stepping now because of
    //          how we fetch this breakpoint
    if(step && step->for_stepping){
    //if(hit && hit->for_stepping){
        t->stepconfig.LR_to_step_to = -1;
        printf("%s: the temp breakpoint on LR for inst step over has hit,"
                " we should be at the instruction right after the branch, "
                " fake single step output here? resetting LR_to_step_to "
                " PC: %#llx\n",
                __func__, t->thread_state.__pc);
        //hit->temporary = 0;
        //breakpoint_hit(hit);
        breakpoint_hit(step);

        //concat(desc, " instruction step over.\n");

        /* should print, should not auto resume */
    //    *should_auto_resume = 0;

        t->stepconfig.just_hit_ss_breakpoint = 1;
        t->stepconfig.set_temp_ss_breakpoint = 0;

        //*need_single_step_now = 0;
        // XXX XXX XXX
        //return;
    }

    /* This could be possible. If the user deletes all breakpoints before
     * we have a chance to handle an exception related to a breakpoint,
     * we'll end up with hit being NULL.
     */
/*    if(!hit){
        if(!step){
            *should_print = 0;
        }

        return;
    }
*/
    /* Of course we can't really have real thread-specific software
     * breakpoints... but we can emulate them.
     */
    if(hit && !hit->threadinfo.all && !hit->hw){
        if(t->tid != hit->threadinfo.pthread_tid){
            /* should not print, should auto resume */
            *should_print = 0;
            return;
        }
    }

    breakpoint_hit(hit);

    if(hit){
        concat(desc, " breakpoint %d at %#lx hit %d time(s).\n",
                hit->id, hit->location, hit->hit_count);
    }
    else if(step){
        concat(desc, " instruction step over.\n");
    }

    if(hit){
        if(!hit->hw){
            t->just_hit_sw_breakpoint = 1;
            breakpoint_disable(hit->id, NULL);
        }

        t->last_hit_bkpt_ID = hit->id;
    }

    /* should print, should not auto resume */
    *should_auto_resume = 0;

    //*need_single_step_now = 1;
}

void handle_exception(Request *request, int *should_auto_resume,
        int *should_print, char **desc){
    /* Finish printing everything while tracing so
     * we don't get caught in the middle of it.
     */
    wait_for_trace();

    mach_port_t task = request->task.name;
    mach_port_t thread = request->thread.name;
    exception_type_t exception = request->exception;
    const char *exc = exc_str(exception);
    long code = ((long *)request->code)[0];
    long subcode = ((long *)request->code)[1];
    
    printf("%s: exc '%s' code %#lx subcode %#lx\n", __func__, exc, code, subcode);

    /* Give focus to whatever caused this exception. */
    struct machthread *focused = get_focused_thread();

    if(!focused || focused->port != thread){
        set_focused_thread(thread);
        focused = get_focused_thread();
    }

    if(!focused){
        *should_print = 0;
        return;
    }

    get_thread_state(focused);

    concat(desc, "\n * Thread #%d (tid = %#llx)", focused->ID, focused->tid);

    SS_BP_LOCK;

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
        char *e = NULL;

        sigsettings(subcode, &notify, &pass, &stop, 0, &e);

        free(e);
        
        concat(desc, ", '%s' received signal ", focused->tname);

        handle_soft_signal(focused->port, subcode,
                desc, notify, pass, stop);

        if(!notify && !stop){
            /* should not print, should auto resume */
            *should_print = 0;
        }
        else if(!notify && stop){
            /* should not print, should not auto resume */
            *should_print = 0;
            *should_auto_resume = 0;
        }
        else if(notify && !stop){
            /* should print, should auto resume */
            concat(desc, "Resuming execution.\n");
        }
        else if(notify && stop){
            /* should print, should not auto resume */
            *should_auto_resume = 0;

            concat(desc, "%#llx in debuggee.\n", focused->thread_state.__pc);
            disassemble_at_location(focused->thread_state.__pc, 4, desc);
        }
    }
    /* A hardware watchpoint hit. However, we need to single step in 
     * order for the CPU to execute the instruction at this address
     * so the value actually changes.
     */
    else if(code == EXC_ARM_DA_DEBUG){
        focused->just_hit_watchpoint = 1;
        focused->last_hit_wp_loc = subcode;
        focused->last_hit_wp_PC = focused->thread_state.__pc;

        /* The software step exception will occur after the user
         * resumes the debuggee.
         */
        enable_single_step(focused);
        
        /* should not print, should auto resume */
        *should_print = 0;
    }
    /* A hardware/software breakpoint hit, or the software step
     * exception has occured.
     */
    else if(exception == EXC_BREAKPOINT && code == EXC_ARM_BREAKPOINT){
        if(subcode == 0){
            if(focused->just_hit_watchpoint){
                handle_hit_watchpoint(focused, should_auto_resume, should_print, desc);
                focused->just_hit_watchpoint = 0;
                return;
            }

            int should_print_override = 0;

            if(focused->stepconfig.is_stepping){
                struct breakpoint *hit = find_bp_with_address(
                        focused->thread_state.__pc);

                printf("%s: hit %p\n", __func__, hit);

                /*
                if(hit){
                    printf("*****%s: setting should_print_override\n", __func__);
                    should_print_override = 1;
                }
                */
                //else{
                    const char *step_kind = "instruction step in";

                    if(focused->stepconfig.step_kind == INST_STEP_OVER)
                        step_kind = "instruction step over";

                    concat(desc, ": '%s': %s!!.", focused->tname, step_kind);
              //  }
            }
    
            handle_single_step(focused, should_auto_resume, should_print, desc);

            if(should_print_override){
                *should_print = 0;
                *should_auto_resume = 1;
            }

            SS_BP_UNLOCK;

            return;
        }
        
        /* In order to implement instruction level step over, we need to
         * set a temporary breakpoint at LR right after the subroutine
         * call is taken. When this breakpoint hits, we fake single
         * step output in handle_hit_breakpoint.
         * XXX nope
         */
        int need_single_step_now = 0;
        
        focused->just_hit_breakpoint = 1;

        concat(desc, ": '%s':", focused->tname);
        handle_hit_breakpoint(focused, should_auto_resume, should_print,
                subcode, &need_single_step_now, desc);
        disassemble_at_location(focused->thread_state.__pc, 4, desc);

        //if(need_single_step_now)
        enable_single_step(focused);
    }
    /* Something else occured. */
    else{
        concat(desc, ": '%s': stop reason: %s (code = %#lx, subcode = %#lx)\n",
                focused->tname, exc, code, subcode);
        
        disassemble_at_location(focused->thread_state.__pc, 4, desc);

        /* should print, should not auto resume */
        *should_auto_resume = 0;
    }

    SS_BP_UNLOCK;
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
}
