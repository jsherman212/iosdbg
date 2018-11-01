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
kern_return_t memutils_read_memory_at_location(void *location, void *buffer, vm_size_t length){
	return vm_read_overwrite(debuggee->task, (vm_address_t)location, length, (vm_address_t)buffer, &length);
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

// Return a neat string filled with the bytes from buffer
// Caller is responsible for freeing it
char *_format_dumped_memory(void *buffer, int length, int extra_padding, int two_column, int split_point){
	// (length * 2) compensates for single digit bytes being represented as two digits (added leading 0)
	// + length compensates for the spaces we're adding
	// extra_padding accounts for spaces we may need to add
	char dump[(length * 2) + length + extra_padding];
	memset(dump, 0, length);
	
	if(extra_padding == 0){
		for(int i=0; i<length; i++){
			sprintf(dump, "%s%02x", dump, *(unsigned char *)(buffer + i));
			strcat(dump, " ");
		}
	}
	else{
		int remaining_bytes = length - extra_padding;
		for(int i=0; i<remaining_bytes; i++)
			sprintf(dump, "%s%02x ", dump, *(unsigned char *)(buffer + i));

		for(int i=0; i<extra_padding; i++)
			sprintf(dump, "%s   ", dump);
	}
	
	return strdup(dump);
}

// Helper function to print memory given a buffer,
// the amount of bytes to print, and what base to print them in.
void _print_dumped_memory(void *buffer, int bytes, int extra_padding, int base, int two_column, int split_point){
	char *fullhex = _format_dumped_memory(buffer, bytes, extra_padding, two_column, split_point);

	if(base == 10){
		// "convert" to base 10
		unsigned long long base10;
		sscanf(fullhex, "%llx", &base10);
		printf("%llu", base10);
	}
	else{
		printf("%s", fullhex);

		// remove all spaces in fullhex
		char *space = NULL;
		while((space = strchr(fullhex, ' ')) != NULL)
			memmove(space, space + 1, strlen(space));
		
		int hexlen = strlen(fullhex);		
		for(int i=0; i<hexlen; i+=2){
			// will every two bytes from the buffer
			char char_str[3];
			sprintf(char_str, "%c%c", fullhex[i], fullhex[i+1]);
			unsigned int char_to_print;
			
			// get the value from char_str
			sscanf(char_str, "%x", &char_to_print);
			
			// check if this character is printable
			if(isgraph(char_to_print))
				printf("%c", char_to_print);
			else
				printf(".");
		}
	}

	free(fullhex);
}

// This function takes a buffer of data and converts it to an unsigned long long.
unsigned long long memutils_buffer_to_number(void *buffer, int length){
	if(!buffer)
		return -1;
	
	char *fullhex = _format_dumped_memory(buffer, length, 0, 0, 0);

	unsigned long long val;
	sscanf(fullhex, "%llx", &val);
	
	free(fullhex);

	return val;
}

// Dump memory
void memutils_dump_memory_from_location(void *location, int amount, int bytes_per_line, int base){
	int lines_to_print = amount / bytes_per_line;

	// bytes_per_line may not divide into amount evenly
	// be sure to keep track of anything extra we need to print
	int extra_bytes_to_print = amount % bytes_per_line;

	char membuffer[bytes_per_line];
	memset(membuffer, 0, bytes_per_line);

	int cur_line = 0;

	while(cur_line < lines_to_print){
		memutils_read_memory_at_location(location, (void *)membuffer, (vm_size_t)bytes_per_line);
		
		printf(" 0x%llx: ", (unsigned long long)location);

		int cur_byte = 0;
		
		while(cur_byte < bytes_per_line){
			int two_columns = (bytes_per_line == 8 || bytes_per_line == 32) && base == 16;
			int split_point = bytes_per_line == 8 ? 4 : 8;
			_print_dumped_memory((void *)membuffer, bytes_per_line, 0, base, two_columns, split_point);
			cur_byte += bytes_per_line;
		}

		printf("\n");
		
		cur_line++;
		location += bytes_per_line;
	}

	if(extra_bytes_to_print > 0){
		memutils_read_memory_at_location(location, (void *)membuffer, (vm_size_t)extra_bytes_to_print);
		
		printf(" 0x%llx: ", (unsigned long long)location);
				
		int two_columns = (bytes_per_line == 8 || bytes_per_line == 16) && base == 16;
		int split_point = bytes_per_line == 8 ? 4 : 8;
		
		int extra_padding = bytes_per_line - extra_bytes_to_print;
		
		_print_dumped_memory((void *)membuffer, bytes_per_line/*extra_bytes_to_print*/, extra_padding, base, two_columns, split_point);
		
		printf("\n");
	}
}
