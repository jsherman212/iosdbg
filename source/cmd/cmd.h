#ifndef _CMD_H_
#define _CMD_H_

#define NUM_TOP_LEVEL_COMMANDS 19

#include "argparse.h"       /* Defines MAX_GROUPS */

enum cmd_error_t do_cmdline_command(char *, char **, char **);

/* Macros for arguments to create_(parent|child)_cmd.
 * These exist for clarity.
 */
#define _AT_LEVEL(x) (x)
#define _NUM_GROUPS(x) (x)
#define _UNK_ARGS(x) (x)
#define _NUM_SUBCMDS(x) (x)

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

    enum cmd_error_t (*cmd_function)(struct cmd_args_t *, int, char **);
    void (*audit_function)(struct cmd_args_t *, char **);
};

struct matchedcmdinfo_t {
    char *args;
    struct regexinfo rinfo;
    struct dbg_cmd_t *cmd;
    enum cmd_error_t (*cmd_function)(struct cmd_args_t *, int, char **);
    void (*audit_function)(struct cmd_args_t *, char **);
};

struct dbg_cmd_t *COMMANDS[NUM_TOP_LEVEL_COMMANDS];

static const char *NO_ARGUMENT_REGEX = "";
static const char *NO_GROUPS[MAX_GROUPS] = {};

#endif
