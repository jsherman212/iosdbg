#include <pthread/pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <readline/readline.h>

#include "strext.h"

static pthread_mutex_t pipe_lock = PTHREAD_MUTEX_INITIALIZER;

int IOSDBG_IO_PIPE[2];

int io_append(const char *fmt, ...){
    pthread_mutex_lock(&pipe_lock);

    va_list args;
    va_start(args, fmt);

    char *dst = NULL;
    vconcat(&dst, fmt, args);

    /* Use ETX to know our message is finished. */
    concat(&dst, "\003");

    va_end(args);

    int w = write(IOSDBG_IO_PIPE[1], dst, strlen(dst));

    free(dst);

    pthread_mutex_unlock(&pipe_lock);

    return w;
}

int io_flush(void){
    pthread_mutex_lock(&pipe_lock);

    int saved_point = rl_point;
    char *saved_line = rl_copy_text(0, rl_end);
    rl_save_prompt();
    rl_replace_line("", 0);
    rl_redisplay();

    int written = 0;

    char ch;
    while(read(IOSDBG_IO_PIPE[0], &ch, sizeof(ch)) > 0){
        if(ch == '\003')
            break;

        write(fileno(rl_outstream), &ch, sizeof(ch));
        written++;
    }

    rl_restore_prompt();
    rl_replace_line(saved_line, 0);
    rl_point = saved_point;
    rl_redisplay();

    free(saved_line);

    pthread_mutex_unlock(&pipe_lock);

    return written;
}

int initialize_iosdbg_io(void){
    return pipe(IOSDBG_IO_PIPE);
}
