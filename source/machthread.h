#ifndef _MACHTHREAD_H_
#define _MACHTHREAD_H_

#include <mach/mach.h>

#define MAXTHREADSIZENAME 64

struct machthread {
    /* Port to this thread. */
    mach_port_t port;

    /* Tells us if this thread is the one being focused on. */
    int focused;

    /* Thread ID. */
    unsigned long long tid;

    /* The name of this thread. */
    char tname[MAXTHREADSIZENAME];

    /* iosdbg assigned thread ID. */
    int ID;

    /* Thread state. */
    arm_thread_state64_t thread_state;
};

enum comparison {
    PORTS,
    IDS,
    FOCUSED
};

struct machthread *machthread_fromport(mach_port_t);
struct machthread *machthread_find(int);
struct machthread *machthread_getfocused(void);
int machthread_setfocusgivenindex(int);
void machthread_updatestate(struct machthread *);
void machthread_updatethreads(thread_act_port_array_t);
void machthread_setfocused(mach_port_t);

char *get_thread_name_from_thread_port(mach_port_t);
unsigned int get_tid_from_thread_port(mach_port_t);

void resetmtid(void);

static int current_machthread_id = 1;

#endif
