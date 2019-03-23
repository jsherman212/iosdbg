#include <limits.h>
#include <stdlib.h>

#include "stack.h"

#define STACK_ERR (void *)LONG_MIN

struct stack_t *stack_new(void){
    struct stack_t *stack = malloc(sizeof(struct stack_t));

    stack->data = NULL;
    stack->top = -1;

    return stack;
}

void stack_push(struct stack_t *stack, void *data){
    if(!stack)
        return;

    stack->data = realloc(stack->data, sizeof(void *) * (++stack->top + 1));
    stack->data[stack->top] = data;
}

void *stack_pop(struct stack_t *stack){
    if(stack_empty(stack))
        return STACK_ERR;

    return stack->data[stack->top--];
}

void *stack_peek(struct stack_t *stack){
    if(stack_empty(stack))
        return STACK_ERR;
    
    return stack->data[stack->top];
}

int stack_empty(struct stack_t *stack){
    if(!stack)
        return 1;

    if(!stack->data)
        return 1;

    return stack->top == -1;
}

void stack_free(struct stack_t *stack){
    if(!stack)
        return;

    if(stack->data)
        free(stack->data);

    stack->data = NULL;

    free(stack);

    stack = NULL;
}
