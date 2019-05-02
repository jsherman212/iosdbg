#ifndef _BREAKPOINT_H_
#define _BREAKPOINT_H_

struct breakpoint {
    int id;
    unsigned long location;
    unsigned long old_instruction;
    int hit_count;
    int disabled;
    int temporary;
    int hw;
    int hw_bp_reg;
};

#define BT (0 << 20)
#define BAS (0xf << 5)
#define PMC (2 << 1)
#define E (1)

#define BP_NO_TEMP 0
#define BP_TEMP 1

#define BP_ENABLED 0
#define BP_DISABLED 1

static int current_breakpoint_id = 1;

/* BRK #0 */
static const unsigned long long BRK = 0x000020D4;

void breakpoint_at_address(unsigned long, int, char **);
void breakpoint_hit(struct breakpoint *);
void breakpoint_delete(int, char **);
void breakpoint_disable(int, char **);
void breakpoint_enable(int, char **);
void breakpoint_disable_all(void);
void breakpoint_enable_all(void);
int breakpoint_disabled(int);
void breakpoint_delete_all(void);
struct breakpoint *find_bp_with_address(unsigned long);

#endif
