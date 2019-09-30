#include <stdlib.h>

#include "linkedlist.h"

struct linkedlist *linkedlist_new(void){
    struct linkedlist *list = malloc(sizeof(struct linkedlist));
    list->front = NULL;

    return list;
}

void linkedlist_add_front(struct linkedlist *list, void *data){
    if(!list->front){
        list->front = malloc(sizeof(struct node));
        list->front->data = data;
        list->front->next = NULL;

        return;
    }

    struct node *old_front = list->front;

    struct node *new_front = malloc(sizeof(struct node));
    new_front->data = data;
    new_front->next = old_front;

    list->front = new_front;
}

void linkedlist_add(struct linkedlist *list, void *data_to_add){
    if(!list->front){
        linkedlist_add_front(list, data_to_add);
        return;
    }

    if(!data_to_add)
        return;

    struct node *current = list->front;

    while(current->next)
        current = current->next;

    struct node *add = malloc(sizeof(struct node));
    add->data = data_to_add;
    add->next = NULL;

    current->next = add;
}

int linkedlist_contains(struct linkedlist *list, void *data){
    if(!list->front)
        return 0;

    struct node *current = list->front;

    while(current->next){
        if(current->data == data)
            return 1;

        current = current->next;
    }

    return 0;
}

void linkedlist_delete(struct linkedlist *list, void *data_to_remove){
    if(!list->front)
        return;

    if(!data_to_remove)
        return;

    if(list->front->data == data_to_remove){
        list->front = list->front->next;
        return;
    }

    struct node *current = list->front;
    struct node *previous = NULL;

    while(current->next){
        previous = current;
        current = current->next;

        if(current->data == data_to_remove){
            /* We're at the node we want to remove,
             * modify connections to skip this one.
             */
            previous->next = current->next;
            current = NULL;
            return;
        }
    }
}

void linkedlist_free(struct linkedlist *list){
    free(list->front);
    list->front = NULL;

    free(list);
    list = NULL;
}

