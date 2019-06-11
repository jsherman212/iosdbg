#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <readline/readline.h>
//#include <readline/history.h>

#include "printutils.h"
#include "strext.h"

pthread_mutex_t MESSAGE_BUF_MUTEX = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t EXCEPTION_BUF_MUTEX = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t MESSAGE_BUF_COND = PTHREAD_COND_INITIALIZER;
pthread_cond_t EXCEPTION_BUF_COND = PTHREAD_COND_INITIALIZER;

char *MESSAGE_BUFFER, *EXCEPTION_BUFFER;

int FLUSH_MESSAGE_BUFFER, FLUSH_EXCEPTION_BUFFER;

#define PROMPT "\033[2m(iosdbg) \033[0m"

static struct {
    int saved_point;
    char *saved_content;
} PROMPT_DATA;

static void remove_prompt(void){
    PROMPT_DATA.saved_point = rl_point;
    PROMPT_DATA.saved_content = rl_copy_text(0, rl_end);

    rl_set_prompt("");
    rl_replace_line("", 0);
    rl_redisplay();
}

static void restore_prompt(void){
    rl_set_prompt(PROMPT);
    rl_replace_line(PROMPT_DATA.saved_content, 0);
    rl_point = PROMPT_DATA.saved_point;
    rl_redisplay();

    free(PROMPT_DATA.saved_content);
    PROMPT_DATA.saved_content = NULL;
}

void *message_buffer_thread(void *arg){
    FLUSH_MESSAGE_BUFFER = 0;

    while(1){
        pthread_mutex_lock(&MESSAGE_BUF_MUTEX); 

        pthread_cond_wait(&MESSAGE_BUF_COND, &MESSAGE_BUF_MUTEX);

        if(MESSAGE_BUFFER){
            printf("%s", MESSAGE_BUFFER);
            free(MESSAGE_BUFFER);
            MESSAGE_BUFFER = NULL;
        }

        FLUSH_MESSAGE_BUFFER = 0;

        pthread_mutex_unlock(&MESSAGE_BUF_MUTEX);
    }

    return NULL;
}

void *exception_buffer_thread(void *arg){
    FLUSH_EXCEPTION_BUFFER = 0;

    while(1){
        pthread_mutex_lock(&EXCEPTION_BUF_MUTEX); 
        //printf("%s: waiting...\n", __func__);

        pthread_cond_wait(&EXCEPTION_BUF_COND, &EXCEPTION_BUF_MUTEX);

        if(EXCEPTION_BUFFER){
            remove_prompt();

            printf("%s", EXCEPTION_BUFFER);
            free(EXCEPTION_BUFFER);
            EXCEPTION_BUFFER = NULL;

            restore_prompt();
        }

        FLUSH_EXCEPTION_BUFFER = 0;

        pthread_mutex_unlock(&EXCEPTION_BUF_MUTEX);
    }

    return NULL;
}
/*
void *stdout_thread(void *arg){
    FLUSH_MESSAGE_BUFFER = 0;
    FLUSH_EXCEPTION_BUFFER = 0;

    while(1){
        pthread_mutex_lock(&MESSAGE_BUF_MUTEX);
        
        if(FLUSH_MESSAGE_BUFFER){
            if(MESSAGE_BUFFER){
                printf("%s", MESSAGE_BUFFER);
                free(MESSAGE_BUFFER);
                MESSAGE_BUFFER = NULL;
            }

            FLUSH_MESSAGE_BUFFER = 0;
        }

        pthread_mutex_unlock(&MESSAGE_BUF_MUTEX);

        pthread_mutex_lock(&EXCEPTION_BUF_MUTEX);

        if(FLUSH_EXCEPTION_BUFFER){
            if(EXCEPTION_BUFFER){
                printf("%s", EXCEPTION_BUFFER);
                free(EXCEPTION_BUFFER);
                EXCEPTION_BUFFER = NULL;
            }

            FLUSH_EXCEPTION_BUFFER = 0;
        }

        // XXX move iosdbg prompt

        pthread_mutex_unlock(&EXCEPTION_BUF_MUTEX);

        usleep(1000);
    }

    return NULL;
}*/

enum {
    EXCEPTION,
    MESSAGE
};

static void write_buffer(int which, const char *msg, va_list args){
    if(which == EXCEPTION){
        vconcat(&EXCEPTION_BUFFER, msg, args);
    }
    else{
        vconcat(&MESSAGE_BUFFER, msg, args);
    }
}

void WriteExceptionBuffer(const char *msg, ...){
    pthread_mutex_lock(&EXCEPTION_BUF_MUTEX);

    va_list args;
    va_start(args, msg);
    write_buffer(EXCEPTION, msg, args);
    va_end(args);

    pthread_cond_signal(&EXCEPTION_BUF_COND);

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

void PrintExceptionBuffer(void){
    pthread_mutex_lock(&EXCEPTION_BUF_MUTEX);
    FLUSH_EXCEPTION_BUFFER = 1;
    pthread_cond_signal(&EXCEPTION_BUF_COND);
    pthread_mutex_unlock(&EXCEPTION_BUF_MUTEX);
}

void PrintMessageBuffer(void){
    pthread_mutex_lock(&MESSAGE_BUF_MUTEX);
    FLUSH_MESSAGE_BUFFER = 1;
    pthread_cond_signal(&MESSAGE_BUF_COND);
    pthread_mutex_unlock(&MESSAGE_BUF_MUTEX);
}

/* Prevent double reprompts. */
void safe_reprompt(void){
    char *linecopy = rl_copy_text(0, rl_end);
    
    rl_line_buffer[rl_point = rl_end = rl_mark = 0] = 0;
    
    if(RL_ISSTATE(RL_STATE_READCMD)){
        rl_on_new_line();
        rl_forced_update_display();
        rl_insert_text(linecopy);
        rl_redisplay();
    }
}
