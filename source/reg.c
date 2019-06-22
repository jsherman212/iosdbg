#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "reg.h"
#include "strext.h"
#include "thread.h"

enum type {
    INTEGER_TYPE,
    LONG_TYPE
};

static char *num_to_str(enum type type, enum format format, void *num){
    char *str = NULL;

    if(type == INTEGER_TYPE){
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

static char *regtoa(struct machthread *thread, enum format format,
        enum regtype *outtype, char *reg,
        char **cleanedreg, char **valstr, char **error){
    if(!reg || strlen(reg) == 0){
        concat(error, "invalid register");
        return NULL;
    }

    clean_reg(&reg, error);

    if(*error)
        return NULL;

    if(cleanedreg)
        *cleanedreg = strdup(reg);

    get_thread_state(thread);
    get_neon_state(thread);

    if(strcmp(reg, "fp") == 0 || strcmp(reg, "x29") == 0){
        *outtype = LONG;
        char *s = num_to_str(LONG_TYPE, format, &(thread->thread_state.__fp));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }
    else if(strcmp(reg, "lr") == 0 || strcmp(reg, "x30") == 0){
        *outtype = LONG;

        char *s = num_to_str(LONG_TYPE, format, &(thread->thread_state.__lr));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }
    else if(strcmp(reg, "sp") == 0 || strcmp(reg, "x31") == 0){
        *outtype = LONG;

        char *s = num_to_str(LONG_TYPE, format, &(thread->thread_state.__sp));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }
    else if(strcmp(reg, "pc") == 0){
        *outtype = LONG;

        char *s = num_to_str(LONG_TYPE, format, &(thread->thread_state.__pc));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }
    else if(strcmp(reg, "cpsr") == 0){
        *outtype = INTEGER;

        char *s = num_to_str(INTEGER_TYPE, format, &(thread->thread_state.__cpsr));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }
    else if(strcmp(reg, "fpsr") == 0){
        *outtype = INTEGER;

        char *s = num_to_str(INTEGER_TYPE, format, &(thread->neon_state.__fpsr));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }
    else if(strcmp(reg, "fpcr") == 0){
        *outtype = INTEGER;

        char *s = num_to_str(INTEGER_TYPE, format, &(thread->neon_state.__fpcr));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }

    char type = reg[0];
    int which = (int)strtol(reg + 1, NULL, 10);

    if(!good_reg(type, which)){
        concat(error, "invalid register '%s'", reg);
        return NULL;
    }

    /* Quadword registers get displayed different than the rest. */
    if(type == 'q' || type == 'v'){
        *outtype = QUADWORD;

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

        if(valstr)
            *valstr = strdup(strchr(str, '{'));

        return str;
    }

    if(type == 's'){
        *outtype = FLOAT;

        char *s = num_to_str(INTEGER_TYPE, format, &(thread->neon_state.__v[which]));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }
    else if(type == 'd'){
        *outtype = DOUBLE;

        char *s = num_to_str(LONG_TYPE, format, &(thread->neon_state.__v[which]));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }
    else if(type == 'w'){
        *outtype = INTEGER;

        char *s = num_to_str(INTEGER_TYPE, format, &(thread->thread_state.__x[which]));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }
    else if(type == 'x'){
        *outtype = LONG;

        char *s = num_to_str(LONG_TYPE, format, &(thread->thread_state.__x[which]));

        if(valstr)
            *valstr = strdup(s);

        return s;
    }

    return NULL;
}

long regtol(struct machthread *thread, enum format format, enum regtype *outtype,
        char *reg, char **cleanedreg, char **valstr, char **error){
    char *str = regtoa(thread, format, outtype, reg, cleanedreg, valstr, error);

    if(!str)
        return LONG_MIN;

    if(*outtype != QUADWORD){
        long val = strtol_err(str, error);

        if(*error){
            free(str);
            return LONG_MIN;
        }

        free(str);

        return val;
    }

    free(str);

    return LONG_MIN;
}

static inline void free_estrs(char *e1, char *e2, char *e3, char *e4){
    free(e1);
    free(e2);
    free(e3);
    free(e4);

    e1 = NULL;
    e2 = NULL;
    e3 = NULL;
    e4 = NULL;
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

    /* Refrain from doing error checking right away; we have to check for the
     * named registers.
     */
    char *e_d = NULL, *e_ld = NULL, *e_f = NULL, *e_lf = NULL;

    int value_d = (int)strtol_err(value, &e_d);
    long value_ld = strtol_err(value, &e_ld);
    float value_f = (float)strtold_err(value, &e_f);
    double value_lf = strtold_err(value, &e_lf);

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
            free_estrs(e_d, e_ld, e_f, e_lf);
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

        free_estrs(e_d, e_ld, e_f, e_lf);

        goto done;
    }
    else if(cpsr || fpsr || fpcr){
        if(e_d){
            concat(error, "invalud value '%s'", e_d);
            free_estrs(e_d, e_ld, e_f, e_lf);
            return;
        }

        if(cpsr)
            thread->thread_state.__cpsr = value_d;
        else if(fpsr)
            thread->neon_state.__fpsr = value_d;
        else
            thread->neon_state.__fpcr = value_d;

        free_estrs(e_d, e_ld, e_f, e_lf);

        goto done;
    }

    char type = reg[0];
    int which = (int)strtol_err(reg + 1, error);

    if(*error){
        free_estrs(e_d, e_ld, e_f, e_lf);
        return;
    }

    if(!good_reg(type, which)){
        concat(error, "invalid register '%s'", reg);
        free_estrs(e_d, e_ld, e_f, e_lf);
        return;
    }

    int gpr = type == 'x' || type == 'w';
    int fpr = (type == 'q' || type == 'v') ||
        type == 'd' || type == 's';
    int quadword = fpr && (type == 'q' || type == 'v');
    
    /* strtol_err was called for e_d and e_ld, so if an error is set in one,
     * it will be set in the other.
     */
    if(gpr && e_d && e_ld){
        concat(error, "%s", e_d);
        free_estrs(e_d, e_ld, e_f, e_lf);
        return;
    }

    /* Same thing below. Quadword strings are processed manually so an error
     * for that case is ignored.
     */
    if(fpr && !quadword && e_f && e_lf){
        concat(error, "%s", e_f);
        free_estrs(e_d, e_ld, e_f, e_lf);
        return;
    }

    if(gpr){
        if(type == 'x')
            thread->thread_state.__x[which] = value_ld;
        else{
            thread->thread_state.__x[which] &= ~0xFFFFFFFFULL;
            thread->thread_state.__x[which] |= value_d;
        }
    }
    else if(type == 'q' || type == 'v'){
        if(value[0] != '{' ||
                value[strlen(value) - 1] != '}'){
            concat(error, "bad value '%s'", value);
            free_estrs(e_d, e_ld, e_f, e_lf);
            return;
        }

        if(strlen(value) == 2){
            concat(error, "bad value '%s'", value);
            free_estrs(e_d, e_ld, e_f, e_lf);
            return;
        }

        /* Remove the brackets. */
        value[strlen(value) - 1] = '\0';
        memmove(value, value + 1, strlen(value));

        char *hi_str = NULL, *lo_str = NULL;

        for(int i=0; i<sizeof(long)*2; i++){
            char *space = strrchr(value, ' ');
            char *curbyte = NULL;

            if(space){
                curbyte = strdup(space + 1);

                /* Truncate what we've already processed. */
                space[0] = '\0';
            }
            else{
                curbyte = strdup(value);
            }

            unsigned int byte =
                (unsigned int)strtol(curbyte, NULL, 0);

            if(i < sizeof(long))
                concat(&lo_str, "%02x", byte);
            else
                concat(&hi_str, "%02x", byte);

            free(curbyte);
        }

        long hi = strtoul(hi_str, NULL, 16);
        long lo = strtoul(lo_str, NULL, 16);

        /* Since this is a 128 bit "number", we have to split it
         * up into two 64 bit pointers to correctly modify it.
         */
        long *H = (long *)(&thread->neon_state.__v[which]);
        long *L = (long *)(&thread->neon_state.__v[which]) + 1;

        *H = hi;
        *L = lo;

        free(hi_str);
        free(lo_str);
    }
    else if(type == 'd'){
        thread->neon_state.__v[which] = *(long *)&value_lf;
    }
    else{
        thread->neon_state.__v[which] = *(int *)&value_f;
    }

    free_estrs(e_d, e_ld, e_f, e_lf);

done:;
    set_thread_state(thread);
    set_neon_state(thread);
}
