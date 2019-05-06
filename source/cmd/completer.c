#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>

#include "cmd.h"
#include "documentation.h"

#include "../queue.h"
#include "../rlext.h"
#include "../strext.h"

static struct matchedcmdinfo_t CURRENT_MATCH_INFO = {0};

static void _reset_matchedcmdinfo(void){
    if(CURRENT_MATCH_INFO.rinfo.argregex)
        free(CURRENT_MATCH_INFO.rinfo.argregex);

    CURRENT_MATCH_INFO.rinfo.argregex = NULL;
    CURRENT_MATCH_INFO.cmd = NULL;
    CURRENT_MATCH_INFO.cmd_function = NULL;
    CURRENT_MATCH_INFO.audit_function = NULL;
}

enum cmd_error_t prepare_and_call_cmdfunc(char *args, char **error){
    if(!CURRENT_MATCH_INFO.cmd_function){
        /* We might have a parent command... */
        if(CURRENT_MATCH_INFO.cmd){
            documentation_for_cmd(CURRENT_MATCH_INFO.cmd);
            _reset_matchedcmdinfo();

            return CMD_SUCCESS;
        }

        return CMD_FAILURE;
    }

    struct cmd_args_t *parsed_args = parse_args(args,
            CURRENT_MATCH_INFO.rinfo.argregex,
            (const char **)(CURRENT_MATCH_INFO.rinfo.groupnames),
            CURRENT_MATCH_INFO.rinfo.num_groups,
            CURRENT_MATCH_INFO.rinfo.unk_num_args,
            error);

    if(*error){
        documentation_for_cmd(CURRENT_MATCH_INFO.cmd);

        _reset_matchedcmdinfo();
        argfree(parsed_args);

        return CMD_FAILURE;
    }

    /* These audit functions perform checks that don't need to be inside
     * of their corresponding cmdfuncs.
     */
    (CURRENT_MATCH_INFO.audit_function)(parsed_args, error);

    if(*error){
        documentation_for_cmd(CURRENT_MATCH_INFO.cmd);

        _reset_matchedcmdinfo();
        argfree(parsed_args);

        return CMD_FAILURE;
    }

    enum cmd_error_t result =
        (CURRENT_MATCH_INFO.cmd_function)(parsed_args, 0, error);

    _reset_matchedcmdinfo();
    argfree(parsed_args);

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
    char *substr_end = strrstr(rl_line_buffer, (char *)text);
    int len = substr_end - rl_line_buffer;

    return substr(rl_line_buffer, 0, len);
}

/*
 * Count the spaces before `text` inside of rl_line_buffer.
 */
static int count_spaces_before(const char *text){
    int len = 0;
    char **words = rl_line_buffer_word_array(&len);

    int idx = 0;

    while(idx < len){
        if(strcmp(words[idx], text) == 0)
            return idx;

        idx++;
    }

    return idx;
}

static char *word_before(char *text){
    int len = 0;
    char **words = rl_line_buffer_word_array(&len);

    int idx = len;

    while(idx--){
        char *cur = words[idx];

        if(strcmp(cur, text) == 0 && idx > 0)
            return words[idx - 1];
    }

    return words[0];
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

                        if(strncmp(curparent->name, parent_cmd_name, parentlen) == 0 &&
                                strncmp(cursubcmd->name, text, len) == 0){
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

                                (*matches)[(*num_matches)++] = strdup(cursubcmd->name);
                                *matches = realloc(*matches, sizeof(char *) *
                                        ((*num_matches) + 1));
                            }
                        }
                    }

                    if(cursubcmd->parentcmd)
                        enqueue(parentcmd_queue, cursubcmd);
                }
            }
        }
        else{
            if(strncmp(current->name, text, len) == 0){
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
                    *matches = realloc(*matches, sizeof(char *) *
                            ((*num_matches) + 1));
                }
            }
        }
    }

    if(matches)
        (*matches)[*num_matches] = NULL;
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

    int ambiguous = 0, cur_level = 0;
    char *token = strtok_r(text_before, " ", &text_before);

    while(token){
        if(ambiguous_command_at_level(token, cur_level))
            return 0;

        cur_level++;

        token = strtok_r(NULL, " ", &text_before);
    }

    return 1;
}

int IS_HELP_COMMAND = 0;

char *completion_generator(const char *text, int state){
    static char **matches;
    static int counter;

    if(state == 0){
        counter = 0;
        //printf("%s: text '%s'\n", __func__, text);

        int level_to_match = count_spaces_before(text);
        int no_ambiguity_before_text = no_ambiguity_before(text);
        
        if(!no_ambiguity_before_text){
            _reset_matchedcmdinfo();
            return NULL;
        }

        matches = malloc(sizeof(char *));
        int num_matches = 0;

        if(IS_HELP_COMMAND){
            printf("rl_line_buffer '%s'\n", rl_line_buffer);
            printf("level to match %d text '%s'\n", level_to_match, text);
            match_at_level(text, level_to_match - 1, &num_matches, &matches);

            if(matches){
                for(int i=0; matches[i]; i++){
                    printf("'%s'\n", matches[i]);
                }
            }
            
        }
        else{
            match_at_level(text, level_to_match, &num_matches, &matches);
            /*if(matches[0] && strcmp(matches[0], "help") == 0){
                //printf("got help cmd\n");
                IS_HELP_COMMAND = 1;
            }
            else{
                IS_HELP_COMMAND = 0;
            }*/
        }


        if(num_matches > 1)
            _reset_matchedcmdinfo();
    }

    return *(matches + counter++);
}

char **completer(const char *text, int start, int end){
    rl_attempted_completion_over = 1;

    return rl_completion_matches(text, completion_generator);
}
