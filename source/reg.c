#include <ctype.h>
#include <limits.h>
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

static void clean_reg(char **reg, char **error){
    /* Get rid of the '$'. */
    if((*reg)[0] == '$')
        memmove(*reg, *reg + 1, strlen(*reg));

    //printf("%s: reg '%s'\n", __func__, *reg);

    const size_t len = strlen(*reg);

    if(len <= 1){
        concat(error, "invalid register '%s'", *reg);
        return;
    }

    for(int i=0; i<len; i++)
        (*reg)[i] = tolower((*reg)[i]);
}

static int good_reg(char type, int num){
    int good_reg = (num >= 0 && num <= 31);
    int good_type = (type == 'q' || type == 'v' || type == 'd' ||
            type == 's' || type == 'x' || type == 'w');

    return (good_reg && good_type);
}

char *regtoa(struct machthread *thread, enum format format,
        char *reg, char **error){
    if(!reg || strlen(reg) == 0){
        concat(error, "invalid register");
        return NULL;
    }

    clean_reg(&reg, error);

    if(*error)
        return NULL;

    printf("%s: reg '%s'\n", __func__, reg);

    get_thread_state(thread);
    get_neon_state(thread);

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
    int which = (int)strtol_err(reg + 1, error);

    if(*error)
        return NULL;

    printf("%s: type %c which %d\n", __func__, type, which);

    if(!good_reg(type, which)){
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

long regtol(struct machthread *thread, enum format format,
        char *reg, char **error){
    char *str = regtoa(thread, format, reg, error);

    if(!str)
        return LONG_MIN;

    long val = strtol_err(str, error);

    if(*error){
        free(str);
        return LONG_MIN;
    }

    free(str);

    return val;
}

void setreg(struct machthread *thread, char *reg,
        char *value, char **error){
    if(!reg || strlen(reg) == 0){
        concat(error, "invalid register");
        return;
    }

    if(!value || strlen(value) == 0){
        concat(error, "invalid value");
        return;
    }

    clean_reg(&reg, error);

    if(*error)
        return;

    printf("%s: reg '%s'\n", __func__, reg);

    // XXX needed?
    get_thread_state(thread);
    get_neon_state(thread);

    /* Refrain from doing error checking right away; we have to check for the
     * named registers.
     */
    char *e_d = NULL, *e_ld = NULL, *e_f = NULL, *e_lf = NULL;
    int value_d = (int)strtol_err(value, &e_d);
    long value_ld = strtol_err(value, &e_ld);
    float value_f = (float)strtold_err(value, &e_f);
    double value_lf = strtold_err(value, &e_ld);

    int fp = (strcmp(reg, "fp") == 0 || strcmp(reg, "x29") == 0);
    int lr = (strcmp(reg, "lr") == 0 || strcmp(reg, "x30") == 0);
    int sp = (strcmp(reg, "sp") == 0 || strcmp(reg, "x31") == 0);
    int pc = strcmp(reg, "pc") == 0;
    int cpsr = strcmp(reg, "cpsr") == 0;

    int fpsr = strcmp(reg, "fpsr") == 0;
    int fpcr = strcmp(reg, "fpcr") == 0;

    if(fp || lr || sp || pc){
        if(e_ld){
            concat(error, "invalid value '%s'", e_ld);
            free(e_ld);
            return;
        }

        if(fp)
            thread->thread_state.__fp = value_ld;
        else if(lr)
            thread->thread_state.__lr = value_ld;
        else if(sp)
            thread->thread_state.__sp = value_ld;
        else
            thread->thread_state.__pc = value_ld;

        goto done;
    }
    
    /*
    if(strcmp(reg, "fp") == 0 || strcmp(reg, "")==0){
        thread->thread_state.__fp = value_ld;
        goto done;
    }
    else if(strcmp(reg, "lr") == 0){
        thread->thread_state.__lr = value_ld;
        goto done;
    }
    else if(strcmp(reg, "sp") == 0){
        thread->thread_state.__sp = value_ld;
        goto done;
    }
    else if(strcmp(reg, "pc") == 0){
        thread->thread_state.__pc = value_ld;
        goto done;
    }
    else if(strcmp(reg, "cpsr") == 0){
        thread->thread_state.__cpsr = value_d;
        goto done;
    }
    else if(strcmp(reg, "fpsr") == 0){
        thread->neon_state.__fpsr = value_d;
        goto done;
    }
    else if(strcmp(reg, "fpcr") == 0){
        thread->neon_state.__fpcr = value_d;
        goto done;
    }*/

    char type = reg[0];
    int which = (int)strtol_err(reg + 1, error);

    if(!good_reg(type, which)){
        concat(error, "invalid register '%s'", reg);
        return;
    }

    int gpr = type == 'x' || type == 'w';
    int fpr = (type == 'q' || type == 'v') ||
        type == 'd' || type == 's';
    int quadword = fpr && (type == 'q' || type == 'v');
    

    printf("%s: type %c which %d gpr %d fpr %d quadword %d\n",
            __func__, type, which, gpr, fpr, quadword);


done:;
    set_thread_state(thread);
    set_neon_state(thread);
}
