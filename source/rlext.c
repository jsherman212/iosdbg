#include <stdio.h>

#include <readline/readline.h>

void rl_line_buffer_replace(char *with){
    rl_delete_text(0, rl_end);
    rl_point = rl_end = rl_mark = 0;
    rl_insert_text(with);
}
