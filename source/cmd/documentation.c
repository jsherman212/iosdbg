#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>

#include "documentation.h"

#include "../printing.h"
#include "../queue.h"
#include "../strext.h"

void show_all_top_level_cmds(void){
    WriteMessageBuffer("List of top level commands:\n");

    int len = 1;
    char **cmds = malloc(sizeof(char *) * len);

    cmds[0] = strdup("");

    int idx = 0;
    int largest_cmd_len = 0;

    while(idx < NUM_TOP_LEVEL_COMMANDS){
        struct dbg_cmd_t *cmd = COMMANDS[idx++];

        if(cmd->level == 0){
            char **cmds_rea = realloc(cmds, sizeof(char *) * (++len + 1));
            cmds = cmds_rea;
            cmds[len - 1] = strdup(cmd->name);
            cmds[len] = NULL;

            int cmdlen = strlen(cmd->name);

            largest_cmd_len =
                cmdlen > largest_cmd_len ? cmdlen : largest_cmd_len;
        }
    }

    for(int i=1; i<len; i++)
        WriteMessageBuffer("%d. %s\n", i, cmds[i]);
    
    token_array_free(cmds, len);
}

void documentation_for_cmd(struct dbg_cmd_t *cmd){
    /* Traverse level order to print out the sub-commands
     * for this command. Parent commands do not have accompanying
     * "cmdfuncs".
     */
    if(!cmd->cmd_function){
        WriteMessageBuffer("%s", cmd->documentation);
        WriteMessageBuffer("This command has the following subcommands:\n");

        int subcmdnum = 1;
        char **subcmds = malloc(sizeof(char *) * subcmdnum);

        subcmds[0] = strdup("");

        struct queue_t *cmdqueue = queue_new();
        enqueue(cmdqueue, cmd);

        int subcmdidx = 0;
        int largest_subcmd_len = strlen(subcmds[0]);

        while(cmdqueue->capacity != -1){
            struct dbg_cmd_t *curparent = queue_peek(cmdqueue);
            struct dbg_cmd_t *cursubcmd = curparent->subcmds[subcmdidx++];

            if(!cursubcmd){
                dequeue(cmdqueue);
                subcmdidx = 0;
                continue;
            }

            /* Only grab direct descendants of `cmd`. */
            if(cursubcmd->level == (cmd->level + 1)){
                char **subcmds_rea = realloc(subcmds, sizeof(char *) * (++subcmdnum + 1));
                subcmds = subcmds_rea;
                subcmds[subcmdnum - 1] = strdup(cursubcmd->name);

                size_t sclen = strlen(subcmds[subcmdnum - 1]);
                
                largest_subcmd_len = 
                    sclen > largest_subcmd_len ? sclen : largest_subcmd_len;
                    
                subcmds[subcmdnum] = NULL;
            }

            if(cursubcmd->parentcmd)
                enqueue(cmdqueue, cursubcmd);
        }
        
        for(int i=1; i<subcmdnum; i++)
            WriteMessageBuffer("%d. %s\n", i, subcmds[i]);

        token_array_free(subcmds, subcmdnum);
        queue_free(cmdqueue);

        if(cmd->alias)
            WriteMessageBuffer("\nThis command has an alias: '%s'\n\n", cmd->alias);
        
        return;
    }

    WriteMessageBuffer("%s", cmd->documentation);

    if(cmd->alias)
        WriteMessageBuffer("This command has an alias: '%s'\n\n", cmd->alias);
}

void documentation_for_cmdname(char *_name, char **error){
    int num_tokens = 0;
    char **tokens = token_array(_name, " ", &num_tokens);

    int idx = 0;
    
    /* If we only have one token, this is a top level command
     * without any sub-commands. There's no need to waste time doing
     * a level order search, a linear search through the command
     * array will suffice.
     */
    if(num_tokens == 1){
        while(idx < NUM_TOP_LEVEL_COMMANDS){
            struct dbg_cmd_t *current = COMMANDS[idx++];

            if(strcmp(current->name, tokens[0]) == 0){
                documentation_for_cmd(current);
                goto out;
            }
        }

        concat(error, "unknown command \"%s\"", _name);
        goto out;
    }

    struct queue_t *cmdqueue = queue_new();

    while(idx < NUM_TOP_LEVEL_COMMANDS){
        struct dbg_cmd_t *cmd = COMMANDS[idx++];
        enqueue(cmdqueue, cmd);

        int subcmdidx = 0;

        while(cmdqueue->capacity != -1){
            struct dbg_cmd_t *curparent = queue_peek(cmdqueue);
            struct dbg_cmd_t *cursubcmd = curparent->subcmds[subcmdidx++];

            if(!cursubcmd){
                dequeue(cmdqueue);
                subcmdidx = 0;
                continue;
            }

            if(cursubcmd->level == (num_tokens - 1) &&
                    strcmp(curparent->name, tokens[cursubcmd->level - 1]) == 0){
                if(strcmp(cursubcmd->name, tokens[cursubcmd->level]) == 0){
                    documentation_for_cmd(cursubcmd);
                    queue_free(cmdqueue);
                    goto out;
                }
            }

            if(cursubcmd->parentcmd)
                enqueue(cmdqueue, cursubcmd);
        }
    }

    queue_free(cmdqueue);

    concat(error, "unknown command \"%s\"", _name);

out:
    token_array_free(tokens, num_tokens);

    return;
}
