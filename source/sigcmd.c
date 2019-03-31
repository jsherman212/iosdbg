#include <stdio.h>

#include "dbgops.h"
#include "defs.h"
#include "sigcmd.h"

enum cmd_error_t cmdfunc_signalhandle(struct cmd_args_t *args, 
        int arg1, char **error){
    ops_printsiginfo();
    if(!args){
        printf("null args\n");
        return CMD_FAILURE;
    }

    char *signals = argnext(args);
    char *notify_str = argnext(args);
    char *pass_str = argnext(args);
    char *stop_str = argnext(args);
    
    

    printf("signals '%s', notify '%s', pass '%s', stop '%s'\n",
            signals, notify_str, pass_str, stop_str);


    return CMD_SUCCESS;
}
