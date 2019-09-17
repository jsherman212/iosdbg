#ifndef _DBGIO_H_
#define _DBGIO_H_

#include <pthread/pthread.h>

extern int IOSDBG_IO_PIPE[2];
extern pthread_mutex_t IO_PIPE_LOCK;

size_t copy_file_contents(char *, size_t, void *, size_t);
int initialize_iosdbg_io(void);
int io_append(const char *, ...);
int io_flush(void);

#endif
