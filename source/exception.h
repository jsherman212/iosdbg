#ifndef _EXCEPTION_H_
#define _EXCEPTION_H_

#include <mach/mach.h>
#include <pthread/pthread.h>

//static pthread_mutex_t HAS_REPLIED_MUTEX = PTHREAD_MUTEX_INITIALIZER;

/*static pthread_cond_t MAIN_THREAD_CHANGED_REPLIED_VAR_COND = PTHREAD_COND_INITIALIZER;
static pthread_cond_t EXC_SERVER_CHANGED_REPLIED_VAR_COND = PTHREAD_COND_INITIALIZER;

static int HAS_REPLIED_TO_LATEST_EXCEPTION = 0;
*/

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

void handle_exception(Request *);
void reply_to_exception(Request *, kern_return_t);

#endif
