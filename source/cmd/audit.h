#ifndef _AUDIT_H_
#define _AUDIT_H_

#include "cmd.h"

void audit_aslr(struct cmd_args *, const char **, char **);
void audit_attach(struct cmd_args *, const char **, char **);
void audit_backtrace(struct cmd_args *, const char **, char **);
void audit_breakpoint_set(struct cmd_args *, const char **, char **);
void audit_continue(struct cmd_args *, const char **, char **);
void audit_detach(struct cmd_args *, const char **, char **);
void audit_disassemble(struct cmd_args *, const char **, char **);
void audit_evaluate(struct cmd_args *, const char **, char **);
void audit_examine(struct cmd_args *, const char **, char **);
void audit_kill(struct cmd_args *, const char **, char **);
void audit_memory_find(struct cmd_args *, const char **, char **);
void audit_memory_write(struct cmd_args *, const char **, char **);
void audit_register_view(struct cmd_args *, const char **, char **);
void audit_register_write(struct cmd_args *, const char **, char **);
void audit_signal_deliver(struct cmd_args *, const char **, char **);
void audit_step_inst_into(struct cmd_args *, const char **, char **);
void audit_step_inst_over(struct cmd_args *, const char **, char **);
void audit_symbols_add(struct cmd_args *, const char **, char **);
void audit_thread_list(struct cmd_args *, const char **, char **);
void audit_thread_select(struct cmd_args *, const char **, char **);
void audit_watchpoint_set(struct cmd_args *, const char **, char **);
void audit_variable_print(struct cmd_args *, const char **, char **);
void audit_variable_set(struct cmd_args *, const char **, char **);
void audit_variable_unset(struct cmd_args *, const char **, char **);

#endif
