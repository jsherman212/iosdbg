#include <stdio.h>

#include <readline/readline.h>
#include <readline/history.h>

/* Prevent double reprompts. */
void safe_reprompt(void){
    char *linecopy = rl_copy_text(0, rl_end);
    
    rl_line_buffer[rl_point = rl_end = rl_mark = 0] = 0;
    
    if(RL_ISSTATE(RL_STATE_READCMD)){
        rl_on_new_line();
        rl_forced_update_display();
        rl_insert_text(linecopy);
        rl_redisplay();
    }
}
