/*
Various utility functions to read and write memory from and to the debuggee. Keep it complicated here so it can be concise everywhere else.

These functions account for ASLR, so no need to add to the address being passed in.
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
// Then, the buffer is formatted to big endian so it is easier for me to understand.
kern_return_t memutils_read_memory_at_location_with_aslr(unsigned long long location, unsigned char *buffer, vm_size_t length){
	return vm_read_overwrite(debuggee->task, location + debuggee->aslr_slide, length, (vm_address_t)buffer, &length);
}

// This function writes data to location.
// Data is automatically put into little endian before writing to location.
kern_return_t memutils_write_memory_at_location_with_aslr(long long location, long long data){
	kern_return_t ret;

	location += debuggee->aslr_slide;

	// put instruction hex into little endian for the machine to understand
	data = CFSwapInt32(data);

	vm_protect(debuggee->task, location, sizeof(data), 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
	ret = vm_write(debuggee->task, location, (vm_offset_t)&data, sizeof(data));
	vm_protect(debuggee->task, location, sizeof(data), 0, VM_PROT_READ | VM_PROT_EXECUTE);

	return ret;
}

// This function takes a buffer of data and converts it to an unsigned long long.
unsigned long long memutils_buffer_to_number(char *buffer){
	// create a string for strtoul
	char *buf = malloc(strlen(buffer));
	strcpy(buf, "0x");

	for(int i=0; i<strlen(buffer)-2; i+=2){
		char *current_byte = malloc(2);
		sprintf(current_byte, "%x%x", (unsigned char)buffer[i], (unsigned char)buffer[i+1]);
		strcat(buf, current_byte);
		free(current_byte);
	}

	unsigned long long val = strtoul(buf, NULL, 16);

	free(buf);

	return val;
}