#ifndef _QUEUE_H_
#define _QUEUE_H_

struct queue_t {
    void **data;
    int capacity;
};

struct queue_t *queue_new(void);
void enqueue(struct queue_t *, void *);
void *dequeue(struct queue_t *);
void *queue_peek(struct queue_t *);
void queue_free(struct queue_t *);

#endif
