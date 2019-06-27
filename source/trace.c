#include <pthread/pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <readline/readline.h>

#include "debuggee.h"
#include "strext.h"
#include "tarrays.h"
#include "trace.h"

static int stop = 0;
static int done_processing = 0;

static int initialize_ktrace_buffer(void){
    int mib[3];

    mib[0] = CTL_KERN;
    mib[1] = KERN_KDEBUG;
    mib[2] = KERN_KDSETUP;

    size_t needed = 0;

    return sysctl(mib, 3, NULL, &needed, NULL, 0);
}

static int get_kbufinfo_buffer(kbufinfo_t *out){
    int mib[3];

    mib[0] = CTL_KERN;
    mib[1] = KERN_KDEBUG;
    mib[2] = KERN_KDGETBUF;

    size_t needed = sizeof(*out);

    return sysctl(mib, 3, out, &needed, NULL, 0);
}

static int read_ktrace_buffer(kd_buf **out, size_t *needed){
    int mib[3];

    mib[0] = CTL_KERN;
    mib[1] = KERN_KDEBUG;
    mib[2] = KERN_KDREADTR;

    *out = malloc(*needed);

    return sysctl(mib, 3, *out, needed, NULL, 0);
}

static int reset_ktrace_buffers(void){
    int mib[3];

    mib[0] = CTL_KERN;
    mib[1] = KERN_KDEBUG;
    mib[2] = KERN_KDREMOVE;

    size_t needed = 0;

    return sysctl(mib, 3, NULL, &needed, NULL, 0);
}

static int set_kdebug_trace_pid(int pid, int value){
    int mib[3];

    mib[0] = CTL_KERN;
    mib[1] = KERN_KDEBUG;
    mib[2] = KERN_KDPIDTR;

    kd_regtype kdregtype = { KDBG_TYPENONE, pid, value, 0, 0 };

    size_t needed = sizeof(kdregtype);

    return sysctl(mib, 3, &kdregtype, &needed, NULL, 0);
}

static int set_kdebug_trace_excluded_pid(int pid, int value){
    int mib[3];

    mib[0] = CTL_KERN;
    mib[1] = KERN_KDEBUG;
    mib[2] = KERN_KDPIDEX;

    kd_regtype kdregtype = { KDBG_TYPENONE, pid, value, 0, 0 };

    size_t needed = sizeof(kdregtype);

    return sysctl(mib, 3, &kdregtype, &needed, NULL, 0);
}

static int kdebug_wait(void){
    int mib[3];

    mib[0] = CTL_KERN;
    mib[1] = KERN_KDEBUG;
    mib[2] = KERN_KDBUFWAIT;

    size_t needed;

    return sysctl(mib, 3, NULL, &needed, NULL, 0);
}

static int set_kdebug_enabled(int value){
    int mib[4];

    mib[0] = CTL_KERN;
    mib[1] = KERN_KDEBUG;
    mib[2] = KERN_KDENABLE;
    mib[3] = value;

    return sysctl(mib, 4, NULL, 0, NULL, 0);
}

static void cleanup(void){
    reset_ktrace_buffers();
    set_kdebug_enabled(0);

    debuggee->currently_tracing = 0;

    done_processing = 1;
}

static void *trace(void *arg){
    while(1){
        /* Spin until we are attached to something. */
        while(debuggee->pid == -1 && !stop)
            done_processing = 1;
        
        done_processing = 0;

        initialize_ktrace_buffer();

        if(stop){
            cleanup();
            pthread_exit(NULL);
        }

        int err = set_kdebug_trace_pid(debuggee->pid, 1);

        /* Target process died. */
        if(err < 0){
            cleanup();
            pthread_exit(NULL);
        }

        /* Don't want the kernel tracing the above events. */
        set_kdebug_enabled(1);

        if(stop){
            cleanup();
            pthread_exit(NULL);
        }

        /* Let the kernel wake up the buffer. See bsd/kern/kdebug.c
         * @ kernel_debug_internal
         */
        kdebug_wait();

        if(stop){
            cleanup();
            pthread_exit(NULL);
        }

        kbufinfo_t kbufinfo;
        get_kbufinfo_buffer(&kbufinfo);

        size_t numbuffers = kbufinfo.nkdbufs * sizeof(kd_buf);

        kd_buf *kdbuf;

        /* Read kernel trace buffer. */
        read_ktrace_buffer(&kdbuf, &numbuffers);

        for(int i=0; i<numbuffers; i++){
            /* Wait until we're not suspended to continue printing. */
            while(debuggee->suspended())
                usleep(400);
            
            kd_buf current = kdbuf[i];

            /* bsd/kern/kdebug.c: kernel_debug_internal */
            int code = current.debugid & ~KDBG_FUNC_MASK;
            unsigned int etype = current.debugid & KDBG_EVENTID_MASK;
            unsigned int stype = current.debugid & KDBG_CSC_MASK;

            char *event = NULL;

            int idx = (code & 0xfff) / 4;

            if(stype == BSC_SysCall){
                if(code > 0x40c0824)
                    idx = (code & ~0xff00000) / 4;

                event = bsd_syscalls[idx];
            }
            else if(stype == MACH_SysCall)
                event = mach_traps[idx];
            else if(stype == MACH_Msg){
                idx = (code & ~0xff000000) / 4;

                event = mach_messages[idx];

                if(!event)
                    continue;
            }
            else{
                continue;
            }

            char *tidstr = NULL;
            concat(&tidstr, "[0x%-6.6llx] ", current.arg5);

            char *calling = NULL, *returning = NULL;

            concat(&calling, "%s%-10s", tidstr, "Calling:");
            concat(&returning, "%s%-10s", tidstr, "Returning:");

            if(current.debugid & DBG_FUNC_START)
                printf("\033[42m\033[30m%-*s\033[0m %-35.35s",
                        (int)strlen(calling), calling, event);

            if(current.debugid & DBG_FUNC_END)
                printf("\033[46m\033[30m%-*s\033[0m %-35.35s",
                        (int)strlen(returning), returning, event);

            free(calling);
            free(returning);

            char *arg1desc = NULL,
                 *arg2desc = NULL,
                 *arg3desc = NULL,
                 *arg4desc = NULL;

            concat(&arg1desc, "\033[32marg1\033[0m = 0x%16.16llx", current.arg1);
            concat(&arg2desc, "\033[94marg2\033[0m = 0x%16.16llx", current.arg2);
            concat(&arg3desc, "\033[38;5;208marg3\033[0m = 0x%16.16llx", current.arg3);
            concat(&arg4desc, "\033[38;5;124marg4\033[0m = 0x%16.16llx", current.arg4);

            printf("%1s%s%2s%s%2s%s%2s%s\n",
                    "", arg1desc, "", arg2desc, "", arg3desc, "", arg4desc);

            free(arg1desc);
            free(arg2desc);
            free(arg3desc);
            free(arg4desc);
        }

        done_processing = 1;

        /* Reset the kernel buffers and go again. */
        reset_ktrace_buffers();
        initialize_ktrace_buffer();
        set_kdebug_enabled(0);

        free(kdbuf);
    }

    return NULL;
}

void start_trace(void){
    debuggee->currently_tracing = 1;

    rl_already_prompted = 1;
    rl_on_new_line();

    stop = 0;

    printf("Press Ctrl+C to stop tracing\n");
    
    if(debuggee->pid == -1){
        printf("Waiting for attach before we start tracing...\n");
        
        /* Bring back the prompt so user knows to attach to something. */
        rl_already_prompted = 0;
        //safe_reprompt();
    }

    if(debuggee->suspended())
        printf("Warning: debuggee is currently suspended,"
                " type c and hit enter to continue\n");

    pthread_t trace_thread;
    pthread_create(&trace_thread, NULL, trace, NULL);
}

void stop_trace(void){
    if(!debuggee->currently_tracing)
        return;

    rl_already_prompted = 0;

    stop = 1;

    /* The trace won't be stopped immediately, so wait until it is. */
    printf("\nShutting down trace...\n");

    while(debuggee->currently_tracing){}
}

void wait_for_trace(void){
    /* We cannot wait if we aren't attached to anything. */
    if(debuggee->pid == -1)
        return;

    if(!debuggee->currently_tracing)
        return;

    while(!done_processing){}
}
