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

#include <signal.h>
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
        struct req req;
        //printf("%s: waiting for an exception\n", __func__);
        mach_msg(&req.hdr,
                MACH_RCV_MSG,
                0,
                sizeof(req),
                debuggee->exception_port,
                MACH_MSG_TIMEOUT_NONE,
                MACH_PORT_NULL);

        //printf("%s: got an exception, would kick main thread out of readline\n", __func__);
        //pthread_kill(MAIN_THREAD_TID, SIGINT);

        /*
        pthread_mutex_lock(&STUFF_CHAR_MUTEX);
        printf("%s: stuffing newline\n", __func__);
        rl_stuff_char('\r');
        GOT_NEWLINE_STUFFED = 1;
        pthread_mutex_unlock(&STUFF_CHAR_MUTEX);
        */
        pthread_mutex_lock(&EXCEPTION_MUTEX);

        //rl_clear_visible_line();
        //rl_redisplay();

        //printf("%s: locked!\n", __func__);
        //rl_already_prompted = 1;
        HANDLING_EXCEPTIONS = 1;

        task_suspend(debuggee->task);

        //debuggee->suspend();
        //debuggee->interrupted = 1;

        Request *request = (Request *)&req;
        enqueue(debuggee->exc_requests, request);

        NUM_EXCEPTIONS++;

        kern_return_t err = KERN_SUCCESS;

        /* Gather up any more exceptions. */
        while(err != MACH_RCV_TIMED_OUT){
            struct req req2;
            err = mach_msg(&req2.hdr,
                    MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                    0,
                    sizeof(req2),
                    debuggee->exception_port,
                    0,
                    MACH_PORT_NULL);

            //printf("%s: err with timeout: '%s'\n", __func__, mach_error_string(err));
            Request *request = (Request *)&req2;

            if(request && err == KERN_SUCCESS){
                enqueue(debuggee->exc_requests, request);
                NUM_EXCEPTIONS++;
            }
        }

        //printf("%s: in total, we got %d exceptions\n", __func__, NUM_EXCEPTIONS);

        AUTO_RESUME = 1;

        /* Display and reply to what we gathered. */
        while(NUM_EXCEPTIONS > 0){
            Request *r = dequeue(debuggee->exc_requests);

            int should_auto_resume = 1, should_print = 1;
            char *what = NULL;

            handle_exception(r,
                    &should_auto_resume,
                    &should_print,
                    &what);

            if(AUTO_RESUME && !should_auto_resume){
                AUTO_RESUME = 0;
            }

            if(should_print){
                printf("%s", what);
                free(what);
            }

            reply_to_exception(r, KERN_SUCCESS);

            NUM_EXCEPTIONS--;
        }
        //printf("%s: AUTO_RESUME %d\n", __func__, AUTO_RESUME);
        if(AUTO_RESUME)
            task_resume(debuggee->task);

        KICK_MAIN_THREAD_OUT_OF_READLINE = 1;

        //SAVED_RL_INSTREAM = dup(fileno(rl_instream));

        //close(fileno(rl_instream));
        //close(STDIN_FILENO);


        //kill(getpid(), SIGINT);
        
        /*printf("%s: waiting for the main thread to be ready...\n", __func__);
        printf("%s: wait for main thread? %d\n",
                __func__, WAIT_FOR_MAIN_THREAD);
                */
        //while(WAIT_FOR_MAIN_THREAD){
          //  pthread_cond_wait(&MAIN_THREAD_READY_COND, &EXCEPTION_MUTEX);
        //}

        //printf("%s: signaling for reprompt\n", __func__);

        HANDLING_EXCEPTIONS = 0;
        //AUTO_RESUME = 1;

        //rl_already_prompted = 0;
       pthread_cond_signal(&REPROMPT_COND); 
        //HANDLING_EXCEPTIONS = 0;
        //printf("\033[2m(iosdbg) \033[0m");
        
        //printf("%s: waiting for the okay to wait indefinitely...\n", __func__);
        //pthread_cond_wait(&RESTART_COND, &EXCEPTION_MUTEX);

        //task_resume(debuggee->task);

        //HANDLING_EXCEPTIONS = 0;

        //if(AUTO_RESUME)
            //ops_resume();
        
        //printf("\033[2m(iosdbg) \033[0m");
        //rl_already_prompted = 1;

        pthread_mutex_unlock(&EXCEPTION_MUTEX);

        //printf("%s: unlocked!\n", __func__);
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
