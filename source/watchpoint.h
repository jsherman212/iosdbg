#ifndef _WATCHPOINT_H_
#define _WATCHPOINT_H_

#include "defs.h"
#include "memutils.h"
#include "linkedlist.h"

/* A watchpoint is disabled when the protections at its location is VM_PROT_READ | VM_PROT_EXECUTE
 * A watchpoint is enabled when the protections at its location is VM_PROT_READ
 */

struct watchpoint {
	// Watchpoint ID.
	int id;

	// Watchpoint location.
	unsigned long long location;

	// Old protection.
	unsigned long long old_protection;

	// How many times this watchpoint has hit.
	unsigned long long hit_count;

	// Whatever is located at the location of this watchpoint.
	void *data;

	// Length of the data we're watching.
	unsigned int data_len;
};

typedef int wp_error_t;

#define WP_SUCCESS (wp_error_t)0
#define WP_FAILURE (wp_error_t)1

#define WP_DISABLE 0
#define WP_ENABLE 1

static int current_watchpoint_id = 1;

wp_error_t watchpoint_at_address(unsigned long long, unsigned int);
void watchpoint_hit(struct watchpoint *);
wp_error_t watchpoint_delete(int);
wp_error_t watchpoint_set_state(int, int);
void watchpoint_enable_all(void);
void watchpoint_disable_all(void);
void watchpoint_delete_all(void);

#endif
