#ifndef _DBGCMD_H_
#define _DBGCMD_H_

#include "argparse.h"

/* When --waitfor is included as an argument for 'attach'. */
extern int keep_checking_for_process;

enum cmd_error_t cmdfunc_aslr(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_attach(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_backtrace(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_break(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_continue(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_delete(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_detach(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_disassemble(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_examine(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_help(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_kill(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_quit(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_regsfloat(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_regsgen(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_set(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_show(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_stepi(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_threadlist(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_threadselect(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_trace(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_unset(struct cmd_args_t *, int, char **);
enum cmd_error_t cmdfunc_watch(struct cmd_args_t *, int, char **);

#endif
