#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "cmd.h"
#include "completer.h"

#include "../printing.h"
#include "../strext.h"

static void expand_aliases(char **command){
    /* Only top level commands (level 0) can have aliases,
     * so we only need to test the first "word".
     */
    char *space = strchr(*command, ' ');
    int bytes_until_space = 0;
    char *token = NULL;

    if(space){
        bytes_until_space = space - (*command);
        token = substr(*command, 0, bytes_until_space);
    }
    else
        token = strdup(*command);

    /* Check for an alias. */
    for(int i=0; i<NUM_TOP_LEVEL_COMMANDS; i++){
        struct dbg_cmd_t *current = COMMANDS[i];

        if(current->alias && strcmp(current->alias, token) == 0){
            /* Replace the alias with its command. */
            strcut(command, 0, bytes_until_space);
            strins(command, current->name, 0);

            rl_replace_line(*command, 0);

            free(token);

            return;
        }
    }

    free(token);
}

static void execute_shell_cmd(char *command,
        char **exit_reason, char **error){
    if(strlen(command) == 0){
        concat(error, "command missing");
        return;
    }

    const int argv_len = 3;
    char **argv = malloc(sizeof(char *) * (argv_len + 1));

    argv[0] = strdup("sh");
    argv[1] = strdup("-c");
    argv[2] = strdup(command);
    argv[3] = NULL;

    pid_t sh_pid;
    int status = posix_spawn(&sh_pid,
            "/bin/sh",
            NULL,
            NULL,
            (char * const *)argv,
            NULL);

    token_array_free(argv, argv_len);

    if(status != 0){
        concat(error, "posix spawn failed: %s\n", strerror(status));
        return;
    }

    waitpid(sh_pid, &status, 0);

    if(WIFEXITED(status))
        concat(exit_reason, "\n\nshell returned %d", WEXITSTATUS(status));
    else if(WIFSIGNALED(status)){
        concat(exit_reason, "\n\nshell terminated due to signal %d",
                WTERMSIG(status));
    }
}

enum cmd_error_t do_cmdline_command(char *user_command_,
        char **expanded_command, int user_invoked, char **error){
    char *user_command = strdup(user_command_);
    enum cmd_error_t result = CMD_SUCCESS;

    strclean(&user_command);

    /* If the user hits enter right away, rl_line_buffer will be empty.
     * The completer relies on rl_line_buffer reflecting the
     * contents of user_command, so make that so.
     */
    rl_replace_line(user_command, 0);

    char *usercommandcpy = strdup(user_command);

    /* If the first character is a '!', this is a shell command. */
    if(*user_command == '!'){
        char *exit_reason = NULL, *error = NULL;
        char *shell_cmd = usercommandcpy + 1;
        execute_shell_cmd(shell_cmd, &exit_reason, &error);

        if(exit_reason){
            WriteMessageBuffer("%s\n", exit_reason);
            free(exit_reason);
        }
        
        if(error)
            result = CMD_FAILURE;

        goto done;
    }

    expand_aliases(&user_command);

    /* When a command's argument is the same as the actual command
     * or one of its sub-commands, the completer will not be able to
     * correctly figure out the level to match at.
     *
     * For example: "attach attach"
     * completion_generator is called two times with the same text parameter.
     * During the first call, the level to match will be 0, because 
     * there's 0 spaces before the first "attach", which is fine.
     * However, during the second call, we want to find the number of
     * spaces before the second "attach". But completion_generator knows
     * no different and finds that the first "attach" matches the text given 
     * to it. While this is "correct", it isn't what we want.
     *
     * To fix this, we silently append a short, randomly generated string
     * to every token in the user's input. This way, every single token
     * will (hopefully) be different, and figuring out the level
     * to match at works as expected. This appended string is ignored
     * inside of match_at_level. After the command is finished and we're
     * about to give control back to the user, rl_line_buffer is replaced
     * with the command the user typed.
     */
    int num_tokens = 0;
    char **tokens = token_array(user_command, " ", &num_tokens);
    char *randcommand = NULL;

    for(int i=0; i<num_tokens; i++){
        char *rstr = strnran(RAND_PAD_LEN);
        char *token = strdup(tokens[i]);

        concat(&token, "%s", rstr);
        concat(&randcommand, "%s ", token);

        free(rstr);
        free(token);
    }

    token_array_free(tokens, num_tokens);

    if(num_tokens > 0){
        strclean(&randcommand);
        rl_replace_line(randcommand, 0);
        LINE_MODIFIED = 1;
    }

    char *arguments = NULL;
    char **prev_completions = NULL, **completions = NULL,
         **rtokens = token_array(randcommand, " ", &num_tokens);

    int current_start = 0, idx = 0, first_match = 1;

    while(idx < num_tokens){
        char *rtok = rtokens[idx];
        size_t rtoklen = strlen(rtok);

        /* Force matching to figure out the command. */
        completions = completer(rtok, current_start, rtoklen);

        /* If we don't get a match the first time around,
         * it's an unknown command.
         */
        if(first_match && !completions){
            concat(error, "undefined command \"%s\". Try \"help\".",
                    usercommandcpy);

            result = CMD_FAILURE;
            
            break;
        }

        first_match = 0;

        /* If there were no matches, we can assume the following
         * tokens are arguments. We've already taken care of the
         * case where the command is unknown, so the command
         * is guarenteed to be known at this point.
         */
        if(!completions){
            while(idx < num_tokens){
                rtok = rtokens[idx++];

                /* We appended a random string, so cut it off. */
                rtok[strlen(rtok) - RAND_PAD_LEN] = '\0';
                concat(&arguments, " %s", rtok);
            }

            /* Get rid of leading/trailing whitespace that
             * would mess up regex matching.
             */
            strclean(&arguments);

            break;
        }

        current_start += rtoklen;
        prev_completions = completions;

        idx++;
    }

    free(randcommand);

    token_array_free(rtokens, num_tokens);

    /* We need to test the previous completions in case
     * this command is a parent command.
     */
    if(prev_completions && *(prev_completions + 1)){
        char *ambiguous_cmd_str = NULL;
        concat(&ambiguous_cmd_str, "ambiguous command \"%s\": ",
                usercommandcpy);

        for(int i=1; prev_completions[i]; i++)
            concat(&ambiguous_cmd_str, "%s, ", prev_completions[i]);

        /* Get rid of the trailing comma. */
        ambiguous_cmd_str[strlen(ambiguous_cmd_str) - 2] = '\0';

        concat(error, "%s", ambiguous_cmd_str);
        free(ambiguous_cmd_str);

        result = CMD_FAILURE;
    }
    else{
        /* Parse arguments, audit them, and if nothing went wrong,
         * call this command's cmdfunc.
         */
        result = prepare_and_call_cmdfunc(arguments, error);
    }

    free(arguments);

    if(user_invoked)
        rl_replace_line(usercommandcpy, 0);
    else
        rl_replace_line("", 0);

    LINE_MODIFIED = 0;

done:;
    if(expanded_command)
        *expanded_command = usercommandcpy;

    return result;
}
