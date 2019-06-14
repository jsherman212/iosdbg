#ifndef _DBGOPS_H_
#define _DBGOPS_H_

void ops_printsiginfo(char **);
void ops_detach(int, char **);
void ops_resume(void);
void ops_suspend(void);
void ops_threadupdate(char **);

#endif
