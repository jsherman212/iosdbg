#ifndef _DBGCMD_H_
#define _DBGCMD_H_

#include <stdlib.h>

#include "argparse.h"
#include "sigcmd.h"

#define NUM_CMDS 27

/* When --waitfor is included as an argument for 'attach'. */
extern int keep_checking_for_process;

#define MAX_GROUPS 4

struct dbg_cmd_t {
    const char *name;
    const char *alias;
    const char *desc;

    struct regexinfo {
        const char *argregex;

        /* How many different capture groups argregex has. */
        int num_groups;

        /* If there is an unknown amount of arguments. For example, the user
         * can set multiple breakpoints or delete multiple convenience variables
         * with one command. In these cases, the group with an unknown amount of
         * results has to be the last item in groupnames.
         */
        int unk_num_args;

        /* Names of the groups, in the order they're expected in the command. */
        const char *groupnames[MAX_GROUPS];
    } rinfo;

    enum cmd_error_t (*function)(struct cmd_args_t *, int, char **);
};

/*
*/
enum cmd_error_t execute_command(char *, char **);

void initialize_commands(void);

#endif
