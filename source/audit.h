#ifndef _AUDIT_H_
#define _AUDIT_H_

#include "cmd.h"

void audit_aslr(struct cmd_args_t *, char **);
void audit_attach(struct cmd_args_t *, char **);
void audit_backtrace(struct cmd_args_t *, char **);
void audit_break(struct cmd_args_t *, char **);
void audit_continue(struct cmd_args_t *, char **);
void audit_delete(struct cmd_args_t *, char **);
void audit_detach(struct cmd_args_t *, char **);
void audit_disassemble(struct cmd_args_t *, char **);
void audit_examine(struct cmd_args_t *, char **);
void audit_help(struct cmd_args_t *, char **);
void audit_kill(struct cmd_args_t *, char **);
void audit_quit(struct cmd_args_t *, char **);
void audit_regs_float(struct cmd_args_t *, char **);
void audit_regs_gen(struct cmd_args_t *, char **);
void audit_set(struct cmd_args_t *, char **);
void audit_show(struct cmd_args_t *, char **);
void audit_signal_handle(struct cmd_args_t *, char **);
void audit_stepi(struct cmd_args_t *, char **);
void audit_thread_list(struct cmd_args_t *, char **);
void audit_thread_select(struct cmd_args_t *, char **);
void audit_trace(struct cmd_args_t *, char **);
void audit_unset(struct cmd_args_t *, char **);
void audit_watch(struct cmd_args_t *, char **);

#endif
