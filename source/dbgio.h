#ifndef _DBGIO_H_
#define _DBGIO_H_

#include <pthread/pthread.h>

extern int IOSDBG_IO_PIPE[2];
extern pthread_mutex_t IO_PIPE_LOCK;

int io_append(const char *, ...);
int io_flush(void);
int initialize_iosdbg_io(void);

#endif
