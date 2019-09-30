#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <readline/readline.h>

#include "documentation.h"

#include "../queue.h"
#include "../strext.h"

static void get_matches(char **array, int len, int longestelem,
        char **outbuffer){
    int rl_outstream_backup = dup(fileno(rl_outstream));

    int tempfds[2];
    pipe(tempfds);

    dup2(tempfds[1], fileno(rl_outstream));

    rl_display_match_list(array, len, longestelem);

    write(tempfds[1], "\003", sizeof(char));
    close(tempfds[1]);

    char ch;
    while(read(tempfds[0], &ch, sizeof(ch)) > 0){
        if(ch == '\003')
            break;

        concat(outbuffer, "%c", ch);
    }

    close(tempfds[0]);

    dup2(rl_outstream_backup, fileno(rl_outstream));
}

void show_all_top_level_cmds(char **outbuffer){
    concat(outbuffer, "List of top level commands:\n");

    int len = 1;
    char **cmds = malloc(sizeof(char *) * len);

    cmds[0] = strdup("");

    int idx = 0;
    int largest_cmd_len = 0;

    while(idx < NUM_TOP_LEVEL_COMMANDS){
        struct dbg_cmd *cmd = COMMANDS[idx++];

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

    get_matches(cmds, len, largest_cmd_len, outbuffer);

    token_array_free(cmds, len);
}

void documentation_for_cmd(struct dbg_cmd *cmd, char **outbuffer){
    /* Traverse level order to print out the sub-commands
     * for this command. Parent commands do not have accompanying
     * "cmdfuncs".
     */
    if(!cmd->cmd_function){
        concat(outbuffer, "%s", cmd->documentation);
        concat(outbuffer, "This command has the following subcommands:\n");

        int subcmdnum = 1;
        char **subcmds = malloc(sizeof(char *) * subcmdnum);

        subcmds[0] = strdup("");

        queue_t *cmdqueue = queue_new();
        enqueue(cmdqueue, cmd);

        int subcmdidx = 0;
        int largest_subcmd_len = strlen(subcmds[0]);

        while(cmdqueue->capacity != -1){
            struct dbg_cmd *curparent = queue_peek(cmdqueue);
            struct dbg_cmd *cursubcmd = curparent->subcmds[subcmdidx++];

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

        get_matches(subcmds, subcmdnum, largest_subcmd_len, outbuffer);
        token_array_free(subcmds, subcmdnum);
        queue_free(cmdqueue);

        if(cmd->alias)
            concat(outbuffer, "\nThis command has an alias: '%s'\n", cmd->alias);
        
        return;
    }

    concat(outbuffer, "%s", cmd->documentation);

    if(cmd->alias)
        concat(outbuffer, "This command has an alias: '%s'\n", cmd->alias);
}

void documentation_for_cmdname(char *_name, char **outbuffer, char **error){
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
            struct dbg_cmd *current = COMMANDS[idx++];

            if(strcmp(current->name, tokens[0]) == 0){
                documentation_for_cmd(current, outbuffer);
                goto out;
            }
        }

        concat(error, "unknown command \"%s\"", _name);
        goto out;
    }

    queue_t *cmdqueue = queue_new();

    while(idx < NUM_TOP_LEVEL_COMMANDS){
        struct dbg_cmd *cmd = COMMANDS[idx++];
        enqueue(cmdqueue, cmd);

        int subcmdidx = 0;

        while(cmdqueue->capacity != -1){
            struct dbg_cmd *curparent = queue_peek(cmdqueue);
            struct dbg_cmd *cursubcmd = curparent->subcmds[subcmdidx++];

            if(!cursubcmd){
                dequeue(cmdqueue);
                subcmdidx = 0;
                continue;
            }

            if(cursubcmd->level == (num_tokens - 1) &&
                    strcmp(curparent->name, tokens[cursubcmd->level - 1]) == 0){
                if(strcmp(cursubcmd->name, tokens[cursubcmd->level]) == 0){
                    documentation_for_cmd(cursubcmd, outbuffer);
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
