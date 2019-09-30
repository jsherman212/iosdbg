#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audit.h"

#include "../debuggee.h"
#include "../linkedlist.h"
#include "../queue.h"
#include "../strext.h"
#include "../thread.h"

static void nfree(int n, ...){
    va_list args;
    va_start(args, n);

    for(int i=0; i<n; i++){
        char *arg = va_arg(args, char *);
        free(arg);
    }

    va_end(args);
}

void audit_aslr(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_attach(struct cmd_args *args, const char **groupnames,
        char **error){
    char *target = argcopy(args, groupnames[1]);

    if(!target){
        concat(error, "no target");
        nfree(1, target);
        return;
    }

    char *waitfor = argcopy(args, groupnames[0]);

    if(waitfor && is_number_fast(target)){
        concat(error, "cannot wait for PIDs");
        nfree(2, waitfor, target);
        return;
    }

    if(strcmp(target, "0") == 0 || strcmp(target, "kernel_task") == 0){
        concat(error, "cannot debug the kernel");
        nfree(2, waitfor, target);
        return;
    }

    pid_t target_pid = strtol(target, NULL, 10);

    if(strcmp(target, "iosdbg") == 0 || target_pid == getpid()){
        concat(error, "cannot attach to myself! Use another iosdbg instance");
        nfree(2, waitfor, target);
        return;
    }

    nfree(2, waitfor, target);
}

void audit_backtrace(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_breakpoint_set(struct cmd_args *args, const char **groupnames,
        char **error){
    // XXX if I allow setting breakpoints before attaching and resolving them
    // later, this will cause problems
    if(debuggee->pid == -1)
        concat(error, "no debuggee");

    char *tidstr = argcopy(args, groupnames[0]);
    char *locations = argcopy(args, groupnames[1]);

    if(!locations){
        concat(error, "need location");
        nfree(1, locations);
        return;
    }

    nfree(2, tidstr, locations);
}

void audit_continue(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_detach(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_disassemble(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    char *location = argcopy(args, groupnames[0]);

    if(!location){
        concat(error, "need location");
        nfree(1, location);
        return;
    }

    char *count = argcopy(args, groupnames[1]);

    if(!count){
        concat(error, "need count");
        nfree(2, location, count);
        return;
    }

    int amount = (int)strtol_err(count, error);

    if(*error){
        nfree(2, location, count);
        return;
    }

    nfree(2, location, count);
}

void audit_evaluate(struct cmd_args *args, const char **groupnames,
        char **error){
    char *expr = argcopy(args, groupnames[0]);

    if(!expr)
        concat(error, "need expression(s)");

    nfree(1, expr);
}

void audit_examine(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    char *location = argcopy(args, groupnames[0]);

    if(!location){
        concat(error, "need location");
        nfree(1, location);
        return;
    }

    char *count = argcopy(args, groupnames[1]);

    if(!count){
        concat(error, "need amount");
        nfree(2, location, count);
        return;
    }

    nfree(2, location, count);
}

void audit_kill(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_memory_find(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");

    char *start = argcopy(args, groupnames[0]);
    char *count = argcopy(args, groupnames[1]);
    char *type = argcopy(args, groupnames[2]);
    char *target = argcopy(args, groupnames[3]);

    if(!type){
        concat(error, "missing type");
        nfree(4, start, count, type, target);
        return;
    }

    if(strcmp(type, "--s") == 0){
        if(!target){
            concat(error, "attempt to search for empty string");
            nfree(4, start, count, type, target);
            return;
        }
    }

    /* Check if this is a valid floating point number. */
    if(strcmp(type, "--f") == 0 ||
            strcmp(type, "--fd") == 0 ||
            strcmp(type, "--fld") == 0){
        strtold_err(target, error);

        if(*error){
            nfree(4, start, count, type, target);
            return;
        }
    }

    nfree(4, start, count, type, target);
}

void audit_memory_write(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_register_view(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_register_write(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_signal_deliver(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    char *sigstr = argcopy(args, groupnames[0]);

    if(!sigstr){
        concat(error, "need signal number");
        nfree(1, sigstr);
        return;
    }

    nfree(1, sigstr);
}

void audit_step_inst_into(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_step_inst_over(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_symbols_add(struct cmd_args *args, const char **groupnames,
        char **error){
    char *filepath = argcopy(args, groupnames[0]);
    
    if(!filepath){
        concat(error, "need path to DWARF file");
        return;
    }

    free(filepath);
}

void audit_thread_list(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    TH_LOCK;
    if(!debuggee->threads || !debuggee->threads->front)
        concat(error, "no threads");
    TH_UNLOCK;
}

void audit_thread_select(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    TH_LOCK;
    if(!debuggee->threads || !debuggee->threads->front){
        TH_UNLOCK;
        concat(error, "no threads");
        return;
    }
    TH_UNLOCK;

    char *tid = argcopy(args, groupnames[0]);

    if(!tid){
        concat(error, "need thread ID");
        nfree(1, tid);
        return;
    }

    nfree(1, tid);
}

void audit_watchpoint_set(struct cmd_args *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    /* tid doesn't need to be checked. */
    char *tid = argcopy(args, groupnames[0]);
    char *type = argcopy(args, groupnames[1]);

    if(type && (strcmp(type, "r") != 0 &&
            strcmp(type, "w") != 0 &&
            strcmp(type, "rw") != 0)){
        concat(error, "invalid watchpoint type '%s'", type);
        nfree(2, tid, type);
        return;
    }

    char *location = argcopy(args, groupnames[2]);

    if(!location){
        concat(error, "need location");
        nfree(3, tid, type, location);
        return;
    }

    char *size = argcopy(args, groupnames[3]);

    if(!size){
        concat(error, "missing data size");
        nfree(4, tid, type, location, size);
    }

    nfree(4, tid, type, location, size);
}
