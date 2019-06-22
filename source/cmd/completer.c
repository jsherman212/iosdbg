#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>

#include "cmd.h"
#include "completer.h"
#include "documentation.h"

#include "../queue.h"
#include "../rlext.h"
#include "../strext.h"

int IS_HELP_COMMAND = 0;

/* Whether or not what the user typed had a randomly generated string
 * appended to the end of each token.
 */
int LINE_MODIFIED = 0;

static struct matchedcmdinfo_t CURRENT_MATCH_INFO = {0};

static void _reset_matchedcmdinfo(void){
    free(CURRENT_MATCH_INFO.rinfo.argregex);

    CURRENT_MATCH_INFO.rinfo.argregex = NULL;
    CURRENT_MATCH_INFO.cmd = NULL;
    CURRENT_MATCH_INFO.cmd_function = NULL;
    CURRENT_MATCH_INFO.audit_function = NULL;
}

enum cmd_error_t prepare_and_call_cmdfunc(char *args,
        char **outbuffer, int *force_show_outbuffer, char **error){
    enum cmd_error_t result = CMD_SUCCESS;

    if(!CURRENT_MATCH_INFO.cmd_function){
        /* We might have a parent command... */
        if(CURRENT_MATCH_INFO.cmd){
            documentation_for_cmd(CURRENT_MATCH_INFO.cmd, outbuffer);
            goto out1;
        }

        result = CMD_FAILURE;
        goto out1;
    }

    struct cmd_args_t *parsed_args = parse_and_create_args(args,
            CURRENT_MATCH_INFO.rinfo.argregex,
            (const char **)(CURRENT_MATCH_INFO.rinfo.groupnames),
            CURRENT_MATCH_INFO.rinfo.num_groups,
            CURRENT_MATCH_INFO.rinfo.unk_num_args,
            error);

    if(*error){
        *force_show_outbuffer = 1;
        documentation_for_cmd(CURRENT_MATCH_INFO.cmd, outbuffer);
        result = CMD_FAILURE;
        goto out;
    }

    /* These audit functions perform checks that don't need to be inside
     * of their corresponding cmdfuncs.
     */
    void (*audit_function)(struct cmd_args_t *, const char **, char **) =
        CURRENT_MATCH_INFO.audit_function;
    
    if(audit_function){
        audit_function(parsed_args,
                (const char **)(CURRENT_MATCH_INFO.rinfo.groupnames), error);
    }

    if(*error){
        *force_show_outbuffer = 1;
        documentation_for_cmd(CURRENT_MATCH_INFO.cmd, outbuffer);
        concat(outbuffer, "\n");
        result = CMD_FAILURE;
        goto out;
    }

    result = (CURRENT_MATCH_INFO.cmd_function)(parsed_args, 0, outbuffer, error);

out:;
    _reset_matchedcmdinfo();
    argfree(parsed_args);
    return result;

out1:;
     _reset_matchedcmdinfo();
     return result;
}

static inline void copy_groupnames(struct dbg_cmd_t *from){
    for(int idx=0; idx<MAX_GROUPS; idx++)
        CURRENT_MATCH_INFO.rinfo.groupnames[idx] = 
            from->rinfo.groupnames[idx];
}

/*
 * Return a string of everything before `text` from rl_line_buffer.
 */
static char *everything_before(const char *text){
    char *everything = strdup("");

    int len = 0;
    char **words = rl_line_buffer_word_array(&len);

    int idx = 0;

    while(idx < len){
        if(strcmp(words[idx], text) == 0){
            token_array_free(words, len);
            return everything;
        }

        concat(&everything, "%s ", words[idx]);
        idx++;
    }

    token_array_free(words, len);

    return everything;
}

/*
 * Count the spaces before `text` inside of rl_line_buffer.
 */
static int count_spaces_before(const char *text){
    int len = 0;
    char **words = rl_line_buffer_word_array(&len);

    int idx = 0;

    while(idx < len){
        if(strcmp(words[idx], text) == 0){
            token_array_free(words, len);

            /* If we're completing for the help command,
             * matching one level below effectively "ignores"
             * the "help" in rl_line_buffer.
             */
            if(IS_HELP_COMMAND)
                return idx - 1;

            return idx;
        }

        idx++;
    }

    token_array_free(words, len);

    if(IS_HELP_COMMAND)
        return idx - 1;

    return idx;
}

static char *word_before(char *text){
    int len = 0;
    char **words = rl_line_buffer_word_array(&len);

    /* If `text` is empty, just return the last word
     * in rl_line_buffer.
     */
    if(strlen(text) == 0){
        char *ret = strdup(words[len - 1]);
        token_array_free(words, len);
        return ret;
    }

    int idx = len;

    while(idx--){
        char *cur = words[idx];

        if(strcmp(cur, text) == 0 && idx > 0){
            char *ret = strdup(words[idx - 1]);
            token_array_free(words, len);
            return ret;
        }
    }

    char *ret = strdup(words[0]);

    token_array_free(words, len);
    
    return ret;
}

/*
 * Match commands at a certain level.
 * `matches` must be freed.
 */
static void match_at_level(const char *text, int target_level,
        int *num_matches, char ***matches){
    struct queue_t *parentcmd_queue = queue_new();
    int subcmdidx = 0, idx = 0;
    size_t len = strlen(text);

    while(idx < NUM_TOP_LEVEL_COMMANDS){
        struct dbg_cmd_t *current = COMMANDS[idx++];

        if(target_level >= 1){
            if(current->parentcmd){
                enqueue(parentcmd_queue, current);

                while(parentcmd_queue->capacity != -1){
                    struct dbg_cmd_t *curparent = queue_peek(parentcmd_queue);
                    struct dbg_cmd_t *cursubcmd = curparent->subcmds[subcmdidx++];

                    if(!cursubcmd){
                        dequeue(parentcmd_queue);
                        subcmdidx = 0;
                        continue;
                    }

                    if(cursubcmd->level == target_level){
                        char *parent_cmd_name = word_before((char *)text);
                        size_t parentlen = strlen(parent_cmd_name);
                        size_t comparelen = len;

                        if(!IS_HELP_COMMAND && LINE_MODIFIED){
                            size_t subfrom = parentlen;

                            if(parentlen > RAND_PAD_LEN)
                                subfrom -= RAND_PAD_LEN;

                            parent_cmd_name[subfrom] = '\0';
                            parentlen = strlen(parent_cmd_name);

                            /* Ignore the random string tacked on. */
                            if(len > RAND_PAD_LEN)
                                comparelen -= RAND_PAD_LEN;
                        }

                        if(strncmp(curparent->name, parent_cmd_name, parentlen) == 0 &&
                                strncmp(cursubcmd->name, text, comparelen) == 0){
                            if(!matches)
                                (*num_matches)++;
                            else{
                                CURRENT_MATCH_INFO.rinfo.argregex =
                                    strdup(cursubcmd->rinfo.argregex);
                                CURRENT_MATCH_INFO.rinfo.num_groups =
                                    cursubcmd->rinfo.num_groups;
                                CURRENT_MATCH_INFO.rinfo.unk_num_args =
                                    cursubcmd->rinfo.unk_num_args;

                                copy_groupnames(cursubcmd);
                                
                                CURRENT_MATCH_INFO.cmd = cursubcmd;

                                CURRENT_MATCH_INFO.cmd_function =
                                    cursubcmd->cmd_function;
                                CURRENT_MATCH_INFO.audit_function =
                                    cursubcmd->audit_function;

                                (*matches)[(*num_matches)++] =
                                    strdup(cursubcmd->name);
                                char **matches_rea = realloc(*matches, sizeof(char *) *
                                        ((*num_matches) + 1));
                                *matches = matches_rea;
                            }
                        }

                        free(parent_cmd_name);
                    }

                    if(cursubcmd->parentcmd)
                        enqueue(parentcmd_queue, cursubcmd);
                }
            }
        }
        else{
            size_t comparelen = len;

            if(!IS_HELP_COMMAND && LINE_MODIFIED){
                if(len > RAND_PAD_LEN)
                    comparelen -= RAND_PAD_LEN;
            }

            if(strncmp(current->name, text, comparelen) == 0){
                if(!matches)
                    (*num_matches)++;
                else{
                    CURRENT_MATCH_INFO.rinfo.argregex =
                        strdup(current->rinfo.argregex);
                    CURRENT_MATCH_INFO.rinfo.num_groups =
                        current->rinfo.num_groups;
                    CURRENT_MATCH_INFO.rinfo.unk_num_args =
                        current->rinfo.unk_num_args;

                    copy_groupnames(current);

                    CURRENT_MATCH_INFO.cmd = current;

                    CURRENT_MATCH_INFO.cmd_function =
                        current->cmd_function;
                    CURRENT_MATCH_INFO.audit_function =
                        current->audit_function;

                    (*matches)[(*num_matches)++] = strdup(current->name);
                    char **matches_rea = realloc(*matches, sizeof(char *) *
                            ((*num_matches) + 1));
                    *matches = matches_rea;
                }
            }
        }
    }

    if(matches)
        (*matches)[*num_matches] = NULL;

    queue_free(parentcmd_queue);
}

static int ambiguous_command_at_level(char *cmd, int target_level){
    int num_matches = 0;
    match_at_level(cmd, target_level, &num_matches, NULL);

    return num_matches > 1;
}

static int no_ambiguity_before(const char *text){
    char *text_before = everything_before(text);

    if(!text_before)
        return 1;

    int cur_level = 0, idx = 0, len = 0;
    char **tokens = token_array(text_before, " ", &len);

    while(idx < len){
        char *token = tokens[idx++];

        if(ambiguous_command_at_level(token, cur_level)){
            token_array_free(tokens, len);
            return 0;
        }

        cur_level++;
    }

    token_array_free(tokens, len);

    return 1;
}

char *completion_generator(const char *text, int state){
    static char **matches;
    static int counter;

    if(state == 0){
        counter = 0;

        int level_to_match = count_spaces_before(text);
        int no_ambiguity_before_text = no_ambiguity_before(text);
        
        if(!no_ambiguity_before_text){
            _reset_matchedcmdinfo();
            return NULL;
        }

        matches = malloc(sizeof(char *));
        int num_matches = 0;

        match_at_level(text, level_to_match, &num_matches, &matches);

        if(num_matches > 1)
            _reset_matchedcmdinfo();
    }

    return *(matches + counter++);
}

char **completer(const char *text, int start, int end){
    rl_attempted_completion_over = 1;

    return rl_completion_matches(text, completion_generator);
}
