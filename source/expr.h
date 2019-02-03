#ifndef _EXPR_H_
#define _EXPR_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "stack.h"

long parse_expr(char *, char **);

#endif
