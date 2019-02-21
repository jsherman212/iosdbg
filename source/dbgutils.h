#ifndef _DBGUTILS_H_
#define _DBGUTILS_H_

#include <unistd.h>

pid_t pid_of_program(char *, char **);
char *progname_from_pid(pid_t);
void setup_servers(void);
void setup_initial_debuggee(void);

#endif
