#include <pthread/pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>

#include "printing.h"

pthread_mutex_t printing_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t printing_cond = PTHREAD_COND_INITIALIZER;

int rl_printf(int setting, const char *msg, ...){
    pthread_mutex_lock(&printing_mutex);

    int adjust;

    /* The only thread which guarantees non-garbled printing is the main
     * thread because it's the one which handles reprompting. In order for
     * other threads to print to stdout cleanly, we wait for the main
     * thread to tell us it has reprompted. From there, we can remove the
     * current prompt, print to stdout, then restore the prompt.
     */
    if(setting == NOT_MAIN_THREAD){
        pthread_cond_wait(&printing_cond, &printing_mutex);
        adjust = 1;
    }
    else{
        adjust = 0;
    }

    char *saved_line;
    int saved_point;

    if(adjust){
        saved_point = rl_point;
        saved_line = rl_copy_text(0, rl_end);
        rl_save_prompt();
        rl_replace_line("", 0);
        rl_redisplay();
    }

    va_list args;
    va_start(args, msg);
    int w = vprintf(msg, args);
    va_end(args);

    if(adjust){
        rl_restore_prompt();
        rl_replace_line(saved_line, 0);
        rl_point = saved_point;
        rl_redisplay();

        free(saved_line);
    }

    pthread_mutex_unlock(&printing_mutex);

    return w;
}

void notify_of_reprompt(void){
    pthread_mutex_lock(&printing_mutex);
    pthread_cond_broadcast(&printing_cond);
    pthread_mutex_unlock(&printing_mutex);
}
