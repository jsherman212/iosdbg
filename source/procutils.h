#ifndef _PROCUTILS_H_
#define _PROCUTILS_H_

#include <unistd.h>

pid_t pid_of_program(char *, char **);
char *progname_from_pid(pid_t, char **);

#endif
