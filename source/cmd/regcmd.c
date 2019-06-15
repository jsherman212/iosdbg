#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "regcmd.h"

#include "../debuggee.h"
#include "../printing.h"
#include "../reg.h"
#include "../strext.h"
#include "../thread.h"

enum cmd_error_t cmdfunc_register_float(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    struct machthread *focused = machthread_getfocused();

    char *curreg = argcopy(args, REGISTER_FLOAT_COMMAND_REGEX_GROUPS[0]);

    while(curreg){
        char *cleanedreg = NULL, *curregval = NULL, *e = NULL;
        enum regtype curregtype = NONE;

        long val = regtol(focused, HEXADECIMAL, &curregtype,
                curreg, &cleanedreg, &curregval, &e);

        if(e){
            concat(outbuffer, "%10s %s\n", "error:", e);
            free(e);
        }

        if(curregtype == FLOAT)
            concat(outbuffer, "%8s = %g\n", cleanedreg, *(float *)&val);
        else if(curregtype == DOUBLE)
            concat(outbuffer, "%8s = %.15g\n", cleanedreg, *(double *)&val);
        else if(curregtype == QUADWORD)
            concat(outbuffer, "%8s = %s\n", cleanedreg, curregval);

        if(cleanedreg)
            free(cleanedreg);
        if(curregval)
            free(curregval);

        free(curreg);
        curreg = argcopy(args, REGISTER_FLOAT_COMMAND_REGEX_GROUPS[0]);
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_register_gen(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    struct machthread *focused = machthread_getfocused();

    /* If there were no arguments, print every register. */
    if(args->num_args == 0){
        get_thread_state(focused);

        for(int i=0; i<29; i++){        
            char *regstr = NULL;
            concat(&regstr, "x%d", i);

            concat(outbuffer, "%10s = 0x%16.16llx\n", regstr, 
                    focused->thread_state.__x[i]);

            free(regstr);
        }
        
        concat(outbuffer, "%10s = 0x%16.16llx\n", "fp", focused->thread_state.__fp);
        concat(outbuffer, "%10s = 0x%16.16llx\n", "lr", focused->thread_state.__lr);
        concat(outbuffer, "%10s = 0x%16.16llx\n", "sp", focused->thread_state.__sp);
        concat(outbuffer, "%10s = 0x%16.16llx\n", "pc", focused->thread_state.__pc);
        concat(outbuffer, "%10s = 0x%8.8x\n", "cpsr", focused->thread_state.__cpsr);

        return CMD_SUCCESS;
    }

    /* Otherwise, print the registers they asked for. */
    char *curreg = argcopy(args, REGISTER_GEN_COMMAND_REGEX_GROUPS[0]);

    while(curreg){
        char *cleanedreg = NULL, *e = NULL;
        enum regtype curregtype = NONE;
        long val = regtol(focused, HEXADECIMAL, &curregtype,
                curreg, &cleanedreg, NULL, &e);

        if(e){
            concat(outbuffer, "%10s %s\n", "error:", e);
            free(e);
        }

        if(curregtype == LONG)
            concat(outbuffer, "%8s = 0x%16.16lx\n", cleanedreg, val);
        else if(curregtype == INTEGER)
            concat(outbuffer, "%8s = 0x%8.8x\n", cleanedreg, val);

        if(cleanedreg)
            free(cleanedreg);
        
        free(curreg);
        curreg = argcopy(args, REGISTER_GEN_COMMAND_REGEX_GROUPS[0]);
    }
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_register_write(struct cmd_args_t *args,
        int arg1, char **outbuffer, char **error){
    char *target_str = argcopy(args, REGISTER_WRITE_COMMAND_REGEX_GROUPS[0]);
    char *value_str = argcopy(args, REGISTER_WRITE_COMMAND_REGEX_GROUPS[1]);

    struct machthread *focused = machthread_getfocused();

    setreg(focused, target_str, value_str, error);

    if(target_str)
        free(target_str);
    if(value_str)
        free(value_str);

    return *error ? CMD_FAILURE : CMD_SUCCESS;
}
