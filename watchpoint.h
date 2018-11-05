#ifndef _WATCHPOINT_H_
#define _WATCHPOINT_H_

#include "defs.h"
#include "memutils.h"
#include "linkedlist.h"

struct watchpoint {
	// Watchpoint ID.
	int id;

	// Watchpoint location.
	unsigned long long location;

	// Old protection.
	unsigned long long old_protection;

	// How many times this watchpoint has hit.
	unsigned long long hit_count;
};

typedef int wp_error_t;

#define WP_SUCCESS (wp_error_t)0
#define WP_FAILURE (wp_error_t)1

static int current_watchpoint_id = 1;



#endif
