#include <pthread/pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <readline/readline.h>

#include "strext.h"

int IOSDBG_IO_PIPE[2];
pthread_mutex_t IO_PIPE_LOCK = PTHREAD_MUTEX_INITIALIZER;

size_t copy_file_contents(char *fname, size_t off, void *buf, size_t size){
    if(!fname || !buf)
        return -1;

    FILE *fp = fopen(fname, "rb");

    if(!fp)
        return -1;

    fseek(fp, off, SEEK_SET);
    size_t r = fread(buf, sizeof(char), size, fp);
    fclose(fp);

    return r;
}

int initialize_iosdbg_io(void){
    return pipe(IOSDBG_IO_PIPE);
}

int io_append(const char *fmt, ...){
    pthread_mutex_lock(&IO_PIPE_LOCK);

    va_list args;
    va_start(args, fmt);

    char *dst = NULL;
    vconcat(&dst, fmt, args);

    va_end(args);

    int w = write(IOSDBG_IO_PIPE[1], dst, strlen(dst));
    free(dst);

    /* Use ETX to know our message is finished. */
    w += write(IOSDBG_IO_PIPE[1], "\003", sizeof(char));

    pthread_mutex_unlock(&IO_PIPE_LOCK);

    return w;
}

int io_flush(void){
    /* We already hold the mutex when this is called. */

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

    return written;
}
