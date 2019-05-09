#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>

#include "strext.h"

char **rl_line_buffer_word_array(int *len){
    return token_array(rl_line_buffer, " ", len);
}
