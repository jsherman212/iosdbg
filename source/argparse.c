#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argparse.h"
#include "convvar.h"

int wants_add_aslr(char *str){
	/* This convenience variable allows the user to tell iosdbg
	 * to ignore ASLR no matter what.
	 */
	char *error;
	char *no_aslr_override = convvar_strval("$NO_ASLR_OVERRIDE", &error);

	if(no_aslr_override && strcmp(no_aslr_override, "void") != 0)
		return 0;

	if(!str)
		return 1;

	return strnstr(str, "--no-aslr", strlen(str)) == NULL;
}

struct arguments_t *parse_args(char *_args, char **error){
	char *args;

	if(!args_)
		args = strdup("");
	else
		args = strdup(_args);

	struct arguments_t *arguments = malloc(sizeof(struct arguments_t));

	arguments->argqueue = queue_new();
	arguments->num_args = 0;
	arguments->add_aslr = wants_add_aslr(args);

	char *token = strtok(args, " ");

	while(token){
		enqueue(arguments->argqueue, strdup(token));
		arguments->num_args++;
		token = strtok(NULL, " ");
	}

	free(args);

	return arguments;
}

char *argpeek(struct arguments_t *args){
	if(!args)
		return NULL;

	if(!args->argsqueue)
		return NULL;


}

char *argnext(struct arguments_t *args){
	if(!args)
		return NULL;

	if(!args->argqueue)
		return NULL;

	return dequeue(args->argqueue);
}

void argfree(struct arguments_t *args){
	if(!args)
		return;

	if(args->argqueue)
		queue_free(args->argqueue);

	free(args);
}
