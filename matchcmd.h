#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

static const char *command_table[] = {"attach",
										"aslr",
										"break",
										"continue",
										"delete",
										"detach",
										"help",
										"kill",
										"quit",
										"regs gen",
										"regs float",
										"set"
										};

struct matchcmd_result {
	// How many matches the user's command got.
	int matches;

	// A comma separated string of what commands the user's command matches.
	char *matchingcmds;

	// If there is a single match, that match is placed here.
	// If there is more than one match or no matches, this is NULL.
	char *correctcmd;
};

struct matchcmd_result *matchcmd(const char *);
void matchcmd_result_free();