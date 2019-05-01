#ifndef _WATCHPOINT_H_
#define _WATCHPOINT_H_

struct watchpoint {
    int id;
    unsigned long location;
    int hit_count;
    void *data;
    unsigned int data_len;
    int hw_wp_reg;
    int LSC;
};

#define WT (0 << 20)
#define PAC (2 << 1)
#define E (1)

#define WP_ENABLED 0
#define WP_DISABLED 1

#define WP_READ (1)
#define WP_WRITE (2)
#define WP_READ_WRITE (3)

static int current_watchpoint_id = 1;

void watchpoint_at_address(unsigned long, unsigned int, int, char **);
void watchpoint_hit(struct watchpoint *);
void watchpoint_delete(int, char **);
void watchpoint_enable_all(void);
void watchpoint_disable_all(void);
void watchpoint_delete_all(void);
struct watchpoint *find_wp_with_address(unsigned long);

#endif
