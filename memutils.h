#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include "defs.h"

kern_return_t memutils_read_memory_at_location(void *, void *, vm_size_t);
kern_return_t memutils_write_memory_to_location(unsigned long long, unsigned long long, vm_size_t);

unsigned long long memutils_buffer_to_number(char *, int);
