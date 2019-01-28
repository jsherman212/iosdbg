#ifndef _STACK_H_
#define _STACK_H_

#include <stdlib.h>
#include <limits.h>

struct stack_t {
	void **data;
	int top;
};

struct stack_t *stack_new(void);
void stack_push(struct stack_t *, void *);
void *stack_pop(struct stack_t *);
void *stack_peek(struct stack_t *);
int stack_empty(struct stack_t *);
void stack_free(struct stack_t *);

#define STACK_ERR (void *)LONG_MIN

#endif
