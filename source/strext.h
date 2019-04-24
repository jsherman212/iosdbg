#ifndef _STREXT_H_
#define _STREXT_H_

void strins(char **, char *, int);
void strcut(char **, int, int);
char *substr(char *, int, int);
char *strrstr(char *, char *);
void strclean(char **);
int is_number_slow(char *);
int is_number_fast(char *);
long strtol_err(char *, char **);
double strtod_err(char *, char **);
int concat(char **dst, const char *src, ...);

#endif
