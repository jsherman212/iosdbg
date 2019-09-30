#ifndef _STACK_H_
#define _STACK_H_

struct stack {
    void **data;
    int top;
};

typedef struct stack _stack_t;

_stack_t *stack_new(void);
void stack_push(_stack_t *, void *);
void *stack_pop(_stack_t *);
void *stack_peek(_stack_t *);
int stack_empty(_stack_t *);
void stack_free(_stack_t *);

#endif
