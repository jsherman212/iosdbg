#ifndef _REG_H_
#define _REG_H_

#include "thread.h"

enum format {
    DECIMAL,
    HEXADECIMAL
};

enum regtype {
    NONE,
    INTEGER,
    LONG,
    FLOAT,
    DOUBLE,
    QUADWORD
};

long regtol(struct machthread *, enum format, enum regtype *, char *,
        char **, char **, char **);
void setreg(struct machthread *, char *, char *, char **);

#endif
