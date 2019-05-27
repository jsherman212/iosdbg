#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>

#include "breakpoint.h"
#include "debuggee.h"
#include "linkedlist.h"
#include "memutils.h"
#include "strext.h"
#include "thread.h"

/* Find an available hardware breakpoint register.*/
static int find_ready_bp_reg(void){
    struct node_t *current = debuggee->breakpoints->front;
    
    /* Keep track of what hardware breakpoint registers are used
     * in the breakpoints currently active.
     */
    int *bp_map = malloc(sizeof(int) * debuggee->num_hw_bps);

    /* -1 means the hardware breakpoint register representing that spot
     * in the array has not been used. 0 means the opposite.
     */
    for(int i=0; i<debuggee->num_hw_bps; i++)
        bp_map[i] = -1;

    while(current){
        struct breakpoint *current_breakpoint = current->data;

        if(current_breakpoint->hw)
            bp_map[current_breakpoint->hw_bp_reg] = 0;

        current = current->next;
    }

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

    bp->thread = thread;

    bp->hw = 0;
    bp->hw_bp_reg = -1;

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
      
        if(bp->thread == BP_ALL_THREADS){
            for(struct node_t *current = debuggee->threads->front;
                    current;
                    current = current->next){
                struct machthread *t = current->data;

                get_debug_state(t);

                t->debug_state.__bcr[available_bp_reg] = bcr;
                t->debug_state.__bvr[available_bp_reg] = bvr;

                set_debug_state(t);
            }

        /*    debuggee->get_task_debug_state();

            debuggee->task_debug_state.__bcr[available_bp_reg] = bcr;
            debuggee->task_debug_state.__bvr[available_bp_reg] = bvr;

            debuggee->set_task_debug_state();
            */
        }
        else{
            // XXX struct machthread *target = find_with_id etc etc
        }
    }
    
    bp->location = location;

    int sz = 0x4;
  
    // XXX why am I using malloc
    unsigned int *orig_instruction = malloc(sizeof(unsigned int));
    err = read_memory_at_location((void *)bp->location, orig_instruction, sz);

    if(err){
        concat(error, "could not set breakpoint:"
                " could not read memory at %#lx", location);
        free(bp);
        return NULL;
    }

    bp->old_instruction = *orig_instruction;
    
    free(orig_instruction);
    
    bp->hit_count = 0;
    bp->disabled = 0;
    bp->temporary = temporary;
    bp->id = current_breakpoint_id;
    
    if(!bp->temporary)
        current_breakpoint_id++;

    debuggee->num_breakpoints++;

    return bp;
}

static void enable_hw_bp(struct breakpoint *bp){
    /*
    debuggee->get_debug_state();

    debuggee->debug_state.__bcr[bp->hw_bp_reg] = 
        BT | 
        BAS | 
        PMC | 
        E;

    debuggee->debug_state.__bvr[bp->hw_bp_reg] = (bp->location & ~0x3);

    debuggee->set_debug_state();
    */

    __uint64_t bcr = (BT | BAS | PMC | E);
    __uint64_t bvr = (bp->location & ~0x3);

    if(bp->thread == BP_ALL_THREADS){
        for(struct node_t *current = debuggee->threads->front;
                current;
                current = current->next){
            struct machthread *t = current->data;

            get_debug_state(t);

            t->debug_state.__bcr[bp->hw_bp_reg] = bcr;
            t->debug_state.__bvr[bp->hw_bp_reg] = bvr;

            set_debug_state(t);
        }
     /*   debuggee->get_task_debug_state();

        debuggee->task_debug_state.__bcr[bp->hw_bp_reg] = bcr;
        debuggee->task_debug_state.__bvr[bp->hw_bp_reg] = bvr;

        debuggee->set_task_debug_state();
    */
    }
    else{
        // XXX struct machthread *target = find_with_id etc etc
    }
}

static void disable_hw_bp(struct breakpoint *bp){
    /*
    debuggee->get_debug_state();
    debuggee->debug_state.__bcr[bp->hw_bp_reg] = 0;
    debuggee->set_debug_state();
    */

    if(bp->thread == BP_ALL_THREADS){
        for(struct node_t *current = debuggee->threads->front;
                current;
                current = current->next){
            struct machthread *t = current->data;

            get_debug_state(t);

            t->debug_state.__bcr[bp->hw_bp_reg] = 0;

            set_debug_state(t);
        }

        /*debuggee->get_task_debug_state();
        debuggee->task_debug_state.__bcr[bp->hw_bp_reg] = 0;
        debuggee->set_task_debug_state();
        */
    }
    else{
        // XXX struct machthread *target = find_with_id etc etc
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
    linkedlist_delete(debuggee->breakpoints, bp);
    
    debuggee->num_breakpoints--;
}

void breakpoint_at_address(unsigned long address, int temporary,
        int thread, char **error){
    struct breakpoint *bp = breakpoint_new(address, temporary, thread, error);

    if(!bp)
        return;

    linkedlist_add(debuggee->breakpoints, bp);

    if(!temporary)
        printf("Breakpoint %d at %#lx\n", bp->id, bp->location);

    /* If we ran out of hardware breakpoints, set a software breakpoint
     * by writing BRK #0 to bp->location.
     */
    if(!bp->hw)
        write_memory_to_location(bp->location, CFSwapInt32(BRK), 4);
}

void breakpoint_hit(struct breakpoint *bp){
    if(!bp)
        return;

    if(bp->temporary)
        breakpoint_delete(bp->id, NULL);
    else
        bp->hit_count++;
}

void breakpoint_delete(int breakpoint_id, char **error){
    struct node_t *current = debuggee->breakpoints->front;

    while(current){
        struct breakpoint *current_breakpoint = current->data;

        if(current_breakpoint->id == breakpoint_id){
            bp_delete_internal(current_breakpoint);
            return;
        }

        current = current->next;
    }

    if(error)
        concat(error, "breakpoint %d not found", breakpoint_id);
}

void breakpoint_disable(int breakpoint_id, char **error){
    struct node_t *current = debuggee->breakpoints->front;

    while(current){
        struct breakpoint *current_breakpoint = current->data;

        if(current_breakpoint->id == breakpoint_id){
            bp_set_state_internal(current_breakpoint, BP_DISABLED);
            return;
        }

        current = current->next;
    }

    if(error)
        concat(error, "breakpoint %d not found", breakpoint_id);
}

void breakpoint_enable(int breakpoint_id, char **error){
    //printf("**********%s: for id %d\n", __func__, breakpoint_id);
    struct node_t *current = debuggee->breakpoints->front;

    while(current){
        struct breakpoint *current_breakpoint = current->data;

        if(current_breakpoint->id == breakpoint_id){
            bp_set_state_internal(current_breakpoint, BP_ENABLED);
            return;
        }

        current = current->next;
    }

    if(error)
        concat(error, "breakpoint %d not found", breakpoint_id);
}

void breakpoint_disable_all(void){
    struct node_t *current = debuggee->breakpoints->front;

    while(current){
        struct breakpoint *bp = current->data;
        bp_set_state_internal(bp, BP_DISABLED);

        current = current->next;
    }
}

void breakpoint_enable_all(void){
    //printf("**********%s\n", __func__);
    struct node_t *current = debuggee->breakpoints->front;

    while(current){
        struct breakpoint *bp = current->data;
        bp_set_state_internal(bp, BP_ENABLED);

        current = current->next;
    }
}

int breakpoint_disabled(int breakpoint_id){
    struct node_t *current = debuggee->breakpoints->front;

    while(current){
        struct breakpoint *current_breakpoint = current->data;

        if(current_breakpoint->id == breakpoint_id)
            return current_breakpoint->disabled;

        current = current->next;
    }

    return 0;
}

void breakpoint_delete_all(void){
    struct node_t *current = debuggee->breakpoints->front;

    while(current){
        struct breakpoint *current_breakpoint = current->data;

        bp_delete_internal(current_breakpoint);

        current = current->next;
    }
}

struct breakpoint *find_bp_with_address(unsigned long addr){
    struct node_t *current = debuggee->breakpoints->front;

    while(current){
        struct breakpoint *current_breakpoint = current->data;
        
        if(current_breakpoint->location == addr)
            return current_breakpoint;
        
        current = current->next;
    }

    return NULL;
}
