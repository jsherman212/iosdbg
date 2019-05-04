#include <limits.h>
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

    kern_return_t err = dump_memory(location, amount);

    if(err){
        asprintf(error, "could not dump memory from %#lx to %#lx: %s", 
                location, location + amount, mach_error_string(err));
        return CMD_FAILURE;
    }

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_memory_find(struct cmd_args_t *args,
        int arg1, char **error){
    char *start_str = argnext(args);
    long start = parse_expr(start_str, error);

    if(*error)
        return CMD_FAILURE;
    
    kern_return_t err = valid_location(start);

    if(err){
        asprintf(error, "invalid starting point: %s", mach_error_string(err));
        return CMD_FAILURE;
    }

    /* The next argument could be the count or the type. */
    char *arg2 = argnext(args);

    char *type_str = NULL;
    long count = LONG_MIN;

    if(is_number_fast(arg2)){
        count = strtol_err(arg2, error);

        if(*error)
            return CMD_FAILURE;

        type_str = argnext(args);
    }
    else
        type_str = arg2;

    char *target_str = argnext(args);
    int target_len = -1;
    void *target = NULL;

    if(strcmp(type_str, "--s") == 0){
        target = target_str;
        target_len = strlen(target_str);
    }
    else if(strstr(type_str, "--f")){
        if(strcmp(type_str, "--f") == 0){
            float f = (float)strtold_err(target_str, error);
            target = &f;
            target_len = sizeof(float);
        }
        else if(strcmp(type_str, "--fd") == 0){
            double d = (double)strtold_err(target_str, error);
            target = &d;
            target_len = sizeof(double);
        }
        else if(strcmp(type_str, "--fld") == 0){
            long double ld = strtold_err(target_str, error);
            target = &ld;
            target_len = sizeof(long double);
        }
    }
    else if(strstr(type_str, "--e")){
        if(strcmp(type_str, "--ec") == 0){
            signed char c = (signed char)parse_expr(target_str, error);
            target = &c;
            target_len = sizeof(signed char);
        }
        else if(strcmp(type_str, "--ecu") == 0){
            unsigned char c = (unsigned char)parse_expr(target_str, error);
            target = &c;
            target_len = sizeof(unsigned char);
        }
        else if(strcmp(type_str, "--es") == 0){
            signed short s = (signed short)parse_expr(target_str, error);
            target = &s;
            target_len = sizeof(signed short);
        }
        else if(strcmp(type_str, "--esu") == 0){
            unsigned short s = (unsigned short)parse_expr(target_str, error);
            target = &s;
            target_len = sizeof(unsigned short);
        }
        else if(strcmp(type_str, "--ed") == 0){
            signed int i = (signed int)parse_expr(target_str, error);
            target = &i;
            target_len = sizeof(signed int);
        }
        else if(strcmp(type_str, "--edu") == 0){
            unsigned int i = (unsigned int)parse_expr(target_str, error);
            target = &i;
            target_len = sizeof(unsigned int);
        }
        else if(strcmp(type_str, "--eld") == 0){
            signed long l = (signed long)parse_expr(target_str, error);
            target = &l;
            target_len = sizeof(signed long);
        }
        else if(strcmp(type_str, "--eldu") == 0){
            unsigned long l = (unsigned long)parse_expr(target_str, error);
            target = &l;
            target_len = sizeof(unsigned long);
        }
    }

    if(*error)
        return CMD_FAILURE;

    /* If count wasn't given, search until read_memory_at_location
     * returns an error.
     */
    long limit = count;

    if(count == LONG_MIN)
        limit = LONG_MAX;
    
    if(limit != LONG_MAX && limit < target_len){
        asprintf(error, "count (%ld) < sizeof(target type) (%d)",
                limit, target_len);
        return CMD_FAILURE;
    }

    kern_return_t read_ret = KERN_SUCCESS;

    long end = start + count;

    if(limit == LONG_MAX)
        end = LONG_MAX;

    printf("Searching from %#lx", start);

    if(end == LONG_MAX)
        printf(" without a limit... (aborting on error)\n");
    else
        printf(" to %#lx...\n", end);
    
    int results_cnt = 0;

    while(start <= end && read_ret == KERN_SUCCESS){
        void *read_buffer = malloc(target_len);
        
        read_ret = read_memory_at_location((void *)start, read_buffer, target_len);

        if(memcmp(read_buffer, target, target_len) == 0){
            results_cnt++;
            dump_memory(start, 0x10);
        }

        start++;
        free(read_buffer);
    }

    printf("\n%d result(s)\n", results_cnt);

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_memory_write(struct cmd_args_t *args, 
        int arg1, char **error){
    char *location_str = argnext(args);
    long location = parse_expr(location_str, error);

    if(*error)
        return CMD_FAILURE;

    char *data_str = argnext(args);
    long data = parse_expr(data_str, error);

    if(*error)
        return CMD_FAILURE;

    char *size_str = argnext(args);
    int size = (int)strtol_err(size_str, error);

    if(*error)
        return CMD_FAILURE;

    kern_return_t ret = write_memory_to_location(location, data, size);

    if(ret){
        asprintf(error, "couldn't write memory: %s", mach_error_string(ret));
        return CMD_FAILURE;
    }

    return CMD_SUCCESS;
}
