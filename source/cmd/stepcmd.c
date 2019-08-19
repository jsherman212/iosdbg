#include "stepcmd.h"

#include "../breakpoint.h"
#include "../dbgops.h"
#include "../memutils.h"
#include "../thread.h"

#include "../disas/branch.h"

static void prepare(int kind){
    int need_ss = 1;

    struct machthread *focused = get_focused_thread();

    /* Disable breakpoints before we start stepping so we don't have
     * to deal with more exceptions. If the user happens to step on
     * a breakpoint, report it as normal.
     */
    breakpoint_disable_all_except(BP_COND_STEPPING);

    if(kind == INST_STEP_INTO)
        focused->stepconfig.step_kind = INST_STEP_INTO;
    else{
        focused->stepconfig.step_kind = INST_STEP_OVER;

        unsigned int opcode = 0;

        struct breakpoint *b = find_bp_with_cond(focused->thread_state.__pc,
                BP_COND_NORMAL);

        if(b)
            opcode = b->old_instruction;
        else{
            read_memory_at_location(focused->thread_state.__pc, &opcode,
                    sizeof(opcode));
        }

        struct branchinfo info = {0};
        int branch = is_branch(opcode, &info);

        if(branch && info.is_subroutine_call){
            if(info.rn != X30){
                if(!focused->stepconfig.set_temp_ss_breakpoint){
                    __uint64_t future_lr = focused->thread_state.__pc + 4;
                    set_stepping_breakpoint(future_lr, focused->ID);
                    focused->stepconfig.set_temp_ss_breakpoint = 1;
                }

                /* Do not single step if we're at a subroutine call. */
                need_ss = 0;
            }
        }
    }

    if(need_ss){
        get_debug_state(focused);
        focused->debug_state.__mdscr_el1 |= 1;
        set_debug_state(focused);

        focused->stepconfig.is_stepping = 1;
    }
}

enum cmd_error_t cmdfunc_step_inst_into(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    prepare(INST_STEP_INTO);

    ops_resume();

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_step_inst_over(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    prepare(INST_STEP_OVER);

    ops_resume();

    return CMD_SUCCESS;
}
