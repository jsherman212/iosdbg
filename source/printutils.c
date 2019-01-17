#include "printutils.h"

// readline compatible printf
// allows caller to choose whether or not to redisplay the readline prompt and accept commands
int rl_printf(int reprompt, const char *message, ...){
	va_list args;
	va_start(args, message);

	int ret = vprintf(message, args);

	va_end(args);
	
	if(strrchr(message, '\n'))	
		rl_on_new_line();
	
	rl_replace_line("", 0);
	
	if(reprompt)
		rl_redisplay();
	
	return ret;
}	

/* Prevent double reprompts. */
void safe_reprompt(void){
	/* If the user types something before we are prompted,
	 * colors do not get reset, so we have to manually
	 * reset them.
	 */
	char *linecopy = rl_copy_text(0, rl_end);
	
	rl_line_buffer[rl_point = rl_end = rl_mark = 0] = 0;
	
	if(RL_ISSTATE(RL_STATE_READCMD)){
		rl_on_new_line();
		rl_forced_update_display();
	}

	printf("\e[0m");

	rl_insert_text(linecopy);
	rl_redisplay();
}
