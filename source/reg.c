#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "reg.h"
#include "strext.h"
#include "thread.h"

enum type {
    INTEGER,
    LONG
};

static char *num_to_str(enum type type, enum format format, void *num){
    char *str = NULL;

    if(type == INTEGER){
        int n = *(int *)num;
        
        if(format == DECIMAL)
            concat(&str, "%d", n);
        else
            concat(&str, "%#x", n);
    }
    else{
        long n = *(long *)num;

        if(format == DECIMAL)
            concat(&str, "%ld", n);
        else
            concat(&str, "%#lx", n);
    }

    return str;
}

/* `format` is ignored when requesting floating point registers. */
char *fetch_reg(struct machthread *thread, enum format format,
        char *reg, char **error){
    get_thread_state(thread);
    get_neon_state(thread);

    if(!reg || strlen(reg) == 0){
        concat(error, "invalid register");
        return NULL;
    }

    /* Get rid of the '$'. */
    if(reg[0] == '$')
        memmove(reg, reg + 1, strlen(reg));

    printf("%s: reg '%s'\n", __func__, reg);

    const size_t len = strlen(reg);

    if(len <= 1){
        concat(error, "invalid register '%s'", reg);
        return NULL;
    }

    for(int i=0; i<len; i++)
        reg[i] = tolower(reg[i]);

    printf("%s: reg '%s'\n", __func__, reg);

    /* Take care of the named registers. */
    if(strcmp(reg, "fp") == 0 || strcmp(reg, "x29") == 0)
        return num_to_str(LONG, format, &(thread->thread_state.__fp));
    else if(strcmp(reg, "lr") == 0 || strcmp(reg, "x30") == 0)
        return num_to_str(LONG, format, &(thread->thread_state.__lr));
    else if(strcmp(reg, "sp") == 0 || strcmp(reg, "x31") == 0)
        return num_to_str(LONG, format, &(thread->thread_state.__sp));
    else if(strcmp(reg, "pc") == 0)
        return num_to_str(LONG, format, &(thread->thread_state.__pc));
    else if(strcmp(reg, "cpsr") == 0)
        return num_to_str(INTEGER, format, &(thread->thread_state.__cpsr));
    else if(strcmp(reg, "fpsr") == 0)
        return num_to_str(INTEGER, format, &(thread->neon_state.__fpsr));
    else if(strcmp(reg, "fpcr") == 0)
        return num_to_str(INTEGER, format, &(thread->neon_state.__fpcr));

    char type = reg[0];

    char *e = NULL;
    int which = (int)strtol_err(reg + 1, &e);

    if(e){
        concat(error, "%s", e);
        free(e);
        return NULL;
    }

    printf("%s: type %c which %d\n", __func__, type, which);

    int good_reg = (which >= 0 && which <= 31);
    int good_type = (type == 'q' || type == 'v' || type == 'd' ||
            type == 's' || type == 'x' || type == 'w');

    if(!good_reg || !good_type){
        concat(error, "invalid register '%s'", reg);
        return NULL;
    }

    printf("%s: final register %c%d\n", __func__, type, which);

    /* Quadword registers get displayed different than the rest. */
    if(type == 'q' || type == 'v'){
        long hi = thread->neon_state.__v[which] >> 64;
        long lo = thread->neon_state.__v[which];

        char *str = NULL;
        concat(&str, "v%d = {", which);

        for(int i=0; i<sizeof(long); i++)
            concat(&str, "0x%02x ", *(uint8_t *)((uint8_t *)(&lo) + i));

        for(int i=0; i<sizeof(long) - 1; i++)
            concat(&str, "0x%02x ", *(uint8_t *)((uint8_t *)(&hi) + i));

        concat(&str, "0x%02x}",
                *(uint8_t *)((uint8_t *)(&hi) + (sizeof(long) - 1)));

        return str;
    }

    if(type == 's')
        return num_to_str(INTEGER, format, &(thread->neon_state.__v[which]));
    else if(type == 'd')
        return num_to_str(LONG, format, &(thread->neon_state.__v[which]));
    else if(type == 'w')
        return num_to_str(INTEGER, format, &(thread->thread_state.__x[which]));
    else if(type == 'x')
        return num_to_str(LONG, format, &(thread->thread_state.__x[which]));

    return NULL;
}
