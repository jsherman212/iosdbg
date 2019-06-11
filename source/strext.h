#ifndef _STREXT_H_
#define _STREXT_H_

#include <stdarg.h>

void strins(char **, char *, int);
void strcut(char **, int, int);
char *substr(char *, int, int);
char *strrstr(char *, char *);
void strclean(char **);
long strtol_err(char *, char **);
long double strtold_err(char *, char **);
int is_number_slow(char *);
int is_number_fast(char *);
int concat(char **dst, const char *src, ...);
int vconcat(char **dst, const char *src, va_list args);
char **token_array(char *, const char *, int *);
void token_array_free(char **, int);
char *strnran(size_t);

#endif
