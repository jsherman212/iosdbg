#include "watchpoint.h"

struct watchpoint *watchpoint_new(unsigned long long location){
	struct watchpoint *wp = malloc(sizeof(struct watchpoint));
	
	kern_return_t result = memutils_valid_location(location);
	if(result)
		return NULL;

	wp->location = location;	
	wp->hit_count = 0;

	return wp;
}
