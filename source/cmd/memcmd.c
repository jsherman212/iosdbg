#include <stdio.h>
#include <stdlib.h>

#include "argparse.h"

enum cmd_error_t cmdfunc_memoryfind(struct cmd_args_t *args,
        int arg1, char **error){
    printf("cmdfunc memory find\n");

    return CMD_SUCCESS;
}
