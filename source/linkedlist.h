#ifndef _LINKEDLIST_H_
#define _LINKEDLIST_H_

struct node {
    void *data;
    struct node *next;
};

struct linkedlist {
    struct node *front;
};

struct linkedlist *linkedlist_new(void);
void linkedlist_add_front(struct linkedlist *, void *);
void linkedlist_add(struct linkedlist *, void *);
int linkedlist_contains(struct linkedlist *, void *);
void linkedlist_delete(struct linkedlist *, void *);
void linkedlist_free(struct linkedlist *);

#endif
