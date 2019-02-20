#ifndef _CONVVAR_H_
#define _CONVVAR_H_

enum convvar_kind {
	CONVVAR_VOID_KIND,
	CONVVAR_INTEGER,
	CONVVAR_DOUBLE,
	CONVVAR_STRING
};

union convvar_data {
	long long integer;
	char *string;
};

enum convvar_state {
	CONVVAR_VOID,
	CONVVAR_NONVOID
};

struct convvar {
	char *name;
	enum convvar_kind kind;
	union convvar_data data;
	enum convvar_state state;
};

void set_convvar(char *, char *, char **);
void del_convvar(char *, char **);
struct convvar *lookup_convvar(char *);
char *convvar_strval(char *, char **);
void void_convvar(char *);
void show_all_cvars(void);
void p_convvar(char *);
void convvar_free(struct convvar *);
void desc_auto_convvar_error_if_needed(char *, char *);

#endif
