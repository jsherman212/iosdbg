#ifndef _ARGPARSE_H_
#define _ARGPARSE_H_

#include "../queue.h"

#define MAX_GROUPS 4

enum cmd_error_t {
    CMD_SUCCESS,
    CMD_FAILURE
};

struct cmd_args_t {
    struct queue_t *argqueue;
    int num_args;
    int add_aslr;
};

struct cmd_args_t *parse_args(char *, const char *, const char **, int, int, char **);
char *argnext(struct cmd_args_t *);
void argfree(struct cmd_args_t *);

#endif
