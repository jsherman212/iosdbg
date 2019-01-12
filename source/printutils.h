#ifndef _PRINTUTILS_H_
#define _PRINTUTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <readline/readline.h>
#include <readline/history.h>

#define RL_REPROMPT (int)1
#define RL_NO_REPROMPT (int)0

int rl_printf(int, const char *, ...);
void safe_reprompt(void);

#endif
