#include <stdio.h>
#include <string.h>

#include "argparse.h"

#include "../expr.h"
#include "../strext.h"
#include "../watchpoint.h"

enum cmd_error_t cmdfunc_watch(struct cmd_args_t *args, 
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

    long location = parse_expr(curarg, error);

    if(*error){
        asprintf(error, "expression evaluation failed: %s\n", *error);
        return CMD_FAILURE;
    }

    /* Current argument: size of data we're watching. */
    curarg = argnext(args);
    int data_len = (int)strtol_err(curarg, error);

    if(*error)
        return CMD_FAILURE;

    return watchpoint_at_address(location, data_len, LSC, error);
}

// XXX TODO 'watchpoint list'
