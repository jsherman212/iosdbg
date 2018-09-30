#include "matchcmd.h"

// Convenient free wrapper for a matchcmd_result struct
void matchcmd_result_free(struct matchcmd_result *result){
	free(result->matchingcmds);

	if(result->correctcmd)
		free(result->correctcmd);

	free(result);
}

// Attempt to "auto complete" a user's command
struct matchcmd_result *matchcmd(const char *command){
	char *regexstr = malloc(1024);
	
	// match any command that starts with the first part of the parameter
	strcpy(regexstr, "^");

	// cast to silence warnings
	char *partialpart = strtok((char *)command, " ");

	struct matchcmd_result *result = malloc(sizeof(struct matchcmd_result));
	result->matchingcmds = NULL;
	result->correctcmd = NULL;

	// try and autocomplete the user's command
	// TODO fix up after exams
	while(partialpart){
		result->matches = 0;

		sprintf(regexstr, "%s%s.* ", regexstr, partialpart);
		// get rid of the extra space that was tacked on
		regexstr[strlen(regexstr) - 1] = '\0';

		regex_t regex;
		int regex_err;

		regex_err = regcomp(&regex, regexstr, REG_EXTENDED);

		if(regex_err)
			return NULL;

		int numcommands = sizeof(command_table) / sizeof(const char *);

		for(int i=0; i<numcommands; i++){
			const char *currentcmd = command_table[i];

			regex_err = regexec(&regex, currentcmd, 0, NULL, 0);

			// we got a match
			if(!regex_err){
				result->matches++;

				if(result->matches == 1){
					result->matchingcmds = malloc(1024);
					// first match should not get the blank string and comma before it
					sprintf(result->matchingcmds, "%s", currentcmd);

					// we found a matching command
					result->correctcmd = malloc(1024);
					strcpy(result->correctcmd, currentcmd);
				}
				else{
					sprintf(result->matchingcmds, "%s, %s", result->matchingcmds, currentcmd);

					// if we found more than one match
					// the user typed in an ambiguous command
					// so free it
					if(result->correctcmd)
						free(result->correctcmd);
					
					// and D E S T R O Y it
					result->correctcmd = NULL;
				}
			}
		}

		if(result->matches == 1)
			return result;

		// restore the space to correctly build up the regex string again
		regexstr[strlen(regexstr)] = ' ';
		regexstr[strlen(regexstr) + 1] = '\0';

		regfree(&regex);

		partialpart = strtok(NULL, " ");
	}
	
	free(regexstr);

	return result;
}