#ifndef _BREAKPOINT_H_
#define _BREAKPOINT_H_

#include <pthread/pthread.h>

extern pthread_mutex_t BREAKPOINT_LOCK;
//pthread_mutex_lock(&BREAKPOINT_LOCK); 
//struct node_t *current = debuggee->breakpoints->front; 
//while(current){ 

#define BP_LOCKED_FOREACH(var) \
    for(struct node_t *var = debuggee->breakpoints->front; \
            var; \
            var = var->next) \

#define BP_END_LOCKED_FOREACH \
    pthread_mutex_unlock(&BREAKPOINT_LOCK)

#define BP_LOCK pthread_mutex_lock(&BREAKPOINT_LOCK)
#define BP_UNLOCK pthread_mutex_unlock(&BREAKPOINT_LOCK)

struct breakpoint {
    int id;
    unsigned long location;
    unsigned long old_instruction;
    int hit_count;
    int disabled;
    int temporary;
    int hw;
    int hw_bp_reg;
    int for_stepping;

    struct {
        int all;
        int iosdbg_tid;
        unsigned long long pthread_tid;
        char *tname;
    } threadinfo;

    __uint64_t bcr;
    __uint64_t bvr;
};

#define BP_ALL_THREADS (-1)

#define BT (0 << 20)
#define BAS (0xf << 5)
#define PMC (2 << 1)
#define E (1)

#define BP_NO_TEMP 0
#define BP_TEMP 1

#define BP_ENABLED 0
#define BP_DISABLED 1

enum {
    /* not a temporary or stepping breakpoint */
    BP_COND_NORMAL,
    BP_COND_TEMP,
    BP_COND_STEPPING
};

static int current_breakpoint_id = 1;

/* BRK #0 */
static const unsigned long long BRK = 0xd4200000;

void breakpoint_at_address(unsigned long, int, int, char **, char **);
void set_stepping_breakpoint(unsigned long, int);

void breakpoint_hit(struct breakpoint *);
void breakpoint_delete(int, char **);
void breakpoint_delete_specific(struct breakpoint *);
void breakpoint_disable(int, char **);
void breakpoint_enable(int, char **);
void breakpoint_disable_all(void);
void breakpoint_enable_all(void);
void breakpoint_enable_all_specific(int);
int breakpoint_disabled(int);
void breakpoint_delete_all(void);
void breakpoint_delete_all_specific(int);
struct breakpoint *find_bp_with_address(unsigned long);
struct breakpoint *find_bp_with_cond(unsigned long, int);
void breakpoint_disable_all_except(int);

#endif
