#include <mach-o/loader.h>
#include <stdio.h>
#include <stdlib.h>

#include "debuggee.h"
#include "linkedlist.h"
#include "memutils.h"
#include "strext.h"

unsigned long find_slide(void){
    kern_return_t err = KERN_SUCCESS;
    vm_address_t addr = 0;
    unsigned int depth;
    vm_size_t sz = 0;

    unsigned long addrof__mh_execute_header = 0;
    struct mach_header_64 mh = {0};

    while(1){
        struct vm_region_submap_info_64 info;
        mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;

        err = vm_region_recurse_64(debuggee->task, &addr, &sz, &depth,
                (vm_region_info_t)&info, &count);

        if(err)
            return 0;

        err = read_memory_at_location((void *)addr, &mh, sizeof(mh));

        if(err == KERN_SUCCESS){
            if(mh.magic == MH_MAGIC_64 && mh.filetype == MH_EXECUTE){
                addrof__mh_execute_header = addr;
                break;
            }
        }

        addr += sz;
    }

    if(addrof__mh_execute_header == 0)
        return -1;
    
    struct load_command *cmd = malloc(sizeof(struct load_command));

    addr += sizeof(mh);

    err = read_memory_at_location((void *)addr, cmd, sizeof(struct load_command));

    for(int i=0; i<mh.ncmds; i++){
        struct segment_command_64 *segcmd = malloc(sizeof(struct segment_command_64));
        read_memory_at_location((void *)addr, segcmd, sizeof(struct segment_command_64));

        if(segcmd && segcmd->cmd == LC_SEGMENT_64){
            if(strcmp(segcmd->segname, "__TEXT") == 0){
                free(cmd);
                free(segcmd);
                return (addrof__mh_execute_header - segcmd->vmaddr);
            }
        }

        free(segcmd);
        
        addr += segcmd->cmdsize;
    }

    free(cmd);

    return -1;
}

kern_return_t restore_exception_ports(void){
    for(mach_msg_type_number_t i=0;
            i<debuggee->original_exception_ports.count;
            i++){
        task_set_exception_ports(debuggee->task, 
                debuggee->original_exception_ports.masks[i], 
                debuggee->original_exception_ports.ports[i], 
                debuggee->original_exception_ports.behaviors[i], 
                debuggee->original_exception_ports.flavors[i]);
    }

    return KERN_SUCCESS;
}

kern_return_t resume(void){
    return task_resume(debuggee->task);
}

#define WARN_ON_MACH_ERR(err) if(err && outbuffer && (*outbuffer)) \
    concat(outbuffer, "%s: %s\n", __func__, mach_error_string(err))

kern_return_t setup_exception_handling(char **outbuffer){
    /* Create an exception port for the debuggee. */
    kern_return_t err = mach_port_allocate(mach_task_self(), 
            MACH_PORT_RIGHT_RECEIVE,
            &debuggee->exception_port);

    WARN_ON_MACH_ERR(err);
    
    /* Be able to send messages on that port. */
    err = mach_port_insert_right(mach_task_self(),
            debuggee->exception_port,
            debuggee->exception_port,
            MACH_MSG_TYPE_MAKE_SEND);

    WARN_ON_MACH_ERR(err);
    
    /* Save the old exception ports. */
    err = task_get_exception_ports(debuggee->task,
            EXC_MASK_ALL, 
            debuggee->original_exception_ports.masks, 
            &debuggee->original_exception_ports.count, 
            debuggee->original_exception_ports.ports, 
            debuggee->original_exception_ports.behaviors, 
            debuggee->original_exception_ports.flavors);

    WARN_ON_MACH_ERR(err);

    /* Add the ability to get exceptions on the debuggee exception port.
     * OR EXCEPTION_DEFAULT with MACH_EXCEPTION_CODES so 64-bit safe
     * exception messages will be provided.
     */
    err = task_set_exception_ports(debuggee->task,
            EXC_MASK_ALL, 
            debuggee->exception_port,
            EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES,
            THREAD_STATE_NONE);

    WARN_ON_MACH_ERR(err);

    return err;
}

kern_return_t deallocate_ports(char **outbuffer){
    kern_return_t err = mach_port_deallocate(mach_task_self(),
            debuggee->exception_port);

    WARN_ON_MACH_ERR(err);
    
    err = mach_port_deallocate(mach_task_self(), debuggee->task);

    WARN_ON_MACH_ERR(err);

    return err;
}

kern_return_t suspend(void){
    return task_suspend(debuggee->task);
}

kern_return_t get_threads(thread_act_port_array_t *threads, char **outbuffer){
    mach_msg_type_number_t thread_count;
    
    kern_return_t err = task_threads(debuggee->task, threads, &thread_count);
    
    WARN_ON_MACH_ERR(err);

    debuggee->thread_count = thread_count;

    return err;
}

int suspended(void){
    struct task_basic_info_64 info = {0};
    mach_msg_type_number_t count = TASK_BASIC_INFO_64_COUNT;

    kern_return_t err = task_info(debuggee->task,
            TASK_BASIC_INFO_64,
            (task_info_t)&info,
            &count);

    if(err)
        return 0;

    return info.suspend_count == 1;
}
