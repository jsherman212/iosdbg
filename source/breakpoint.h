#ifndef _BREAKPOINT_H_
#define _BREAKPOINT_H_

struct breakpoint {
    /* Breakpoint ID */
    int id;

    /* Breakpoint location */
    unsigned long location;

    /* Instruction at this location */
    unsigned long old_instruction;

    /* How many times this breakpoint has hit. */
    int hit_count;

    /* Whether or not this breakpoint is disabled. */
    int disabled;

    // Whether or not this breakpoint deletes itself after hitting.
    int temporary;
    
    // Whether or not this breakpoint is a hardware breakpoint.
    int hw;

    // If this is a hardware breakpoint, what breakpoint register it corresponds to.
    int hw_bp_reg;
};

#define BT (0 << 20)
#define BAS (0xf << 5)
#define PMC (2 << 1)
#define E (1)

typedef int bp_error_t;

#define BP_SUCCESS (bp_error_t)0
#define BP_FAILURE (bp_error_t)1

#define BP_NO_TEMP 0
#define BP_TEMP 1

#define BP_ENABLED 0
#define BP_DISABLED 1

static int current_breakpoint_id = 1;

/* BRK #0 */
static const unsigned long long BRK = 0x000020D4;

bp_error_t breakpoint_at_address(unsigned long, int, char **);
void breakpoint_hit(struct breakpoint *);
bp_error_t breakpoint_delete(int);
bp_error_t breakpoint_disable(int);
bp_error_t breakpoint_enable(int);
void breakpoint_disable_all(void);
void breakpoint_enable_all(void);
int breakpoint_disabled(int);
void breakpoint_delete_all(void);
struct breakpoint *find_bp_with_address(unsigned long);

#endif
