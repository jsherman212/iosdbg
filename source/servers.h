#ifndef _SERVERS_H_
#define _SERVERS_H_

#include <pthread/pthread.h>

extern pthread_t exception_server_thread;
extern pthread_t death_server_thread;

void setup_servers(void);

#endif
