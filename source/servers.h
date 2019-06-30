#ifndef _SERVERS_H_
#define _SERVERS_H_

#include <pthread.h>

#include "queue.h"

extern pthread_mutex_t EXCEPTION_QUEUE_MUTEX;

#define EXC_QUEUE_LOCK pthread_mutex_lock(&EXCEPTION_QUEUE_MUTEX)
#define EXC_QUEUE_UNLOCK pthread_mutex_unlock(&EXCEPTION_QUEUE_MUTEX)

extern struct queue_t *EXCEPTION_QUEUE;
extern int NEED_REPLY;

void setup_servers(char **);

#endif
