#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "argparse.h"

#include "../debuggee.h"
#include "../strext.h"

enum cmd_error_t cmdfunc_register_float(struct cmd_args_t *args, 
        int arg1, char **error){
    /* If the user wants a quadword register,
     * the max string length would be 87.
     */
    const int sz = 90;

    char *regstr = malloc(sz);

    /* Iterate through and show all the registers the user asked for. */
    char *curreg = argnext(args);

    while(curreg){
        debuggee->get_neon_state();

        if(strcmp(curreg, "fpsr") == 0){
            curreg = argnext(args);
            printf("%10s = 0x%8.8x\n", "fpsr", debuggee->neon_state.__fpsr);
            continue;
        }
        else if(strcmp(curreg, "fpcr") == 0){
            curreg = argnext(args);
            printf("%10s = 0x%8.8x\n", "fpcr", debuggee->neon_state.__fpcr);
            continue;
        }
        
        memset(regstr, '\0', sz);

        char reg_type = tolower(curreg[0]);
        
        /* Move up a byte for the register number. */
        memmove(curreg, curreg + 1, strlen(curreg));

        int reg_num = (int)strtol_err(curreg, error);

        if(*error){
            free(regstr);
            return CMD_FAILURE;
        }

        int good_reg_num = (reg_num >= 0 && reg_num <= 31);
        int good_reg_type = ((reg_type == 'q' || reg_type == 'v') 
                || reg_type == 'd' || reg_type == 's');

        if(!good_reg_num || !good_reg_type){
            printf("%8sInvalid register\n", "");
            curreg = argnext(args);
            continue;
        }
        /* Quadword */
        else if(reg_type == 'q' || reg_type == 'v'){
            long hi = debuggee->neon_state.__v[reg_num] >> 64;
            long lo = debuggee->neon_state.__v[reg_num];

            concat(&regstr, "v%d = {", reg_num);

            for(int i=0; i<sizeof(long); i++)
                concat(&regstr, "0x%02x ", *(uint8_t *)((uint8_t *)(&lo) + i));

            for(int i=0; i<sizeof(long) - 1; i++)
                concat(&regstr, "0x%02x ", *(uint8_t *)((uint8_t *)(&hi) + i));

            concat(&regstr, "0x%02x}",
                    *(uint8_t *)((uint8_t *)(&hi) + (sizeof(long) - 1)));
        }
        /* Doubleword */
        else if(reg_type == 'd')
            sprintf(regstr, "d%d = %.15g", reg_num, 
                    *(double *)&debuggee->neon_state.__v[reg_num]);
        /* Word */
        else if(reg_type == 's')
            sprintf(regstr, "s%d = %g", reg_num, 
                    *(float *)&debuggee->neon_state.__v[reg_num]);

        /* Figure out how many bytes the register takes up in the string. */
        char *space = strchr(regstr, ' ');
        int bytes = space - regstr;

        int add = 8 - bytes;
        
        printf("%*s\n", (int)(strlen(regstr) + add), regstr);
        
        curreg = argnext(args);
    }

    free(regstr);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_register_gen(struct cmd_args_t *args, 
        int arg1, char **error){
    debuggee->get_thread_state();

    const int sz = 8;

    /* If there were no arguments, print every register. */
    if(args->num_args == 0){
        for(int i=0; i<29; i++){        
            char *regstr = malloc(sz);
            memset(regstr, '\0', sz);

            sprintf(regstr, "x%d", i);

            printf("%10s = 0x%16.16llx\n", regstr, 
                    debuggee->thread_state.__x[i]);

            free(regstr);
        }
        
        printf("%10s = 0x%16.16llx\n", "fp", debuggee->thread_state.__fp);
        printf("%10s = 0x%16.16llx\n", "lr", debuggee->thread_state.__lr);
        printf("%10s = 0x%16.16llx\n", "sp", debuggee->thread_state.__sp);
        printf("%10s = 0x%16.16llx\n", "pc", debuggee->thread_state.__pc);
        printf("%10s = 0x%8.8x\n", "cpsr", debuggee->thread_state.__cpsr);

        return CMD_SUCCESS;
    }

    /* Otherwise, print every register they asked for. */
    char *curreg = argnext(args);

    while(curreg){
        char reg_type = tolower(curreg[0]);

        if(reg_type != 'x' && reg_type != 'w'){
            char *curreg_cpy = strdup(curreg);
            size_t curreg_cpy_len = strlen(curreg_cpy);

            /* We need to be able to free it. */
            for(int i=0; i<curreg_cpy_len; i++)
                curreg_cpy[i] = tolower(curreg_cpy[i]);

            if(strcmp(curreg_cpy, "fp") == 0)
                printf("%8s = 0x%16.16llx\n", "fp", debuggee->thread_state.__fp);
            else if(strcmp(curreg_cpy, "lr") == 0)
                printf("%8s = 0x%16.16llx\n", "lr", debuggee->thread_state.__lr);
            else if(strcmp(curreg_cpy, "sp") == 0)
                printf("%8s = 0x%16.16llx\n", "sp", debuggee->thread_state.__sp);
            else if(strcmp(curreg_cpy, "pc") == 0)
                printf("%8s = 0x%16.16llx\n", "pc", debuggee->thread_state.__pc);
            else if(strcmp(curreg_cpy, "cpsr") == 0)
                printf("%8s = 0x%8.8x\n", "cpsr", debuggee->thread_state.__cpsr);
            else
                printf("Invalid register\n");

            free(curreg_cpy);

            curreg = argnext(args);
            continue;
        }

        /* Move up one byte to get to the "register number". */
        memmove(curreg, curreg + 1, strlen(curreg));

        int reg_num = (int)strtol_err(curreg, error);

        if(*error)
            return CMD_FAILURE;
        
        if(reg_num < 0 || reg_num > 29){
            curreg = argnext(args);
            continue;
        }

        char *regstr = malloc(sz);
        memset(regstr, '\0', sz);

        sprintf(regstr, "%c%d", reg_type, reg_num);

        if(reg_type == 'x')
            printf("%8s = 0x%16.16llx\n", regstr,
                    debuggee->thread_state.__x[reg_num]);
        else
            printf("%8s = 0x%8.8x\n", regstr, 
                    (int)debuggee->thread_state.__x[reg_num]);

        free(regstr);

        curreg = argnext(args);
    }
    
    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_register_write(struct cmd_args_t *args,
        int arg1, char **error){
    char *target_str = argnext(args);
    char *value_str = argnext(args);

    size_t target_str_len = strlen(target_str);

    for(int i=0; i<target_str_len; i++)
        target_str[i] = tolower(target_str[i]);

    char reg_type = target_str[0];
    int reg_num = (int)strtol_err(target_str + 1, error);

    if(*error)
        return CMD_FAILURE;

    int gpr = reg_type == 'x' || reg_type == 'w';
    int fpr = (reg_type == 'q' || reg_type == 'v') ||
        reg_type == 'd' || reg_type == 's';
    int quadword = fpr && (reg_type == 'q' || reg_type == 'v');

    int good_reg_num = (reg_num >= 0 && reg_num <= 31);
    int good_reg_type = gpr || fpr;

    debuggee->get_thread_state();
    debuggee->get_neon_state();

    /* Various representations of our value string. */
    int valued = (int)strtol_err(value_str, error);

    if(gpr && *error)
        return CMD_FAILURE;

    long valuellx = strtol_err(value_str, error);

    if(gpr && *error)
        return CMD_FAILURE;

    /* The functions above will have set error
     * if we have a floating point value, so
     * clear it.
     */
    *error = NULL;

    float valuef = (float)strtold_err(value_str, error);

    if(fpr && !quadword && *error)
        return CMD_FAILURE;

    double valuedf = strtold_err(value_str, error);

    if(fpr && !quadword && *error)
        return CMD_FAILURE;

    /* Take care of any special registers. */
    if(strcmp(target_str, "fp") == 0)
        debuggee->thread_state.__fp = valuellx;
    else if(strcmp(target_str, "lr") == 0)
        debuggee->thread_state.__lr = valuellx;
    else if(strcmp(target_str, "sp") == 0)
        debuggee->thread_state.__sp = valuellx;
    else if(strcmp(target_str, "pc") == 0)
        debuggee->thread_state.__pc = valuellx;
    else if(strcmp(target_str, "cpsr") == 0)
        debuggee->thread_state.__cpsr = valued;
    else if(strcmp(target_str, "fpsr") == 0)
        debuggee->neon_state.__fpsr = valued;
    else if(strcmp(target_str, "fpcr") == 0)
        debuggee->neon_state.__fpcr = valued;
    else{
        if(!good_reg_num || !good_reg_type){
            asprintf(error, "bad register '%s'", target_str);
            return CMD_FAILURE;
        }

        if(gpr){
            if(reg_type == 'x')
                debuggee->thread_state.__x[reg_num] = valuellx;
            else{
                debuggee->thread_state.__x[reg_num] &= ~0xFFFFFFFFULL;
                debuggee->thread_state.__x[reg_num] |= valued;
            }
        }
        else{
            if(reg_type == 'q' || reg_type == 'v'){
                if(value_str[0] != '{' ||
                        value_str[strlen(value_str) - 1] != '}'){
                    asprintf(error, "bad value '%s'", value_str);
                    return CMD_FAILURE;
                }

                if(strlen(value_str) == 2){
                    asprintf(error, "bad value '%s'", value_str);
                    return CMD_FAILURE;
                }

                /* Remove the brackets. */
                value_str[strlen(value_str) - 1] = '\0';
                memmove(value_str, value_str + 1, strlen(value_str));

                size_t value_str_len = strlen(value_str);

                char *hi_str = malloc(value_str_len + 1);
                char *lo_str = malloc(value_str_len + 1);

                memset(hi_str, '\0', value_str_len);
                memset(lo_str, '\0', value_str_len);

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

                    if(i < sizeof(long)){
                        lo_str = realloc(lo_str, strlen(lo_str) +
                                strlen(curbyte) + 3);
                        concat(&lo_str, "%02x", byte);
                    }
                    else{
                        hi_str = realloc(hi_str, strlen(hi_str) +
                                strlen(curbyte) + 3);
                        concat(&hi_str,  "%02x", byte);
                    }

                    free(curbyte);
                }

                long hi = strtoul(hi_str, NULL, 16);
                long lo = strtoul(lo_str, NULL, 16);

                /* Since this is a 128 bit "number", we have to split it
                 * up into two 64 bit pointers to correctly modify it.
                 */
                long *H = (long *)(&debuggee->neon_state.__v[reg_num]);
                long *L = (long *)(&debuggee->neon_state.__v[reg_num]) + 1;

                *H = hi;
                *L = lo;

                free(hi_str);
                free(lo_str);
            }
            else if(reg_type == 'd')
                debuggee->neon_state.__v[reg_num] = *(long *)&valuedf;
            else
                debuggee->neon_state.__v[reg_num] = *(int *)&valuef;
        }
    }

    debuggee->set_thread_state();
    debuggee->set_neon_state();

    return CMD_SUCCESS;
}
