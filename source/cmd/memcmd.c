#include <stdio.h>
#include <stdlib.h>

#include "argparse.h"

#include "../debuggee.h"
#include "../expr.h"
#include "../memutils.h"
#include "../strext.h"

enum cmd_error_t cmdfunc_disassemble(struct cmd_args_t *args, 
        int arg1, char **error){
    char *location_str = argnext(args);
    long location = parse_expr(location_str, error);

    if(*error)
        return CMD_FAILURE;

    char *amount_str = argnext(args);
    int amount = (int)strtol_err(amount_str, error);

    if(*error)
        return CMD_FAILURE;

    if(amount <= 0){
        asprintf(error, "bad amount %d", amount);
        return CMD_FAILURE;
    }

    if(args->add_aslr)
        location += debuggee->aslr_slide;

    kern_return_t err = disassemble_at_location(location, amount);

    if(err){
        asprintf(error, "could not disassemble from %#lx to %#lx: %s", 
                location, location + amount, mach_error_string(err));
        return CMD_FAILURE;
    }
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_examine(struct cmd_args_t *args, 
        int arg1, char **error){
    char *location_str = argnext(args);
    long location = parse_expr(location_str, error);

    if(*error)
        return CMD_FAILURE;

    /* Next, however many bytes are wanted. */
    char *size = argnext(args);
    int amount = (int)strtol_err(size, error);

    if(*error)
        return CMD_FAILURE;
    
    if(amount < 0){
        asprintf(error, "negative amount");
        return CMD_FAILURE;
    }

    if(args->add_aslr)
        location += debuggee->aslr_slide;

    kern_return_t err = dump_memory(location, amount);

    if(err){
        asprintf(error, "could not dump memory from %#lx to %#lx: %s", 
                location, location + amount, mach_error_string(err));
        return CMD_FAILURE;
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_memoryfind(struct cmd_args_t *args,
        int arg1, char **error){
    printf("cmdfunc memory find\n");

    return CMD_SUCCESS;
}
