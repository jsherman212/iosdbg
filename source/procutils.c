#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

#include "strext.h"

static struct kinfo_proc *fill_kinfo_proc_buffer(size_t *length, char **error){
    int err;
    struct kinfo_proc *result = NULL;

    static const int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };

    *length = 0;

    err = sysctl((int *)name, (sizeof(name) / sizeof(name[0])) - 1, NULL,
            length, NULL, 0);
    
    if(err){
        concat(error, "couldn't get the size of our kinfo_proc buffer: %s\n", 
                strerror(errno));
        return NULL;
    }
    
    result = malloc(*length);
    err = sysctl((int *)name, (sizeof(name) / sizeof(name[0])) - 1, result,
            length, NULL, 0);
    
    if(err){
        concat(error, "second sysctl call failed: %s\n", strerror(errno));
        return NULL;
    }

    return result;
}

pid_t pid_of_program(char *progname, char **error){
    size_t length;

    struct kinfo_proc *result = fill_kinfo_proc_buffer(&length, error);

    if(*error)
        return -1;

    int num_procs = length / sizeof(struct kinfo_proc);
    int matches = 0;
    char *matchstr = malloc(20);
    memset(matchstr, '\0', 20);
    pid_t final_pid = -1;
    int maxnamelen = MAXCOMLEN + 1;

    for(int i=0; i<num_procs; i++){
        struct kinfo_proc *current = &result[i];
        
        if(current){
            pid_t pid = current->kp_proc.p_pid;
            char *pname = current->kp_proc.p_comm;
            char p_stat = current->kp_proc.p_stat;
            int pnamelen = strlen(pname);
            int charstocompare = pnamelen < maxnamelen ? pnamelen : maxnamelen;

            if(strncmp(pname, progname, charstocompare) == 0 && p_stat != SZOMB){
                matches++;
                concat(&matchstr, " PID %d: %s\n", pid, pname);
                final_pid = pid;
            }
        }
    }

    free(result);
    
    if(matches > 1){
        concat(error, "multiple instances of '%s': \n%s", progname, matchstr);
        free(matchstr);
        return -1;
    }

    free(matchstr);

    if(matches == 0){
        concat(error, "could not find a process named '%s'", progname);
        return -1;
    }

    if(matches == 1)
        return final_pid;
    
    return -1;
}

char *progname_from_pid(pid_t pid, char **error){
    size_t length;

    struct kinfo_proc *result = fill_kinfo_proc_buffer(&length, error);

    if(*error)
        return NULL;

    int num_procs = length / sizeof(struct kinfo_proc);

    for(int i=0; i<num_procs; i++){
        struct kinfo_proc *current = &result[i];

        if(current){
            if(current->kp_proc.p_pid == pid)
                return strdup(current->kp_proc.p_comm);
        }
    }

    concat(error, "could not find process for pid %d", pid);

    return NULL;
}
