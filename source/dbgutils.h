#ifndef _DBGUTILS_H_
#define _DBGUTILS_H_

#include <mach/message.h>
#include <spawn.h>
#include <sys/sysctl.h>
#include "defs.h"
#include "dbgcmd.h"
#include "machthread.h"

pid_t pid_of_program(char *, char **);
char *progname_from_pid(pid_t);
void setup_servers(void);
void setup_initial_debuggee(void);

#endif
