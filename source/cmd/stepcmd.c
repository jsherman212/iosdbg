#include <stdio.h>

#include "stepcmd.h"

#include "../breakpoint.h"
#include "../dbgops.h"
#include "../memutils.h"
#include "../thread.h"

#include "../disas/branch.h"

static void prepare(int kind){
    struct machthread *focused = get_focused_thread();

    int need_ss = 1;

    if(kind == INST_STEP_INTO)
        focused->stepconfig.step_kind = INST_STEP_INTO;
    else{
        focused->stepconfig.step_kind = INST_STEP_OVER;

        unsigned int opcode = 0;
        read_memory_at_location((void *)focused->thread_state.__pc,
                &opcode, sizeof(opcode));

        struct branchinfo info = {0};
        int branch = is_branch(opcode, &info);
        if(branch && info.is_subroutine_call ){//&&
                //!focused->stepconfig.set_temp_ss_breakpoint){

            if(info.rn != X30){
                if(!focused->stepconfig.set_temp_ss_breakpoint){
                    printf("%s: at a subroutine call, will set bp on LR,"
                            " which will be PC+4 aka %#llx, now: LR = %#llx PC = %#llx\n",
                            __func__, focused->thread_state.__pc + 4,
                            focused->thread_state.__lr, focused->thread_state.__pc);
                    __uint64_t future_lr = focused->thread_state.__pc + 4;
                    set_stepping_breakpoint(future_lr, focused->ID);
                    focused->stepconfig.set_temp_ss_breakpoint = 1;
                }

                need_ss = 0;
            }
        }
    }

    if(need_ss){
        breakpoint_disable_all();

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
