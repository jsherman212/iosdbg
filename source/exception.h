#ifndef _EXCEPTION_H_
#define _EXCEPTION_H_

#include "defs.h"

void handle_exception(Request *);
void reply_to_exception(Request *, kern_return_t);

#endif
