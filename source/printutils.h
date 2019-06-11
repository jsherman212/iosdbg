#ifndef _PRINTUTILS_H_
#define _PRINTUTILS_H_

#include <pthread/pthread.h>

/*
extern pthread_mutex_t MESSAGE_BUF_MUTEX;
extern pthread_mutex_t EXCEPTION_BUF_MUTEX;

extern char *MESSAGE_BUFFER;
extern char *EXCEPTION_BUFFER;

extern int FLUSH_MESSAGE_BUFFER,
       FLUSH_EXCEPTION_BUFFER;
*/
extern char *MESSAGE_BUFFER;
extern char *EXCEPTION_BUFFER;
extern char *ERROR_BUFFER;
void *message_buffer_thread(void *);
void *exception_buffer_thread(void *);
void *error_buffer_thread(void *);
void WriteExceptionBuffer(const char *, ...);
void WriteMessageBuffer(const char *, ...);
void WriteErrorBuffer(const char *, ...);
void PrintExceptionBuffer(void);
void PrintMessageBuffer(void);
void PrintErrorBuffer(void);
void safe_reprompt(void);

#endif
