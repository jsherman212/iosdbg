#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char answer(const char *question, ...){
    va_list args;
    va_start(args, question);

    vprintf(question, args);

    va_end(args);

    char *answer = NULL;
    size_t len;
    
    getline(&answer, &len, stdin);
    answer[strlen(answer) - 1] = '\0';
    
    /* Allow the user to hit enter as another way
     * of saying yes.
     */
    if(strlen(answer) == 0)
        return 'y';

    char ret = tolower(answer[0]);

    while(ret != 'y' && ret != 'n'){
        va_list args;
        va_start(args, question);

        vprintf(question, args);

        va_end(args);

        free(answer);
        answer = NULL;

        getline(&answer, &len, stdin);
        answer[strlen(answer) - 1] = '\0';
        
        ret = tolower(answer[0]);
    }
    
    free(answer);

    return ret;
}
