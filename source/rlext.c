#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>

void rl_line_buffer_replace(char *with){
    rl_delete_text(0, rl_end);
    rl_point = rl_end = rl_mark = 0;
    rl_insert_text(with);
}

char **rl_line_buffer_word_array(int *len){
    char *rl_line_buffer_cpy = strdup(rl_line_buffer);

    *len = 0;
    char **words = malloc(*len);

    char *word = strtok_r(rl_line_buffer_cpy, " ", &rl_line_buffer_cpy);

    while(word){
        words = realloc(words, sizeof(char *) * (++(*len)));
        words[*len - 1] = word;

        word = strtok_r(NULL, " ", &rl_line_buffer_cpy);
    }

    free(rl_line_buffer_cpy);

    return words;
}

