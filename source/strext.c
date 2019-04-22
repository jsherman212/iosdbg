#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expr.h"

/* Insert `str` at `where` in `target`. */
void strins(char **target, char *str, int where){
    if(!target || !(*target))
        return;

    if(!str)
        return;

    size_t targetlen = strlen(*target);

    if(where < 0 || where > targetlen)
        return;

    size_t slen = strlen(str);

    if(slen == 0)
        return;

    *target = realloc(*target, targetlen + slen + 1);
    (*target)[targetlen + slen] = '\0';
    char *saved = strdup(*target + where);
    strncpy(*target + where, str, slen);
    strncpy(*target + slen + where, saved, strlen(saved));
    free(saved);
}

/* Cut [start, start+bytes] from `target`. */
void strcut(char **target, int start, int bytes){
    if(!target || !(*target))
        return;

    size_t targetlen = strlen(*target);

    if(start < 0 || start > targetlen)
        return;

    int endidx = start + bytes;

    if(start == 0 && endidx == 0){
        (*target)[0] = '\0';
        return;
    }

    if(bytes <= 0 || endidx > targetlen)
        return;

    char *saved = strdup(*target + endidx);
    size_t savedlen = strlen(saved);
    strncpy(*target + start, saved, savedlen);
    free(saved);
    (*target)[start + savedlen] = '\0';
}

/* Return a substring of [start, start+len].
 * Must be freed.
 */
char *substr(char *str, int start, int len){
    if(!str)
        return NULL;

    size_t slen = strlen(str);

    if(start < 0 || start > slen)
        return NULL;

    if(len <= 0 || (start + len) > slen)
        return NULL;

    return strndup(str + start, len);
}

char *strrstr(char *s1, char *s2){
    if(*s2 == '\0')
        return s1;

    char *s1cpy = s1 + strlen(s1);

    while(s1cpy != s1){
        s1cpy--;

        char *s1cpy_cpy = s1cpy;
        char *s2cpy = s2;

        for(;;){
            if(*(s1cpy_cpy++) != *(s2cpy++))
                break;
            else if(*s2cpy == '\0')
                return s1cpy;
        }
    }

    return NULL;
}

void strclean(char **target){
    while(isblank((*target)[0]))
        memmove((*target), (*target) + 1, strlen((*target)));

    if(strlen(*target) == 0)
        return;

    while(isblank((*target)[strlen((*target)) - 1]))
        (*target)[strlen((*target)) - 1] = '\0';
}

int is_number_slow(char *str){
    if(!str)
        return 0;

    char *error = NULL;
    parse_expr(str, &error);

    if(error){
        return 0;
        free(error);
    }

    return 1;
}

int is_number_fast(char *str){
    if(!str)
        return 0;

    size_t len = strlen(str);

    for(int i=0; i<len; i++){
        if(!isxdigit(str[i]))
            return 0;
    }

    return 1;
}

long strtol_err(char *str, char **error){
    if(!str){
        asprintf(error, "NULL argument `str`");
        return -1;
    }

    char *endptr = NULL;
    long result = strtol(str, &endptr, 0);

    if(endptr && *endptr != '\0'){
        asprintf(error, "invalid number '%s'", str);
        return -1;
    }

    return result;
}

double strtod_err(char *str, char **error){
    if(!str){
        asprintf(error, "NULL argument `str`");
        return -1.0;
    }

    char *endptr = NULL;
    double result = strtod(str, &endptr);

    if(endptr && *endptr != '\0'){
        asprintf(error, "invalid number '%s'", str);
        return -1.0;
    }

    return result;
}
