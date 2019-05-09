#ifndef _DOCFUNC_H_
#define _DOCFUNC_H_

#include "cmd.h"

void show_all_top_level_cmds(void);

/* Receives a dbg_cmd_t struct and displays its
 * documentation. Only called from
 * prepare_and_call_cmdfunc.
 */
void documentation_for_cmd(struct dbg_cmd_t *);

/* Receives a string, denoting the command name,
 * and shows documentation. Only called by
 * cmdfunc_help.
 */
void documentation_for_cmdname(char *, char **);

#endif
