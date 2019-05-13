#include <armadillo.h>
#include <ctype.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "breakpoint.h"
#include "debuggee.h"
#include "convvar.h"
#include "memutils.h"
#include "strext.h"

/* Thanks https://opensource.apple.com/source/CF/CF-299/Base.subproj/CFByteOrder.h */
unsigned int CFSwapInt32(unsigned int arg){
    unsigned int result;
    result = ((arg & 0xFF) << 24) | ((arg & 0xFF00) << 8) 
        | ((arg >> 8) & 0xFF00) | ((arg >> 24) & 0xFF);
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

kern_return_t disassemble_at_location(unsigned long location, int num_instrs){
    const int data_size = 0x4;
    unsigned long current_location = location;

    char *locstr = NULL;
    concat(&locstr, "%#lx", location);

    char *error = NULL;
    set_convvar("$_", locstr, &error);

    desc_auto_convvar_error_if_needed("$_", error);

    free(locstr);

    if(error)
        free(error);

    while(current_location < (location + (num_instrs * data_size))){
        uint8_t *data = malloc(data_size);
        kern_return_t err = read_memory_at_location((void *)current_location,
                data, data_size);

        unsigned long instr = 0;

        /* Do not show any of the BRK #0 written by software breakpoints
         * when the user wants to disassemble memory.
         */
        struct breakpoint *active = find_bp_with_address(current_location);

        if(active)
            instr = CFSwapInt32(active->old_instruction);
        else{
            instr = CFSwapInt32(*(unsigned long *)data);

            char *val = NULL;
            concat(&val, "%#lx", instr);

            error = NULL;
            set_convvar("$__", val, &error);

            desc_auto_convvar_error_if_needed("$__", error);

            free(val);

            if(error)
                free(error);
        }

        char *disassembled = ArmadilloDisassembleB(instr, current_location);

        err = debuggee->get_thread_state();
        
        if(err){
            free(data);
            free(disassembled);
            return KERN_FAILURE;
        }

        printf("%s%#lx:  %s\n",
                debuggee->thread_state.__pc == current_location
                ? "->  " : "    ", current_location, disassembled);

        free(disassembled);

        current_location += data_size;
    }

    return KERN_SUCCESS;
}

kern_return_t dump_memory(unsigned long location, vm_size_t amount){
    if(amount == 0)
        return KERN_SUCCESS;

    const int row_size = 0x10;

    int amount_dumped = 0;
    unsigned long current_location = location;

    while(amount_dumped < amount){
        uint8_t membuffer[row_size];

        kern_return_t ret = read_memory_at_location((void *)current_location,
                membuffer, row_size);

        if(ret)
            return ret;

        int current_row_length = amount - amount_dumped;
        
        if(current_row_length >= row_size)
            current_row_length = row_size;

        printf("  %#lx: ", current_location);

        for(int i=0; i<current_row_length; i++)
            printf("%02x ", (uint8_t)(*(membuffer + i)));

        if(current_row_length < 0x10){
            /* Print filler spaces.
             * Two spaces for would be '%02x', one more for the space after.
             */
            for(int i=current_row_length; i<row_size; i++)
                printf("   ");
        }
        
        printf("  ");

        for(int i=0; i<current_row_length; i++){
            uint8_t cur_char = *(membuffer + i);

            if(isgraph(cur_char))
                putchar(cur_char);
            else
                putchar('.');
        }

        putchar('\n');

        amount_dumped += row_size;
        current_location += row_size;
    }

    return KERN_SUCCESS;
}

kern_return_t read_memory_at_location(void *location, void *buffer,
        vm_size_t length){
    return vm_read_overwrite(debuggee->task,
            (vm_address_t)location,
            length,
            (vm_address_t)buffer,
            &length);
}

kern_return_t write_memory_to_location(vm_address_t location,
        vm_offset_t data, vm_size_t size){
    kern_return_t ret;

    /* Get old protections and figure out whether the
     * location we're writing to exists.
     */
    vm_region_basic_info_data_64_t info;
    vm_address_t region_location = location;
    vm_size_t region_size;
    mach_port_t object_name;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    
    ret = vm_region_64(debuggee->task,
            &region_location,
            &region_size,
            VM_REGION_BASIC_INFO,
            (vm_region_info_t)&info,
            &info_count,
            &object_name);
    
    if(ret)
        return ret;
    
    /* Get raw bytes from this number. */
    void *data_ptr = (uint8_t *)&data;

    vm_protect(debuggee->task,
            location,
            size,
            0,
            VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);

    ret = vm_write(debuggee->task,
            location,
            (pointer_t)data_ptr,
            size);
    
    vm_protect(debuggee->task,
            location,
            size,
            0,
            info.protection);
    
    return ret;
}

kern_return_t valid_location(long location){
    vm_region_basic_info_data_64_t info;
    vm_address_t loc = location;
    vm_size_t region_size;
    mach_port_t object_name;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    
    return vm_region_64(debuggee->task, 
            &loc,
            &region_size,
            VM_REGION_BASIC_INFO,
            (vm_region_info_t)&info,
            &info_count,
            &object_name);
}
