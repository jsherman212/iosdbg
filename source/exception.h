#ifndef _EXCEPTION_H_
#define _EXCEPTION_H_

#include <mach/mach.h>
#include <pthread/pthread.h>

extern pthread_mutex_t SET_SS_BP_LOCK_MUTEX;

#define SS_BP_LOCK pthread_mutex_lock(&SET_SS_BP_LOCK_MUTEX)
#define SS_BP_UNLOCK pthread_mutex_unlock(&SET_SS_BP_LOCK_MUTEX)

typedef struct {
    mach_msg_header_t Head;
    /* start of the kernel processed data */
    mach_msg_body_t msgh_body;
    mach_msg_port_descriptor_t thread;
    mach_msg_port_descriptor_t task;
    /* end of the kernel processed data */
    NDR_record_t NDR;
    exception_type_t exception;
    mach_msg_type_number_t codeCnt;
    int code[2];
    mach_msg_trailer_t trailer;
} Request;

typedef struct {
    mach_msg_header_t Head;
    NDR_record_t NDR;
    kern_return_t RetCode;
} Reply;

void handle_exception(Request *, int *, int *, char **);
void reply_to_exception(Request *, kern_return_t);

#endif
