#include <errno.h>
#include <mach/mach.h>
#include <pthread/pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/event.h>
#include <unistd.h>

#include <readline/readline.h>

#include "convvar.h"
#include "dbgops.h"
#include "debuggee.h"
#include "exception.h"
#include "linkedlist.h"
#include "printing.h"
#include "queue.h"
#include "strext.h"
#include "trace.h"

static void *exception_server(void *arg){
    pthread_setname_np("exception handling thread");

    struct req {
        mach_msg_header_t hdr;
        char data[256];
    };

    int num_exceptions = 0, will_auto_resume = 1;

    while(MACH_PORT_VALID(debuggee->exception_port)){ 
        struct req *req = malloc(sizeof(struct req));
        
        mach_msg(&(req->hdr),
                MACH_RCV_MSG,
                0,
                sizeof(struct req),
                debuggee->exception_port,
                MACH_MSG_TIMEOUT_NONE,
                MACH_PORT_NULL);

        /* We got something, suspend debuggee execution. */
        debuggee->suspend();

        Request *request = (Request *)req;
        enqueue(debuggee->exc_requests, request);

        num_exceptions++;

        kern_return_t err = KERN_SUCCESS;

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

            Request *request = (Request *)req;

            if(request && err == KERN_SUCCESS){
                enqueue(debuggee->exc_requests, request);
                num_exceptions++;
            }
        }

        /* Assume we need to automatically resume after this exception.
         * If this flag is set to 0, it will never be set to 1 again.
         */
        will_auto_resume = 1;

        /* Display and reply to what we gathered. */
        while(num_exceptions > 0){
            Request *r = dequeue(debuggee->exc_requests);

            int should_auto_resume = 1, should_print = 1;
            char *what = NULL;

            handle_exception(r,
                    &should_auto_resume,
                    &should_print,
                    &what);

            if(will_auto_resume && !should_auto_resume)
                will_auto_resume = 0;

            if(should_print)
                WriteExceptionBuffer("%s", what);

            free(what);

            reply_to_exception(r, KERN_SUCCESS);

            free(r);

            num_exceptions--;
        }

        if(will_auto_resume)
            debuggee->resume();

        PrintExceptionBuffer();
    }

    return NULL;
}

static void *death_server(void *arg){
    pthread_setname_np("death event thread");

    int kqid = *(int *)arg;

    struct kevent death_event;

    /* Provide a struct for the kernel to write to if any changes occur. */
    int changes = kevent(kqid, NULL, 0, &death_event, 1, NULL);

    /* Don't report if we detached earlier. */
    if(debuggee->pid == -1 || changes < 0){
        free(arg);
        pthread_exit(NULL);
    }

    wait_for_trace();

    /* Figure out how the debuggee exited. */
    int status;
    waitpid(debuggee->pid, &status, 0);

    char *error = NULL;

    if(WIFEXITED(status)){
        int wexitstatus = WEXITSTATUS(status);
        WriteExceptionBuffer("\n[%s (%d) exited normally (status = 0x%8.8x)]\n", 
                debuggee->debuggee_name, debuggee->pid, wexitstatus);

        char *wexitstatusstr = NULL;
        concat(&wexitstatusstr, "%#x", wexitstatus);

        void_convvar("$_exitsignal");
        set_convvar("$_exitcode", wexitstatusstr, &error);

        desc_auto_convvar_error_if_needed("$_exitcode", error);

        free(wexitstatusstr);
    }
    else if(WIFSIGNALED(status)){
        int wtermsig = WTERMSIG(status);
        WriteExceptionBuffer("\n[%s (%d) terminated due to signal %d]\n", 
                debuggee->debuggee_name, debuggee->pid, wtermsig);

        char *wtermsigstr = NULL;
        concat(&wtermsigstr, "%#x", wtermsig);

        void_convvar("$_exitcode");
        set_convvar("$_exitsignal", wtermsigstr, &error);

        desc_auto_convvar_error_if_needed("$_exitsignal", error);

        free(wtermsigstr);
    }

    free(arg);
    ops_detach(1);

    PrintExceptionBuffer();

    if(error)
        free(error);

    close(kqid);

    return NULL;
}

void setup_servers(void){
    debuggee->setup_exception_handling();

    /* Start the exception server. */
    pthread_t exception_server_thread;
    pthread_create(&exception_server_thread, NULL, exception_server, NULL);

    int kqid = kqueue();

    if(kqid == -1){
        printf("Could not create kernel event queue\n");
        return;
    }

    struct kevent kev;

    EV_SET(&kev, debuggee->pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, NULL);

    /* Tell the kernel to add this event to the monitored list. */
    kevent(kqid, &kev, 1, NULL, 0, NULL);

    int *intptr = malloc(sizeof(int));
    *intptr = kqid;

    /* Check if the debuggee dies. */
    pthread_t death_server_thread;
    pthread_create(&death_server_thread, NULL, death_server, intptr);
}
