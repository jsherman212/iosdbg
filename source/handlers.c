#include <stdio.h>

#include "debuggee.h"
#include "linkedlist.h"

unsigned long long find_slide(void){
    vm_region_basic_info_data_64_t info;
    vm_address_t address = 0;
    vm_size_t size;
    mach_port_t object_name;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    
    kern_return_t err = vm_region_64(debuggee->task,
            &address,
            &size,
            VM_REGION_BASIC_INFO,
            (vm_region_info_t)&info,
            &info_count,
            &object_name);

    if(err)
        return err;
    
    return address - 0x100000000;
}

kern_return_t restore_exception_ports(void){
    for(mach_msg_type_number_t i=0;
            i<debuggee->original_exception_ports.count;
            i++)
        task_set_exception_ports(debuggee->task, 
                debuggee->original_exception_ports.masks[i], 
                debuggee->original_exception_ports.ports[i], 
                debuggee->original_exception_ports.behaviors[i], 
                debuggee->original_exception_ports.flavors[i]);

    return KERN_SUCCESS;
}

kern_return_t resume(void){
    return task_resume(debuggee->task);
}

#define WARN_ON_MACH_ERR(err) if(err) \
    printf("%s: %s\n", __func__, mach_error_string(err))

kern_return_t setup_exception_handling(void){
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

kern_return_t deallocate_ports(void){
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

kern_return_t update_threads(thread_act_port_array_t *threads){
    mach_msg_type_number_t thread_count;
    
    kern_return_t err = task_threads(debuggee->task, threads, &thread_count);
    
    WARN_ON_MACH_ERR(err);

    debuggee->thread_count = thread_count;

    return err;
}
