#define PCRE2_CODE_UNIT_WIDTH 8

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pcre2.h>

#include "argparse.h"

#include "../strext.h"

struct cmd_args *parse_and_create_args(char *_args, 
        const char *pattern,
        const char **groupnames,
        int num_groups,
        int unk_amount_of_args,
        char **error){
    struct cmd_args *arguments = malloc(sizeof(struct cmd_args));

    arguments->num_args = 0;
    arguments->argmaps = linkedlist_new();

    if(!_args)
        return arguments;

    char *args = strdup(_args);

    PCRE2_SIZE erroroffset;
    int errornumber;

    pcre2_code *re = pcre2_compile((PCRE2_SPTR)pattern,
            PCRE2_ZERO_TERMINATED,
            0,
            &errornumber,
            &erroroffset,
            NULL);

    if(!re){
        PCRE2_UCHAR buf[2048];
        pcre2_get_error_message(errornumber, buf, sizeof(buf));

        concat(error, "regex compilation failed at offset %zu: %s",
                erroroffset, buf);

        argfree(arguments);
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
        concat(error, "malformed arguments");

        pcre2_match_data_free(match_data);
        pcre2_code_free(re);

        argfree(arguments);
        free(args);

        return NULL;
    }

    /* If we have an unknown amount of arguments,
     * the group name for those arguments will
     * be the very last thing in the groupnames array.
     * Normally we'd just loop through the groupnames array,
     * but in this case, we have to do that but exclude the
     * last group name. Then we have to keep matching for that
     * group in another loop.
     */
    int idx_limit = num_groups;

    if(unk_amount_of_args)
        idx_limit--;
    
    for(int i=0; i<idx_limit; i++){
        PCRE2_SPTR current_group = (PCRE2_SPTR)groupnames[i];
        int substr_idx = pcre2_substring_number_from_name(re,
                current_group);

        PCRE2_UCHAR *substr_buf = NULL;
        PCRE2_SIZE substr_buf_len = 0;

        if(substr_idx == PCRE2_ERROR_NOUNIQUESUBSTRING){
            int copybyname_rc = pcre2_substring_get_byname(match_data,
                    current_group,
                    &substr_buf,
                    &substr_buf_len);
        }
        else{
            int substr_rc = pcre2_substring_get_bynumber(match_data,
                    substr_idx,
                    &substr_buf,
                    &substr_buf_len);
        }

        char *argument = NULL;

        if(substr_buf){
            arguments->num_args++;
            argument = strdup((char *)substr_buf);
        }

        argins(arguments, (const char *)current_group, argument);
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

        char *argument = NULL;

        if(substr_buf){
            arguments->num_args++;
            argument = strdup((char *)substr_buf);
        }

        argins(arguments, (const char *)current_group, argument);

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

struct cmd_args *argdup(struct cmd_args *with){
    struct cmd_args *duped = malloc(sizeof(struct cmd_args));
    duped->argmaps = linkedlist_new();
    duped->num_args = with->num_args;
    
    for(struct node *current = with->argmaps->front;
            current;
            current = current->next){
        struct argmap *with_map = current->data;

        struct argmap *duped_map = malloc(sizeof(struct argmap));
        duped_map->arggroup = strdup(with_map->arggroup);
        duped_map->argvals = queue_new();
        duped_map->argvalcnt = with_map->argvalcnt;

        for(int i=0; i<duped_map->argvalcnt; i++){
            char *with_arg = dequeue(with_map->argvals);

            if(with_arg){
                char *duped_arg = strdup(with_arg);
                enqueue(duped_map->argvals, duped_arg);
            }
            else{
                enqueue(duped_map->argvals, NULL);
            }

            enqueue(with_map->argvals, with_arg);
        }

        linkedlist_add(duped->argmaps, duped_map);
    }

    return duped;
}

void argins(struct cmd_args *args, const char *arggroup, char *argval){
    if(!args || !arggroup)
        return;

    for(struct node *current = args->argmaps->front;
            current;
            current = current->next){
        struct argmap *map = current->data;

        if(strcmp(map->arggroup, arggroup) == 0){
            enqueue(map->argvals, argval);
            map->argvalcnt++;
            return;
        }
    }

    struct argmap *map = malloc(sizeof(struct argmap));
    map->arggroup = strdup(arggroup);
    map->argvals = queue_new();

    enqueue(map->argvals, argval);

    map->argvalcnt = 1;

    linkedlist_add(args->argmaps, map);
}

char *argcopy(struct cmd_args *args, const char *group){
    if(!args || !group)
        return NULL;

    if(!args->argmaps)
        return NULL;

    for(struct node *current = args->argmaps->front;
            current;
            current = current->next){
        struct argmap *map = current->data;

        if(map->arggroup && strcmp(map->arggroup, group) == 0)
            return dequeue(map->argvals);
    }

    return NULL;
}

void argfree(struct cmd_args *args){
    if(!args)
        return;

    struct node *current = args->argmaps->front;

    while(current){
        struct argmap *map = current->data;
        free(map->arggroup);

        char *arg = dequeue(map->argvals);

        while(arg){
            free(arg);
            arg = dequeue(map->argvals);
        }

        current = current->next;

        linkedlist_delete(args->argmaps, map);
        free(map);
    }

    linkedlist_free(args->argmaps);
    free(args);
}
