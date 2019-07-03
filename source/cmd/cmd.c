#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "audit.h"
#include "bpcmd.h"
#include "cmd.h"
#include "misccmd.h"
#include "memcmd.h"
#include "regcmd.h"
#include "sigcmd.h"
#include "stepcmd.h"
#include "tcmd.h"
#include "wpcmd.h"
#include "varcmd.h"

#include "completer.h"

#include "../strext.h"

char *HISTORY_PATH = NULL;

static struct dbg_cmd_t *common_initialization(const char *name,
        const char *alias, const char *documentation, int level,
        const char *argregex, int num_groups, int unk_num_args,
        const char *groupnames[MAX_GROUPS],
        enum cmd_error_t (*cmd_function)(struct cmd_args_t *, int, char **, char **),
        void (*audit_function)(struct cmd_args_t *, const char **, char **)){
    struct dbg_cmd_t *c = malloc(sizeof(struct dbg_cmd_t));

    c->name = strdup(name);
    
    if(!alias)
        c->alias = NULL;
    else
        c->alias = strdup(alias);

    if(!documentation)
        c->documentation = strdup("");
    else
        c->documentation = strdup(documentation);
    
    c->rinfo.argregex = strdup(argregex);
    c->rinfo.num_groups = num_groups;
    c->rinfo.unk_num_args = unk_num_args;

    for(int i=0; i<c->rinfo.num_groups; i++)
        c->rinfo.groupnames[i] = strdup(groupnames[i]);
    
    c->level = level;

    c->cmd_function = cmd_function;
    c->audit_function = audit_function;
    
    return c;
}

static struct dbg_cmd_t *create_parent_cmd(const char *name,
        const char *alias, const char *documentation, int level,
        const char *argregex, int num_groups, int unk_num_args,
        const char *groupnames[MAX_GROUPS], int numsubcmds,
        enum cmd_error_t (*cmd_function)(struct cmd_args_t *, int, char **, char **),
        void (*audit_function)(struct cmd_args_t *, const char **, char **)){
    struct dbg_cmd_t *c = common_initialization(name,
            alias, documentation, level, argregex, num_groups,
            unk_num_args, groupnames, cmd_function, audit_function);

    c->parentcmd = 1;

    c->subcmds = malloc(sizeof(struct dbg_cmd_t) * (numsubcmds + 1));
    c->subcmds[numsubcmds] = NULL;

    return c;
}

static struct dbg_cmd_t *create_child_cmd(const char *name,
        const char *alias, const char *documentation, int level,
        const char *argregex, int num_groups, int unk_num_args,
        const char *groupnames[MAX_GROUPS],
        enum cmd_error_t (*cmd_function)(struct cmd_args_t *, int, char **, char **),
        void (*audit_function)(struct cmd_args_t *, const char **, char **)){
    struct dbg_cmd_t *c = common_initialization(name,
            alias, documentation, level, argregex, num_groups,
            unk_num_args, groupnames, cmd_function, audit_function);

    c->parentcmd = 0;
    c->subcmds = NULL;

    return c;
}

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
    else{
        token = strdup(*command);
    }

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

void initialize_commands(void){
    int cmdidx = 0;

#define ADD_CMD(x) COMMANDS[cmdidx++] = x

    struct dbg_cmd_t *aslr = create_parent_cmd("aslr",
            NULL, ASLR_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(0), cmdfunc_aslr,
            audit_aslr);

    ADD_CMD(aslr);

    struct dbg_cmd_t *attach = create_parent_cmd("attach",
            NULL, ATTACH_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            ATTACH_COMMAND_REGEX, _NUM_GROUPS(2), _UNK_ARGS(0),
            ATTACH_COMMAND_REGEX_GROUPS, _NUM_SUBCMDS(0), cmdfunc_attach,
            audit_attach);

    ADD_CMD(attach);

    struct dbg_cmd_t *backtrace = create_parent_cmd("backtrace",
            "bt", BACKTRACE_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(0), cmdfunc_backtrace,
            audit_backtrace);

    ADD_CMD(backtrace);

    struct dbg_cmd_t *breakpoint = create_parent_cmd("breakpoint",
            "b", BREAKPOINT_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(3), NULL, NULL);
    {
        struct dbg_cmd_t *delete = create_child_cmd("delete",
                NULL, BREAKPOINT_DELETE_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                BREAKPOINT_DELETE_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(0),
                BREAKPOINT_DELETE_COMMAND_REGEX_GROUPS, cmdfunc_breakpoint_delete,
                NULL);
        struct dbg_cmd_t *list = create_child_cmd("list",
                NULL, BREAKPOINT_LIST_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
                NO_GROUPS, cmdfunc_breakpoint_list,
                NULL);
        struct dbg_cmd_t *set = create_child_cmd("set",
                NULL, BREAKPOINT_SET_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                BREAKPOINT_SET_COMMAND_REGEX, _NUM_GROUPS(2), _UNK_ARGS(1),
                BREAKPOINT_SET_COMMAND_REGEX_GROUPS, cmdfunc_breakpoint_set,
                audit_breakpoint_set);

        breakpoint->subcmds[0] = delete;
        breakpoint->subcmds[1] = list;
        breakpoint->subcmds[2] = set;
    }

    ADD_CMD(breakpoint);

    struct dbg_cmd_t *_continue = create_parent_cmd("continue",
            "c", CONTINUE_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(0), cmdfunc_continue,
            audit_continue);

    ADD_CMD(_continue);

    struct dbg_cmd_t *detach = create_parent_cmd("detach",
            NULL, DETACH_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(0), cmdfunc_detach,
            audit_detach);

    ADD_CMD(detach);

    struct dbg_cmd_t *disassemble = create_parent_cmd("disassemble",
            "dis", DISASSEMBLE_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            DISASSEMBLE_COMMAND_REGEX, _NUM_GROUPS(2), _UNK_ARGS(0),
            DISASSEMBLE_COMMAND_REGEX_GROUPS, _NUM_SUBCMDS(0), cmdfunc_disassemble,
            audit_disassemble);

    ADD_CMD(disassemble);

    struct dbg_cmd_t *evaluate = create_parent_cmd("evaluate",
            NULL, EVALUATE_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            EVALUATE_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(1),
            EVALUATE_COMMAND_REGEX_GROUPS, _NUM_SUBCMDS(0), cmdfunc_evaluate,
            audit_evaluate);

    ADD_CMD(evaluate);

    struct dbg_cmd_t *examine = create_parent_cmd("examine",
            "x", EXAMINE_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            EXAMINE_COMMAND_REGEX, _NUM_GROUPS(2), _UNK_ARGS(0),
            EXAMINE_COMMAND_REGEX_GROUPS, _NUM_SUBCMDS(0), cmdfunc_examine,
            audit_examine);

    ADD_CMD(examine);

    struct dbg_cmd_t *help = create_parent_cmd("help",
            NULL, HELP_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            HELP_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(0),
            HELP_COMMAND_REGEX_GROUPS, _NUM_SUBCMDS(0), cmdfunc_help,
            NULL);

    ADD_CMD(help);

    struct dbg_cmd_t *kill = create_parent_cmd("kill",
            NULL, KILL_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(0), cmdfunc_kill,
            audit_kill);

    ADD_CMD(kill);

    struct dbg_cmd_t *memory = create_parent_cmd("memory",
            NULL, MEMORY_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(2), NULL, NULL);
    {
        struct dbg_cmd_t *find = create_child_cmd("find",
                NULL, MEMORY_FIND_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                MEMORY_FIND_COMMAND_REGEX, _NUM_GROUPS(4), _UNK_ARGS(0),
                MEMORY_FIND_COMMAND_REGEX_GROUPS, cmdfunc_memory_find,
                audit_memory_find);
        struct dbg_cmd_t *write = create_child_cmd("write",
                NULL, MEMORY_WRITE_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                MEMORY_WRITE_COMMAND_REGEX, _NUM_GROUPS(3), _UNK_ARGS(0),
                MEMORY_WRITE_COMMAND_REGEX_GROUPS, cmdfunc_memory_write,
                audit_memory_write);

        memory->subcmds[0] = find;
        memory->subcmds[1] = write;
    }

    ADD_CMD(memory);

    struct dbg_cmd_t *interrupt = create_parent_cmd("interrupt",
            NULL, INTERRUPT_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(0), cmdfunc_interrupt,
            NULL);

    ADD_CMD(interrupt);

    struct dbg_cmd_t *quit = create_parent_cmd("quit",
            "q", QUIT_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(0), cmdfunc_quit,
            NULL);

    ADD_CMD(quit);

    struct dbg_cmd_t *_register = create_parent_cmd("register",
            NULL, REGISTER_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(2), NULL, NULL);
    {
        struct dbg_cmd_t *view = create_child_cmd("view",
                NULL, REGISTER_VIEW_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                REGISTER_VIEW_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(1),
                REGISTER_VIEW_COMMAND_REGEX_GROUPS, cmdfunc_register_view,
                audit_register_view);
        struct dbg_cmd_t *write = create_child_cmd("write",
                NULL, REGISTER_WRITE_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                REGISTER_WRITE_COMMAND_REGEX, _NUM_GROUPS(2), _UNK_ARGS(0),
                REGISTER_WRITE_COMMAND_REGEX_GROUPS, cmdfunc_register_write,
                audit_register_write);

        _register->subcmds[0] = view;
        _register->subcmds[1] = write;
    }

    ADD_CMD(_register);

    struct dbg_cmd_t *signal = create_parent_cmd("signal",
            NULL, SIGNAL_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(2), NULL, NULL);
    {
        struct dbg_cmd_t *deliver = create_child_cmd("deliver",
                NULL, SIGNAL_DELIVER_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                SIGNAL_DELIVER_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(0),
                SIGNAL_DELIVER_COMMAND_REGEX_GROUPS, cmdfunc_signal_deliver,
                audit_signal_deliver);
        struct dbg_cmd_t *handle = create_child_cmd("handle",
                NULL, SIGNAL_HANDLE_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                SIGNAL_HANDLE_COMMAND_REGEX, _NUM_GROUPS(4), _UNK_ARGS(0),
                SIGNAL_HANDLE_COMMAND_REGEX_GROUPS, cmdfunc_signal_handle,
                NULL);

        signal->subcmds[0] = deliver;
        signal->subcmds[1] = handle;
    }

    ADD_CMD(signal);

    struct dbg_cmd_t *step = create_parent_cmd("step",
            NULL, STEP_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(2), NULL, NULL);
    {
        struct dbg_cmd_t *inst_into = create_child_cmd("inst-into",
                NULL, STEP_INST_INTO_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
                NO_GROUPS, cmdfunc_step_inst_into,
                audit_step_inst_into);
        struct dbg_cmd_t *inst_over = create_child_cmd("inst-over",
                NULL, STEP_INST_OVER_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
                NO_GROUPS, cmdfunc_step_inst_over,
                audit_step_inst_over);

        step->subcmds[0] = inst_into;
        step->subcmds[1] = inst_over;
    }

    ADD_CMD(step);

    struct dbg_cmd_t *thread = create_parent_cmd("thread",
            NULL, THREAD_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(2), NULL, NULL);
    {
        struct dbg_cmd_t *list = create_child_cmd("list",
                NULL, THREAD_LIST_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
                NO_GROUPS, cmdfunc_thread_list,
                audit_thread_list);
        struct dbg_cmd_t *select = create_child_cmd("select",
                NULL, THREAD_SELECT_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                THREAD_SELECT_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(0),
                THREAD_SELECT_COMMAND_REGEX_GROUPS, cmdfunc_thread_select,
                audit_thread_select);

        thread->subcmds[0] = list;
        thread->subcmds[1] = select;
    }

    ADD_CMD(thread);

    struct dbg_cmd_t *trace = create_parent_cmd("trace",
            NULL, TRACE_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(0), cmdfunc_trace,
            NULL);

    ADD_CMD(trace);

    struct dbg_cmd_t *variable = create_parent_cmd("variable",
            NULL, VARIABLE_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(3), NULL, NULL);
    {
        struct dbg_cmd_t *print = create_child_cmd("print",
                NULL, VARIABLE_PRINT_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                VARIABLE_PRINT_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(1),
                VARIABLE_PRINT_COMMAND_REGEX_GROUPS, cmdfunc_variable_print,
                NULL);
        struct dbg_cmd_t *set = create_child_cmd("set",
                NULL, VARIABLE_SET_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                VARIABLE_SET_COMMAND_REGEX, _NUM_GROUPS(2), _UNK_ARGS(0),
                VARIABLE_SET_COMMAND_REGEX_GROUPS, cmdfunc_variable_set,
                NULL);
        struct dbg_cmd_t *unset = create_child_cmd("unset",
                NULL, VARIABLE_UNSET_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                VARIABLE_UNSET_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(1),
                VARIABLE_UNSET_COMMAND_REGEX_GROUPS, cmdfunc_variable_unset,
                NULL);

        variable->subcmds[0] = print;
        variable->subcmds[1] = set;
        variable->subcmds[2] = unset;
    }

    ADD_CMD(variable);

    struct dbg_cmd_t *watchpoint = create_parent_cmd("watchpoint",
            "w", WATCHPOINT_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(3), NULL, NULL);
    {
        struct dbg_cmd_t *delete = create_child_cmd("delete",
                NULL, WATCHPOINT_DELETE_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                WATCHPOINT_DELETE_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(1),
                WATCHPOINT_DELETE_COMMAND_REGEX_GROUPS, cmdfunc_watchpoint_delete,
                NULL);
        struct dbg_cmd_t *list = create_child_cmd("list",
                NULL, WATCHPOINT_LIST_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
                NO_GROUPS, cmdfunc_watchpoint_list,
                NULL);
        struct dbg_cmd_t *set = create_child_cmd("set",
                NULL, WATCHPOINT_SET_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                WATCHPOINT_SET_COMMAND_REGEX, _NUM_GROUPS(4), _UNK_ARGS(0),
                WATCHPOINT_SET_COMMAND_REGEX_GROUPS, cmdfunc_watchpoint_set,
                audit_watchpoint_set);

        watchpoint->subcmds[0] = delete;
        watchpoint->subcmds[1] = list;
        watchpoint->subcmds[2] = set;
    }

    ADD_CMD(watchpoint);
}

void load_history(void){
    char *home = getenv("HOME");

    if(!home){
        printf("warning: HOME not found in environment list\n"
                "cannot load command history\n");
        return;
    }

    concat(&HISTORY_PATH, "%s/.iosdbg/iosdbg-history", home);

    if(read_history(HISTORY_PATH)){
        int fd = creat(HISTORY_PATH, 0644);
        close(fd);
    }

    FILE *histfp = fopen(HISTORY_PATH, "r");

    int numlines = 0;
    char ch;

    while((ch = fgetc(histfp)) != EOF){
        if(ch == '\n' || ch == '\r')
            numlines++;
    }

    long int sz = ftell(histfp);
    fclose(histfp);

    /* Figure out if we need to resize the file. A 1mb limit seems fine.
     * Since we still want to keep recent history, leave the most recent half.
     */
    const int limit = 1048576;

    if(sz >= limit)
        history_truncate_file(HISTORY_PATH, numlines/2);
}

enum cmd_error_t do_cmdline_command(char *user_command_,
        char **expanded_command, int user_invoked, int *force_show_outbuffer,
        char **outbuffer, char **error){
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
        char *exit_reason = NULL;
        char *shell_cmd = usercommandcpy + 1;
        execute_shell_cmd(shell_cmd, &exit_reason, error);

        if(exit_reason)
            concat(outbuffer, "%s\n", exit_reason);
        
        free(exit_reason);

        if(*error)
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
        char *token = strdup(tokens[i]);
        char *rstr = strnran(RAND_PAD_LEN);

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
        result = prepare_and_call_cmdfunc(arguments, outbuffer,
                force_show_outbuffer, error);
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
