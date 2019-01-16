#include "handlers.h"

unsigned long long find_slide(void){
	vm_region_basic_info_data_64_t info;
	vm_address_t address = 0;
	vm_size_t size;
	mach_port_t object_name;
	mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
	
	kern_return_t err = vm_region_64(debuggee->task, &address, &size, VM_REGION_BASIC_INFO, (vm_region_info_t)&info, &info_count, &object_name);

	if(err)
		return err;
	
	return address - 0x100000000;
}

kern_return_t restore_exception_ports(void){
	for(mach_msg_type_number_t i=0; i<debuggee->original_exception_ports.count; i++)
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

kern_return_t setup_exception_handling(void){
	// make an exception port for the debuggee
	kern_return_t err = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &debuggee->exception_port);
	
	CHECK_MACH_ERROR(err);

	// be able to send messages on that exception port
	err = mach_port_insert_right(mach_task_self(), debuggee->exception_port, debuggee->exception_port, MACH_MSG_TYPE_MAKE_SEND);
	
	CHECK_MACH_ERROR(err);

	mach_port_t port_set;

	err = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &port_set);

	CHECK_MACH_ERROR(err);

	err = mach_port_move_member(mach_task_self(), debuggee->exception_port, port_set);

	CHECK_MACH_ERROR(err);

	// allocate port to notify us of termination
	err = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &debuggee->death_port);

	CHECK_MACH_ERROR(err);

	err = mach_port_move_member(mach_task_self(), debuggee->death_port, port_set);
	
	CHECK_MACH_ERROR(err);

	mach_port_t p;
	err = mach_port_request_notification(mach_task_self(), debuggee->task, MACH_NOTIFY_DEAD_NAME, 0, debuggee->death_port, MACH_MSG_TYPE_MAKE_SEND_ONCE, &p);
	
	CHECK_MACH_ERROR(err);

	// save the old exception ports
	err = task_get_exception_ports(debuggee->task, EXC_MASK_ALL, debuggee->original_exception_ports.masks, &debuggee->original_exception_ports.count, debuggee->original_exception_ports.ports, debuggee->original_exception_ports.behaviors, debuggee->original_exception_ports.flavors);

	CHECK_MACH_ERROR(err);

	// add the ability to get exceptions on the debuggee exception port
	// OR EXCEPTION_DEFAULT with MACH_EXCEPTION_CODES so 64-bit safe exception messages will be provided 
	err = task_set_exception_ports(debuggee->task, EXC_MASK_ALL, debuggee->exception_port, EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, THREAD_STATE_NONE);

	CHECK_MACH_ERROR(err);

	return err;
}

kern_return_t deallocate_ports(void){
	kern_return_t err = mach_port_deallocate(mach_task_self(), debuggee->exception_port);

	CHECK_MACH_ERROR(err);
	
	return err;
}

kern_return_t suspend(void){
	return task_suspend(debuggee->task);
}

kern_return_t update_threads(thread_act_port_array_t *threads){
	mach_msg_type_number_t thread_count;
	
	kern_return_t err = task_threads(debuggee->task, threads, &thread_count);
	
	debuggee->thread_count = thread_count;

	return err;
}

kern_return_t get_debug_state(void){
	mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;

	struct machthread *focused = machthread_getfocused();

	kern_return_t kret = thread_get_state(focused->port, ARM_DEBUG_STATE64, (thread_state_t)&debuggee->debug_state, &count);

	return kret;
}

/* Must call get_debug_state before calling this. */
kern_return_t set_debug_state(void){
	mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;

	struct machthread *focused = machthread_getfocused();
	
	kern_return_t kret = thread_set_state(focused->port, ARM_DEBUG_STATE64, (thread_state_t)&debuggee->debug_state, count);

	return kret;
}

kern_return_t get_thread_state(void){
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;

	struct machthread *focused = machthread_getfocused();

	kern_return_t kret = thread_get_state(focused->port, ARM_THREAD_STATE64, (thread_state_t)&debuggee->thread_state, &count);

	return kret;
}

/* Must call get_thread_state before calling this. */
kern_return_t set_thread_state(void){
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;

	struct machthread *focused = machthread_getfocused();
	
	kern_return_t kret = thread_set_state(focused->port, ARM_THREAD_STATE64, (thread_state_t)&debuggee->thread_state, count);

	return kret;
}

kern_return_t get_neon_state(void){
	mach_msg_type_number_t count = ARM_NEON_STATE64_COUNT;
	
	struct machthread *focused = machthread_getfocused();

	kern_return_t kret = thread_get_state(focused->port, ARM_NEON_STATE64, (thread_state_t)&debuggee->neon_state, &count);

	return kret;
}

/* Must call get_neon_state before calling this. */
kern_return_t set_neon_state(void){
	mach_msg_type_number_t count = ARM_NEON_STATE64_COUNT;
	
	struct machthread *focused = machthread_getfocused();

	kern_return_t kret = thread_set_state(focused->port, ARM_NEON_STATE64, (thread_state_t)&debuggee->neon_state, count);

	return kret;
}
