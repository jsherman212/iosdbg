#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>

#include "breakpoint.h"
#include "debuggee.h"
#include "linkedlist.h"
#include "memutils.h"
#include "printing.h"
#include "strext.h"
#include "thread.h"

pthread_mutex_t BREAKPOINT_LOCK = PTHREAD_MUTEX_INITIALIZER;

/* Find an available hardware breakpoint register.*/
static int find_ready_bp_reg(void){
    /* Keep track of what hardware breakpoint registers are used
     * in the breakpoints currently active.
     */
    int *bp_map = malloc(sizeof(int) * debuggee->num_hw_bps);

    /* -1 means the hardware breakpoint register representing that spot
     * in the array has not been used. 0 means the opposite.
     */
    memset(bp_map, -1, sizeof(int) * debuggee->num_hw_bps);

    BP_LOCKED_FOREACH(current){
        struct breakpoint *bp = current->data;

        if(bp->hw)
            bp_map[bp->hw_bp_reg] = 0;
    }
    BP_END_LOCKED_FOREACH;

    /* Now search bp_map for any empty spots. */
    for(int i=0; i<debuggee->num_hw_bps; i++){
        if(bp_map[i] != 0){
            free(bp_map);
            return i;
        }
    }

    free(bp_map);

    /* No available hardware watchpoint registers found. */
    return -1;
}

struct breakpoint *breakpoint_new(unsigned long location, int temporary, 
        int thread, char **error){
    kern_return_t err = valid_location(location);

    if(err){
        concat(error, "could not set breakpoint: %s",
                mach_error_string(err));
        return NULL;
    }
    
    struct breakpoint *bp = malloc(sizeof(struct breakpoint));

    bp->threadinfo.tname = NULL;

    if(thread == BP_ALL_THREADS){
        bp->threadinfo.all = 1;
        bp->threadinfo.iosdbg_tid = 0;
        bp->threadinfo.pthread_tid = 0;
    }
    else{
        bp->threadinfo.all = 0;
        bp->threadinfo.iosdbg_tid = thread;

        struct machthread *target = find_thread_from_ID(bp->threadinfo.iosdbg_tid);

        if(!target){
            concat(error, "no such thread with ID %d", bp->threadinfo.iosdbg_tid);
            free(bp);
            return NULL;
        }

        bp->threadinfo.pthread_tid = target->tid;
        concat(&bp->threadinfo.tname, "%s", target->tname);
    }

    bp->hw = 0;
    bp->hw_bp_reg = -1;
    bp->bcr = 0;
    bp->bvr = 0;

    int available_bp_reg = find_ready_bp_reg();

    /* We have an available breakpoint register, use it. */
    if(available_bp_reg != -1){
        bp->hw = 1;
        bp->hw_bp_reg = available_bp_reg;

        /* Setup the DBGBCR<n>_EL1 register.
         * We need the following criteria to correctly set up this breakpoint:
         *  - BT must be 0b0000 for an unlinked instruction address match, where
         *    DBGBVR<n>_EL1 is the location of the breakpoint.
         *  - BAS must be 0b1111 to tell the machine to match the instruction
         *    at DBGBVR<n>_EL1.
         *  - PMC must be 0b10 so these breakpoints generate debug events in EL0, 
         *    where we are executing.
         *  - E must be 0b1 so this breakpoint is enabled.
         */
        __uint64_t bcr = (BT | BAS | PMC | E);

        /* Bits[1:0] must be clear in DBGBVR<n>_EL1 or else the instruction
         * is mis-aligned, so clear those bits in the location.
         */
        __uint64_t bvr = (location & ~0x3);

        bp->bcr = bcr;
        bp->bvr = bvr;

        if(bp->threadinfo.all){
            TH_LOCKED_FOREACH(current){
                struct machthread *t = current->data;

                get_debug_state(t);

                t->debug_state.__bcr[available_bp_reg] = bcr;
                t->debug_state.__bvr[available_bp_reg] = bvr;

                set_debug_state(t);
            }
            TH_END_LOCKED_FOREACH;
        }
        else{
            struct machthread *target = find_thread_from_ID(bp->threadinfo.iosdbg_tid);

            if(target){
                get_debug_state(target);

                target->debug_state.__bcr[available_bp_reg] = bcr;
                target->debug_state.__bvr[available_bp_reg] = bvr;

                set_debug_state(target);
            }
            else{
                concat(error, "the thread for your breakpoint has gone away,"
                        " please try again.");

                if(bp->threadinfo.tname)
                    free(bp->threadinfo.tname);

                free(bp);

                return NULL;
            }
        }
    }
    
    bp->location = location;

    int sz = 4, orig_instruction = 0;

    struct breakpoint *dup = find_bp_with_address(bp->location);

    if(!dup){
        err = read_memory_at_location((void *)bp->location, &orig_instruction, sz);

        if(err){
            concat(error, "could not set breakpoint:"
                    " could not read memory at %#lx", location);
            free(bp);
            return NULL;
        }

        bp->old_instruction = orig_instruction;
    }
    else{
        /* If two software breakpoints are set at the same place,
         * the second one will record the original instruction as BRK #0.
         */
        bp->old_instruction = dup->old_instruction;
    }
    
    bp->hit_count = 0;
    bp->disabled = 0;
    bp->temporary = temporary;
    bp->id = current_breakpoint_id;
    
    // XXX once I open up temp breakpoints as a feature this will cause issues
    if(!bp->temporary)
        current_breakpoint_id++;

    return bp;
}

static void enable_hw_bp(struct breakpoint *bp){
    if(bp->threadinfo.all){
        TH_LOCKED_FOREACH(current){
            struct machthread *t = current->data;

            get_debug_state(t);

            t->debug_state.__bcr[bp->hw_bp_reg] = bp->bcr;
            t->debug_state.__bvr[bp->hw_bp_reg] = bp->bvr;

            set_debug_state(t);
        }
        TH_END_LOCKED_FOREACH;
    }
    else{
        struct machthread *target = find_thread_from_ID(bp->threadinfo.iosdbg_tid);
        
        if(target){
            get_debug_state(target);

            target->debug_state.__bcr[bp->hw_bp_reg] = bp->bcr;
            target->debug_state.__bvr[bp->hw_bp_reg] = bp->bvr;

            set_debug_state(target);
        }
    }
}

static void disable_hw_bp(struct breakpoint *bp){
    if(bp->threadinfo.all){
        TH_LOCKED_FOREACH(current){
            struct machthread *t = current->data;

            get_debug_state(t);
            t->debug_state.__bcr[bp->hw_bp_reg] = 0;
            set_debug_state(t);
        }
        TH_END_LOCKED_FOREACH;
    }
    else{
        struct machthread *target = find_thread_from_ID(bp->threadinfo.iosdbg_tid);

        if(target){
            get_debug_state(target);
            target->debug_state.__bcr[bp->hw_bp_reg] = 0;
            set_debug_state(target);
        }
    }
}

/* Set whether or not a breakpoint is disabled or enabled,
 * and take action accordingly.
 */
static void bp_set_state_internal(struct breakpoint *bp, int disabled){
    if(bp->hw){
        if(disabled)
            disable_hw_bp(bp);
        else
            enable_hw_bp(bp);
    }
    else{
        if(disabled)
            write_memory_to_location(bp->location, bp->old_instruction, 4);   
        else
            write_memory_to_location(bp->location, CFSwapInt32(BRK), 4);
    }

    bp->disabled = disabled;
}

static void bp_delete_internal(struct breakpoint *bp){
    bp_set_state_internal(bp, BP_DISABLED);
    
    if(bp->threadinfo.tname)
        free(bp->threadinfo.tname);

    linkedlist_delete(debuggee->breakpoints, bp);    
    debuggee->num_breakpoints--;
}

void breakpoint_at_address(unsigned long address, int temporary,
        int thread, char **outbuffer, char **error){
    struct breakpoint *bp = breakpoint_new(address, temporary, thread, error);

    if(!bp)
        return;

    BP_LOCK;
    linkedlist_add(debuggee->breakpoints, bp);
    BP_UNLOCK;

    if(!temporary){
        concat(outbuffer, "Breakpoint %d at %#lx", bp->id, bp->location);

        if(!bp->threadinfo.all){
            concat(outbuffer, ", for thread #%d (tid: %#llx), '%s'\n",
                    bp->threadinfo.iosdbg_tid, bp->threadinfo.pthread_tid,
                    bp->threadinfo.tname);
        }
        else{
            concat(outbuffer, "\n");
        }
    }

    /* If we ran out of hardware breakpoints, set a software breakpoint
     * by writing BRK #0 to bp->location.
     */
    if(!bp->hw)
        write_memory_to_location(bp->location, CFSwapInt32(BRK), 4);

    debuggee->num_breakpoints++;
}

void breakpoint_hit(struct breakpoint *bp){
    if(!bp)
        return;

    if(bp->temporary)
        breakpoint_delete_specific(bp);
    else
        bp->hit_count++;
}

void breakpoint_delete(int breakpoint_id, char **error){
    BP_LOCKED_FOREACH(current){
        struct breakpoint *bp = current->data;

        if(bp->id == breakpoint_id){
            bp_delete_internal(bp);
            BP_END_LOCKED_FOREACH;
            return;
        }
    }
    BP_END_LOCKED_FOREACH;

    if(error)
        concat(error, "breakpoint %d not found", breakpoint_id);
}

void breakpoint_delete_specific(struct breakpoint *bp){
    if(!bp)
        return;

    bp_delete_internal(bp);
}

void breakpoint_disable(int breakpoint_id, char **error){
    BP_LOCKED_FOREACH(current){
        struct breakpoint *bp = current->data;

        if(bp->id == breakpoint_id){
            bp_set_state_internal(bp, BP_DISABLED);
            BP_END_LOCKED_FOREACH;
            return;
        }
    }
    BP_END_LOCKED_FOREACH;

    if(error)
        concat(error, "breakpoint %d not found", breakpoint_id);
}

void breakpoint_enable(int breakpoint_id, char **error){
    BP_LOCKED_FOREACH(current){
        struct breakpoint *bp = current->data;

        if(bp->id == breakpoint_id){
            bp_set_state_internal(bp, BP_ENABLED);
            BP_END_LOCKED_FOREACH;
            return;
        }
    }
    BP_END_LOCKED_FOREACH;

    if(error)
        concat(error, "breakpoint %d not found", breakpoint_id);
}

void breakpoint_disable_all(void){
    BP_LOCKED_FOREACH(current){
        struct breakpoint *bp = current->data;
        bp_set_state_internal(bp, BP_DISABLED);
    }
    BP_END_LOCKED_FOREACH;
}

void breakpoint_enable_all(void){
    BP_LOCKED_FOREACH(current){
        struct breakpoint *bp = current->data;
        bp_set_state_internal(bp, BP_ENABLED);
    }
    BP_END_LOCKED_FOREACH;
}

int breakpoint_disabled(int breakpoint_id){
    BP_LOCKED_FOREACH(current){
        struct breakpoint *bp = current->data;

        if(bp->id == breakpoint_id){
            int disabled = bp->disabled;
            BP_END_LOCKED_FOREACH;
            return disabled;
        }
    }
    BP_END_LOCKED_FOREACH;

    return 0;
}

void breakpoint_delete_all(void){
    BP_LOCKED_FOREACH(current){
        struct breakpoint *bp = current->data;
        bp_delete_internal(bp);
    }
    BP_END_LOCKED_FOREACH;
}

struct breakpoint *find_bp_with_address(unsigned long addr){
    BP_LOCKED_FOREACH(current){
        struct breakpoint *bp = current->data;
        
        if(bp->location == addr){
            struct breakpoint *found = bp;
            BP_END_LOCKED_FOREACH;
            return found;
        }
    }
    BP_END_LOCKED_FOREACH;

    return NULL;
}
