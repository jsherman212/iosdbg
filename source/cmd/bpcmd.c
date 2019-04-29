#include <stdio.h>

#include "argparse.h"

#include "../breakpoint.h"
#include "../debuggee.h"
#include "../expr.h"

enum cmd_error_t cmdfunc_break(struct cmd_args_t *args, 
        int arg1, char **error){
    char *location_str = argnext(args);
    long location = parse_expr(location_str, error);

    if(*error){
        asprintf(error, "expression evaluation failed: %s", *error);
        return CMD_FAILURE;
    }

    if(args->add_aslr)
        location += debuggee->aslr_slide;

    return breakpoint_at_address(location, BP_NO_TEMP, error);    
}

// XXX TODO 'breakpoint list'
