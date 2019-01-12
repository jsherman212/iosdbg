#ifndef _WATCHPOINT_H_
#define _WATCHPOINT_H_

#include "memutils.h"
#include "linkedlist.h"

struct watchpoint {
	/* Watchpoint ID */
	int id;
	
	// Watchpoint location.
	unsigned long location;

	// How many times this watchpoint has hit.
	int hit_count;

	// Whatever is located at the location of this watchpoint.
	void *data;

	// Length of the data we're watching.
	unsigned int data_len;

	// The watchpoint register we're using.
	int hw_wp_reg;

	/* Load/store control for this watchpoint. */
	int LSC;
};

#define WT (0 << 20)
#define PAC (2 << 1)
#define E (1)

typedef int wp_error_t;

#define WP_SUCCESS (wp_error_t)0
#define WP_FAILURE (wp_error_t)1
#define WP_LIMIT_REACHED (wp_error_t)2

#define WP_ENABLED 0
#define WP_DISABLED 1

#define WP_READ (1)
#define WP_WRITE (2)
#define WP_READ_WRITE (3)

static int current_watchpoint_id = 1;

wp_error_t watchpoint_at_address(unsigned long, unsigned int, int);
void watchpoint_hit(struct watchpoint *);
wp_error_t watchpoint_delete(int);
void watchpoint_enable_all(void);
void watchpoint_disable_all(void);
void watchpoint_delete_all(void);
struct watchpoint *find_wp_with_address(unsigned long);

#endif
