#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

static struct {
    int notify;
    int pass;
    int stop;
} SETTINGS[NSIG] = {
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 },
    { 1, 1, 1 }
};

void sigsettings(int signo, 
        int *notify,
        int *pass,
        int *stop,
        int set,
        char **error){
    if(signo < 1 || signo >= NSIG){
        asprintf(error, "unknown signal %d", signo);
        return;
    }

    if(!set && (!notify || !pass || !stop)){
        asprintf(error, "cannot place values into NULL pointers");
        return;
    }

    /* User wants to see how we handle a certain signal. */
    if(!set){
        *notify = SETTINGS[signo].notify;
        *pass = SETTINGS[signo].pass;
        *stop = SETTINGS[signo].stop;

        return;
    }

    /* Otherwise, we're changing settings. */
    SETTINGS[signo].notify = *notify;
    SETTINGS[signo].pass = *pass;
    SETTINGS[signo].stop = *stop;
}
