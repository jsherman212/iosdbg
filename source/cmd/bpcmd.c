#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>

#include "argparse.h"

#include "../breakpoint.h"
#include "../debuggee.h"
#include "../expr.h"
#include "../interaction.h"
#include "../linkedlist.h"
#include "../strext.h"

enum cmd_error_t cmdfunc_breakpoint_delete(struct cmd_args_t *args,
        int arg1, char **error){
    if(debuggee->num_breakpoints == 0){
        asprintf(error, "no breakpoints");
        return CMD_FAILURE;
    }

    char *cur_id = argnext(args);

    if(!cur_id){
        char ans = answer("Delete all breakpoints? (y/n) ");

        if(ans == 'n'){
            printf("Nothing deleted.\n");
            return CMD_SUCCESS;
        }

        int num_deleted = debuggee->num_breakpoints;

        breakpoint_delete_all();

        printf("All breakpoint(s) removed. (%d breakpoint(s))\n", num_deleted);

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

        breakpoint_delete(id, error);

        if(*error){
            printf("%s\n", *error);
            free(*error);
            *error = NULL;
        }
        else
            printf("Breakpoint %d deleted\n", id);

        cur_id = argnext(args);
    }
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_breakpoint_list(struct cmd_args_t *args,
        int arg1, char **error){
    if(debuggee->num_breakpoints == 0){
        asprintf(error, "no breakpoints");
        return CMD_FAILURE;
    }

    printf("Current breakpoints:\n");

    int count = 1;

    for(struct node_t *current = debuggee->breakpoints->front;
            current;
            current = current->next){
        struct breakpoint *b = current->data;

        printf("%4s%d<id: %d>: address = %-16.16lx, hit count = %d\n",
                "", b->id, count++, b->location, b->hit_count);
    }
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_breakpoint_set(struct cmd_args_t *args, 
        int arg1, char **error){
    char *location_str = argnext(args);
    long location = eval_expr(location_str, error);

    if(*error){
        asprintf(error, "expression evaluation failed: %s", *error);
        return CMD_FAILURE;
    }

    breakpoint_at_address(location, BP_NO_TEMP, error);    

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}
