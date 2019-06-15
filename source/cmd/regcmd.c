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
    /* Iterate through and show all the registers the user asked for. */
    char *curreg = argcopy(args, REGISTER_FLOAT_COMMAND_REGEX_GROUPS[0]);

    while(curreg){
        struct machthread *focused = machthread_getfocused();

        char *e = NULL;
        char *r = fetch_reg(focused, HEXADECIMAL, curreg, &e);
        printf("%s: r '%s' error '%s'\n", __func__, r, e?e:"NULL");

        get_neon_state(focused);

        const size_t curreg_len = strlen(curreg);

        for(int i=0; i<curreg_len; i++)
            curreg[i] = tolower(curreg[i]);

        if(strcmp(curreg, "fpsr") == 0){
            free(curreg);
            curreg = argcopy(args, REGISTER_FLOAT_COMMAND_REGEX_GROUPS[0]);
            concat(outbuffer, "%10s = 0x%8.8x\n", "fpsr", focused->neon_state.__fpsr);
            continue;
        }
        else if(strcmp(curreg, "fpcr") == 0){
            free(curreg);
            curreg = argcopy(args, REGISTER_FLOAT_COMMAND_REGEX_GROUPS[0]);
            concat(outbuffer, "%10s = 0x%8.8x\n", "fpcr", focused->neon_state.__fpcr);
            continue;
        }
        
        char reg_type = curreg[0];
        
        /* Move up a byte for the register number. */
        memmove(curreg, curreg + 1, strlen(curreg));

        int reg_num = (int)strtol_err(curreg, error);

        if(*error){
            free(curreg);
            return CMD_FAILURE;
        }

        char *regstr = NULL;

        int good_reg_num = (reg_num >= 0 && reg_num <= 31);
        int good_reg_type = ((reg_type == 'q' || reg_type == 'v') 
                || reg_type == 'd' || reg_type == 's');

        if(!good_reg_num || !good_reg_type){
            concat(outbuffer, "%8sInvalid register\n", "");
            free(curreg);
            curreg = argcopy(args, REGISTER_FLOAT_COMMAND_REGEX_GROUPS[0]);
            continue;
        }
        /* Quadword */
        else if(reg_type == 'q' || reg_type == 'v'){
            long hi = focused->neon_state.__v[reg_num] >> 64;
            long lo = focused->neon_state.__v[reg_num];

            concat(&regstr, "v%d = {", reg_num);

            for(int i=0; i<sizeof(long); i++)
                concat(&regstr, "0x%02x ", *(uint8_t *)((uint8_t *)(&lo) + i));

            for(int i=0; i<sizeof(long) - 1; i++)
                concat(&regstr, "0x%02x ", *(uint8_t *)((uint8_t *)(&hi) + i));

            concat(&regstr, "0x%02x}",
                    *(uint8_t *)((uint8_t *)(&hi) + (sizeof(long) - 1)));
        }
        /* Doubleword */
        else if(reg_type == 'd'){
            concat(&regstr, "d%d = %.15g", reg_num, 
                    *(double *)&focused->neon_state.__v[reg_num]);
        }
        /* Word */
        else if(reg_type == 's'){
            concat(&regstr, "s%d = %g", reg_num, 
                    *(float *)&focused->neon_state.__v[reg_num]);
        }

        /* Figure out how many bytes the register takes up in the string. */
        char *space = strchr(regstr, ' ');
        int bytes = space - regstr;
        int add = 8 - bytes;
        
        concat(outbuffer, "%*s\n", (int)(strlen(regstr) + add), regstr);
    
        if(regstr)
            free(regstr);    

        free(curreg);
        curreg = argcopy(args, REGISTER_FLOAT_COMMAND_REGEX_GROUPS[0]);
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_register_gen(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    struct machthread *focused = machthread_getfocused();

    get_thread_state(focused);

    /* If there were no arguments, print every register. */
    if(args->num_args == 0){
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

    /* Otherwise, print every register they asked for. */
    char *curreg = argcopy(args, REGISTER_GEN_COMMAND_REGEX_GROUPS[0]);

    while(curreg){
        char *e = NULL;
        char *r = fetch_reg(focused, HEXADECIMAL, curreg, &e);
        printf("%s: r '%s' error '%s'\n", __func__, r, e?e:"NULL");

        const size_t curreg_len = strlen(curreg);

        for(int i=0; i<curreg_len; i++)
            curreg[i] = tolower(curreg[i]);

        char reg_type = curreg[0];

        if(reg_type != 'x' && reg_type != 'w'){
            if(strcmp(curreg, "fp") == 0)
                concat(outbuffer, "%8s = 0x%16.16llx\n", "fp", focused->thread_state.__fp);
            else if(strcmp(curreg, "lr") == 0)
                concat(outbuffer, "%8s = 0x%16.16llx\n", "lr", focused->thread_state.__lr);
            else if(strcmp(curreg, "sp") == 0)
                concat(outbuffer, "%8s = 0x%16.16llx\n", "sp", focused->thread_state.__sp);
            else if(strcmp(curreg, "pc") == 0)
                concat(outbuffer, "%8s = 0x%16.16llx\n", "pc", focused->thread_state.__pc);
            else if(strcmp(curreg, "cpsr") == 0)
                concat(outbuffer, "%8s = 0x%8.8x\n", "cpsr", focused->thread_state.__cpsr);
            else
                concat(outbuffer, "Invalid register\n");

            free(curreg);
            curreg = argcopy(args, REGISTER_GEN_COMMAND_REGEX_GROUPS[0]);

            continue;
        }

        /* Move up one byte to get to the "register number". */
        memmove(curreg, curreg + 1, strlen(curreg));

        int reg_num = (int)strtol_err(curreg, error);

        if(*error){
            free(curreg);
            return CMD_FAILURE;
        }
        
        if(reg_num < 0 || reg_num > 29){
            free(curreg);
            curreg = argcopy(args, REGISTER_GEN_COMMAND_REGEX_GROUPS[0]);
            continue;
        }

        char *regstr = NULL;
        concat(&regstr, "%c%d", reg_type, reg_num);

        if(reg_type == 'x'){
            concat(outbuffer, "%8s = 0x%16.16llx\n", regstr,
                    focused->thread_state.__x[reg_num]);
        }
        else{
            concat(outbuffer, "%8s = 0x%8.8x\n", regstr, 
                    (int)focused->thread_state.__x[reg_num]);
        }

        free(regstr);
        free(curreg);

        curreg = argcopy(args, REGISTER_GEN_COMMAND_REGEX_GROUPS[0]);
    }
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_register_write(struct cmd_args_t *args,
        int arg1, char **outbuffer, char **error){
    char *target_str = argcopy(args, REGISTER_WRITE_COMMAND_REGEX_GROUPS[0]);
    char *value_str = argcopy(args, REGISTER_WRITE_COMMAND_REGEX_GROUPS[1]);

    size_t target_str_len = strlen(target_str);

    for(int i=0; i<target_str_len; i++)
        target_str[i] = tolower(target_str[i]);

    char reg_type = target_str[0];
    int reg_num = (int)strtol_err(target_str + 1, error);

    if(*error)
        goto fail;

    int gpr = reg_type == 'x' || reg_type == 'w';
    int fpr = (reg_type == 'q' || reg_type == 'v') ||
        reg_type == 'd' || reg_type == 's';
    int quadword = fpr && (reg_type == 'q' || reg_type == 'v');

    int good_reg_num = (reg_num >= 0 && reg_num <= 31);
    int good_reg_type = gpr || fpr;

    /* Various representations of our value string. */
    int valued = (int)strtol_err(value_str, error);

    if(gpr && *error)
        goto fail;

    long valuellx = strtol_err(value_str, error);

    if(gpr && *error)
        goto fail;

    /* The functions above will have set error
     * if we have a floating point value, so
     * clear it.
     */
    *error = NULL;

    float valuef = (float)strtold_err(value_str, error);

    if(fpr && !quadword && *error)
        goto fail;

    double valuedf = strtold_err(value_str, error);

    if(fpr && !quadword && *error)
        goto fail;

    struct machthread *focused = machthread_getfocused();

    get_thread_state(focused);
    get_neon_state(focused);

    /* Take care of any special registers. */
    if(strcmp(target_str, "fp") == 0)
        focused->thread_state.__fp = valuellx;
    else if(strcmp(target_str, "lr") == 0)
        focused->thread_state.__lr = valuellx;
    else if(strcmp(target_str, "sp") == 0)
        focused->thread_state.__sp = valuellx;
    else if(strcmp(target_str, "pc") == 0)
        focused->thread_state.__pc = valuellx;
    else if(strcmp(target_str, "cpsr") == 0)
        focused->thread_state.__cpsr = valued;
    else if(strcmp(target_str, "fpsr") == 0)
        focused->neon_state.__fpsr = valued;
    else if(strcmp(target_str, "fpcr") == 0)
        focused->neon_state.__fpcr = valued;
    else{
        if(!good_reg_num || !good_reg_type){
            concat(error, "bad register '%s'", target_str);
            goto fail;
        }

        if(gpr){
            if(reg_type == 'x')
                focused->thread_state.__x[reg_num] = valuellx;
            else{
                focused->thread_state.__x[reg_num] &= ~0xFFFFFFFFULL;
                focused->thread_state.__x[reg_num] |= valued;
            }
        }
        else{
            if(reg_type == 'q' || reg_type == 'v'){
                if(value_str[0] != '{' ||
                        value_str[strlen(value_str) - 1] != '}'){
                    concat(error, "bad value '%s'", value_str);
                    goto fail;
                }

                if(strlen(value_str) == 2){
                    concat(error, "bad value '%s'", value_str);
                    goto fail;
                }

                /* Remove the brackets. */
                value_str[strlen(value_str) - 1] = '\0';
                memmove(value_str, value_str + 1, strlen(value_str));

                char *hi_str = NULL, *lo_str = NULL;

                for(int i=0; i<sizeof(long)*2; i++){
                    char *space = strrchr(value_str, ' ');
                    char *curbyte = NULL;

                    if(space){
                        curbyte = strdup(space + 1);

                        /* Truncate what we've already processed. */
                        space[0] = '\0';
                    }
                    else
                        curbyte = strdup(value_str);

                    unsigned int byte =
                        (unsigned int)strtol(curbyte, NULL, 0);

                    if(i < sizeof(long))
                        concat(&lo_str, "%02x", byte);
                    else
                        concat(&hi_str, "%02x", byte);

                    free(curbyte);
                }

                long hi = strtoul(hi_str, NULL, 16);
                long lo = strtoul(lo_str, NULL, 16);

                /* Since this is a 128 bit "number", we have to split it
                 * up into two 64 bit pointers to correctly modify it.
                 */
                long *H = (long *)(&focused->neon_state.__v[reg_num]);
                long *L = (long *)(&focused->neon_state.__v[reg_num]) + 1;

                *H = hi;
                *L = lo;

                free(hi_str);
                free(lo_str);
            }
            else if(reg_type == 'd')
                focused->neon_state.__v[reg_num] = *(long *)&valuedf;
            else
                focused->neon_state.__v[reg_num] = *(int *)&valuef;
        }
    }

    set_thread_state(focused);
    set_neon_state(focused);

    free(target_str);
    free(value_str);

    return CMD_SUCCESS;

fail:
    free(target_str);
    free(value_str);

    return CMD_FAILURE;
}
