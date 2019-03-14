#ifndef _ARGPARSE_H_
#define _ARGPARSE_H_

#include "queue.h"

struct arguments_t {
	struct queue_t *argqueue;
	int num_args;
	int add_aslr;
};

struct arguments_t *parse_args(char *, char **);
char *argnext(struct arguments_t *);
void argfree(struct arguments_t *);

#endif
