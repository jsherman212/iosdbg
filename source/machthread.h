#ifndef _MACHTHREAD_H_
#define _MACHTHREAD_H_

#include "defs.h"
#include "linkedlist.h"

#define MAXTHREADSIZENAME 64

struct machthread {
	// Port to this thread.
	mach_port_t port;

	// Tells us if this thread is the one being focused on.
	int focused;

	// Thread ID.
	unsigned long long tid;

	// The name of this thread.
	char tname[MAXTHREADSIZENAME];

	// iosdbg assigned thread ID.
	int ID;

	// Thread state.
	arm_thread_state64_t thread_state;
};

struct machthread *machthread_new(mach_port_t);

// Get the machthread that corresponds with this port.
struct machthread *machthread_fromport(mach_port_t);

// Find a machthread structure given an ID.
struct machthread *machthread_find(int);

// Get the machthread we're focused on.
struct machthread *machthread_getfocused(void);

// Set our focus on a thread given an index
int machthread_setfocusgivenindex(int);

// When a thread no longer exists, call this function to
// reassign new IDs.
void machthread_reassignall(void);

// Update the thread state of the machthread passed in.
void machthread_updatestate(struct machthread *);

// Update the linkedlist of the debuggee's threads
// given a thread_act_port_array_t.
// Will not add duplicate threads to the linked list 
void machthread_updatethreads(thread_act_port_array_t);

// Set focus to this thread port.
void machthread_setfocused(mach_port_t);

// Free wrapper for a machthread.
void machthread_free(struct machthread *);

// other utility functions that could be used from anywhere
char *get_thread_name_from_thread_port(mach_port_t);
unsigned long long get_tid_from_thread_port(mach_port_t);

static int current_machthread_id = 1;

#endif
