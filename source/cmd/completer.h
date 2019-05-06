#ifndef _COMPLETER_H_
#define _COMPLETER_H_

#define MAX_GROUPS 4

extern int IS_HELP_COMMAND;

enum cmd_error_t prepare_and_call_cmdfunc(char *, char **);
char **completer(const char *, int, int);

#endif
