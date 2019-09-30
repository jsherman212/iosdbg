#include <stdio.h>
#include <stdlib.h>

#include "tcmd.h"

#include "../debuggee.h"
#include "../linkedlist.h"
#include "../memutils.h"
#include "../strext.h"
#include "../thread.h"

#include "../symbol/dbgsymbol.h"

enum cmd_error_t cmdfunc_thread_list(struct cmd_args *args, 
        int arg1, char **outbuffer, char **error){
    TH_LOCKED_FOREACH(current){
        struct machthread *t = current->data;

        concat(outbuffer, "%4sthread %d, tid = %#llx, where = 0x%-16.16llx",
                t->focused ? "* " : "", t->ID, t->tid, t->thread_state.__pc);

        char *frstr = NULL;
        create_frame_string(t->thread_state.__pc, &frstr);

        concat(outbuffer, " %s, name = '%s'\n", frstr, t->tname);
        free(frstr);
    }
    TH_END_LOCKED_FOREACH;

    return CMD_SUCCESS;
}

enum cmd_error_t cmdfunc_thread_select(struct cmd_args *args, 
        int arg1, char **outbuffer, char **error){
    char *thread_id_str = argcopy(args, THREAD_SELECT_COMMAND_REGEX_GROUPS[0]);
    int thread_id = (int)strtol_err(thread_id_str, error);

    free(thread_id_str);

    if(*error)
        return CMD_FAILURE;

    if(thread_id < 1 || thread_id > debuggee->thread_count){
        concat(error, "out of bounds, must be [1, %d]", 
                debuggee->thread_count);
        return CMD_FAILURE;
    }

    int result = set_focused_thread_with_idx(thread_id);
    
    if(result){
        concat(error, "could not set focused thread to thread %d", thread_id);
        return CMD_FAILURE;
    }

    struct machthread *f = get_focused_thread();

    concat(outbuffer, "Selected thread %d, tid = %#llx, '%s'\n",
            f->ID, f->tid, f->tname);

    disassemble_at_location(f->thread_state.__pc, 4, outbuffer);

    return CMD_SUCCESS;
}
