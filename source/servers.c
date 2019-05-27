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
#include "printutils.h"
#include "queue.h"
#include "strext.h"
#include "trace.h"

#include "cmd/cmd.h"
#include "cmd/misccmd.h"

extern pthread_mutex_t HAS_REPLIED_MUTEX;

extern pthread_cond_t MAIN_THREAD_CHANGED_REPLIED_VAR_COND;
extern pthread_cond_t EXC_SERVER_CHANGED_REPLIED_VAR_COND;

extern int HAS_REPLIED_TO_LATEST_EXCEPTION;

#ifndef PRINT
//#define PRINT
#endif

static void *exception_server(void *arg){
    struct msg {
        mach_msg_header_t hdr;
        char data[256];
    } req, rpl;

    //struct msg req, rpl;

    kern_return_t err;
    // while MACH_PORT_VALID...
    while(1){
        mach_msg(&req.hdr,
                MACH_RCV_MSG,
                0,
                sizeof(req),
                debuggee->exception_port,
                MACH_MSG_TIMEOUT_NONE,
                MACH_PORT_NULL);

        Request *r = (Request *)&req;

        if(r){
            //printf("\n%s: enqueueing request %p, pending exceptions %d\n", __func__, r,
            //      debuggee->pending_exceptions);
            
            // XXX start critical section
            pthread_mutex_lock(&HAS_REPLIED_MUTEX);


            enqueue(debuggee->exc_requests, r);
            debuggee->pending_exceptions++;

            debuggee->exc_num++;

            printf("%d. *****START HANDLING\n", debuggee->exc_num);
            handle_exception(r);
            printf("%d. *****END HANDLING\n", debuggee->exc_num);

            while(!HAS_REPLIED_TO_LATEST_EXCEPTION){
                printf("%s: waiting for the main thread...\n", __func__);
                pthread_cond_wait(&MAIN_THREAD_CHANGED_REPLIED_VAR_COND,
                        &HAS_REPLIED_MUTEX);
            }

            HAS_REPLIED_TO_LATEST_EXCEPTION = 0;

            printf("%s: signaling the main thread...\n", __func__);
            pthread_cond_signal(&EXC_SERVER_CHANGED_REPLIED_VAR_COND);
            
            pthread_mutex_unlock(&HAS_REPLIED_MUTEX);
            /*
            // XXX higher value in usleep minimizes the possibility of a screw up
            while(!HAS_REPLIED_TO_LATEST_EXCEPTION){usleep(10000);}

            HAS_REPLIED_TO_LATEST_EXCEPTION = 0;
            */
        }
    }
}

static void *death_server(void *arg){
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
        
        do_cmdline_command("detach", NULL, 0, &error);

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
