#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "argparse.h"

#include "../debuggee.h"
#include "../strext.h"

enum cmd_error_t cmdfunc_regsfloat(struct cmd_args_t *args, 
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
            long *hi = malloc(sizeof(long));
            long *lo = malloc(sizeof(long));
            
            *hi = debuggee->neon_state.__v[reg_num] >> 64;
            *lo = debuggee->neon_state.__v[reg_num];
            
            void *hi_data = (uint8_t *)hi;
            void *lo_data = (uint8_t *)lo;

            sprintf(regstr, "v%d = {", reg_num);

            for(int i=0; i<sizeof(long); i++)
                concat(&regstr, "0x%02x ", *(uint8_t *)(lo_data + i));
            
            for(int i=0; i<sizeof(long) - 1; i++)
                concat(&regstr, "0x%02x ", *(uint8_t *)(hi_data + i));

            concat(&regstr, "0x%02x}", *(uint8_t *)(hi_data + (sizeof(long) - 1)));

            free(hi);
            free(lo);
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

enum cmd_error_t cmdfunc_regsgen(struct cmd_args_t *args, 
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
                    (long long)debuggee->thread_state.__x[reg_num]);
        else
            printf("%8s = 0x%8.8x\n", regstr, 
                    (int)debuggee->thread_state.__x[reg_num]);

        free(regstr);

        curreg = argnext(args);
    }
    
    return CMD_SUCCESS;
}
