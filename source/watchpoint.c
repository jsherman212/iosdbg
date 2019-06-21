#include <mach/kmod.h>
#include <stdio.h>
#include <stdlib.h>

#include "debuggee.h"
#include "linkedlist.h"
#include "memutils.h"
#include "printing.h"
#include "strext.h"
#include "thread.h"
#include "watchpoint.h"

pthread_mutex_t WATCHPOINT_LOCK = PTHREAD_MUTEX_INITIALIZER;

/* Find an available hardware watchpoint register.*/
static int find_ready_wp_reg(void){
    /* Keep track of what hardware watchpoint registers are used
     * in the watchpoints currently active.
     */
    int *wp_map = malloc(sizeof(int) * debuggee->num_hw_wps);

    /* -1 means the hardware watchpoint register representing that spot
     * in the array has not been used. 0 means the opposite.
     */
    memset(wp_map, -1, sizeof(int) * debuggee->num_hw_wps);

    WP_LOCKED_FOREACH(current){
        struct watchpoint *wp = current->data;
        wp_map[wp->hw_wp_reg] = 0;
    }
    WP_END_LOCKED_FOREACH;

    /* Now search wp_map for any empty spots. */
    for(int i=0; i<debuggee->num_hw_wps; i++){
        if(wp_map[i] != 0){
            free(wp_map);
            return i;
        }
    }

    free(wp_map);

    /* No available hardware watchpoint registers found. */
    return -1;
}

static struct watchpoint *watchpoint_new(unsigned long location,
        unsigned int data_len, int LSC, int thread, char **error){
    if(data_len == 0 || data_len > sizeof(unsigned long)){
        concat(error, "data length (%d) is invalid", data_len);
        return NULL;
    }

    kern_return_t result = valid_location(location);
    
    if(result){
        concat(error, "could not set watchpoint: %s",
                mach_error_string(result));
        return NULL;
    }
    
    struct watchpoint *wp = malloc(sizeof(struct watchpoint));

    wp->threadinfo.tname = NULL;

    if(thread == WP_ALL_THREADS){
        wp->threadinfo.all = 1;
        wp->threadinfo.iosdbg_tid = 0;
        wp->threadinfo.pthread_tid = 0;
    }
    else{
        wp->threadinfo.all = 0;
        wp->threadinfo.iosdbg_tid = thread;

        struct machthread *target = find_thread_from_ID(wp->threadinfo.iosdbg_tid);

        if(!target){
            concat(error, "no such thread with ID %d", wp->threadinfo.iosdbg_tid);
            free(wp);
            return NULL;
        }

        wp->threadinfo.pthread_tid = target->tid;
        concat(&wp->threadinfo.tname, "%s", target->tname);
    }

    wp->user_location = location;
    wp->hit_count = 0;    
    wp->data_len = data_len;
    wp->data = malloc(wp->data_len);

    result = read_memory_at_location((void *)wp->user_location, wp->data,
            wp->data_len);

    if(result){
        concat(error, "could not set watchpoint: could not read memory at %#lx",
                location);
        free(wp->data);

        if(wp->threadinfo.tname)
            free(wp->threadinfo.tname);

        free(wp);

        return NULL;
    }

    int available_wp_reg = find_ready_wp_reg();

    if(available_wp_reg == -1){
        concat(error, "could not set watchpoint: "
                "no more hardware watchpoint registers available (%d/%d) used", 
                debuggee->num_hw_wps, debuggee->num_hw_wps);
        free(wp->data);

        if(wp->threadinfo.tname)
            free(wp->threadinfo.tname);

        free(wp);

        return NULL;
    }

    wp->hw_wp_reg = available_wp_reg;

    /* Setup the DBGWCR<n>_EL1 register.
     * We need the following criteria to correctly set up this watchpoint:
     *  - BAS must be <data_len> active bits.
     *  - LSC varies on what arguments the user provides.
     *  - PAC must be 0b10 so these watchpoints generate debug events in EL0,
     *    where we are executing.
     *  - E must be 0b1 so this watchpoint is enabled.
     */
    unsigned BAS = (((1 << wp->data_len) - 1) << (wp->user_location & 0x7) << 5);

    __uint64_t wcr = BAS | LSC | PAC | E;

    /* ARM depricates programming a DBGWVR<n>_EL1 to an address that
     * is not double-word aligned.
     */
    __uint64_t wvr = (wp->user_location & ~0x7);

    wp->aligned_location = wvr;

    wp->wcr = wcr;
    wp->wvr = wvr;

    if(wp->threadinfo.all){
        TH_LOCKED_FOREACH(current){
            struct machthread *t = current->data;

            get_debug_state(t);

            t->debug_state.__wcr[wp->hw_wp_reg] = wp->wcr;
            t->debug_state.__wvr[wp->hw_wp_reg] = wp->wvr;

            set_debug_state(t);
        }
        TH_END_LOCKED_FOREACH;
    }
    else{
        struct machthread *target = find_thread_from_ID(wp->threadinfo.iosdbg_tid);

        if(target){
            get_debug_state(target);

            target->debug_state.__wcr[wp->hw_wp_reg] = wp->wcr;
            target->debug_state.__wvr[wp->hw_wp_reg] = wp->wvr;

            set_debug_state(target);
        }
        else{
            concat(error, "the thread for your watchpoint has gone away,"
                    " please try again");

            free(wp->data);

            if(wp->threadinfo.tname)
                free(wp->threadinfo.tname);

            free(wp);

            return NULL;
        }
    }

    wp->id = current_watchpoint_id++;

    return wp;
}

static void enable_wp(struct watchpoint *wp){
    if(wp->threadinfo.all){
        TH_LOCKED_FOREACH(current){
            struct machthread *t = current->data;

            get_debug_state(t);

            t->debug_state.__wcr[wp->hw_wp_reg] = wp->wcr;
            t->debug_state.__wvr[wp->hw_wp_reg] = wp->wvr;

            set_debug_state(t);
        }
        TH_END_LOCKED_FOREACH;
    }
    else{
        struct machthread *target = find_thread_from_ID(wp->threadinfo.iosdbg_tid);

        if(target){
            get_debug_state(target);

            target->debug_state.__wcr[wp->hw_wp_reg] = wp->wcr;
            target->debug_state.__wvr[wp->hw_wp_reg] = wp->wvr;

            set_debug_state(target);
        }
    }
}

static void disable_wp(struct watchpoint *wp){
    if(wp->threadinfo.all){
        TH_LOCKED_FOREACH(current){
            struct machthread *t = current->data;

            get_debug_state(t);
            t->debug_state.__wcr[wp->hw_wp_reg] = 0;
            set_debug_state(t);
        }
        TH_END_LOCKED_FOREACH;
    }
    else{
        struct machthread *target = find_thread_from_ID(wp->threadinfo.iosdbg_tid);

        if(target){
            get_debug_state(target);
            target->debug_state.__wcr[wp->hw_wp_reg] = 0;
            set_debug_state(target);
        }
    }
}

static void wp_set_state_internal(struct watchpoint *wp, int disabled){
    if(disabled)
        disable_wp(wp);
    else
        enable_wp(wp);
}

static void wp_delete_internal(struct watchpoint *wp){
    disable_wp(wp);
    free(wp->data);

    if(wp->threadinfo.tname)
        free(wp->threadinfo.tname);

    linkedlist_delete(debuggee->watchpoints, wp);
    debuggee->num_watchpoints--;
}

void watchpoint_at_address(unsigned long location, unsigned int data_len,
        int LSC, int thread, char **outbuffer, char **error){
    struct watchpoint *wp = watchpoint_new(location, data_len,
            (LSC << 3), thread, error);

    if(!wp)
        return;
    
    WP_LOCK;
    linkedlist_add(debuggee->watchpoints, wp);
    WP_UNLOCK;

    const char *type = "r";

    if(LSC == WP_WRITE)
        type = "w";
    else if(LSC == WP_READ_WRITE)
        type = "rw";

    wp->type = type;

    concat(outbuffer, "Watchpoint %d: addr = %#lx size = %d type = %s",
            wp->id, wp->user_location, wp->data_len, wp->type);

    if(!wp->threadinfo.all){
        concat(outbuffer, ", for thread %d (tid: %#llx), '%s'\n",
                wp->threadinfo.iosdbg_tid, wp->threadinfo.pthread_tid,
                wp->threadinfo.tname);
    }
    else{
        concat(outbuffer, "\n");
    }
    
    debuggee->num_watchpoints++;
}

void watchpoint_hit(struct watchpoint *wp){
    if(!wp)
        return;

    wp->hit_count++;
}

void watchpoint_delete_specific(struct watchpoint *wp){
    if(!wp)
        return;

    wp_delete_internal(wp);
}

void watchpoint_delete(int wp_id, char **error){
    WP_LOCKED_FOREACH(current){
        struct watchpoint *wp = current->data;

        if(wp->id == wp_id){
            wp_delete_internal(wp);
            WP_END_LOCKED_FOREACH;
            return;
        }
    }
    WP_END_LOCKED_FOREACH;

    concat(error, "watchpoint %d not found", wp_id);
}

void watchpoint_enable_all(void){
    WP_LOCKED_FOREACH(current){
        struct watchpoint *wp = current->data;
        wp_set_state_internal(wp, WP_ENABLED);
    }
    WP_END_LOCKED_FOREACH;
}

void watchpoint_disable_all(void){
    WP_LOCKED_FOREACH(current){
        struct watchpoint *wp = current->data;
        wp_set_state_internal(wp, WP_DISABLED);
    }
    WP_END_LOCKED_FOREACH;
}

void watchpoint_delete_all(void){
    WP_LOCKED_FOREACH(current){
        struct watchpoint *wp = current->data;
        wp_delete_internal(wp);
    }
    WP_END_LOCKED_FOREACH;
}

struct watchpoint *find_wp_with_address(unsigned long addr){
    /* Double-word align addr to match with wp->aligned_location. */
    addr &= ~0x7;

    WP_LOCKED_FOREACH(current){
        struct watchpoint *wp = current->data;

        if(wp->aligned_location == addr){
            struct watchpoint *found = wp;
            WP_END_LOCKED_FOREACH;
            return found;
        }
    }
    WP_END_LOCKED_FOREACH;
    
    return NULL;
}
