#ifndef _WATCHPOINT_H_
#define _WATCHPOINT_H_

#include <pthread/pthread.h>

extern pthread_mutex_t WATCHPOINT_LOCK;

#define WP_LOCKED_FOREACH(var) \
    pthread_mutex_lock(&WATCHPOINT_LOCK); \
    for(struct node_t *var = debuggee->watchpoints->front; \
            var; \
            var = var->next) \

#define WP_END_LOCKED_FOREACH \
    pthread_mutex_unlock(&WATCHPOINT_LOCK)

#define WP_LOCK pthread_mutex_lock(&WATCHPOINT_LOCK)
#define WP_UNLOCK pthread_mutex_unlock(&WATCHPOINT_LOCK)

struct watchpoint {
    int id;
    unsigned long user_location;
    unsigned long aligned_location;
    int hit_count;
    void *data;
    unsigned int data_len;
    int hw_wp_reg;
    const char *type;

    struct {
        int all;
        int iosdbg_tid;
        unsigned long long pthread_tid;
        char *tname;
    } threadinfo;

    __uint64_t wcr;
    __uint64_t wvr;
};

#define WP_ALL_THREADS (-1)

#define PAC (2 << 1)
#define E (1)

#define WP_ENABLED 0
#define WP_DISABLED 1

#define WP_READ (1)
#define WP_WRITE (2)
#define WP_READ_WRITE (3)

static int current_watchpoint_id = 1;

void watchpoint_at_address(unsigned long, unsigned int, int, int, char **, char **);
void watchpoint_hit(struct watchpoint *);
void watchpoint_delete_specific(struct watchpoint *);
void watchpoint_delete(int, char **);
void watchpoint_enable_all(void);
void watchpoint_disable_all(void);
void watchpoint_delete_all(void);
struct watchpoint *find_wp_with_address(unsigned long);

#endif
