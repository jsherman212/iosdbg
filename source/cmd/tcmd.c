#include <stdio.h>

#include "argparse.h"

#include "../debuggee.h"
#include "../linkedlist.h"
#include "../strext.h"
#include "../thread.h"

enum cmd_error_t cmdfunc_threadlist(struct cmd_args_t *args, 
        int arg1, char **error){
    struct node_t *current = debuggee->threads->front;

    while(current){
        struct machthread *t = current->data;

        printf("\t%sthread #%d, tid = %#llx, name = '%s', where = %#llx\n", 
                t->focused ? "* " : "", t->ID, t->tid, t->tname, 
                t->thread_state.__pc);
        
        current = current->next;
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_threadselect(struct cmd_args_t *args, 
        int arg1, char **error){
    /* Current argument: the ID of the thread the user wants to focus on. */
    char *curarg = argnext(args);
    int thread_id = (int)strtol_err(curarg, error);

    if(*error)
        return CMD_FAILURE;

    if(thread_id < 1 || thread_id > debuggee->thread_count){
        asprintf(error, "out of bounds, must be in [1, %d]", 
                debuggee->thread_count);
        return CMD_FAILURE;
    }

    int result = machthread_setfocusgivenindex(thread_id);
    
    if(result){
        asprintf(error, "could not set focused thread to thread %d", thread_id);
        return CMD_FAILURE;
    }

    printf("Selected thread #%d\n", thread_id);
    
    return CMD_SUCCESS;
}
