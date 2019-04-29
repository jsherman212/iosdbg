#ifndef _MEMCMD_H_
#define _MEMCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_memoryfind(struct cmd_args_t *, int, char **);

static const char *MEMORY_COMMAND_DOCUMENTATION =
    "Memory command class.\n";

static const char *MEMORY_FIND_COMMAND_DOCUMENTATION =
    "Memory find documentation\n";

#endif
