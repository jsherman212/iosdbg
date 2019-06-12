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

#include "cmd/cmd.h"
#include "cmd/misccmd.h"

#include "handlers.h"
static void *exception_server(void *arg){
    pthread_setname_np("exception thread");

    struct req {
        mach_msg_header_t hdr;
        char data[256];
    };

    NUM_EXCEPTIONS = 0;
    AUTO_RESUME = 1;

    while(MACH_PORT_VALID(debuggee->exception_port)){ 
        // pthread_testcancel
        //struct req req;
        //memset(&req, 0, sizeof(req));
        struct req *req = malloc(sizeof(struct req));
        
        kern_return_t err = mach_msg(&(req->hdr),//&req.hdr,
                MACH_RCV_MSG,
                0,
                sizeof(struct req),
                debuggee->exception_port,
                MACH_MSG_TIMEOUT_NONE,
                MACH_PORT_NULL);

        /* We got something, suspend debuggee execution. */
        debuggee->suspend();

        //Request *request = (Request *)&req;
        Request *request = (Request *)req;
        //printf("%s: request %p err %s\n",
          //      __func__, request, mach_error_string(err));
        enqueue(debuggee->exc_requests, request);

        NUM_EXCEPTIONS++;

        err = KERN_SUCCESS;

        /* Gather up any more exceptions. */
        while(err != MACH_RCV_TIMED_OUT){
            //struct req req2;
            struct req *req2 = malloc(sizeof(struct req));
            //memset(&req2, 0, sizeof(req2));
            err = mach_msg(&(req2->hdr),//&req2.hdr,
                    MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                    0,
                    sizeof(struct req),
                    debuggee->exception_port,
                    0,
                    MACH_PORT_NULL);

            //Request *request = (Request *)&req2;
            Request *request = (Request *)req2;
            //printf("%s: in loop: request %p err %s\n",
              //      __func__, request, mach_error_string(err));

            if(request && err == KERN_SUCCESS){
                enqueue(debuggee->exc_requests, request);
                NUM_EXCEPTIONS++;
            }
        }

        AUTO_RESUME = 1;

        /* Display and reply to what we gathered. */
        while(NUM_EXCEPTIONS > 0){
            Request *r = dequeue(debuggee->exc_requests);
            //printf("%s: dequeueing exception %p\n", __func__, r);

            int should_auto_resume = 1, should_print = 1;
            char *what = NULL;

            handle_exception(r,
                    &should_auto_resume,
                    &should_print,
                    &what);

            if(AUTO_RESUME && !should_auto_resume)
                AUTO_RESUME = 0;

            if(should_print){
                WriteExceptionBuffer("%s", what);
                free(what);
            }

            reply_to_exception(r, KERN_SUCCESS);

            free(r);

            NUM_EXCEPTIONS--;
        }
/*
        printf("%s: auto resume %d, anything more? %p\n", __func__, AUTO_RESUME,
            queue_peek(debuggee->exc_requests));
        printf("%s: debuggee suspend count %d\n", __func__, sus_count());
*/
        if(AUTO_RESUME)
            debuggee->resume();

        PrintExceptionBuffer();
    }

    return NULL;
}

static void *death_server(void *arg){
    pthread_setname_np("death event thread");

    int kqid = *(int *)arg;

    while(1){
        struct kevent death_event;

        /* Provide a struct for the kernel to write to if any changes occur. */
        int changes = kevent(kqid, NULL, 0, &death_event, 1, NULL);

        /* Don't report if we detached earlier. */
        if(debuggee->pid == -1){
            free(arg);
            pthread_exit(NULL);
        }

        if(changes < 0){
            printf("kevent: %s\n", strerror(errno));
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
            printf("\n[%s (%d) exited normally (status = 0x%8.8x)]\n", 
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
            printf("\n[%s (%d) terminated due to signal %d]\n", 
                    debuggee->debuggee_name, debuggee->pid, wtermsig);

            char *wtermsigstr = NULL;
            concat(&wtermsigstr, "%#x", wtermsig);

            void_convvar("$_exitcode");
            set_convvar("$_exitsignal", wtermsigstr, &error);

            desc_auto_convvar_error_if_needed("$_exitsignal", error);

            free(wtermsigstr);
        }

        free(arg);

        pthread_mutex_lock(&DEATH_SERVER_DETACHED_MUTEX);

        ops_detach(1);

        pthread_cond_signal(&DEATH_SERVER_DETACHED_COND);
        pthread_mutex_unlock(&DEATH_SERVER_DETACHED_MUTEX);

        if(error)
            free(error);

        close(kqid);
        safe_reprompt();
        pthread_exit(NULL);
    }

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
