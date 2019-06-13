#ifndef _PRINTING_H_
#define _PRINTING_H_

#include <pthread/pthread.h>

extern char *ERROR_BUFFER, *EXCEPTION_BUFFER, *MESSAGE_BUFFER;

void *error_buffer_thread(void *);
void *exception_buffer_thread(void *);
void *message_buffer_thread(void *);

void WriteErrorBuffer(const char *, ...);
void WriteExceptionBuffer(const char *, ...);
void WriteMessageBuffer(const char *, ...);

void PrintErrorBuffer(void);
void PrintExceptionBuffer(void);
void PrintMessageBuffer(void);

void ClearErrorBuffer(void);
void ClearExceptionBuffer(void);
void ClearMessageBuffer(void);

#endif
