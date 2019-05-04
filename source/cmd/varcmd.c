#include <stdio.h>
#include <stdlib.h>

#include "argparse.h"

#include "../convvar.h"

enum cmd_error_t cmdfunc_variable_print(struct cmd_args_t *args,
        int arg1, char **error){
    /* If there were no arguments, print everything. */
    if(args->num_args == 0){
        show_all_cvars();
        return CMD_SUCCESS;
    }

    char *cur_convvar = argnext(args);

    while(cur_convvar){
        p_convvar(cur_convvar);
        cur_convvar = argnext(args);
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_variable_set(struct cmd_args_t *args,
        int arg1, char **error){
    char *var = argnext(args);
    char *value = argnext(args);

    set_convvar(var, value, error);

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_variable_unset(struct cmd_args_t *args,
        int arg1, char **error){
    char *var = argnext(args);

    void_convvar(var);

    return CMD_SUCCESS;
}
