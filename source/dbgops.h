#ifndef _DBGOPS_H_
#define _DBGOPS_H_

#include <mach/kmod.h>

void ops_printsiginfo(char **);
void ops_detach(int, char **);
kern_return_t ops_resume(void);
kern_return_t ops_suspend(void);
void ops_threadupdate(char **);

#endif
