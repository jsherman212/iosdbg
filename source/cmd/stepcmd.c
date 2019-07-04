#include <stdio.h>

#include "stepcmd.h"

#include "../breakpoint.h"
#include "../dbgops.h"
#include "../memutils.h"
#include "../thread.h"

#include "../disas/branch.h"

static void prepare(int kind){
    breakpoint_disable_all();

    struct machthread *focused = get_focused_thread();

    get_debug_state(focused);
    focused->debug_state.__mdscr_el1 |= 1;
    set_debug_state(focused);

    focused->stepconfig.is_stepping = 1;

    if(kind == INST_STEP_INTO)
        focused->stepconfig.step_kind = INST_STEP_INTO;
    else{
        /* Check if we're currently at a subroutine call. */
        focused->stepconfig.step_kind = INST_STEP_OVER;

        unsigned int opcode = 0;
        read_memory_at_location((void *)focused->thread_state.__pc,
                &opcode, sizeof(opcode));

        struct branchinfo info = {0};
        int branch = is_branch(opcode, &info);

        if(branch && info.is_subroutine_call &&
                focused->stepconfig.LR_to_step_to == -1){
            if(info.rn != X30){
                printf("%s: we need to save LR\n", __func__);
                focused->stepconfig.need_to_save_LR = 1;
            }
        }
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
