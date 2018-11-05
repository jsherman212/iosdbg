#include "watchpoint.h"

struct watchpoint *watchpoint_new(unsigned long long location){
	struct watchpoint *wp = malloc(sizeof(struct watchpoint));

	wp->location = location;
	
	wp->hit_count = 0;

	return wp;
}
