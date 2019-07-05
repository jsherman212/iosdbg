#ifndef _THREAD_H_
#define _THREAD_H_

#include <mach/mach.h>
#include <pthread/pthread.h>

#include "linkedlist.h"

enum {
    STEP_NONE,
    INST_STEP_INTO,
    INST_STEP_OVER
};

#define TH_FOREACH(var) \
    for(struct node_t *var = debuggee->threads->front; \
            var; \
            var = var->next) \

extern pthread_mutex_t THREAD_LOCK;

#define TH_LOCKED_FOREACH(var) \
    pthread_mutex_lock(&THREAD_LOCK); \
    for(struct node_t *var = debuggee->threads->front; \
            var; \
            var = var->next) \

#define TH_END_LOCKED_FOREACH \
    pthread_mutex_unlock(&THREAD_LOCK)

#define TH_LOCK pthread_mutex_lock(&THREAD_LOCK)
#define TH_UNLOCK pthread_mutex_unlock(&THREAD_LOCK)

#define MAXTHREADSIZENAME 64

extern mach_port_t THREAD_DEATH_NOTIFY_PORT;

struct machthread {
    /* Port for this thread. */
    mach_port_t port;

    /* Tells us if this thread is the one being focused on. */
    int focused;

    /* pthread thread ID. */
    unsigned long long tid;

    /* The name of this thread. */
    char tname[MAXTHREADSIZENAME];

    /* iosdbg assigned thread ID. */
    int ID;

    /* Thread state. */
    arm_thread_state64_t thread_state;

    /* Debug state. */
    arm_debug_state64_t debug_state;

    /* Neon state. */
    arm_neon_state64_t neon_state;

    int just_hit_watchpoint;
    int just_hit_breakpoint;
    int just_hit_sw_breakpoint;
    
    /* Keeps track of the location of the data in the last hit watchpoint. */
    unsigned long last_hit_wp_loc;

    /* Keeps track of where the last watchpoint hit. */
    unsigned long last_hit_wp_PC;

    /* Keeps track of the ID of the last breakpoint that hit on this thread. */
    int last_hit_bkpt_ID;

    struct {
        int is_stepping;
        int step_kind;
        int keep_stepping;
        unsigned long LR_to_step_to;
        int need_to_save_LR;
        int just_hit_ss_breakpoint;
        int set_temp_ss_breakpoint;
    } stepconfig;
};

enum comparison {
    PORTS,
    IDS,
    FOCUSED,
    TID
};

struct machthread *thread_from_port(mach_port_t);
struct machthread *find_thread_from_ID(int);
struct machthread *find_thread_from_TID(unsigned long long);
struct machthread *get_focused_thread(void);

/* For clarity. This is only used with the functions below. */
#define FOR_ALL_THREADS (NULL)

kern_return_t get_thread_state(struct machthread *);
kern_return_t set_thread_state(struct machthread *);
kern_return_t get_debug_state(struct machthread *);
kern_return_t set_debug_state(struct machthread *);
kern_return_t get_neon_state(struct machthread *);
kern_return_t set_neon_state(struct machthread *);

int set_focused_thread_with_idx(int);
void update_all_thread_states(struct machthread *);
void update_thread_list(thread_act_port_array_t,
        mach_msg_type_number_t, char **);
void set_focused_thread(mach_port_t);
void resetmtid(void);

static int current_machthread_id = 1;

#endif
