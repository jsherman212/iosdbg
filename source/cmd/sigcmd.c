#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argparse.h"

#include "../dbgops.h"
#include "../sigsupport.h"

static const int UNKNOWN_SIGNAL = -1;

/* (SIG[A-Z]+\d?)(?:.*) */
static int sigstr_to_signum(const char *sigstr){
    if(strcmp(sigstr, "SIGHUP") == 0)       return SIGHUP;
    if(strcmp(sigstr, "SIGINT") == 0)       return SIGINT;
    if(strcmp(sigstr, "SIGQUIT") == 0)      return SIGQUIT;
    if(strcmp(sigstr, "SIGILL") == 0)       return SIGILL;
    if(strcmp(sigstr, "SIGTRAP") == 0)      return SIGTRAP;
    if(strcmp(sigstr, "SIGABRT") == 0)      return SIGABRT;
    if(strcmp(sigstr, "SIGEMT") == 0)       return SIGEMT;
    if(strcmp(sigstr, "SIGFPE") == 0)       return SIGFPE;
    if(strcmp(sigstr, "SIGKILL") == 0)      return SIGKILL;
    if(strcmp(sigstr, "SIGBUS") == 0)       return SIGBUS;
    if(strcmp(sigstr, "SIGSEGV") == 0)      return SIGSEGV;
    if(strcmp(sigstr, "SIGSYS") == 0)       return SIGSYS;
    if(strcmp(sigstr, "SIGPIPE") == 0)      return SIGPIPE;
    if(strcmp(sigstr, "SIGALRM") == 0)      return SIGALRM;
    if(strcmp(sigstr, "SIGTERM") == 0)      return SIGTERM;
    if(strcmp(sigstr, "SIGURG") == 0)       return SIGURG;
    if(strcmp(sigstr, "SIGSTOP") == 0)      return SIGSTOP;
    if(strcmp(sigstr, "SIGTSTP") == 0)      return SIGTSTP;
    if(strcmp(sigstr, "SIGCONT") == 0)      return SIGCONT;
    if(strcmp(sigstr, "SIGCHLD") == 0)      return SIGCHLD;
    if(strcmp(sigstr, "SIGTTIN") == 0)      return SIGTTIN;
    if(strcmp(sigstr, "SIGTTOU") == 0)      return SIGTTOU;
    if(strcmp(sigstr, "SIGIO") == 0)        return SIGIO;
    if(strcmp(sigstr, "SIGXCPU") == 0)      return SIGXCPU;
    if(strcmp(sigstr, "SIGXFSZ") == 0)      return SIGXFSZ;
    if(strcmp(sigstr, "SIGVTALRM") == 0)    return SIGVTALRM;
    if(strcmp(sigstr, "SIGPROF") == 0)      return SIGPROF;
    if(strcmp(sigstr, "SIGWINCH") == 0)     return SIGWINCH;
    if(strcmp(sigstr, "SIGINFO") == 0)      return SIGINFO;
    if(strcmp(sigstr, "SIGUSR1") == 0)      return SIGUSR1;
    if(strcmp(sigstr, "SIGUSR2") == 0)      return SIGUSR2;

    return UNKNOWN_SIGNAL;
}

static int preference(char *str){
    if(strstr(str, "true") || strstr(str, "1"))
        return 1;

    return 0;
}

enum cmd_error_t cmdfunc_signal_handle(struct cmd_args_t *args, 
        int arg1, char **error){
    char *signals = argnext(args);
    char *notify_str = argnext(args);
    char *pass_str = argnext(args);
    char *stop_str = argnext(args);
    
    /* If no arguments were given, the user wants to see settings. */
    if(!signals || !notify_str || !pass_str || !stop_str){
        ops_printsiginfo();
        return CMD_SUCCESS;
    }

    char *signal = strtok_r(signals, " ", &signals);

    while(signal){
        int sig = sigstr_to_signum(signal);

        int notify = preference(notify_str);
        int pass = preference(pass_str);
        int stop = preference(stop_str);

        char *e = NULL;
        sigsettings(sig, &notify, &pass, &stop, 1, &e);

        if(e){
            printf("error: %s\n", e);
            free(e);
        }

        signal = strtok_r(NULL, " ", &signals);
    }

    return CMD_SUCCESS;
}
