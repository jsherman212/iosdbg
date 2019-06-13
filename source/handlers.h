#ifndef _HANDLERS_H_
#define _HANDLERS_H_

#include <mach/mach.h>

unsigned long find_slide(void);

kern_return_t restore_exception_ports(void);
kern_return_t resume(void);
kern_return_t setup_exception_handling(void);
kern_return_t deallocate_ports(void);
kern_return_t suspend(void);
kern_return_t get_threads(thread_act_port_array_t *);

int suspended(void);

#endif
