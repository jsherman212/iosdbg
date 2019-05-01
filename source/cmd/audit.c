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

void audit_aslr(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1)
        asprintf(error, "no debuggee");
}

void audit_attach(struct cmd_args_t *args, char **error){
    char *arg1 = argnext(args);

    if(!arg1){
        asprintf(error, "no target");
        return;
    }

    char *arg2 = argnext(args);

    char *target = arg1;

    /* arg1 could be '--waitfor' or the target. 
     * If it is '--waitfor', the target should follow.
     */
    if(strcmp(arg1, "--waitfor") == 0){
        if(!arg2){
            asprintf(error, "missing target for --waitfor");
            return;
        }

        /* We cannot wait for PIDs. */
        if(is_number_fast(arg2)){
            asprintf(error, "cannot wait for PIDs");
            return;
        }

        target = arg2;
    }

    /* We cannot debug the kernel. */
    if(strcmp(target, "0") == 0 || strcmp(target, "kernel_task") == 0){
        asprintf(error, "cannot debug the kernel");
        return;
    }

    pid_t target_pid = strtol(target, NULL, 10);

    /* We cannot debug ourselves. */
    if(strcmp(target, "iosdbg") == 0 || target_pid == getpid()){
        asprintf(error, "cannot attach to myself");
        return;
    }

    repair_cmd_args(args, 2, arg1, arg2);
}

void audit_backtrace(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1)
        asprintf(error, "no debuggee");
}

void audit_break(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "no debuggee");
        return;
    }

    char *arg1 = argnext(args);

    if(!arg1){
        asprintf(error, "need location");
        return;
    }

    repair_cmd_args(args, 1, arg1);
}

void audit_continue(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1)
        asprintf(error, "no debuggee");
}

void audit_delete(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "no debuggee");
        return;
    }

    /* First argument is the type. */
    char *arg1 = argnext(args);

    if(!arg1){
        asprintf(error, "need type");
        return;
    }

    if(strcmp(arg1, "b") != 0 && strcmp(arg1, "w") != 0){
        asprintf(error, "unknown type '%s'", arg1);
        return;
    }

    char *arg2 = argnext(args);

    repair_cmd_args(args, 2, arg1, arg2);
}

void audit_detach(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1)
        asprintf(error, "no debuggee");
}

void audit_disassemble(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "no debuggee");
        return;
    }

    /* First argument is the location. */
    char *arg1 = argnext(args);

    if(!arg1){
        asprintf(error, "need location");
        return;
    }

    /* Next argument is the amount of instructions to disassemble. */
    char *arg2 = argnext(args);

    if(!arg2){
        asprintf(error, "need amount");
        return;
    }

    int amount = (int)strtol_err(arg2, error);

    if(*error)
        return;

    repair_cmd_args(args, 2, arg1, arg2);
}

void audit_examine(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "no debuggee");
        return;
    }

    /* First argument is the location. */
    char *arg1 = argnext(args);

    if(!arg1){
        asprintf(error, "need location");
        return;
    }

    /* Next argument is the amount of bytes to display. */
    char *arg2 = argnext(args);

    if(!arg2){
        asprintf(error, "need amount");
        return;
    }

    repair_cmd_args(args, 2, arg1, arg2);
}

void audit_help(struct cmd_args_t *args, char **error){
    char *arg1 = argnext(args);

    if(!arg1){
        asprintf(error, "need command");
        return;
    }

    repair_cmd_args(args, 1, arg1);
}

void audit_kill(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1)
        asprintf(error, "no debuggee");
}

void audit_memory_find(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1)
        asprintf(error, "no debuggee");
}

void audit_quit(struct cmd_args_t *args, char **error){
    /* Nothing needs to be done. */
}

void audit_regs_float(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "no debuggee");
        return;
    }

    if(args->num_args == 0)
        asprintf(error, "need a register");
}

void audit_regs_gen(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1)
        asprintf(error, "no debuggee");
}

void audit_set(struct cmd_args_t *args, char **error){
    /* First argument is the specifier. */
    char *arg1 = argnext(args);

    if(!arg1){
        asprintf(error, "missing specifier");
        return;
    }

    char specifier = *arg1;

    if(specifier != '*' && specifier != '$'){
        asprintf(error, "invalid specifier '%c'", specifier);
        return;
    }

    /* Second argument is the target. */
    char *arg2 = argnext(args);

    if(!arg2){
        asprintf(error, "need target");
        return;
    }

    /* Third argument is the value. */
    char *arg3 = argnext(args);

    if(!arg3){
        asprintf(error, "need value");
        return;
    }

    repair_cmd_args(args, 3, arg1, arg2, arg3);
}

void audit_show(struct cmd_args_t *args, char **error){
    /* Nothing to do. */
}

void audit_signal_handle(struct cmd_args_t *args, char **error){
    /* Nothing to do. */
}

void audit_stepi(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1)
        asprintf(error, "no debuggee");
}

void audit_thread_list(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "no debuggee");
        return;
    }

    if(!debuggee->threads || !debuggee->threads->front)
        asprintf(error, "no threads");
}

void audit_thread_select(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "no debuggee");
        return;
    }

    if(!debuggee->threads || !debuggee->threads->front){
        asprintf(error, "no threads");
        return;
    }

    /* First argument is the thread ID. */
    char *arg1 = argnext(args);

    if(!arg1)
        asprintf(error, "need thread ID");

    repair_cmd_args(args, 1, arg1);
}

void audit_trace(struct cmd_args_t *args, char **error){
    /* Nothing to do. */
}

void audit_unset(struct cmd_args_t *args, char **error){
    if(args->num_args == 0)
        asprintf(error, "need a convenience variable");
}

void audit_watchpoint_delete(struct cmd_args_t *args, char **error){
    /* Nothing to do. */
}

void audit_watchpoint_set(struct cmd_args_t *args, char **error){
    if(debuggee->pid == -1){
        asprintf(error, "no debuggee");
        return;
    }

    /* First argument is the watchpoint type or the location. */
    char *arg1 = argnext(args);

    if(!arg1){
        asprintf(error, "need type or location");
        return;
    }

    /* The argument is the watchpoint type. */
    if(!is_number_slow(arg1)){
        if(strcmp(arg1, "--r") != 0 &&
                strcmp(arg1, "--w") != 0 &&
                strcmp(arg1, "--rw") != 0){
            asprintf(error, "invalid watchpoint type '%s'", arg1);
            return;
        }

        /* The watchpoint location should follow. */
        char *arg2 = argnext(args);

        if(!arg2){
            asprintf(error, "need location");
            return;
        }

        /* Finally, the size of the data we're watching. */
        char *arg3 = argnext(args);

        if(!arg3){
            asprintf(error, "missing data size");
            return;
        }

        repair_cmd_args(args, 3, arg1, arg2, arg3);

        return;
    }

    /* In this case, the data size follows. */
    char *arg2 = argnext(args);

    if(!arg2){
        asprintf(error, "missing data size");
        return;
    }

    repair_cmd_args(args, 2, arg1, arg2);
}
