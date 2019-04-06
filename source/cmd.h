#ifndef _CMD_H_
#define _CMD_H_

#define MAX_GROUPS 4
#define NUM_TOP_LEVEL_COMMANDS 20

#include "argparse.h"

struct regexinfo {
    char *argregex;

    /* How many different capture groups argregex has. */
    int num_groups;

    /* If there is an unknown amount of arguments. For example, the user
     * can set multiple breakpoints or delete multiple convenience variables
     * with one command. In these cases, the group with an unknown amount of
     * results has to be the last item in groupnames.
     */
    int unk_num_args;

    /* Names of the groups, in the order they're expected in the command. */
    char *groupnames[MAX_GROUPS];
};

struct dbg_cmd_t {
    char *name;
    char *alias;
    char *documentation;
    
    struct regexinfo rinfo;

    /*
     * The array of subcmds can be thought as a tree,
     * which can be traversed level-order via queues.
     */
    struct dbg_cmd_t **subcmds;

    int level;
    int parentcmd;

    /* struct cmd_args_t * */
    enum cmd_error_t (*function)(struct cmd_args_t *, int, char **);
};

struct matchedcmdinfo_t {
    char *args;
    struct regexinfo rinfo;
    
    /* struct cmd_args_t * */
    enum cmd_error_t (*function)(struct cmd_args_t *, int, char **);
};

struct dbg_cmd_t *COMMANDS[NUM_TOP_LEVEL_COMMANDS];

#endif
