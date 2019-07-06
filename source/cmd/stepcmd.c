#include <stdio.h>

#include "stepcmd.h"

#include "../breakpoint.h"
#include "../dbgops.h"
#include "../memutils.h"
#include "../thread.h"

#include "../disas/branch.h"


// XXX
#include "../exception.h"
static void prepare(int kind){
    int need_ss = 1;

    struct machthread *focused = get_focused_thread();
    breakpoint_disable_all_except(BP_COND_STEPPING);

    if(kind == INST_STEP_INTO)
        focused->stepconfig.step_kind = INST_STEP_INTO;
    else{
        //breakpoint_disable_all();
        focused->stepconfig.step_kind = INST_STEP_OVER;

        unsigned int opcode = 0;
        
        read_memory_at_location((void *)focused->thread_state.__pc,
                &opcode, sizeof(opcode));

        struct breakpoint *b = find_bp_with_cond(focused->thread_state.__pc,
                BP_COND_NORMAL);

        if(b)
            opcode = b->old_instruction;

        printf("%s: opcode %#x\n", __func__, opcode);

        SS_BP_LOCK;
        struct branchinfo info = {0};
        int branch = is_branch(opcode, &info);

        if(branch && info.is_subroutine_call){
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

                // XXX don't need to single step if we're gonna be
                //      setting a breakpoint anyway
                need_ss = 0;

                // XXX if we're at a software breakpoint disable it
                // so we can get past it
                if(b && !b->hw){
                    /*BP_LOCK;
                    breakpoint_disable_specific(b);
                    BP_UNLOCK;*/
                //    need_ss = 1;
                }
            }
        }
    }

    //breakpoint_disable_all();
    if(need_ss){
        get_debug_state(focused);
        focused->debug_state.__mdscr_el1 |= 1;
        set_debug_state(focused);

        focused->stepconfig.is_stepping = 1;
    }
    SS_BP_UNLOCK;
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
