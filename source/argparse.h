#ifndef _ARGPARSE_H_
#define _ARGPARSE_H_

#include "queue.h"

struct cmd_args_t {
    struct queue_t *argqueue;
    int num_args;
    int add_aslr;
};

struct cmd_args_t *parse_args(char *_args,
        const char *pattern,
        const char **groupnames,
        int num_groups,
        int unk_amount_of_args,
        char **error);
char *argnext(struct cmd_args_t *);
void argfree(struct cmd_args_t *);
void desc(struct cmd_args_t *);

#endif
