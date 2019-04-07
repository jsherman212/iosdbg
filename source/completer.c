#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>

#include "cmd.h"
#include "queue.h"
#include "strext.h"

static struct matchedcmdinfo_t CURRENT_MATCH_INFO = {0};

static void _reset_matchedcmdinfo(void){
    if(CURRENT_MATCH_INFO.rinfo.argregex)
        free(CURRENT_MATCH_INFO.rinfo.argregex);

    CURRENT_MATCH_INFO.rinfo.argregex = NULL;
    CURRENT_MATCH_INFO.function = NULL;
}

enum cmd_error_t prepare_and_call_cmdfunc(char *args, char **error){
    if(!CURRENT_MATCH_INFO.function){
        //printf("%s: function is NULL\n", __func__);
        return CMD_FAILURE;
    }

    printf("Got args '%s'\n", args);

    struct cmd_args_t *parsed_args = parse_args(args,
            CURRENT_MATCH_INFO.rinfo.argregex,
            (const char **)(CURRENT_MATCH_INFO.rinfo.groupnames),
            CURRENT_MATCH_INFO.rinfo.num_groups,
            CURRENT_MATCH_INFO.rinfo.unk_num_args,
            error);

    if(*error){
        argfree(parsed_args);
        return CMD_FAILURE;
    }

    enum cmd_error_t result = CURRENT_MATCH_INFO.function(parsed_args,
            0, error);

    argfree(parsed_args);

    _reset_matchedcmdinfo();

    return result;
}

static void copy_groupnames(struct dbg_cmd_t *from){
    for(int idx=0; idx<MAX_GROUPS; idx++)
        CURRENT_MATCH_INFO.rinfo.groupnames[idx] = from->rinfo.groupnames[idx];
}

/*
 * Return a string of everything before `text` from rl_line_buffer.
 */
static char *everything_before(const char *text, int ignore_spaces){
    char *substr_end = strrstr(rl_line_buffer, (char*)text);
    int len = substr_end - rl_line_buffer;

    return substr(rl_line_buffer, 0, ignore_spaces ? len - 1 : len);
}

/*
 * Count the spaces before `text` inside of rl_line_buffer.
 */
static int count_spaces_before(const char *text){
    if(!text)
        return 0;

    size_t len = strlen(text);
    char *before_text = NULL;

    if(len == 0)
        before_text = substr(rl_line_buffer, 0, strlen(rl_line_buffer));
    else
        before_text = everything_before(text, 0);

    if(!before_text)
        return 0;

    int idx = 0, spaces = 0;
    len = strlen(before_text);

    while(idx < len){
        if(isspace(before_text[idx++]))
            spaces++;
    }

    free(before_text);

    return spaces;
}

static char *word_before(char *text){
    if(!text)
        return NULL;

    char *substr_end = strrstr(rl_line_buffer, text);

    int start_idx = substr_end - rl_line_buffer;

    if(substr_end == rl_line_buffer)
        start_idx = strlen(rl_line_buffer) - 1;

    int idx = start_idx;
    int space_count = 0;

    while(idx >= 0 && space_count < 2){
        if(isspace(rl_line_buffer[idx]))
            space_count++;

        idx--;
    }

    idx++;

    int start = idx, copylen = 0;

    do{
        idx++;
        copylen++;
    }while(idx < strlen(rl_line_buffer) && !isspace(rl_line_buffer[idx]));

    char *ret = substr(rl_line_buffer, start, copylen);
    strclean(&ret);

    return ret;
}

/*
 * Match commands at a certain level.
 * `matches` must be freed.
 */
static void match_at_level(const char *text, int target_level,
        int *num_matches, char ***matches){
    struct queue_t *parentcmd_queue = queue_new();
    int subcmdidx = 0;
    int idx = 0;
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
                                //CURRENT_MATCH_INFO.rinfo.groupnames =
                                  //  cursubcmd->rinfo.groupnames;
                                copy_groupnames(cursubcmd);
                                
                                CURRENT_MATCH_INFO.function = cursubcmd->function;

                                (*matches)[(*num_matches)++] = strdup(cursubcmd->name);
                                (*matches) = realloc((*matches), sizeof(char *) *
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
                    //CURRENT_MATCH_INFO.rinfo.groupnames =
                      //  current->rinfo.groupnames;
                    copy_groupnames(current);
                    CURRENT_MATCH_INFO.function = current->function;

                    (*matches)[(*num_matches)++] = strdup(current->name);
                    (*matches) = realloc((*matches), sizeof(char *) *
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

static int no_ambiguity_before(const char *text, int *nothing_before){
    char *text_before = everything_before(text, 0);

    if(!text_before){
        *nothing_before = 1;
        return 1;
    }

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

char *completion_generator(const char *text, int state){
    static char **matches;
    static int counter;

    if(state == 0){
        counter = 0;
        int level_to_match = count_spaces_before(text);

        int nothing_before_text = 0;
        int no_ambiguity_before_text = no_ambiguity_before(text,
                &nothing_before_text);
        
        if(!no_ambiguity_before_text){
            _reset_matchedcmdinfo();
            return NULL;
        }

        int num_matches = 0;
        matches = malloc(sizeof(char *));
        
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
