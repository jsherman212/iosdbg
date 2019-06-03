#ifndef _ARGPARSE_H_
#define _ARGPARSE_H_

#include "../linkedlist.h"
#include "../queue.h"

#define MAX_GROUPS (4)

enum cmd_error_t {
    CMD_SUCCESS,
    CMD_FAILURE,
    CMD_QUIT
};

struct argmap {
    char *arggroup;
    struct queue_t *argvals;
};

struct cmd_args_t {
    struct queue_t *argqueue;
    int num_args;

    struct linkedlist *argmaps;
};

struct cmd_args_t *parse_and_create_args(char *, const char *, const char **,
        int, int, char **);
char *argcopy(struct cmd_args_t *, const char *);
// XXX move this fxn up in the source file and delete this prototype
void argins(struct cmd_args_t *, const char *, char *);

char *argnext(struct cmd_args_t *);
void argfree(struct cmd_args_t *);

#endif
