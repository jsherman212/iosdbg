#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#pragma once
struct node_t {
	void *data;
	struct node_t *next;
};

#pragma once
struct linkedlist {
	struct node_t *front;
};

struct linkedlist *linkedlist_new();
void linkedlist_add_front(struct linkedlist *, void *);
void linkedlist_add(struct linkedlist *, void *);
void linkedlist_delete(struct linkedlist *, void *);
void linkedlist_print(struct linkedlist *);
void linkedlist_free(struct linkedlist *);