#include "stepcmd.h"

#include "../breakpoint.h"
#include "../dbgops.h"
#include "../thread.h"

enum cmd_error_t cmdfunc_step_into(struct cmd_args_t *args, 
        int arg1, char **outbuffer, char **error){
    breakpoint_disable_all();

    struct machthread *focused = get_focused_thread();

    get_debug_state(focused);
    focused->debug_state.__mdscr_el1 |= 1;
    set_debug_state(focused);

    focused->is_single_stepping = 1;

    ops_resume();

    return CMD_SUCCESS;
}
