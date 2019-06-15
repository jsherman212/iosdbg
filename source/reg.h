#ifndef _REG_H_
#define _REG_H_

#include "thread.h"

enum format {
    DECIMAL,
    HEXADECIMAL
};

char *regtoa(struct machthread *, enum format, char *, char **);
long regtol(struct machthread *, enum format, char *, char **);
void setreg(struct machthread *, char *, char *, char **);

#endif
