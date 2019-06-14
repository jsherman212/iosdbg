#ifndef _PRINTING_H_
#define _PRINTING_H_

enum {
    WAIT_FOR_REPROMPT,
    DONT_WAIT_FOR_REPROMPT 
};

int rl_printf(int, const char *, ...);

#endif
