#include <pthread/pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "convvar.h"
#include "dbgops.h"
#include "debuggee.h"
#include "handlers.h"
#include "linkedlist.h"
#include "memutils.h"
#include "rlext.h"
#include "sigsupport.h"
#include "strext.h"
#include "thread.h"
#include "trace.h"

#include "cmd/audit.h"
#include "cmd/bpcmd.h"
#include "cmd/cmd.h"
#include "cmd/completer.h"
#include "cmd/misccmd.h"
#include "cmd/memcmd.h"
#include "cmd/regcmd.h"
#include "cmd/sigcmd.h"
#include "cmd/tcmd.h"
#include "cmd/wpcmd.h"
#include "cmd/varcmd.h"

struct debuggee *debuggee;

char **bsd_syscalls;
char **mach_traps;
char **mach_messages;

int bsd_syscalls_arr_len;
int mach_traps_arr_len;
int mach_messages_arr_len;

pthread_mutex_t REPROMPT_MUTEX = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t HAS_REPLIED_MUTEX = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t DEATH_SERVER_DETACHED_MUTEX  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t EXCEPTION_SERVER_IS_DETACHING_MUTEX = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t REPROMPT_COND = PTHREAD_COND_INITIALIZER;
pthread_cond_t MAIN_THREAD_CHANGED_REPLIED_VAR_COND = PTHREAD_COND_INITIALIZER;
pthread_cond_t DEATH_SERVER_DETACHED_COND = PTHREAD_COND_INITIALIZER;
pthread_cond_t EXCEPTION_SERVER_IS_DETACHING_COND = PTHREAD_COND_INITIALIZER;
pthread_cond_t IS_DONE_HANDLING_EXCEPTIONS_BEFORE_DETACH_COND = PTHREAD_COND_INITIALIZER;
pthread_cond_t WAIT_TO_SIGNAL_EXCEPTION_SERVER_IS_DETACHING_COND = PTHREAD_COND_INITIALIZER;

int HAS_REPLIED_TO_LATEST_EXCEPTION = 0;
int HANDLING_EXCEPTION = 0;

static void interrupt(int x1){
    if(KEEP_CHECKING_FOR_PROCESS)
        printf("\n");

    KEEP_CHECKING_FOR_PROCESS = 0;

    stop_trace();

    if(debuggee->pid != -1)
        kill(debuggee->pid, SIGSTOP);
}

static void install_handlers(void){
    debuggee->find_slide = &find_slide;
    debuggee->restore_exception_ports = &restore_exception_ports;
    debuggee->resume = &resume;
    debuggee->setup_exception_handling = &setup_exception_handling;
    debuggee->deallocate_ports = &deallocate_ports;
    debuggee->suspend = &suspend;
    debuggee->update_threads = &update_threads;
}

static int _rl_getc(FILE *stream){
    int gotc = rl_getc(stream);

    if(gotc == '\t'){
        int len = 0;
        char **tokens = rl_line_buffer_word_array(&len);

        if(len >= 1){
            char **completions = completer(tokens[0], 0, strlen(tokens[0]));

            if(completions && strcmp(*completions, "help") == 0)
                IS_HELP_COMMAND = 1;
        }

        token_array_free(tokens, len);
    }
    else if(gotc == '\r'){
        IS_HELP_COMMAND = 0;
    }

    return gotc;
}

static void initialize_readline(void){
    rl_catch_signals = 0;

    rl_attempted_completion_function = completer;

    /* "Enable" command completion for the help command
     * by checking for it in rl_line_buffer every time
     * the user types something.
     */
    rl_getc_function = _rl_getc;
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

    debuggee->num_breakpoints = 0;
    debuggee->num_watchpoints = 0;

    debuggee->last_hit_bkpt_ID = 0;

    debuggee->is_single_stepping = 0;

    debuggee->want_detach = 0;

    debuggee->tracing_disabled = 0;
    debuggee->currently_tracing = 0;

    debuggee->pending_exceptions = 0;

    /* Figure out how many hardware breakpoints/watchpoints are supported. */
    size_t len = sizeof(int);

    sysctlbyname("hw.optional.breakpoint", &debuggee->num_hw_bps,
            &len, NULL, 0);
    
    len = sizeof(int);

    sysctlbyname("hw.optional.watchpoint", &debuggee->num_hw_wps,
            &len, NULL, 0);

    /* Create some iosdbg managed convenience variableiables. */
    char *error = NULL;

    set_convvar("$_", "", &error);
    set_convvar("$__", "", &error);
    set_convvar("$_exitcode", "", &error);
    set_convvar("$_exitsignal", "", &error);
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
                BREAKPOINT_SET_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(1),
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
            NO_GROUPS, _NUM_SUBCMDS(3), NULL, NULL);
    {
        struct dbg_cmd_t *_float = create_child_cmd("float",
                NULL, REGISTER_FLOAT_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                REGISTER_FLOAT_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(1),
                REGISTER_FLOAT_COMMAND_REGEX_GROUPS, cmdfunc_register_float,
                audit_register_float);
        struct dbg_cmd_t *gen = create_child_cmd("gen",
                NULL, REGISTER_GEN_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                REGISTER_GEN_COMMAND_REGEX, _NUM_GROUPS(1), _UNK_ARGS(1),
                REGISTER_GEN_COMMAND_REGEX_GROUPS, cmdfunc_register_gen,
                audit_register_gen);
        struct dbg_cmd_t *write = create_child_cmd("write",
                NULL, REGISTER_WRITE_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                REGISTER_WRITE_COMMAND_REGEX, _NUM_GROUPS(2), _UNK_ARGS(0),
                REGISTER_WRITE_COMMAND_REGEX_GROUPS, cmdfunc_register_write,
                audit_register_write);

        _register->subcmds[0] = _float;
        _register->subcmds[1] = gen;
        _register->subcmds[2] = write;
    }

    ADD_CMD(_register);

    struct dbg_cmd_t *signal = create_parent_cmd("signal",
            NULL, SIGNAL_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(1), NULL, NULL);
    {
        struct dbg_cmd_t *handle = create_child_cmd("handle",
                NULL, SIGNAL_HANDLE_COMMAND_DOCUMENTATION, _AT_LEVEL(1),
                SIGNAL_HANDLE_COMMAND_REGEX, _NUM_GROUPS(4), _UNK_ARGS(0),
                SIGNAL_HANDLE_COMMAND_REGEX_GROUPS, cmdfunc_signal_handle,
                NULL);

        signal->subcmds[0] = handle;
    }

    ADD_CMD(signal);

    struct dbg_cmd_t *stepi = create_parent_cmd("stepi",
            NULL, STEPI_COMMAND_DOCUMENTATION, _AT_LEVEL(0),
            NO_ARGUMENT_REGEX, _NUM_GROUPS(0), _UNK_ARGS(0),
            NO_GROUPS, _NUM_SUBCMDS(0), cmdfunc_stepi,
            audit_stepi);

    ADD_CMD(stepi);

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
                WATCHPOINT_SET_COMMAND_REGEX, _NUM_GROUPS(3), _UNK_ARGS(0),
                WATCHPOINT_SET_COMMAND_REGEX_GROUPS, cmdfunc_watchpoint_set,
                audit_watchpoint_set);

        watchpoint->subcmds[0] = delete;
        watchpoint->subcmds[1] = list;
        watchpoint->subcmds[2] = set;
    }

    ADD_CMD(watchpoint);
}

static void inputloop(void){
    char *line = NULL, *prevline = NULL;

    static const char *prompt = "\033[2m(iosdbg) \033[0m";

    while(1){
        pthread_mutex_lock(&HAS_REPLIED_MUTEX);

        while(HANDLING_EXCEPTION)
            pthread_cond_wait(&REPROMPT_COND, &HAS_REPLIED_MUTEX);

        pthread_mutex_unlock(&HAS_REPLIED_MUTEX);

        line = readline(prompt);

        if(!line)
            break;

        size_t linelen = strlen(line);

        /* If the user hits enter, repeat the last command,
         * and do not add to the command history if the length
         * of line is 0.
         */
        if(linelen == 0 && prevline){
            size_t prevlinelen = strlen(prevline);

            char *line_replacement = realloc(line, prevlinelen + 1);
            strncpy(line_replacement, prevline, prevlinelen + 1);

            line = line_replacement;
        }
        else if(linelen > 0 &&
                (!prevline || (prevline && strcmp(line, prevline) != 0))){
            add_history(line);
        }

        char *linecpy = NULL, *error = NULL;
        enum cmd_error_t result = do_cmdline_command(line, &linecpy, 1, &error);

        if(result == CMD_FAILURE && error){
            printf("error: %s\n", error);
            free(error);
        }
        else if(result == CMD_QUIT){
            if(linecpy)
                free(linecpy);

            if(prevline)
                free(prevline);

            if(error)
                free(error);

            free(line);

            return;
        }

        if(linecpy){
            size_t linecpylen = strlen(linecpy);
            char *prevline_replacement = realloc(prevline, linecpylen + 1);
            strncpy(prevline_replacement, linecpy, linecpylen + 1);
            free(linecpy);
            prevline = prevline_replacement;
        }

        free(line);
    }

    printf("readline returned NULL... please file an issue on Github\n");
}

static void early_configuration(void){
    /* By default, don't pass SIGINT and SIGTRAP to debuggee. */
    int notify = 1, pass = 0, stop = 1;
    char *error = NULL;

    sigsettings(SIGINT, &notify, &pass, &stop, 1, &error);

    if(error){
        printf("error: %s\n", error);
        free(error);
    }

    sigsettings(SIGTRAP, &notify, &pass, &stop, 1, &error);

    if(error){
        printf("error: %s\n", error);
        free(error);
    }
}

int main(int argc, char **argv, const char **envp){
    pthread_setname_np("iosdbg main thread");

    early_configuration();
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
            "Command name abbreviations are allowed if unambiguous.\n"
            "Type '!' before your input to execute a shell command.\n");

    inputloop();

    return 0;
}
