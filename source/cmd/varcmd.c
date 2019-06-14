#include <stdio.h>
#include <stdlib.h>

#include "varcmd.h"

#include "../convvar.h"

enum cmd_error_t cmdfunc_variable_print(struct cmd_args_t *args,
        int arg1, char **outbuffer, char **error){
    /* If there were no arguments, print everything. */
    if(args->num_args == 0){
        show_all_cvars(outbuffer);
        return CMD_SUCCESS;
    }

    char *cur_convvar = argcopy(args, VARIABLE_PRINT_COMMAND_REGEX_GROUPS[0]);

    while(cur_convvar){
        p_convvar(cur_convvar, outbuffer);
        free(cur_convvar);
        cur_convvar = argcopy(args, VARIABLE_PRINT_COMMAND_REGEX_GROUPS[0]);
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_variable_set(struct cmd_args_t *args,
        int arg1, char **outbuffer, char **error){
    char *var = argcopy(args, VARIABLE_SET_COMMAND_REGEX_GROUPS[0]);
    char *value = argcopy(args, VARIABLE_SET_COMMAND_REGEX_GROUPS[1]);

    set_convvar(var, value, error);

    free(var);
    free(value);

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_variable_unset(struct cmd_args_t *args,
        int arg1, char **outbuffer, char **error){
    char *var = argcopy(args, VARIABLE_UNSET_COMMAND_REGEX_GROUPS[0]);

    void_convvar(var);

    free(var);

    return CMD_SUCCESS;
}
