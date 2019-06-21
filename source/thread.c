#include <stdio.h>
#include <stdlib.h>

#include "debuggee.h"
#include "printing.h"
#include "strext.h"
#include "thread.h"

mach_port_t THREAD_DEATH_NOTIFY_PORT = MACH_PORT_NULL;
pthread_mutex_t THREAD_LOCK = PTHREAD_MUTEX_INITIALIZER;

static char *get_pthread_name(mach_port_t thread_port){
    if(thread_port == MACH_PORT_NULL)
        return NULL;

    thread_extended_info_data_t exinfo;
    mach_msg_type_number_t count = THREAD_EXTENDED_INFO_COUNT;

    kern_return_t kret = thread_info(thread_port,
            THREAD_EXTENDED_INFO,
            (thread_info_t)&exinfo,
            &count);

    if(kret)
        return NULL;

    return strdup(exinfo.pth_name);
}

static unsigned long long get_pthread_tid(mach_port_t thread_port,
        char **outbuffer){
    if(thread_port == MACH_PORT_NULL)
        return KERN_FAILURE;

    thread_identifier_info_data_t ident;
    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;

    kern_return_t kret = thread_info(thread_port,
            THREAD_IDENTIFIER_INFO,
            (thread_info_t)&ident,
            &count);

    if(kret){
        concat(outbuffer, "warning: couldn't get pthread tid"
                " for thread %#x: %s\n",
                thread_port, mach_error_string(kret));
        return -1;
    }

    return ident.thread_id;
}

static void get_thread_info(struct machthread *thread, char **outbuffer){
    thread->tid = get_pthread_tid(thread->port, outbuffer);

    memset(thread->tname, '\0', sizeof(thread->tname));
    char *tname = get_pthread_name(thread->port);
    
    if(tname){
        strncpy(thread->tname, tname, MAXTHREADSIZENAME);
        free(tname);
    }
    else{
        strcpy(thread->tname, "");
    }
    
    update_all_thread_states(thread);
}

static struct machthread *machthread_new(mach_port_t thread_port,
        char **outbuffer){
    if(!MACH_PORT_VALID(thread_port))
        return NULL;

    struct machthread *mt = malloc(sizeof(struct machthread));

    mt->port = thread_port;

    get_thread_info(mt, outbuffer);

    mt->focused = 0;
    mt->ID = current_machthread_id++;
    mt->just_hit_watchpoint = 0;
    mt->just_hit_breakpoint = 0;
    mt->just_hit_sw_breakpoint = 0;
    mt->last_hit_wp_loc = 0;
    mt->last_hit_wp_PC = 0;
    mt->last_hit_bkpt_ID = 0;
    mt->ignore_upcoming_exception = 0;

    kern_return_t kret = KERN_SUCCESS;

    if(THREAD_DEATH_NOTIFY_PORT == MACH_PORT_NULL){
        kret = mach_port_allocate(mach_task_self(),
                MACH_PORT_RIGHT_RECEIVE, &THREAD_DEATH_NOTIFY_PORT);

        if(kret){
            concat(outbuffer, "warning: could not create initial thread"
                    " death notify port: %s\n", mach_error_string(kret));
        }
    }
    
    mach_port_t prev;

    kret = mach_port_request_notification(mach_task_self(), mt->port,
            MACH_NOTIFY_DEAD_NAME, 0, THREAD_DEATH_NOTIFY_PORT,
            MACH_MSG_TYPE_MAKE_SEND_ONCE, &prev);

    if(kret){
        concat(outbuffer, "warning: could not register Mach death notification"
                " for thread %#x: %s\n", mt->port, mach_error_string(kret));
    }

    return mt;  
}

/* Find a machthread with a given condition, defined in compway. */
static struct machthread *find_with_cond(enum comparison compway,
        void *comparingwith){
    TH_LOCK;
    if(!debuggee->threads){
        TH_UNLOCK;
        return NULL;
    }
    TH_UNLOCK;
 
    TH_LOCKED_FOREACH(current){
        struct machthread *t = current->data;

        int cond = 0;

        if(compway == PORTS)
            cond = t->port == *(mach_port_t *)comparingwith;
        else if(compway == IDS)
            cond = t->ID == *(int *)comparingwith;
        else if(compway == FOCUSED)
            cond = t->focused;
        else if(compway == TID)
            cond = t->tid == *(unsigned long long *)comparingwith;

        if(cond){
            struct machthread *found = t;
            TH_END_LOCKED_FOREACH;
            return found;
        }
    }
    TH_END_LOCKED_FOREACH;

    /* Not found. */
    return NULL;
}

struct machthread *thread_from_port(mach_port_t thread_port){
    if(thread_port == MACH_PORT_NULL)
        return NULL;

    mach_port_t *thread_port_ptr = malloc(sizeof(thread_port));
    *thread_port_ptr = thread_port;

    struct machthread *ret = find_with_cond(PORTS, thread_port_ptr);

    free(thread_port_ptr);
    
    return ret;
}

struct machthread *find_thread_from_ID(int ID){
    int *IDptr = malloc(sizeof(ID));
    *IDptr = ID;
    
    struct machthread *ret = find_with_cond(IDS, IDptr);

    free(IDptr);

    return ret;
}

struct machthread *find_thread_from_TID(unsigned long long tid){
    unsigned long long *tid_ptr = malloc(sizeof(unsigned long long));
    *tid_ptr = tid;

    struct machthread *ret = find_with_cond(TID, tid_ptr);

    free(tid_ptr);

    return ret;
}

struct machthread *get_focused_thread(void){
    return find_with_cond(FOCUSED, NULL);
}

kern_return_t get_thread_state(struct machthread *thread){
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;

    if(thread){
        return thread_get_state(thread->port,
                ARM_THREAD_STATE64,
                (thread_state_t)&thread->thread_state,
                &count);
    }

    TH_LOCKED_FOREACH(current){
        struct machthread *t = current->data;

        thread_get_state(t->port,
                ARM_THREAD_STATE64,
                (thread_state_t)&t->thread_state,
                &count);
    }
    TH_END_LOCKED_FOREACH;

    return KERN_SUCCESS;
}

kern_return_t set_thread_state(struct machthread *thread){
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;

    if(thread){
        return thread_set_state(thread->port,
                ARM_THREAD_STATE64,
                (thread_state_t)&thread->thread_state,
                count);
    }

    TH_LOCKED_FOREACH(current){
        struct machthread *t = current->data;

        thread_set_state(t->port,
                ARM_THREAD_STATE64,
                (thread_state_t)&t->thread_state,
                count);
    }
    TH_END_LOCKED_FOREACH;

    return KERN_SUCCESS;
}

kern_return_t get_debug_state(struct machthread *thread){
    mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;

    if(thread){
        return thread_get_state(thread->port,
                ARM_DEBUG_STATE64,
                (thread_state_t)&thread->debug_state,
                &count);
    }

    TH_LOCKED_FOREACH(current){
        struct machthread *t = current->data;

        thread_get_state(t->port,
                ARM_DEBUG_STATE64,
                (thread_state_t)&t->debug_state,
                &count);
    }
    TH_END_LOCKED_FOREACH;

    return KERN_SUCCESS;
}

kern_return_t set_debug_state(struct machthread *thread){
    mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;

    if(thread){
        return thread_set_state(thread->port,
                ARM_DEBUG_STATE64,
                (thread_state_t)&thread->debug_state,
                count);
    }

    TH_LOCKED_FOREACH(current){
        struct machthread *t = current->data;

        thread_set_state(t->port,
                ARM_DEBUG_STATE64,
                (thread_state_t)&t->debug_state,
                count);
    }
    TH_END_LOCKED_FOREACH;

    return KERN_SUCCESS;
}

kern_return_t get_neon_state(struct machthread *thread){
    mach_msg_type_number_t count = ARM_NEON_STATE64_COUNT;

    if(thread){
        return thread_get_state(thread->port,
                ARM_NEON_STATE64,
                (thread_state_t)&thread->neon_state,
                &count);
    }
    
    TH_LOCKED_FOREACH(current){
        struct machthread *t = current->data;

        thread_get_state(t->port,
                ARM_NEON_STATE64,
                (thread_state_t)&t->neon_state,
                &count);
    }
    TH_END_LOCKED_FOREACH;

    return KERN_SUCCESS;
}

kern_return_t set_neon_state(struct machthread *thread){
    mach_msg_type_number_t count = ARM_NEON_STATE64_COUNT;

    if(thread){
        return thread_set_state(thread->port,
                ARM_NEON_STATE64,
                (thread_state_t)&thread->neon_state,
                count);
    }
    
    TH_LOCKED_FOREACH(current){
        struct machthread *t = current->data;

        thread_set_state(t->port,
                ARM_NEON_STATE64,
                (thread_state_t)&t->neon_state,
                count);
    }
    TH_END_LOCKED_FOREACH;

    return KERN_SUCCESS;
}

int set_focused_thread_with_idx(int focus_index){
    /* We print out the thread list starting at 1. */
    focus_index--;
    int counter = 0;
    struct machthread *newfocus = NULL;

    TH_LOCKED_FOREACH(current){
        if(counter > focus_index)
            break;

        newfocus = current->data;
        counter++;
    }
    TH_END_LOCKED_FOREACH;

    if(!newfocus)
        return -1;

    set_focused_thread(newfocus->port);

    return 0;
}

void update_all_thread_states(struct machthread *mt){
    if(!mt)
        return;

    if(mt->port == MACH_PORT_NULL)
        return;
    
    get_thread_state(mt);
    get_debug_state(mt);
    get_neon_state(mt);
}

void update_thread_list(thread_act_port_array_t threads,
        char **outbuffer){
    TH_LOCK;

    if(!debuggee->threads || !threads){
        TH_UNLOCK;
        return;
    }

    /* Check if there are no threads in the linked list. This should only
     * be the case right after we attach to our target program.
     */
    if(!debuggee->threads->front){
        for(int i=0; i<debuggee->thread_count; i++){
            struct machthread *add = machthread_new(threads[i], outbuffer);

            if(add)
                linkedlist_add(debuggee->threads, add);
        }

        TH_UNLOCK;

        return;
    }

    mach_port_t focused_th_port = MACH_PORT_NULL;

    TH_FOREACH(current){
        struct machthread *t = current->data;

        if(t->focused)
            focused_th_port = t->port;

        linkedlist_delete(debuggee->threads, t);
    }

    resetmtid();

    for(int i=0; i<debuggee->thread_count; i++){
        struct machthread *add = machthread_new(threads[i], outbuffer);

        if(threads[i] == focused_th_port)
            add->focused = 1;

        if(add)
            linkedlist_add(debuggee->threads, add);
    }

    TH_UNLOCK;
}

void set_focused_thread(mach_port_t thread_port){
    if(thread_port == MACH_PORT_NULL)
        return;

    struct machthread *prevfocus = get_focused_thread();
    struct machthread *newfocus = thread_from_port(thread_port);
    
    if(!newfocus)
        return;

    newfocus->focused = 1;
    
    if(prevfocus)
        prevfocus->focused = 0;
}

void resetmtid(void){
    current_machthread_id = 1;
}
