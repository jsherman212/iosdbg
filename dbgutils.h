#ifndef _DBGUTILS_H_
#define _DBGUTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <mach/mach.h>
#include "defs.h"
#include "dbgcmd.h"

pid_t pid_of_program(char *);
int suspend_threads(void);
void resume_threads(void);
void *death_server(void *);
const char *get_exception_code(exception_type_t);
void setup_exception_handling(void);
void setup_initial_debuggee(void);
void interrupt(int);

#endif
