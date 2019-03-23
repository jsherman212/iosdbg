#define PCRE2_CODE_UNIT_WIDTH 8

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcre2.h>

#include "argparse.h"
#include "linkedlist.h"

int wants_add_aslr(char *str){
    /* This convenience variable allows the user to tell iosdbg
     * to ignore ASLR no matter what.
     */
    /*
    char *error;
    char *no_aslr_override = convvar_strval("$NO_ASLR_OVERRIDE", &error);

    if(no_aslr_override && strcmp(no_aslr_override, "void") != 0)
        return 0;
    */

    if(!str)
        return 1;

    return strnstr(str, "--no-aslr", strlen(str)) == NULL;
}

struct cmd_args_t *parse_args(char *_args, 
        const char *pattern,
        const char **groupnames,
        int num_groups,
        int unk_amount_of_args,
        char **error){
    char *args = strdup(_args);

    struct cmd_args_t *arguments = malloc(sizeof(struct cmd_args_t));

    arguments->argqueue = queue_new();
    arguments->num_args = 0;
    arguments->add_aslr = wants_add_aslr(args);

    PCRE2_SIZE erroroffset;
    int errornumber;

    pcre2_code *re = pcre2_compile((PCRE2_SPTR)pattern,
            PCRE2_ZERO_TERMINATED,
            0,//PCRE2_DUPNAMES,//PCRE2_MULTILINE, //0,
            &errornumber,
            &erroroffset,
            NULL);

    if(!re){
        PCRE2_UCHAR buf[2048];
        pcre2_get_error_message(errornumber, buf, sizeof(buf));

        asprintf(error, "compilation failed at offset %zu: %s",
                erroroffset, buf);

        free(args);

        return NULL;
    }

    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, NULL);
    size_t arglen = strlen(args);

    int rc = pcre2_match(re,
            (PCRE2_SPTR)args,
            arglen,
            0,
            0,
            match_data,
            NULL);

    if(rc < 0){
        PCRE2_UCHAR buf[2048];
        pcre2_get_error_message(rc, buf, sizeof(buf));

        asprintf(error, "your arguments could not be processed"
                " (rc = %d): %s", rc, buf);

        pcre2_match_data_free(match_data);
        pcre2_code_free(re);

        free(args);

        return NULL;
    }

    /* If we have an unknown amount of arguments,
     * the group name for those arguments will
     * be the very last thing in the groupnames array.
     * Normally we'd just loop through the groupnames array,
     * but in this case, we have to do that but exclude the
     * last group name. Then we have to keep matching for that
     * group name in another loop. TODO rephrase this rambling
     */
    int idx_limit = num_groups;

    if(unk_amount_of_args)
        idx_limit--;
    
    for(int i=0; i<idx_limit; i++){
        PCRE2_SPTR current_group = (PCRE2_SPTR)groupnames[i];
        int substr_idx = pcre2_substring_number_from_name(re,
                current_group);

        PCRE2_UCHAR *substr_buf;
        PCRE2_SIZE substr_buf_len;

        if(substr_idx == PCRE2_ERROR_NOUNIQUESUBSTRING){
            int copybyname_rc = pcre2_substring_get_byname(match_data,
                    current_group,
                    &substr_buf,
                    &substr_buf_len);

            if(copybyname_rc < 0)
                continue;
        }
        else{
            int substr_rc = pcre2_substring_get_bynumber(match_data,
                    substr_idx,
                    &substr_buf,
                    &substr_buf_len);

            /* It is fine if this substring wasn't found. */
            if(substr_rc < 0)
                continue;
        }

        char *arg = strdup((char *)substr_buf);

        if(strlen(arg) > 0 && strcmp(arg, "--no-aslr") != 0){
            arguments->num_args++;
            enqueue(arguments->argqueue, arg);
        }
        else
            free(arg);
    }

    if(!unk_amount_of_args){
        pcre2_match_data_free(match_data);
        pcre2_code_free(re);

        free(args);

        return arguments;
    }

    /* Now we have to match the rest of the arguments. */
    for(;;){
        PCRE2_SPTR current_group = (PCRE2_SPTR)groupnames[idx_limit];
        int substr_idx = pcre2_substring_number_from_name(re,
                current_group);
        
        if(substr_idx < 0)
            break;

        PCRE2_UCHAR *substr_buf;
        PCRE2_SIZE substr_buf_len;

        if(substr_idx == PCRE2_ERROR_NOUNIQUESUBSTRING){
            int copybyname_rc = pcre2_substring_get_byname(match_data,
                    current_group,
                    &substr_buf,
                    &substr_buf_len);

            /* If there were no more matches for this group, we're done. */
            if(copybyname_rc < 0)
                break;
        }
        else{
            int substr_rc = pcre2_substring_get_bynumber(match_data,
                    substr_idx,
                    &substr_buf,
                    &substr_buf_len);

            if(substr_rc < 0)
                break;
        }

        char *arg = strdup((char *)substr_buf);

        if(strlen(arg) > 0 && strcmp(arg, "--no-aslr") != 0){
            arguments->num_args++;
            enqueue(arguments->argqueue, arg);
        }
        else
            free(arg);

        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
        PCRE2_SIZE start_offset = ovector[1];

        rc = pcre2_match(re,
                (PCRE2_SPTR)args,
                arglen,
                start_offset,
                0,
                match_data,
                NULL);
    }

    pcre2_match_data_free(match_data);
    pcre2_code_free(re);

    free(args);

    return arguments;
}

char *argnext(struct cmd_args_t *args){
    if(!args)
        return NULL;

    if(!args->argqueue)
        return NULL;

    return dequeue(args->argqueue);
}

void argfree(struct cmd_args_t *args){
    if(!args)
        return;

    char *arg = argnext(args);

    while(arg){
        free(arg);
        arg = argnext(args);
    }

    queue_free(args->argqueue);
    free(args);
}

void desc(struct cmd_args_t *args){
    if(!args)
        return;

    char *arg = argnext(args);

    if(!arg)
        printf("no arguments\n");

    int count = 1;

    while(arg){
        printf("Argument %d: '%s'\n", count++, arg);
        arg = argnext(args);
    }

    printf("\n%d arguments, %s ASLR\n", args->num_args,
            args->add_aslr ? "wants to add" : "doesn't want to add");
}
