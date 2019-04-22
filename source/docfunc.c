#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "docfunc.h"
#include "queue.h"

void documentation_for_cmd(struct dbg_cmd_t *cmd){
    /* Traverse level order to print out the sub-commands
     * for this command. Parent commands do not have accompanying
     * "cmdfuncs".
     */
    if(!cmd->cmd_function){
        printf("%s", cmd->documentation);
        printf("This command has the following subcommands:\n\n");

        int subcmdnum = 1;

        struct queue_t *cmdqueue = queue_new();
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

            /* Only print direct descendants of `cmd`. */
            if(cursubcmd->level == (cmd->level + 1))
                printf("%d: %s\n", subcmdnum++, cursubcmd->name);

            if(cursubcmd->parentcmd)
                enqueue(cmdqueue, cursubcmd);
        }

        queue_free(cmdqueue);
        
        return;
    }

    printf("%s", cmd->documentation);
}

void documentation_for_cmdname(char *_name, char **error){
    int num_tokens = 0;

    char *name = strdup(_name);

    char *token = strtok_r(name, " ", &name);
    char **tokens = malloc(sizeof(char *) * (num_tokens + 1));

    tokens[num_tokens] = NULL;

    while(token){
        tokens = realloc(tokens, sizeof(char *) * (++num_tokens + 1));
        tokens[num_tokens - 1] = strdup(token);
        tokens[num_tokens] = NULL;

        token = strtok_r(NULL, " ", &name);
    }

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

        asprintf(error, "Unknown command \"%s\"", _name);
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

    asprintf(error, "Unknown command \"%s\"", _name);

out:
    for(int i=0; i<num_tokens; i++)
        free(tokens[i]);
    
    free(tokens);
    free(name);

    return;
}
