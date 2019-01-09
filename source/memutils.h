#ifndef _MEMUTILS_H_
#define _MEMUTILS_H_

#include "defs.h"

#define DISAS_DONT_SHOW_ARROW_AT_LOCATION_PARAMETER 0
#define DISAS_SHOW_ARROW_AT_LOCATION_PARAMETER 1

unsigned int CFSwapInt32(unsigned int);
unsigned long long CFSwapInt64(unsigned long long);

kern_return_t memutils_disassemble_at_location(unsigned long long location, int num_instrs, int show_arrow_at_location_param);
kern_return_t memutils_dump_memory_new(unsigned long long location, vm_size_t amount);
kern_return_t memutils_read_memory_at_location(void *, void *, vm_size_t);
kern_return_t memutils_write_memory_to_location(vm_address_t, vm_offset_t);
long long memutils_buffer_to_number(void *, int);
kern_return_t memutils_valid_location(unsigned long long);

#endif
