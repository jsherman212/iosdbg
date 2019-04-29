#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "convvar.h"
#include "dbgops.h"
#include "debuggee.h"
#include "handlers.h"
#include "linkedlist.h"
#include "memutils.h"
#include "printutils.h"
#include "sigsupport.h"
#include "strext.h"
#include "thread.h"
#include "trace.h"

#include "cmd/audit.h"
#include "cmd/bpcmd.h"
#include "cmd/completer.h"
#include "cmd/cmd.h"
#include "cmd/misccmd.h"
#include "cmd/memcmd.h"
#include "cmd/regcmd.h"
#include "cmd/sigcmd.h"
#include "cmd/tcmd.h"
#include "cmd/wpcmd.h"

struct debuggee *debuggee;

char **bsd_syscalls;
char **mach_traps;
char **mach_messages;

int bsd_syscalls_arr_len;
int mach_traps_arr_len;
int mach_messages_arr_len;

static void interrupt(int x1){
    if(keep_checking_for_process)
        printf("\n");

    keep_checking_for_process = 0;
    
    if(debuggee->pid != -1){
        if(debuggee->interrupted)
            return;

        kern_return_t err = debuggee->suspend();

        if(err){
            printf("Cannot suspend: %s\n", mach_error_string(err));
            debuggee->interrupted = 0;

            return;
        }

        debuggee->interrupted = 1;
    }

    stop_trace();
    
    if(debuggee->pid != -1){
        printf("\n");

        debuggee->get_thread_state();

        disassemble_at_location(debuggee->thread_state.__pc, 0x4);
        
        printf("%s stopped.\n", debuggee->debuggee_name);

        safe_reprompt();
    }
}

static void install_handlers(void){
    debuggee->find_slide = &find_slide;
    debuggee->restore_exception_ports = &restore_exception_ports;
    debuggee->resume = &resume;
    debuggee->setup_exception_handling = &setup_exception_handling;
    debuggee->deallocate_ports = &deallocate_ports;
    debuggee->suspend = &suspend;
    debuggee->update_threads = &update_threads;
    debuggee->get_debug_state = &get_debug_state;
    debuggee->set_debug_state = &set_debug_state;
    debuggee->get_thread_state = &get_thread_state;
    debuggee->set_thread_state = &set_thread_state;
    debuggee->get_neon_state = &get_neon_state;
    debuggee->set_neon_state = &set_neon_state;
}

static void initialize_readline(void){
    rl_catch_signals = 0;
    rl_erase_empty_line = 1;

    rl_attempted_completion_function = completer;
}

static void get_code_and_event_from_line(char *line,
        char **code, char **event, char **freethis){
    char *linecopy = strdup(line);
    size_t linelen = strlen(line);

    int idx = 0;

    while(idx < linelen && !isblank(line[idx]))
        idx++;

    linecopy[idx] = '\0';

    *code = linecopy;

    while(idx < linelen && isblank(line[idx]))
        idx++;

    *event = &linecopy[idx];

    /* Strip any whitespace from the end. */
    while(idx < linelen && !isblank(line[idx]))
        idx++;

    linecopy[idx] = '\0';
    
    *freethis = linecopy;
}

static int setup_tracing(void){
    FILE *tracecodes = fopen("/usr/share/misc/trace.codes", "r");

    if(!tracecodes){
        printf("Could not read /usr/share/misc/trace.codes."
                "Tracing is disabled.\n");
        
        debuggee->tracing_disabled = 1;

        return 1;
    }

    int largest_mach_msg_entry = 0;
    int curline = 0;

    /* For safety, allocate everything and set first element to NULL. */
    bsd_syscalls = malloc(sizeof(char *));
    mach_traps = malloc(sizeof(char *));
    mach_messages = malloc(sizeof(char *));

    bsd_syscalls[0] = NULL;
    mach_traps[0] = NULL;
    mach_messages[0] = NULL;

    char *line = NULL;
    size_t len;

    /* Get the sizes of each array before allocating so we can
     * set every element to NULL so there are no problems with freeing.
     */
    while(getline(&line, &len, tracecodes) != -1){
        line[strlen(line) - 1] = '\0';

        char *code = NULL, *event = NULL, *freethis = NULL;

        get_code_and_event_from_line(line, &code, &event, &freethis);

        unsigned long codenum = strtol(code, NULL, 16);

        if(strnstr(event, "BSC", 3)){
            int eventidx = (codenum & 0xfff) / 4;

            /* There's a couple more not following the
             * "increment by 4" code pattern.
             */
            if(codenum > 0x40c0824){
                eventidx = (codenum & ~0xff00000) / 4;

                bsd_syscalls = realloc(bsd_syscalls, sizeof(char *) *
                        (curline + eventidx));
            }
            else
                bsd_syscalls = realloc(bsd_syscalls, sizeof(char *) *
                        (curline + 1));

            bsd_syscalls_arr_len = eventidx;
        }
        else if(strnstr(event, "MSC", 3)){
            int eventidx = (codenum & 0xfff) / 4;

            mach_traps = realloc(mach_traps, sizeof(char *) * (curline + 1));

            mach_traps_arr_len = eventidx;
        }
        else if(strnstr(event, "MSG", 3)){
            int eventidx = (codenum & ~0xff000000) / 4;

            if(eventidx > largest_mach_msg_entry){
                int num_ptrs_to_allocate = eventidx - largest_mach_msg_entry;
                int cur_array_size = largest_mach_msg_entry;

                mach_messages = realloc(mach_messages, sizeof(char *) *
                        (cur_array_size + num_ptrs_to_allocate + 1));

                largest_mach_msg_entry = eventidx + 1;
            }

            mach_messages_arr_len = largest_mach_msg_entry;
        }

        free(freethis);

        curline++;
    }

    /* Set every element in each array to NULL. */
    for(int i=0; i<bsd_syscalls_arr_len; i++)
        bsd_syscalls[i] = NULL;

    for(int i=0; i<mach_traps_arr_len; i++)
        mach_traps[i] = NULL;

    for(int i=0; i<mach_messages_arr_len; i++)
        mach_messages[i] = NULL;

    rewind(tracecodes);

    /* Go again and fill up the array. */
    while(getline(&line, &len, tracecodes) != -1){
        line[strlen(line) - 1] = '\0';

        char *code = NULL, *event = NULL, *freethis = NULL;

        get_code_and_event_from_line(line, &code, &event, &freethis);

        unsigned long codenum = strtol(code, NULL, 16);

        if(strnstr(event, "BSC", 3)){
            int eventidx = (codenum & 0xfff) / 4;

            if(codenum > 0x40c0824)
                eventidx = (codenum & ~0xff00000) / 4;

            /* Get rid of the prefix. */
            bsd_syscalls[eventidx] = malloc(strlen(event + 4) + 1);
            strcpy(bsd_syscalls[eventidx], event + 4);
        }
        else if(strnstr(event, "MSC", 3)){
            int eventidx = (codenum & 0xfff) / 4;

            mach_traps[eventidx] = malloc(strlen(event + 4) + 1);
            strcpy(mach_traps[eventidx], event + 4);
        }
        else if(strnstr(event, "MSG", 3)){
            int eventidx = (codenum & ~0xff000000) / 4;

            mach_messages[eventidx] = malloc(strlen(event + 4) + 1);
            strcpy(mach_messages[eventidx], event + 4);
        }

        free(freethis);
    }

    if(line)
        free(line);

    fclose(tracecodes);

    return 0;
}

static void setup_initial_debuggee(void){
    debuggee = malloc(sizeof(struct debuggee));

    /* If we aren't attached to anything, debuggee's pid is -1. */
    debuggee->pid = -1;
    debuggee->interrupted = 0;
    debuggee->breakpoints = linkedlist_new();
    debuggee->watchpoints = linkedlist_new();
    debuggee->threads = linkedlist_new();

    debuggee->num_breakpoints = 0;
    debuggee->num_watchpoints = 0;

    debuggee->last_hit_bkpt_ID = 0;

    debuggee->is_single_stepping = 0;

    debuggee->want_detach = 0;

    debuggee->tracing_disabled = 0;
    debuggee->currently_tracing = 0;

    debuggee->pending_messages = 0;

    /* Figure out how many hardware breakpoints/watchpoints are supported. */
    size_t len = sizeof(int);

    sysctlbyname("hw.optional.breakpoint", &debuggee->num_hw_bps,
            &len, NULL, 0);
    
    len = sizeof(int);

    sysctlbyname("hw.optional.watchpoint", &debuggee->num_hw_wps,
            &len, NULL, 0);

    /* Create some iosdbg managed convenience variables. */
    char *error = NULL;

    set_convvar("$_", "", &error);
    set_convvar("$__", "", &error);
    set_convvar("$_exitcode", "", &error);
    set_convvar("$_exitsignal", "", &error);

    /* The user can set this so iosdbg never adds ASLR. */
    set_convvar("$NO_ASLR_OVERRIDE", "", &error);
}

static inline void threadupdate(void){
    if(debuggee->pid != -1)
        ops_threadupdate();
}

static struct dbg_cmd_t *common_initialization(const char *name,
        const char *alias, const char *documentation, int level,
        const char *argregex, int num_groups, int unk_num_args,
        const char *groupnames[MAX_GROUPS],
        enum cmd_error_t (*cmd_function)(struct cmd_args_t *, int, char **),
        void (*audit_function)(struct cmd_args_t *, char **)){
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
        enum cmd_error_t (*cmd_function)(struct cmd_args_t *, int, char **),
        void (*audit_function)(struct cmd_args_t *, char **)){
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
        enum cmd_error_t (*cmd_function)(struct cmd_args_t *, int, char **),
        void (*audit_function)(struct cmd_args_t *, char **)){
    struct dbg_cmd_t *c = common_initialization(name,
            alias, documentation, level, argregex, num_groups,
            unk_num_args, groupnames, cmd_function, audit_function);

    c->parentcmd = 0;
    c->subcmds = NULL;

    return c;
}

static void initialize_commands(void){
    int cmdidx = 0;

#define ADD_CMD(x) COMMANDS[cmdidx++] = x

    struct dbg_cmd_t *aslr = create_parent_cmd("aslr",
            NULL, ASLR_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 0, cmdfunc_aslr,
            audit_aslr);

    ADD_CMD(aslr);

    struct dbg_cmd_t *attach = create_parent_cmd("attach",
            NULL, ATTACH_COMMAND_DOCUMENTATION, 0,
            ATTACH_COMMAND_REGEX, 2, 0,
            ATTACH_COMMAND_REGEX_GROUPS, 0, cmdfunc_attach,
            audit_attach);

    ADD_CMD(attach);

    struct dbg_cmd_t *backtrace = create_parent_cmd("backtrace",
            "bt", BACKTRACE_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 0, cmdfunc_backtrace,
            audit_backtrace);

    ADD_CMD(backtrace);

    struct dbg_cmd_t *breakpoint = create_parent_cmd("break",
            "b", BREAKPOINT_COMMAND_DOCUMENTATION, 0,
            BREAKPOINT_COMMAND_REGEX, 1, 1,
            BREAKPOINT_COMMAND_REGEX_GROUPS, 0, cmdfunc_break,
            audit_break);

    ADD_CMD(breakpoint);

    struct dbg_cmd_t *continue_ = create_parent_cmd("continue",
            "c", CONTINUE_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 0, cmdfunc_continue,
            audit_continue);

    ADD_CMD(continue_);

    struct dbg_cmd_t *delete = create_parent_cmd("delete",
            "d", DELETE_COMMAND_DOCUMENTATION, 0,
            DELETE_COMMAND_REGEX, 2, 1,
            DELETE_COMMAND_REGEX_GROUPS, 0, cmdfunc_delete,
            audit_delete);

    ADD_CMD(delete);

    struct dbg_cmd_t *detach = create_parent_cmd("detach",
            NULL, DETACH_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 0, cmdfunc_detach,
            audit_detach);

    ADD_CMD(detach);

    struct dbg_cmd_t *disassemble = create_parent_cmd("disassemble",
            "dis", DISASSEMBLE_COMMAND_DOCUMENTATION, 0,
            DISASSEMBLE_COMMAND_REGEX, 2, 0,
            DISASSEMBLE_COMMAND_REGEX_GROUPS, 0, cmdfunc_disassemble,
            audit_disassemble);

    ADD_CMD(disassemble);

    struct dbg_cmd_t *examine = create_parent_cmd("examine",
            "x", EXAMINE_COMMAND_DOCUMENTATION, 0,
            EXAMINE_COMMAND_REGEX, 2, 0,
            EXAMINE_COMMAND_REGEX_GROUPS, 0, cmdfunc_examine,
            audit_examine);

    ADD_CMD(examine);

    struct dbg_cmd_t *help = create_parent_cmd("help",
            NULL, HELP_COMMAND_DOCUMENTATION, 0,
            HELP_COMMAND_REGEX, 1, 0,
            HELP_COMMAND_REGEX_GROUPS, 0, cmdfunc_help,
            audit_help);

    ADD_CMD(help);

    struct dbg_cmd_t *kill = create_parent_cmd("kill",
            NULL, KILL_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 0, cmdfunc_kill,
            audit_kill);

    ADD_CMD(kill);

    struct dbg_cmd_t *memory = create_parent_cmd("memory",
            NULL, MEMORY_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 1, NULL, NULL);
    
    struct dbg_cmd_t *memory_find = create_child_cmd("find",
            NULL, MEMORY_FIND_COMMAND_DOCUMENTATION, 1,
            "", 0, 0,
            NO_GROUPS, cmdfunc_memoryfind,
            audit_memory_find);

    memory->subcmds[0] = memory_find;
    
    ADD_CMD(memory);

    struct dbg_cmd_t *quit = create_parent_cmd("quit",
            "q", QUIT_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 0, cmdfunc_quit,
            audit_quit);

    ADD_CMD(quit);

    struct dbg_cmd_t *regs = create_parent_cmd("regs",
            NULL, REGS_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 2, NULL, NULL);

    struct dbg_cmd_t *regs_float = create_child_cmd("float",
            NULL, REGS_FLOAT_COMMAND_DOCUMENTATION, 1,
            REGS_FLOAT_COMMAND_REGEX, 1, 1,
            REGS_FLOAT_COMMAND_REGEX_GROUPS, cmdfunc_regsfloat,
            audit_regs_float);
    struct dbg_cmd_t *regs_gen = create_child_cmd("gen",
            NULL, REGS_GEN_COMMAND_DOCUMENTATION, 1,
            REGS_GEN_COMMAND_REGEX, 1, 1,
            REGS_GEN_COMMAND_REGEX_GROUPS, cmdfunc_regsgen,
            audit_regs_gen);

    regs->subcmds[0] = regs_float;
    regs->subcmds[1] = regs_gen;

    ADD_CMD(regs);

    struct dbg_cmd_t *set = create_parent_cmd("set",
            NULL, SET_COMMAND_DOCUMENTATION, 0,
            SET_COMMAND_REGEX, 3, 0,
            SET_COMMAND_REGEX_GROUPS, 0, cmdfunc_set,
            audit_set);

    ADD_CMD(set);

    struct dbg_cmd_t *show = create_parent_cmd("show",
            NULL, SHOW_COMMAND_DOCUMENTATION, 0,
            SHOW_COMMAND_REGEX, 1, 1,
            SHOW_COMMAND_REGEX_GROUPS, 0, cmdfunc_show,
            audit_show);

    ADD_CMD(show);

    struct dbg_cmd_t *signal = create_parent_cmd("signal",
            NULL, SIGNAL_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 1, NULL, NULL);

    struct dbg_cmd_t *signal_handle = create_child_cmd("handle",
            NULL, SIGNAL_HANDLE_COMMAND_DOCUMENTATION, 1,
            SIGNAL_HANDLE_COMMAND_REGEX, 4, 0,
            SIGNAL_HANDLE_COMMAND_REGEX_GROUPS, cmdfunc_signalhandle,
            audit_signal_handle);

    signal->subcmds[0] = signal_handle;

    ADD_CMD(signal);

    struct dbg_cmd_t *stepi = create_parent_cmd("stepi",
            NULL, STEPI_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 0, cmdfunc_stepi,
            audit_stepi);

    ADD_CMD(stepi);

    struct dbg_cmd_t *thread = create_parent_cmd("thread",
            NULL, THREAD_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 2, NULL, NULL);

    struct dbg_cmd_t *list = create_child_cmd("list",
            NULL, THREAD_LIST_COMMAND_DOCUMENTATION, 1,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, cmdfunc_threadlist,
            audit_thread_list);
    struct dbg_cmd_t *select = create_child_cmd("select",
            NULL, THREAD_SELECT_COMMAND_DOCUMENTATION, 1,
            THREAD_SELECT_COMMAND_REGEX, 1, 0,
            THREAD_SELECT_COMMAND_REGEX_GROUPS, cmdfunc_threadselect,
            audit_thread_select);

    thread->subcmds[0] = list;
    thread->subcmds[1] = select;

    ADD_CMD(thread);

    struct dbg_cmd_t *trace = create_parent_cmd("trace",
            NULL, TRACE_COMMAND_DOCUMENTATION, 0,
            NO_ARGUMENT_REGEX, 0, 0,
            NO_GROUPS, 0, cmdfunc_trace,
            audit_trace);

    ADD_CMD(trace);

    struct dbg_cmd_t *unset = create_parent_cmd("unset",
            NULL, UNSET_COMMAND_DOCUMENTATION, 0,
            UNSET_COMMAND_REGEX, 1, 1,
            UNSET_COMMAND_REGEX_GROUPS, 0, cmdfunc_unset,
            audit_unset);

    ADD_CMD(unset);

    struct dbg_cmd_t *watch = create_parent_cmd("watch",
            "w", WATCH_COMMAND_DOCUMENTATION, 0,
            WATCH_COMMAND_REGEX, 3, 0,
            WATCH_COMMAND_REGEX_GROUPS, 0, cmdfunc_watch,
            audit_watch);

    ADD_CMD(watch);
}

static void _rl_line_buffer_replace(char *with){
    rl_delete_text(0, rl_end);
    rl_point = rl_end = rl_mark = 0;
    rl_insert_text(with);
}

static void expand_aliases(char **line){
    /* Only top level commands (level 0) can have aliases,
     * so we only need to test the first "word".
     */
    char *space = strchr(*line, ' ');
    int bytes_until_space = 0;
    char *token = NULL;

    if(space){
        bytes_until_space = space - (*line);
        token = substr(*line, 0, bytes_until_space);
    }
    else
        token = strdup(*line);

    /* Check for an alias. */
    for(int i=0; i<NUM_TOP_LEVEL_COMMANDS; i++){
        struct dbg_cmd_t *current = COMMANDS[i];

        if(current->alias && strcmp(current->alias, token) == 0){
            /* Replace the alias with its command. */
            strcut(line, 0, bytes_until_space);
            strins(line, current->name, 0);

            _rl_line_buffer_replace(*line);

            free(token);

            return;
        }
    }

    free(token);
}

static void inputloop(void){
    char *line = NULL;
    char *prevline = NULL;

    static const char *prompt = "\e[2m(iosdbg) \e[0m";

    while((line = readline(prompt)) != NULL){
        /* If the user hits enter, repeat the last command,
         * and do not add to the command history if the length
         * of line is 0.
         */
        if(strlen(line) == 0 && prevline){
            line = realloc(line, strlen(prevline) + 1);
            strncpy(line, prevline, strlen(prevline) + 1);
        }
        else if(strlen(line) > 0 &&
                (!prevline || (prevline && strcmp(line, prevline) != 0))){
            add_history(line);
        }

        strclean(&line);

        /* If the user hits enter right away, rl_line_buffer will be empty.
         * The completer relies on rl_line_buffer reflecting the
         * contents of line, so make that so.
         */
        _rl_line_buffer_replace(line);

        char *linecpy = strdup(line);
        
        expand_aliases(&line);

        int current_start = 0;

        char *tok = strtok_r(line, " ", &line);
        char *arguments = strdup("");

        char **prev_completions = NULL;
        char **completions = NULL;

        int first_match = 1;

        while(tok){
            size_t toklen = strlen(tok);

            /* Force matching to figure out the command. */
            completions = completer(tok, current_start, toklen);

            /* If we don't get a match the first time around,
             * it's an unknown command.
             */
            if(first_match && !completions){
                printf("Unknown command \"%s\".  Try \"help\".\n", linecpy);
                
                /* Keep going so we can free this string. */
                while(tok)
                    tok = strtok_r(NULL, " ", &line);

                break;
            }

            first_match = 0;

            /* If there were no matches, we can assume the following
             * tokens are arguments.
             */
            if(!completions){
                while(tok){
                    concat(&arguments, " %s", tok);
                    tok = strtok_r(NULL, " ", &line);
                }

                /* Get rid of leading/training whitespace that
                 * would mess up regex matching.
                 */
                strclean(&arguments);

                break;
            }

            current_start += toklen;
            tok = strtok_r(NULL, " ", &line);

            prev_completions = completions;
        }

        /* We need to test the previous completions in case
         * this command is a parent command.
         */
        if(prev_completions && *(prev_completions + 1)){
            char *ambiguous_cmd_str = NULL;
            asprintf(&ambiguous_cmd_str, "Ambiguous command \"%s\": ",
                    linecpy);

            for(int i=1; prev_completions[i]; i++)
                concat(&ambiguous_cmd_str, "%s, ", prev_completions[i]);

            /* Get rid of the trailing comma. */
            ambiguous_cmd_str[strlen(ambiguous_cmd_str) - 2] = '\0';

            printf("%s\n", ambiguous_cmd_str);

            free(ambiguous_cmd_str);
        }
        else{
            char *error = NULL;

            /* Parse arguments, audit them, and if nothing went wrong,
             * call this command's cmdfunc.
             */
            enum cmd_error_t result =
                prepare_and_call_cmdfunc(arguments, &error);

            if(result && error){
                printf("error: %s\n", error);
                free(error);
            }
        }

        free(arguments);

        size_t linecpylen = strlen(linecpy);
        prevline = realloc(prevline, linecpylen + 1);
        strncpy(prevline, linecpy, linecpylen + 1);

        free(linecpy);
        free(line);
    }
}

int main(int argc, char **argv, const char **envp){
    if(getuid() && geteuid()){
        printf("iosdbg requires root to operate correctly\n");
        return 1;
    }

    /* By default, don't pass SIGINT and SIGTRAP to debuggee. */
    int notify = 1, pass = 0, stop = 1;
    char *error = NULL;

    sigsettings(SIGINT, &notify, &pass, &stop, 1, &error);

    if(error){
        printf("error: %s\n", error);
        free(error);
        return 1;
    }

    sigsettings(SIGTRAP, &notify, &pass, &stop, 1, &error);

    if(error){
        printf("error: %s\n", error);
        free(error);
        return 1;
    }

    setup_initial_debuggee();
    install_handlers();
    initialize_readline();
    initialize_commands();

    signal(SIGINT, interrupt);
    
    bsd_syscalls = NULL;
    mach_traps = NULL;
    mach_messages = NULL;

    bsd_syscalls_arr_len = 0;
    mach_traps_arr_len = 0;
    mach_messages_arr_len = 0;

    int err = setup_tracing();

    if(err)
        printf("Could not setup for future tracing. Tracing is disabled.\n");

    printf("For help, type \"help\".\n"
            "Command name abbreviations are allowed if unambiguous.\n");

    inputloop();

    return 0;
}
