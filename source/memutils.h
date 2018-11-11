#ifndef _MEMUTILS_H_
#define _MEMUTILS_H_

#include "defs.h"

unsigned int CFSwapInt32(unsigned int);
unsigned long long CFSwapInt64(unsigned long long);

kern_return_t memutils_read_memory_at_location(void *, void *, vm_size_t);
kern_return_t memutils_write_memory_to_location(vm_address_t, vm_offset_t);
unsigned long long memutils_buffer_to_number(void *, int);
void memutils_dump_memory_from_location(void *, int, int, int);
kern_return_t memutils_valid_location(unsigned long long);

#endif
