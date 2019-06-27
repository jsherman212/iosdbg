#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wpcmd.h"

#include "../debuggee.h"
#include "../expr.h"
#include "../interaction.h"
#include "../linkedlist.h"
#include "../strext.h"
#include "../watchpoint.h"

enum cmd_error_t cmdfunc_watchpoint_delete(struct cmd_args_t *args,
        int arg1, char **outbuffer, char **error){
    if(debuggee->num_watchpoints == 0){
        concat(error, "no watchpoints");
        return CMD_FAILURE;
    }

    char *ids = argcopy(args, WATCHPOINT_DELETE_COMMAND_REGEX_GROUPS[0]);

    if(!ids){
        char ans = answer("Delete all watchpoints? (y/n) ");

        if(ans == 'n'){
            concat(outbuffer, "Nothing deleted.\n");
            return CMD_SUCCESS;
        }

        int num_deleted = debuggee->num_watchpoints;

        watchpoint_delete_all();

        concat(outbuffer, "All watchpoint(s) removed. (%d watchpoint(s))\n", num_deleted);

        return CMD_SUCCESS;
    }

    int len = 0;
    char **all_ids = token_array(ids, " ", &len);

    free(ids);

    for(int i=0; i<len; i++){
        char *e = NULL;
        int id = (int)strtol_err(all_ids[i], &e);

        free(e);
        e = NULL;

        watchpoint_delete(id, &e);

        if(e)
            concat(outbuffer, "%s\n", e);
        else
            concat(outbuffer, "Watchpoint %d deleted\n", id);

        free(e);
    }

    token_array_free(all_ids, len);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_watchpoint_list(struct cmd_args_t *args,
        int arg1, char **outbuffer, char **error){
    if(debuggee->num_watchpoints == 0){
        concat(error, "no watchpoints");
        return CMD_FAILURE;
    }

    concat(outbuffer, "Current watchpoints:\n");

    WP_LOCKED_FOREACH(current){
        struct watchpoint *w = current->data;

        concat(outbuffer, "%4s%d: address = %-16.16lx, hit count = %d,"
                " size = %d, type = %s\n",
                "", w->id, w->user_location, w->hit_count, w->data_len, w->type);

        if(!(w->threadinfo.all)){
            concat(outbuffer, "%8sfor thread %d (tid: %#llx), '%s'\n",
                    "", w->threadinfo.iosdbg_tid, w->threadinfo.pthread_tid,
                    w->threadinfo.tname);
        }
    }
    WP_END_LOCKED_FOREACH;
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_watchpoint_set(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    char *thread_str = argcopy(args, WATCHPOINT_SET_COMMAND_REGEX_GROUPS[0]);
    int thread = WP_ALL_THREADS;

    if(thread_str){
        thread = (int)strtol(thread_str, NULL, 10);

        if(!(thread >= 1 && thread <= debuggee->thread_count)){
            concat(error, "bad thread number %d", thread);
            free(thread_str);
            return CMD_FAILURE;
        }

        if(!debuggee->suspended()){
            int len = (int)strlen("warning: ");

            concat(outbuffer, "warning: debuggee is not stopped"
                    ", thread IDs could have changed.\n"
                    "%*sIt is suggested to interrupt the debuggee,"
                    " remind yourself of the list of threads,\n"
                    "%*sand set the watchpoint again.\n",
                    len, "", len, "");
        }
    }

    char *type_str = argcopy(args, WATCHPOINT_SET_COMMAND_REGEX_GROUPS[1]);
    int LSC;

    if(!type_str)
        LSC = WP_WRITE;
    else{
        if(strcmp(type_str, "r") == 0)
            LSC = WP_READ;
        else if(strcmp(type_str, "w") == 0)
            LSC = WP_WRITE;
        else
            LSC = WP_READ_WRITE;

        free(type_str);
    }

    char *location_str = argcopy(args, WATCHPOINT_SET_COMMAND_REGEX_GROUPS[2]);
    long location = eval_expr(location_str, error);

    free(location_str);

    if(*error)
        return CMD_FAILURE;

    char *data_len_str = argcopy(args, WATCHPOINT_SET_COMMAND_REGEX_GROUPS[3]);
    int data_len = (int)strtol_err(data_len_str, error);

    free(data_len_str);

    if(*error)
        return CMD_FAILURE;

    watchpoint_at_address(location, data_len, LSC,
            thread, outbuffer, error);

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}
