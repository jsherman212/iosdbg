#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expr.h"

static int _concat_internal(char **dst, const char *src, va_list args){
    if(!src || !dst)
        return 0;

    if(!(*dst)){
        *dst = malloc(0);
        *(*dst) = '\0';
    }
    
    size_t srclen = strlen(src);
    size_t dstlen = strlen(*dst);

    const size_t needed = vsnprintf(NULL, 0, src, args);
    char *dst1 = malloc(srclen + dstlen + needed);
    strncpy(dst1, *dst, dstlen + 1);

    int w = vsnprintf(&dst1[dstlen], srclen + dstlen + needed, src, args);

    char *dst2 = realloc(dst1, strlen(dst1) + 1);
    *dst = dst2;

    return w;
}

int vconcat(char **dst, const char *src, va_list args){
    return _concat_internal(dst, src, args);
}

int concat(char **dst, const char *src, ...){
    va_list args;
    va_start(args, src);

    int w = _concat_internal(dst, src, args);

    va_end(args);

    return w;
}

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

    char *target_rea = realloc(*target, targetlen + slen + 1);
    *target = target_rea;
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
    if(!(*target))
        return;

    while(isblank((*target)[0]))
        memmove(*target, *target + 1, strlen(*target));

    if(strlen(*target) == 0)
        return;

    while(isblank((*target)[strlen(*target) - 1]))
        (*target)[strlen(*target) - 1] = '\0';
}

long strtol_err(char *str, char **error){
    if(!str){
        concat(error, "NULL argument `str`");
        return -1;
    }

    char *endptr = NULL;
    long result = strtol(str, &endptr, 0);

    if(endptr && *endptr != '\0'){
        concat(error, "invalid number '%s'", str);
        return -1;
    }

    return result;
}

long double strtold_err(char *str, char **error){
    if(!str){
        concat(error, "NULL argument `str`");
        return -1.0;
    }

    char *endptr = NULL;
    long double result = strtold(str, &endptr);

    if(endptr && *endptr != '\0'){
        concat(error, "invalid number '%s'", str);
        return -1.0;
    }

    return result;
}

int is_number_slow(char *str){
    if(!str)
        return 0;

    char *error = NULL;
    eval_expr(str, &error);

    if(error){
        free(error);
        return 0;
    }

    return 1;
}

int is_number_fast(char *str){
    if(!str)
        return 0;

    char *error = NULL;
    strtol_err(str, &error);

    if(error){
        free(error);
        return 0;
    }
    
    return 1;
}

char **token_array(char *str, const char *delim, int *len){
    if(!str || !delim || !len)
        return NULL;

    char *str_cpy = strdup(str);

    *len = 0;
    char **words = malloc(*len);

    char *word = strtok_r(str_cpy, delim, &str_cpy);

    while(word){
        char **words_rea = realloc(words, sizeof(char *) * (++(*len)));
        words = words_rea;
        words[*len - 1] = strdup(word);
        word = strtok_r(NULL, " ", &str_cpy);
    }

    free(str_cpy);

    return words;
}

void token_array_free(char **arr, int len){
    for(int i=0; i<len; i++){
        free(*(arr + i));
        *(arr + i) = NULL;
    }

    free(arr);
    arr = NULL;
}

char *strnran(size_t len){
    const char *chars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    const size_t charslen = strlen(chars);

    char *rstr = malloc(len + 1);

    for(size_t i=0; i<len; i++)
        rstr[i] = chars[rand() % charslen];

    rstr[len] = '\0';

    return rstr;
}

int is_whitespace(char *str){
    if(!str)
        return 0;

    size_t len = strlen(str);
    int idx = len;

    while(idx >= 0){
        if(isalnum(str[idx--]))
            return 0;
    }

    return 1;
}
