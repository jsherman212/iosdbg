#ifndef _QUEUE_H_
#define _QUEUE_H_

struct queue {
    void **data;
    int capacity;
};

typedef struct queue queue_t;

queue_t *queue_new(void);
void enqueue(queue_t *, void *);
void *dequeue(queue_t *);
void *queue_peek(queue_t *);
void queue_free(queue_t *);

#endif
