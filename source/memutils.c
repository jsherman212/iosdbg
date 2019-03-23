#include <armadillo.h>
#include <ctype.h>
#include <mach/kmod.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "breakpoint.h"
#include "convvar.h"
#include "defs.h"
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

kern_return_t disassemble_at_location(unsigned long long location, int num_instrs){
    const int data_size = 0x4;
    unsigned long long current_location = location;

    char *locstr;
    asprintf(&locstr, "%#llx", location);

    char *error = NULL;
    set_convvar("$_", locstr, &error);

    desc_auto_convvar_error_if_needed("$_", error);

    free(locstr);

    while(current_location < (location + (num_instrs * data_size))){
        char *data = malloc(data_size);
        kern_return_t err = memutils_read_memory_at_location((void *)current_location, data, data_size);

        unsigned long instr;

        /* Do not show any of the BRK #0 written by software breakpoints
         * when the user wants to disassemble memory.
         */
        struct breakpoint *active = find_bp_with_address(current_location);

        if(active)
            instr = CFSwapInt32(active->old_instruction);
        else{
            /* Format the memory given back. */
            char *bigendian = malloc((data_size * 2) + 1);
            memset(bigendian, '\0', (data_size * 2) + 1);

            for(int i=0; i<data_size; i++)
                sprintf(bigendian, "%s%02x", bigendian, (unsigned char)data[i]);

            free(data);
            
            instr = strtoul(bigendian, NULL, 16);       

            asprintf(&bigendian, "%#lx", instr);

            error = NULL;
            set_convvar("$__", bigendian, &error);

            desc_auto_convvar_error_if_needed("$__", error);

            free(bigendian);
        }

        char *disassembled = ArmadilloDisassembleB(instr, current_location);

        debuggee->get_thread_state();

        printf("%s%#llx:  %s\n", debuggee->thread_state.__pc == current_location ? "->  " : "    ", current_location, disassembled);

        free(disassembled);

        current_location += data_size;
    }

    return KERN_SUCCESS;
}

kern_return_t memutils_dump_memory(unsigned long long location, vm_size_t amount){
    if(amount == 0)
        return KERN_SUCCESS;

    // display memory in rows of 0x10 bytes
    const int row_size = 0x10;

    int amount_dumped = 0;
    unsigned long long current_location = location;

    while(amount_dumped < amount){
        char *membuffer = malloc(row_size);
        memset(membuffer, '\0', row_size);

        kern_return_t ret = memutils_read_memory_at_location((void *)current_location, membuffer, row_size);

        if(ret)
            return ret;

        int current_row_length = amount - amount_dumped;
        
        if(current_row_length >= row_size)
            current_row_length = row_size;

        unsigned char data_array[current_row_length];

        for(int i=0; i<current_row_length; i++)
            data_array[i] = (unsigned char)membuffer[i];

        free(membuffer);

        printf("  %#llx: ", current_location);

        for(int i=0; i<current_row_length; i++)
            printf("%02x ", data_array[i]);

        if(current_row_length < 0x10){
            // print filler spaces
            // 2 spaces for would be '%02x', one more for the space after
            for(int i=current_row_length; i<row_size; i++)
                printf("   ");
        }
        
        printf("  ");

        // print what the bytes represent
        for(int i=0; i<current_row_length; i++){
            unsigned char cur_char = data_array[i];

            if(isgraph(cur_char))
                printf("%c", cur_char);
            else
                printf(".");
        }

        printf("\n");

        amount_dumped += row_size;
        current_location += row_size;
    }

    return KERN_SUCCESS;
}

// This function reads memory from an address and places the data into buffer.
// Before writing this data back call CFSwapInt32 on it
kern_return_t memutils_read_memory_at_location(void *location, void *buffer, vm_size_t length){
    return vm_read_overwrite(debuggee->task, (vm_address_t)location, length, (vm_address_t)buffer, &length);
}

// This function writes data to location.
kern_return_t memutils_write_memory_to_location(vm_address_t location, vm_offset_t data){
    kern_return_t ret;

    // get old protections and figure out whether the location we're writing to exists
    vm_region_basic_info_data_64_t info;
    // we don't want to modify the real location...
    vm_address_t region_location = location;
    vm_size_t region_size;
    mach_port_t object_name;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    
    ret = vm_region_64(debuggee->task, &region_location, &region_size, VM_REGION_BASIC_INFO, (vm_region_info_t)&info, &info_count, &object_name);
    
    if(ret)
        return ret;
    
    int size = 0;
    vm_offset_t data_copy = data;

    do{
        data_copy >>= 8;
        size++;
    }while(data_copy != 0);

    if(size > sizeof(unsigned long long)){
        printf("Number too large\n");
        return KERN_INVALID_ARGUMENT;
    }
    
    // get raw bytes from this number   
    void *data_ptr = (uint8_t *)&data;

    vm_protect(debuggee->task, location, size, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    ret = vm_write(debuggee->task, location, (pointer_t)data_ptr, size);
    vm_protect(debuggee->task, location, size, 0, info.protection);
    
    return ret;
}

long long memutils_buffer_to_number(void *buffer, int length){
    if(!buffer)
        return -1;

    char *fullhex = malloc((length * 2) + 1);
    memset(fullhex, '\0', length);

    for(int i=0; i<length; i++)
        sprintf(fullhex, "%s%02x", fullhex, *(unsigned char *)(buffer + i));

    long long val;
    sscanf(fullhex, "%llx", &val);

    free(fullhex);

    return val;
}

// Test a location to see if it's out of bounds or invalid
kern_return_t memutils_valid_location(unsigned long location){
    vm_region_basic_info_data_64_t info;
    vm_address_t loc = location;
    vm_size_t region_size;
    mach_port_t object_name;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    
    return vm_region_64(debuggee->task, &loc, &region_size, VM_REGION_BASIC_INFO, (vm_region_info_t)&info, &info_count, &object_name);
}
