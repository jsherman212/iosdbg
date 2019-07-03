#include <stdio.h>

#include "stepcmd.h"

#include "../breakpoint.h"
#include "../dbgops.h"
#include "../thread.h"

static void prepare(int kind){
    breakpoint_disable_all();

    struct machthread *focused = get_focused_thread();

    get_debug_state(focused);
    focused->debug_state.__mdscr_el1 |= 1;
    set_debug_state(focused);

    focused->stepconfig.is_stepping = 1;

    if(kind == INST_STEP_INTO)
        focused->stepconfig.step_kind = INST_STEP_INTO;
    else
        focused->stepconfig.step_kind = INST_STEP_OVER;
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
