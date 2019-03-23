#ifndef _DBGCMD_H_
#define _DBGCMD_H_

#include <stdlib.h>

#include "argparse.h"

typedef int cmd_error_t;

#define CMD_SUCCESS (cmd_error_t)0
#define CMD_FAILURE (cmd_error_t)1

#define PT_DETACH   11
#define PT_SIGEXC   12
#define PT_ATTACHEXC    14
#define PT_THUPDATE 13

/* When --waitfor is included as an argument for 'attach'. */
extern int keep_checking_for_process;

#define MAX_GROUPS 3

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

    cmd_error_t (*function)(struct cmd_args_t *, int, char **);
};

cmd_error_t cmdfunc_aslr(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_attach(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_backtrace(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_break(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_continue(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_delete(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_detach(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_disassemble(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_examine(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_help(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_kill(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_quit(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_regsfloat(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_regsgen(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_set(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_show(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_stepi(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_threadlist(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_threadselect(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_trace(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_unset(struct cmd_args_t *, int, char **);
cmd_error_t cmdfunc_watch(struct cmd_args_t *, int, char **);

cmd_error_t execute_command(char *, char **);

void initialize_commands(void);

#endif
