#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <readline/readline.h>

#include "printing.h"
#include "strext.h"

static pthread_mutex_t ERROR_BUF_MUTEX = PTHREAD_MUTEX_INITIALIZER,
                       EXCEPTION_BUF_MUTEX = PTHREAD_MUTEX_INITIALIZER,
                       MESSAGE_BUF_MUTEX = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t ERROR_BUF_COND = PTHREAD_COND_INITIALIZER,
                      EXCEPTION_BUF_COND = PTHREAD_COND_INITIALIZER,
                      MESSAGE_BUF_COND = PTHREAD_COND_INITIALIZER;

static pthread_cond_t DONE_PRINTING_ERROR_BUF_COND = PTHREAD_COND_INITIALIZER,
                      DONE_PRINTING_EXCEPTION_BUF_COND = PTHREAD_COND_INITIALIZER,
                      DONE_PRINTING_MESSAGE_BUF_COND = PTHREAD_COND_INITIALIZER;

char *ERROR_BUFFER, *EXCEPTION_BUFFER, *MESSAGE_BUFFER;

#define PROMPT "\033[2m(iosdbg) \033[0m"

static struct {
    int saved_point;
    char *saved_content;
} SAVED_PROMPT_DATA;

static void remove_prompt(void){
    SAVED_PROMPT_DATA.saved_point = rl_point;
    SAVED_PROMPT_DATA.saved_content = rl_copy_text(0, rl_end);

    rl_set_prompt("");
    rl_replace_line("", 0);
    rl_redisplay();
}

static void restore_prompt(void){
    rl_set_prompt(PROMPT);
    rl_replace_line(SAVED_PROMPT_DATA.saved_content, 0);
    rl_point = SAVED_PROMPT_DATA.saved_point;
    rl_redisplay();

    free(SAVED_PROMPT_DATA.saved_content);
    SAVED_PROMPT_DATA.saved_content = NULL;
}

void *error_buffer_thread(void *arg){
    pthread_setname_np("error buffer thread");

    while(1){
        pthread_mutex_lock(&ERROR_BUF_MUTEX); 

        pthread_cond_wait(&ERROR_BUF_COND, &ERROR_BUF_MUTEX);

        if(ERROR_BUFFER){
            printf("%s", ERROR_BUFFER);
            free(ERROR_BUFFER);
            ERROR_BUFFER = NULL;
        }

        pthread_cond_signal(&DONE_PRINTING_ERROR_BUF_COND);

        pthread_mutex_unlock(&ERROR_BUF_MUTEX);
    }

    return NULL;
}

void *exception_buffer_thread(void *arg){
    pthread_setname_np("exception buffer thread");

    while(1){
        pthread_mutex_lock(&EXCEPTION_BUF_MUTEX); 

        pthread_cond_wait(&EXCEPTION_BUF_COND, &EXCEPTION_BUF_MUTEX);

        if(EXCEPTION_BUFFER){
            remove_prompt();

            printf("%s", EXCEPTION_BUFFER);
            free(EXCEPTION_BUFFER);
            EXCEPTION_BUFFER = NULL;

            restore_prompt();
        }

        pthread_cond_signal(&DONE_PRINTING_EXCEPTION_BUF_COND);

        pthread_mutex_unlock(&EXCEPTION_BUF_MUTEX);
    }

    return NULL;
}

void *message_buffer_thread(void *arg){
    pthread_setname_np("message buffer thread");

    while(1){
        pthread_mutex_lock(&MESSAGE_BUF_MUTEX); 

        pthread_cond_wait(&MESSAGE_BUF_COND, &MESSAGE_BUF_MUTEX);

        if(MESSAGE_BUFFER){
            printf("%s", MESSAGE_BUFFER);
            free(MESSAGE_BUFFER);
            MESSAGE_BUFFER = NULL;
        }

        pthread_cond_signal(&DONE_PRINTING_MESSAGE_BUF_COND);

        pthread_mutex_unlock(&MESSAGE_BUF_MUTEX);
    }

    return NULL;
}

enum {
    ERROR,
    EXCEPTION,
    MESSAGE
};

static void write_buffer(int which, const char *msg, va_list args){
    if(which == ERROR)
        vconcat(&ERROR_BUFFER, msg, args);
    else if(which == EXCEPTION)
        vconcat(&EXCEPTION_BUFFER, msg, args);
    else
        vconcat(&MESSAGE_BUFFER, msg, args);
}

void WriteErrorBuffer(const char *msg, ...){
    pthread_mutex_lock(&ERROR_BUF_MUTEX);

    va_list args;
    va_start(args, msg);
    write_buffer(ERROR, msg, args);
    va_end(args);

    pthread_mutex_unlock(&ERROR_BUF_MUTEX);
}

void WriteExceptionBuffer(const char *msg, ...){
    pthread_mutex_lock(&EXCEPTION_BUF_MUTEX);

    va_list args;
    va_start(args, msg);
    write_buffer(EXCEPTION, msg, args);
    va_end(args);

    pthread_mutex_unlock(&EXCEPTION_BUF_MUTEX);
}

void WriteMessageBuffer(const char *msg, ...){
    pthread_mutex_lock(&MESSAGE_BUF_MUTEX);

    va_list args;
    va_start(args, msg);
    write_buffer(MESSAGE, msg, args);
    va_end(args);

    pthread_mutex_unlock(&MESSAGE_BUF_MUTEX);
}

void PrintErrorBuffer(void){
    pthread_mutex_lock(&ERROR_BUF_MUTEX);
    pthread_cond_signal(&ERROR_BUF_COND);
    pthread_cond_wait(&DONE_PRINTING_ERROR_BUF_COND, &ERROR_BUF_MUTEX);
    pthread_mutex_unlock(&ERROR_BUF_MUTEX);
}

void PrintExceptionBuffer(void){
    pthread_mutex_lock(&EXCEPTION_BUF_MUTEX);
    pthread_cond_signal(&EXCEPTION_BUF_COND);
    pthread_cond_wait(&DONE_PRINTING_EXCEPTION_BUF_COND, &EXCEPTION_BUF_MUTEX);
    pthread_mutex_unlock(&EXCEPTION_BUF_MUTEX);
}

void PrintMessageBuffer(void){
    pthread_mutex_lock(&MESSAGE_BUF_MUTEX);
    pthread_cond_signal(&MESSAGE_BUF_COND);
    pthread_cond_wait(&DONE_PRINTING_MESSAGE_BUF_COND, &MESSAGE_BUF_MUTEX);
    pthread_mutex_unlock(&MESSAGE_BUF_MUTEX);
}

void ClearErrorBuffer(void){
    pthread_mutex_lock(&ERROR_BUF_MUTEX);
    free(ERROR_BUFFER);
    ERROR_BUFFER = NULL;
    pthread_mutex_unlock(&ERROR_BUF_MUTEX);
}

void ClearExceptionBuffer(void){
    pthread_mutex_lock(&EXCEPTION_BUF_MUTEX);
    free(EXCEPTION_BUFFER);
    EXCEPTION_BUFFER = NULL;
    pthread_mutex_unlock(&EXCEPTION_BUF_MUTEX);
}

void ClearMessageBuffer(void){
    pthread_mutex_lock(&MESSAGE_BUF_MUTEX);
    free(MESSAGE_BUFFER);
    MESSAGE_BUFFER = NULL;
    pthread_mutex_unlock(&MESSAGE_BUF_MUTEX);
}
