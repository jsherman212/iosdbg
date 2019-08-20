#include <stdio.h>
#include <stdlib.h>

#include "symcmd.h"

#include "../symbol/sym.h"

#include "../debuggee.h"
#include "../strext.h"

enum cmd_error_t cmdfunc_symbols_add(struct cmd_args_t *args, int arg1,
        char **outbuffer, char **error){
    char *filepath = argcopy(args, SYMBOLS_ADD_COMMAND_REGEX_GROUPS[0]);

    sym_error_t sym_err = {0};
    if(sym_init_with_dwarf_file(filepath, &debuggee->dwarfinfo, &sym_err)){
        concat(error, "failed: %s\n", sym_strerror(sym_err));
        return CMD_FAILURE;
    }

    free(filepath);

    return CMD_SUCCESS;
}
