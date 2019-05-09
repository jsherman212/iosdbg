#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argparse.h"

#include "../debuggee.h"
#include "../expr.h"
#include "../interaction.h"
#include "../linkedlist.h"
#include "../strext.h"
#include "../watchpoint.h"

enum cmd_error_t cmdfunc_watchpoint_delete(struct cmd_args_t *args,
        int arg1, char **error){
    if(debuggee->num_watchpoints == 0){
        asprintf(error, "no watchpoints");
        return CMD_FAILURE;
    }

    char *cur_id = argnext(args);

    if(!cur_id){
        char ans = answer("Delete all watchpoints? (y/n) ");

        if(ans == 'n'){
            printf("Nothing deleted.\n");
            return CMD_SUCCESS;
        }

        int num_deleted = debuggee->num_watchpoints;

        watchpoint_delete_all();

        printf("All watchpoint(s) removed. (%d watchpoint(s))\n", num_deleted);

        return CMD_SUCCESS;
    }

    while(cur_id){
        int id = (int)strtol_err(cur_id, error);

        if(*error){
            printf("%s\n", *error);
            free(*error);
            *error = NULL;
            cur_id = argnext(args);
            continue;
        }

        watchpoint_delete(id, error);

        if(*error){
            printf("%s\n", *error);
            free(*error);
            *error = NULL;
        }
        else
            printf("Watchpoint %d deleted\n", id);

        cur_id = argnext(args);
    }
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_watchpoint_list(struct cmd_args_t *args,
        int arg1, char **error){
    if(debuggee->num_watchpoints == 0){
        asprintf(error, "no watchpoints");
        return CMD_FAILURE;
    }

    printf("Current watchpoints:\n");

    int count = 1;

    for(struct node_t *current = debuggee->watchpoints->front;
            current;
            current = current->next){
        struct watchpoint *w = current->data;

        printf("%4s%d<id: %d>: address = %-16.16lx, hit count = %d, size = %d",
                "", count++, w->id, w->location, w->hit_count, w->data_len);

        const char *type = "r";

        if(w->LSC == WP_WRITE)
            type = "w";
        else if(w->LSC == WP_READ_WRITE)
            type = "rw";

        printf(", type = %s\n", type);
    }
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_watchpoint_set(struct cmd_args_t *args, 
        int arg1, char **error){
    /* Current argument: watchpoint type or location. */
    char *curarg = argnext(args);
    int LSC = WP_WRITE;

    /* Check if the user specified a watchpoint type. If they didn't,
     * this watchpoint will match on reads and writes.
     */
    if(!is_number_slow(curarg)){
        if(strcmp(curarg, "--r") == 0)
            LSC = WP_READ;
        else if(strcmp(curarg, "--w") == 0)
            LSC = WP_WRITE;
        else if(strcmp(curarg, "--rw") == 0)
            LSC = WP_READ_WRITE;

        /* If we had a type before the location, we need to get the next
         * argument. After that, current argument is the location to watch.
         */
        curarg = argnext(args);
    }

    long location = eval_expr(curarg, error);

    if(*error){
        asprintf(error, "expression evaluation failed: %s\n", *error);
        return CMD_FAILURE;
    }

    /* Current argument: size of data we're watching. */
    curarg = argnext(args);
    int data_len = (int)strtol_err(curarg, error);

    if(*error)
        return CMD_FAILURE;

    watchpoint_at_address(location, data_len, LSC, error);

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}
