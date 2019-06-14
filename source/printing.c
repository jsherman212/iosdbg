#include <mach/mach_time.h>
#include <pthread/pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <readline/readline.h>

#include "printing.h"

pthread_mutex_t printing_mutex = PTHREAD_MUTEX_INITIALIZER;

int rl_printf(int setting, const char *msg, ...){
    pthread_mutex_lock(&printing_mutex);

    int adjust;

    if(setting == WAIT_FOR_REPROMPT){
        /* Wait until readline has reprompted us from the main thread
         * in order to adjust the prompt and assure clean output if we're
         * printing from another thread.
         * For safety, include a timeout of five seconds.
         */
        mach_timebase_info_data_t info;
        kern_return_t err = mach_timebase_info(&info);

        if(err){
            pthread_mutex_unlock(&printing_mutex);
            return -1;
        }

        double timeout = 5.0;

        uint64_t start = mach_absolute_time();
        uint64_t end = start;

        while(!(rl_readline_state & RL_STATE_READCMD)){
            end = mach_absolute_time();

            uint64_t elapsed = end - start;
            uint64_t nanos = elapsed * (info.numer / info.denom);

            if(((double)nanos / NSEC_PER_SEC) >= timeout)
                break;
        }

        adjust = 1;
    }
    else{
        adjust = rl_readline_state & RL_STATE_READCMD;
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
