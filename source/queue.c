#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "queue.h"

struct queue_t *queue_new(void){
    struct queue_t *queue = malloc(sizeof(struct queue_t));

    queue->data = NULL;
    queue->capacity = -1;

    return queue;
}

void enqueue(struct queue_t *queue, void *data){
    if(!queue)
        return;

    queue->data = realloc(queue->data, sizeof(void *)
            * (++queue->capacity + 1));
    queue->data[queue->capacity] = data;
}

void *dequeue(struct queue_t *queue){
    if(!queue)
        return NULL;

    if(!queue->data)
        return NULL;

    if(queue->capacity == -1)
        return NULL;

    //void *ret = queue->data[0];
    void *ret = *queue->data;
    
    /* Move everything back one. */
    memmove(queue->data, queue->data + 1, queue->capacity-- * sizeof(void *));

    return ret;
}

void *queue_peek(struct queue_t *queue){
    if(!queue)
        return NULL;

    if(!queue->data)
        return NULL;

    if(queue->capacity == -1)
        return NULL;

    return queue->data[0];
}

void queue_free(struct queue_t *queue){
    if(!queue)
        return;

    if(queue->data)
        free(queue->data);

    queue->data = NULL;

    free(queue);

    queue = NULL;
}
