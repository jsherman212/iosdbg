#ifndef _DBGIO_H_
#define _DBGIO_H_

extern int IOSDBG_IO_PIPE[2];

int io_append(const char *, ...);
int io_flush(void);
int initialize_iosdbg_io(void);

#endif
