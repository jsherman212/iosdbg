#ifndef _PRINTING_H_
#define _PRINTING_H_

enum {
    MAIN_THREAD,
    NOT_MAIN_THREAD
};

int rl_printf(int, const char *, ...);
void notify_of_reprompt(void);

#endif
