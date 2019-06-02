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
        concat(error, "no breakpoints");
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

    int len = 0;
    char **ids = token_array(cur_id, " ", &len);

    for(int i=0; i<len; i++){
        char *e = NULL;
        int id = (int)strtol_err(ids[i], &e);

        if(e){
            free(e);
            e = NULL;
        }

        breakpoint_delete(id, &e);

        if(e){
            printf("%s\n", e);
            free(e);
        }
        else{
            printf("Breakpoint %d deleted\n", id);
        }
    }

    token_array_free(ids, len);
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_breakpoint_list(struct cmd_args_t *args,
        int arg1, char **error){
    if(debuggee->num_breakpoints == 0){
        concat(error, "no breakpoints");
        return CMD_FAILURE;
    }

    printf("Current breakpoints:\n");

    for(struct node_t *current = debuggee->breakpoints->front;
            current;
            current = current->next){
        struct breakpoint *b = current->data;

        printf("%4s%d: address = %-16.16lx, hit count = %d, hardware = %d\n",
                "", b->id, b->location, b->hit_count, b->hw);
    }
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_breakpoint_set(struct cmd_args_t *args, 
        int arg1, char **error){
    char *location_str = argnext(args);
    long location = eval_expr(location_str, error);

    free(location_str);

    if(*error)
        return CMD_FAILURE;

    breakpoint_at_address(location, BP_NO_TEMP, BP_ALL_THREADS, error);    

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}
