/*
Various utility functions to read and write memory from and to the debuggee. Keep it complicated here so it can be concise everywhere else.
*/

#include "memutils.h"

/* Thanks https://opensource.apple.com/source/CF/CF-299/Base.subproj/CFByteOrder.h */
unsigned int CFSwapInt32(unsigned int arg){
    unsigned int result;
    result = ((arg & 0xFF) << 24) | ((arg & 0xFF00) << 8) | ((arg >> 8) & 0xFF00) | ((arg >> 24) & 0xFF);
    return result;
}

unsigned long long CFSwapInt64(unsigned long long arg){
    union CFSwap {
        unsigned long long sv;
        unsigned int ul[2];
    } tmp, result;
    tmp.sv = arg;
    result.ul[0] = CFSwapInt32(tmp.ul[1]); 
    result.ul[1] = CFSwapInt32(tmp.ul[0]);
    return result.sv;
}

// This function reads memory from an address and places the data into buffer.
kern_return_t memutils_read_memory_at_location(unsigned long long location, unsigned char *buffer, vm_size_t length){
	return vm_read_overwrite(debuggee->task, location, length, (vm_address_t)buffer, &length);
}

// This function writes data to location.
// Data is automatically put into little endian before writing to location.
kern_return_t memutils_write_memory_to_location(unsigned long long location, unsigned long long data, vm_size_t size){
	kern_return_t ret;

	// put instruction hex into little endian for the machine to understand
	data = CFSwapInt32(data);

	vm_protect(debuggee->task, location, size, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
	ret = vm_write(debuggee->task, location, (vm_offset_t)&data, size);
	vm_protect(debuggee->task, location, size, 0, VM_PROT_READ | VM_PROT_EXECUTE);

	return ret;
}

// This function takes a buffer of data and converts it to an unsigned long long.
unsigned long long memutils_buffer_to_number(char *buffer, int length){
	// create a string for strtoull
	char *buf = malloc(strlen(buffer));

	// append 0x for strtoull
	if(!strstr(buffer, "0x"))
		strcpy(buf, "0x");

	for(int i=0; i<length; i++){
		// TODO don't malloc a byte allocate it on the stack
		char *current_byte = malloc(1);

		sprintf(current_byte, "%x", (unsigned char)buffer[i]);

		// if we have a single digit hex, we need to put a zero in front of it
		if(strtoull(current_byte, NULL, 16) < 0x10){
			memset(current_byte, 0, 1);
			sprintf(current_byte, "0%x", (unsigned char)buffer[i]);
		}

		strcat(buf, current_byte);
		free(current_byte);
	}

	unsigned long long val = strtoull(buf, NULL, 16);

	free(buf);

	return val;
}