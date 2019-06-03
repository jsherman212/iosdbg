#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>

#include "argparse.h"
#include "bpcmd.h"

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
    char *thread_str = argcopy(args, BREAKPOINT_SET_COMMAND_REGEX_GROUPS[0]);

    int thread = BP_ALL_THREADS;

    if(thread_str){
        thread = (int)strtol(thread_str, NULL, 10);

        if(!(thread >= 1 && thread <= debuggee->thread_count)){
            concat(error, "bad thread number %d", thread);
            free(thread_str);
            return CMD_FAILURE;
        }

        if(!debuggee->interrupted){
            int len = (int)strlen("warning: ");

            printf("warning: debuggee is not stopped"
                    ", thread IDs could have changed.\n"
                    "%*sIt is suggested to interrupt the debuggee,"
                    " remind yourself of the list of threads,\n"
                    "%*sand set the breakpoint again.\n",
                    len, "", len, "");
        }
    }

    free(thread_str);

    char *location_str = argcopy(args, BREAKPOINT_SET_COMMAND_REGEX_GROUPS[1]);

    while(location_str){
        char *e = NULL;
        long location = eval_expr(location_str, &e);

        if(e){
            printf("warning: could not set breakpoint: %s\n", e);
            free(e);
            e = NULL;
        }
        else{
            breakpoint_at_address(location, BP_NO_TEMP, thread, &e);

            if(e){
                printf("warning: could not set breakpoint: %s\n", e);
                free(e);
            }
        }

        free(location_str);
        location_str = argcopy(args, BREAKPOINT_SET_COMMAND_REGEX_GROUPS[1]);
    }

    return CMD_SUCCESS;
}
