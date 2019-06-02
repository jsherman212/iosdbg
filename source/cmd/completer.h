#ifndef _COMPLETER_H_
#define _COMPLETER_H_

#define MAX_GROUPS 4
#define RAND_PAD_LEN ((size_t)10)

extern int IS_HELP_COMMAND;
extern int LINE_MODIFIED;

enum cmd_error_t prepare_and_call_cmdfunc(char *, char **);
char **completer(const char *, int, int);

#endif
