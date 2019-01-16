#ifndef _HANDLERS_H_
#define _HANDLERS_H_

#include "defs.h"
#include "machthread.h"

unsigned long long find_slide(void);
kern_return_t restore_exception_ports(void);
kern_return_t resume(void);
kern_return_t setup_exception_handling(void);
kern_return_t deallocate_ports(void);
kern_return_t suspend(void);
kern_return_t update_threads(thread_act_port_array_t *);
kern_return_t get_debug_state(void);
kern_return_t set_debug_state(void);
kern_return_t get_thread_state(void);
kern_return_t set_thread_state(void);
kern_return_t get_neon_state(void);
kern_return_t set_neon_state(void);

#endif
