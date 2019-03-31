#include <stdio.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "convvar.h"
#include "defs.h"
#include "dbgcmd.h"
#include "handlers.h"
#include "linkedlist.h"
#include "machthread.h"
#include "memutils.h"
#include "printutils.h"
#include "sigsupport.h"
#include "trace.h"

struct debuggee *debuggee;

char **bsd_syscalls;
char **mach_traps;
char **mach_messages;

int bsd_syscalls_arr_len;
int mach_traps_arr_len;
int mach_messages_arr_len;

void interrupt(int x1){
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

void install_handlers(void){
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

int reset_colors_hook(void){
    printf("\e[0m");

    return 0;
}

void initialize_readline(void){
    rl_catch_signals = 0;
    rl_erase_empty_line = 1;
    
    /* Our prompt is colored, so we need to reset colors
     * when Readline is ready for input.
     */
    rl_pre_input_hook = &reset_colors_hook;
    
    /* rl_event_hook is used to reset colors on SIGINT and
     * enter button press to repeat the previous command.
     */ 
    rl_event_hook = &reset_colors_hook;
    rl_input_available_hook = &reset_colors_hook;
}

void get_code_and_event_from_line(char *line, char **code, char **event,
        char **freethis){
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

int setup_tracing(void){
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

void setup_initial_debuggee(void){
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

void threadupdate(void){
    if(debuggee->pid != -1){
        thread_act_port_array_t threads;
        debuggee->update_threads(&threads);
        
        machthread_updatethreads(threads);

        struct machthread *focused = machthread_getfocused();

        if(!focused){
            printf("[Previously selected thread dead, selecting thread #1]\n\n");
            machthread_setfocused(threads[0]);
            focused = machthread_getfocused();
        }

        if(focused)
            machthread_updatestate(focused);
    }
}

void runmainloop(void){
    char *line = NULL;
    char *prevline = NULL;
    
    while((line = readline(prompt)) != NULL){
        /* If the user hits enter, repeat the last command,
         * and do not add to the command history if the length
         * of line is 0.
         */
        if(strlen(line) == 0 && prevline){
            line = realloc(line, strlen(prevline) + 1);
            strcpy(line, prevline);
        }
        else if(strlen(line) > 0 &&
                (!prevline || (prevline && strcmp(line, prevline) != 0))){
            add_history(line);
        }
        
        threadupdate();

        char *error = NULL;
        int result = execute_command(line, &error);

        if(result && error){
            printf("error: %s\n", error);
            free(error);
        }

        prevline = realloc(prevline, strlen(line) + 1);
        strcpy(prevline, line);

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
    
    bsd_syscalls = NULL;
    mach_traps = NULL;
    mach_messages = NULL;

    bsd_syscalls_arr_len = 0;
    mach_traps_arr_len = 0;
    mach_messages_arr_len = 0;

    int err = setup_tracing();

    if(err)
        printf("Could not setup for future tracing. Tracing is disabled.\n");
    
    signal(SIGINT, interrupt);


    runmainloop();

    return 0;
}
