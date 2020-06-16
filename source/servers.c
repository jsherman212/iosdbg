#include <errno.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/event.h>
#include <unistd.h>

#include "convvar.h"
#include "dbgio.h"
#include "dbgops.h"
#include "debuggee.h"
#include "exception.h"
#include "linkedlist.h"
#include "queue.h"
#include "servers.h"
#include "strext.h"
#include "thread.h"
#include "trace.h"

pthread_mutex_t EXCEPTION_QUEUE_MUTEX = PTHREAD_MUTEX_INITIALIZER;
queue_t *EXCEPTION_QUEUE = NULL;
int NEED_REPLY = 0;

struct req {
    mach_msg_header_t hdr;
    char data[256];
};

static void *exception_server(void *arg){
    pthread_setname_np("exception handling thread");

    int will_auto_resume = 1;

    EXCEPTION_QUEUE = queue_new();
    queue_t *exc_queue_internal = queue_new();

    while(MACH_PORT_VALID(debuggee->exception_port)){ 
        struct req *req = malloc(sizeof(struct req));

        /* Set a one second timeout so we can check if we need to
         * shutdown this thread.
         * For whatever reason, after deallocating this exception port,
         * mach_msg doesn't return.
         */
        kern_return_t err = mach_msg(&(req->hdr),
                MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                0,
                sizeof(struct req),
                debuggee->exception_port,
                1000,
                MACH_PORT_NULL);

        if(err == MACH_RCV_TIMED_OUT){
            free(req);
            continue;
        }

        /* We got something, suspend debuggee execution. */
        ops_suspend();

        enqueue(exc_queue_internal, (Request *)req);

        EXC_QUEUE_LOCK;
        NEED_REPLY = 1;
        enqueue(EXCEPTION_QUEUE, (Request *)req);
        EXC_QUEUE_UNLOCK;

        err = KERN_SUCCESS;

        /* Gather up any more exceptions. */
        while(err != MACH_RCV_TIMED_OUT){
            req = malloc(sizeof(struct req));

            err = mach_msg(&(req->hdr),
                    MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                    0,
                    sizeof(struct req),
                    debuggee->exception_port,
                    0,
                    MACH_PORT_NULL);

            if(err == KERN_SUCCESS){
                enqueue(exc_queue_internal, (Request *)req);
                
                EXC_QUEUE_LOCK;
                enqueue(EXCEPTION_QUEUE, (Request *)req);
                EXC_QUEUE_UNLOCK;
            }
        }

        /* Assume we need to automatically resume after this exception.
         * If this flag is set to 0, it will never be set to 1 again.
         */
        will_auto_resume = 1;
        char *exception_buffer = NULL;

        Request *r = dequeue(exc_queue_internal);

        while(r){
            int should_auto_resume = 1, should_print = 1;
            char *what = NULL;

            handle_exception(r,
                    &should_auto_resume,
                    &should_print,
                    &what);

            if(will_auto_resume && !should_auto_resume)
                will_auto_resume = 0;

            if(should_print)
                concat(&exception_buffer, "%s", what);

            free(what);

            r = dequeue(exc_queue_internal);
        }

        if(will_auto_resume)
            ops_resume();

        if(exception_buffer){
            io_append("%s", exception_buffer);
            free(exception_buffer);
        }
    }

    queue_free(EXCEPTION_QUEUE);
    queue_free(exc_queue_internal);

    return NULL;
}

static void *death_server(void *arg){
    pthread_setname_np("death event thread");

    int kqid = *(int *)arg;

    struct kevent death_event;

    /* Provide a struct for the kernel to write to if any changes occur. */
    int changes = kevent(kqid, NULL, 0, &death_event, 1, NULL);

    /* Don't report if we detached earlier. */
    if(debuggee->pid == -1){
        free(arg);
        pthread_exit(NULL);
    }

    wait_for_trace();

    /* Figure out how the debuggee exited. */
    int status;
    waitpid(debuggee->pid, &status, 0);

    char *exitbuf = NULL, *error = NULL;

    if(WIFEXITED(status)){
        int wexitstatus = WEXITSTATUS(status);
        concat(&exitbuf, "\n[%s (%d) exited normally (status = 0x%8.8x)]\n", 
                debuggee->debuggee_name, debuggee->pid, wexitstatus);

        char *wexitstatusstr = NULL;
        concat(&wexitstatusstr, "%#x", wexitstatus);

        void_convvar("$_exitsignal");
        set_convvar("$_exitcode", wexitstatusstr, &error);

        desc_auto_convvar_error_if_needed(&exitbuf, "$_exitcode", error);

        free(wexitstatusstr);
    }
    else if(WIFSIGNALED(status)){
        int wtermsig = WTERMSIG(status);
        concat(&exitbuf, "\n[%s (%d) terminated due to signal %d]\n", 
                debuggee->debuggee_name, debuggee->pid, wtermsig);

        char *wtermsigstr = NULL;
        concat(&wtermsigstr, "%#x", wtermsig);

        void_convvar("$_exitcode");
        set_convvar("$_exitsignal", wtermsigstr, &error);

        desc_auto_convvar_error_if_needed(&exitbuf, "$_exitsignal", error);

        free(wtermsigstr);
    }

    ops_detach(1, &exitbuf);

    io_append("%s", exitbuf);

    free(exitbuf);
    free(error);

    close(kqid);

    free(arg);

    return NULL;
}

static void *thread_monitor_server(void *arg){
    pthread_setname_np("debuggee thread monitor");

    while(MACH_PORT_VALID(THREAD_DEATH_NOTIFY_PORT)){
        struct req *req = malloc(sizeof(struct req));

        /* Update the list of threads every second or right
         * after one goes away.
         */
        mach_msg(&(req->hdr),
                MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                0,
                sizeof(struct req),
                THREAD_DEATH_NOTIFY_PORT,
                1000,
                MACH_PORT_NULL);

        free(req);

        char *thbuffer = NULL;
        
        ops_threadupdate(&thbuffer);

        if(thbuffer){
            io_append("%s", thbuffer);
            free(thbuffer);
        }
    }
    
    return NULL;
}

void setup_servers(char **outbuffer){
    debuggee->setup_exception_handling(outbuffer);

    pthread_t exception_server_thread;
    pthread_create(&exception_server_thread, NULL, exception_server, NULL);

    int kqid = kqueue();

    if(kqid == -1)
        concat(outbuffer, "warning: could not create kernel event queue\n");
    else{
        struct kevent kev;

        EV_SET(&kev, debuggee->pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, NULL);

        /* Tell the kernel to add this event to the monitored list. */
        kevent(kqid, &kev, 1, NULL, 0, NULL);

        int *intptr = malloc(sizeof(int));
        *intptr = kqid;

        pthread_t death_server_thread;
        pthread_create(&death_server_thread, NULL, death_server, intptr);
    }

    pthread_t tmon_thread;
    pthread_create(&tmon_thread, NULL, thread_monitor_server, NULL);
}
