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
    queue_t *argvals;
    int argvalcnt;
};

struct cmd_args {
    int num_args;
    struct linkedlist *argmaps;
};

struct cmd_args *parse_and_create_args(char *, const char *, const char **,
        int, int, char **);
struct cmd_args *argdup(struct cmd_args *);
char *argcopy(struct cmd_args *, const char *);
void argins(struct cmd_args *, const char *, char *);
void argfree(struct cmd_args *);

#endif
