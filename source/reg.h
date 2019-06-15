#ifndef _REG_H_
#define _REG_H_

#include "thread.h"

enum format {
    DECIMAL,
    HEXADECIMAL
};

/* Return a string containing the value of the register requested. */
char *fetch_reg(struct machthread *, enum format, char *, char **);

#endif
