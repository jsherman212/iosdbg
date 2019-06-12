#include <stdio.h>
#include <stdlib.h>

#include "tcmd.h"

#include "../debuggee.h"
#include "../linkedlist.h"
#include "../printing.h"
#include "../strext.h"
#include "../thread.h"

enum cmd_error_t cmdfunc_thread_list(struct cmd_args_t *args, 
        int arg1, char **error){
    for(struct node_t *current = debuggee->threads->front;
            current;
            current = current->next){
        struct machthread *t = current->data;

        WriteMessageBuffer("\t%sthread #%d, tid = %#llx, name = '%s', where = %#llx\n", 
                t->focused ? "* " : "", t->ID, t->tid, t->tname, 
                t->thread_state.__pc);
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_thread_select(struct cmd_args_t *args, 
        int arg1, char **error){
    char *thread_id_str = argcopy(args, THREAD_SELECT_COMMAND_REGEX_GROUPS[0]);
    int thread_id = (int)strtol_err(thread_id_str, error);

    free(thread_id_str);

    if(*error)
        return CMD_FAILURE;

    if(thread_id < 1 || thread_id > debuggee->thread_count){
        concat(error, "out of bounds, must be in [1, %d]", 
                debuggee->thread_count);
        return CMD_FAILURE;
    }

    int result = machthread_setfocusgivenindex(thread_id);
    
    if(result){
        concat(error, "could not set focused thread to thread %d", thread_id);
        return CMD_FAILURE;
    }

    WriteMessageBuffer("Selected thread #%d\n", thread_id);
    
    return CMD_SUCCESS;
}
