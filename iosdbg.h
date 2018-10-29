#ifndef _IOSDBG_H_
#define _IOSDBG_H_

#include "mach_exc.h"
#include "dbgcmd.h" // Includes defs.h, for execute_command

/* Both unused. */
kern_return_t catch_mach_exception_raise_state(mach_port_t exception_port, exception_type_t exception, exception_data_t code, mach_msg_type_number_t code_count, int *flavor, thread_state_t in_state, mach_msg_type_number_t in_state_count, thread_state_t out_state, mach_msg_type_number_t *out_state_count){return KERN_FAILURE;}
kern_return_t catch_mach_exception_raise_state_identity(mach_port_t exception_port, mach_port_t thread, mach_port_t task, exception_type_t exception, exception_data_t code, mach_msg_type_number_t code_count, int *flavor, thread_state_t in_state, mach_msg_type_number_t in_state_count, thread_state_t out_state, mach_msg_type_number_t *out_state_count){return KERN_FAILURE;}

#endif
