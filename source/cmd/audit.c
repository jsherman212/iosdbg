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

static void free_on_failure(int argcount, ...){
    va_list args;
    va_start(args, argcount);

    for(int i=0; i<argcount; i++){
        char *arg = va_arg(args, char *);

        if(arg)
            free(arg);
    }

    va_end(args);
}

static void repair_cmd_args(struct cmd_args_t *_args, int argcount, ...){
    va_list args;
    va_start(args, argcount);

    for(int i=0; i<argcount; i++){
        char *arg = va_arg(args, char *);

        if(arg)
            enqueue(_args->argqueue, arg);
    }

    va_end(args);
}

static void repair_cmd_args2(struct cmd_args_t *_args, int unlim,
        const char **groupnames, int argcount, ...){
    va_list args;
    va_start(args, argcount);

    if(unlim)
        argcount--;

    for(int i=0; i<argcount; i++){
        char *arg = va_arg(args, char *);

        if(arg){
            argins(_args, groupnames[i], arg);
        }
    }

    if(unlim){
        char *arg = va_arg(args, char *);

        while(arg){
            if(arg){
                argins(_args, groupnames[argcount - 1], arg);
            }

            arg = va_arg(args, char *);
        }
    }

    va_end(args);
}

void audit_aslr(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_attach(struct cmd_args_t *args, const char **groupnames,
        char **error){
    char *target = argcopy(args, groupnames[1]);

    if(!target){
        concat(error, "no target");
        free_on_failure(1, target);
        return;
    }

    char *waitfor = argcopy(args, groupnames[0]);

    printf("%s: got waitfor '%s' target '%s'\n",
            __func__, waitfor ? waitfor : "NULL", target);

    if(waitfor && is_number_fast(target)){
        concat(error, "cannot wait for PIDs");
        free_on_failure(2, waitfor, target);
        return;
    }

    /* We cannot debug the kernel. */
    if(strcmp(target, "0") == 0 || strcmp(target, "kernel_task") == 0){
        concat(error, "cannot debug the kernel");
        free_on_failure(2, waitfor, target);
        return;
    }

    pid_t target_pid = strtol(target, NULL, 10);

    /* We cannot debug ourselves. */
    if(strcmp(target, "iosdbg") == 0 || target_pid == getpid()){
        concat(error, "cannot attach to myself! Use another iosdbg instance");
        free_on_failure(2, waitfor, target);
        return;
    }

    repair_cmd_args2(args, 0, groupnames, 2, waitfor, target);
}

void audit_backtrace(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_breakpoint_set(struct cmd_args_t *args, const char **groupnames,
        char **error){
    // XXX if I allow setting breakpoints before attaching and resolving them
    // later, this will cause problems
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_continue(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_detach(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_disassemble(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    char *location = argcopy(args, groupnames[0]);

    if(!location){
        concat(error, "need location");
        free_on_failure(1, location);
        return;
    }

    char *count = argcopy(args, groupnames[1]);

    if(!count){
        concat(error, "need count");
        free_on_failure(2, location, count);
        return;
    }

    int amount = (int)strtol_err(count, error);

    if(*error){
        free_on_failure(2, location, count);
        return;
    }

    repair_cmd_args2(args, 0, groupnames, 2, location, count);
}

void audit_examine(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    char *location = argcopy(args, groupnames[0]);

    if(!location){
        concat(error, "need location");
        free_on_failure(1, location);
        return;
    }

    /* Next argument is the amount of bytes to display. */
    char *count = argcopy(args, groupnames[1]);

    if(!count){
        concat(error, "need amount");
        free_on_failure(2, location, count);
        return;
    }

    repair_cmd_args2(args, 0, groupnames, 2, location, count);
}

void audit_kill(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_memory_find(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");

    char *start = argcopy(args, groupnames[0]);
    char *count = argcopy(args, groupnames[1]);
    char *type = argcopy(args, groupnames[2]);
    char *target = argcopy(args, groupnames[3]);

    if(strcmp(type, "--s") == 0){
        if(!target){
            concat(error, "attempt to search for empty string");
            free_on_failure(4, start, count, type, target);
            return;
        }
    }

    /* Check if this is a valid floating point number. */
    if(strcmp(type, "--f") == 0 ||
            strcmp(type, "--fd") == 0 ||
            strcmp(type, "--fld") == 0){
        strtold_err(target, error);

        if(*error){
            free_on_failure(4, start, count, type, target);
            return;
        }
    }

    repair_cmd_args2(args, 0, groupnames, 4, start, count, type, target);
}

void audit_memory_write(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_register_float(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    if(args->num_args == 0)
        concat(error, "need a register");
}

void audit_register_gen(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_register_write(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_stepi(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1)
        concat(error, "no debuggee");
}

void audit_thread_list(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    if(!debuggee->threads || !debuggee->threads->front)
        concat(error, "no threads");
}

void audit_thread_select(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    if(!debuggee->threads || !debuggee->threads->front){
        concat(error, "no threads");
        return;
    }

    char *tid = argcopy(args, groupnames[0]);

    if(!tid){
        concat(error, "need thread ID");
        free_on_failure(1, tid);
        return;
    }

    repair_cmd_args2(args, 0, groupnames, 1, tid);
}

void audit_watchpoint_set(struct cmd_args_t *args, const char **groupnames,
        char **error){
    if(debuggee->pid == -1){
        concat(error, "no debuggee");
        return;
    }

    char *type = argcopy(args, groupnames[0]);

    if(!type || (strcmp(type, "--r") != 0 &&
            strcmp(type, "--w") != 0 &&
            strcmp(type, "--rw") != 0)){
        concat(error, "invalid watchpoint type");
        free_on_failure(1, type);
        return;
    }

    char *location = argcopy(args, groupnames[1]);

    if(!location){
        concat(error, "need location");
        free_on_failure(2, type, location);
    }

    char *size = argcopy(args, groupnames[2]);

    if(!size){
        concat(error, "missing data size");
        free_on_failure(3, type, location, size);
    }

    repair_cmd_args2(args, 0, groupnames, 3, type, location, size);
}
