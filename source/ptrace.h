#ifndef _PTRACE_H_
#define _PTRACE_H_

#include <sys/types.h>

extern int ptrace(int arg0, pid_t arg1, caddr_t arg2, int arg3);

#define PT_DETACH   11
#define PT_SIGEXC   12
#define PT_ATTACHEXC    14
#define PT_THUPDATE 13

#endif
