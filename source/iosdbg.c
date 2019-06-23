#include <errno.h>
#include <pthread/pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "convvar.h"
#include "dbgops.h"
#include "debuggee.h"
#include "handlers.h"
#include "linkedlist.h"
#include "memutils.h"
#include "printing.h"
#include "rlext.h"
#include "sigsupport.h"
#include "strext.h"
#include "thread.h"
#include "trace.h"

#include "cmd/cmd.h"        /* For initialize_commands & do_cmdline_command */
#include "cmd/completer.h"  /* For IS_HELP_COMMAND */
#include "cmd/misccmd.h"    /* For KEEP_CHECKING_FOR_PROCESS */

struct debuggee *debuggee = NULL;

char **bsd_syscalls = NULL, **mach_traps = NULL, **mach_messages = NULL;
int bsd_syscalls_arr_len = 0, mach_traps_arr_len = 0, mach_messages_arr_len = 0;

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
    debuggee->get_threads = &get_threads;
    debuggee->suspended = &suspended;
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

            char **bsd_syscalls_rea;

            /* There's a couple more not following the
             * "increment by 4" code pattern.
             */
            if(codenum > 0x40c0824){
                eventidx = (codenum & ~0xff00000) / 4;

                bsd_syscalls_rea = realloc(bsd_syscalls, sizeof(char *) *
                        (curline + eventidx));
            }
            else{
                bsd_syscalls_rea = realloc(bsd_syscalls, sizeof(char *) *
                        (curline + 1));
            }

            bsd_syscalls = bsd_syscalls_rea;
            bsd_syscalls_arr_len = eventidx;
        }
        else if(strnstr(event, "MSC", 3)){
            int eventidx = (codenum & 0xfff) / 4;

            char **mach_traps_rea = realloc(mach_traps,
                    sizeof(char *) * (curline + 1));
            
            mach_traps = mach_traps_rea;
            mach_traps_arr_len = eventidx;
        }
        else if(strnstr(event, "MSG", 3)){
            int eventidx = (codenum & ~0xff000000) / 4;

            if(eventidx > largest_mach_msg_entry){
                int num_ptrs_to_allocate = eventidx - largest_mach_msg_entry;
                int cur_array_size = largest_mach_msg_entry;

                char **mach_messages_rea = realloc(mach_messages, sizeof(char *) *
                        (cur_array_size + num_ptrs_to_allocate + 1));

                mach_messages = mach_messages_rea;
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

    free(line);

    fclose(tracecodes);

    return 0;
}

static void setup_initial_debuggee(void){
    debuggee = malloc(sizeof(struct debuggee));

    /* If we aren't attached to anything, debuggee's pid is -1. */
    debuggee->pid = -1;

    debuggee->num_breakpoints = 0;
    debuggee->num_watchpoints = 0;
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

static int QUIT = 0;

static void linecb(char *line){
    static char *prevline = NULL;

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
            (!prevline || (prevline && strcmp(line, prevline) != 0)) &&
            !is_whitespace(line)){
        add_history(line);

        if(IOSDBG_HISTORY){
            fwrite(line, sizeof(char), linelen, IOSDBG_HISTORY);
            fputc('\n', IOSDBG_HISTORY);
            fflush(IOSDBG_HISTORY);
        }
    }

    int force_show_outbuffer = 0;
    char *outbuffer = NULL, *linecpy = NULL, *error = NULL;
    enum cmd_error_t result = do_cmdline_command(line,
            &linecpy, 1, &force_show_outbuffer, &outbuffer, &error);

    if(force_show_outbuffer){
        rl_printf(MAIN_THREAD, "%s", outbuffer);
        free(outbuffer);
        outbuffer = NULL;
    }

    if(result == CMD_FAILURE && error){
        rl_printf(MAIN_THREAD, "error: %s\n", error);
        free(error);
    }
    else{
        if(outbuffer){
            rl_printf(MAIN_THREAD, "%s", outbuffer);
            free(outbuffer);
        }

        if(result == CMD_QUIT){
            free(line);
            free(linecpy);
            free(prevline);
            free(error);

            rl_callback_handler_remove();
            QUIT = 1;
     
            return;
        }
    }

    if(linecpy){
        size_t linecpylen = strlen(linecpy);
        char *prevline_replacement = realloc(prevline, linecpylen + 1);
        strncpy(prevline_replacement, linecpy, linecpylen + 1);
        prevline = prevline_replacement;
    }

    free(linecpy);
    free(line);
}

static inline long secs_to_usecs(double seconds){
    return seconds * 1e+6;
}

static void inputloop(void){
    static const char *prompt = "\033[2m(iosdbg) \033[0m";
    rl_callback_handler_install(prompt, linecb);

    while(1){
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fileno(rl_instream), &fds);

        struct timeval tv = {0};
        tv.tv_sec = 0;
        tv.tv_usec = secs_to_usecs(0.001);

        int ret = select(FD_SETSIZE, &fds, NULL, NULL, &tv);

        if(ret < 0){
            if(errno != EINTR){
                rl_callback_handler_remove();
                return;
            }

            continue;
        }

        if(FD_ISSET(fileno(rl_instream), &fds)){
            rl_callback_read_char();

            if(QUIT)
                return;
        }
        else if(ret == 0){
            /* select only times out when we are issuing a command. When issuing
             * commands, the prompt is visible, making this a good time to
             * wake up any other threads waiting to print to stdout.
             */
            notify_of_reprompt();
        }
    }
}

static void early_configuration(void){
    /* By default, don't pass SIGINT and SIGTRAP to debuggee. */
    int notify = 1, pass = 0, stop = 1;
    char *error = NULL;

    sigsettings(SIGINT, &notify, &pass, &stop, 1, &error);

    if(error){
        printf("error: %s\n", error);
        free(error);
        error = NULL;
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
    load_history();
    
    struct sigaction sa = {0};
    
    sa.sa_handler = interrupt;
    sa.sa_flags = 0;
    
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    
    int err = setup_tracing();

    if(err)
        printf("Could not setup for future tracing. Tracing is disabled.\n");

    printf("For help, type \"help\".\n"
            "Command name abbreviations are allowed if unambiguous.\n"
            "Type '!' before your input to execute a shell command.\n");

    inputloop();

    if(IOSDBG_HISTORY)
        fclose(IOSDBG_HISTORY);

    return 0;
}
