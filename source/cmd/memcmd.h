#ifndef _MEMCMD_H_
#define _MEMCMD_H_

#include "argparse.h"

enum cmd_error_t cmdfunc_disassemble(struct cmd_args *, int, char **, char **);
enum cmd_error_t cmdfunc_examine(struct cmd_args *, int, char **, char **);
enum cmd_error_t cmdfunc_memory_find(struct cmd_args *, int, char **, char **);
enum cmd_error_t cmdfunc_memory_write(struct cmd_args *, int, char **, char **);

static const char *DISASSEMBLE_COMMAND_DOCUMENTATION =
    "Disassemble debuggee memory.\n"
    "This command has two mandatory arguments and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tThis expression will be evaluated and used as where iosdbg"
    " will start disassembling.\n"
    "\tcount\n"
    "\t\tHow many bytes iosdbg will disassemble.\n"
    "\nSyntax:\n"
    "\tdisassemble location count\n"
    "\n";

static const char *EXAMINE_COMMAND_DOCUMENTATION =
    "View debuggee memory.\n"
    "This command has two mandatory arguments and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tThis expression will be evaluted and used as where iosdbg"
    " will start dumping memory.\n"
    "\tcount\n"
    "\t\tHow many bytes iosdbg will dump.\n"
    "\nSyntax:\n"
    "\texamine location count\n"
    "\n";

static const char *MEMORY_COMMAND_DOCUMENTATION =
    "'memory' describes the group of commands which deal with manipulating"
    " debuggee memory.\n";

static const char *MEMORY_FIND_COMMAND_DOCUMENTATION =
    "Search debuggee memory.\n"
    "This command has three mandatory arguments and one optional argument.\n"
    "\nMandatory arguments:\n"
    "\tstart\n"
    "\t\tThis expression will be evaluated and used as the starting point"
    " of the search.\n"
    "\ttype\n"
    "\t\tThe type of the data you're searching for.\n"
    "\t\tValid types:\n"
    "\t\t--s\tstring\n"
    "\t\t--f\tfloat\n"
    "\t\t--fd\tdouble\n"
    "\t\t--fld\tlong double\n"
    "\t\t--ec\texpression, treat result as signed char\n"
    "\t\t--ecu\texpression, treat result as unsigned char\n"
    "\t\t--es\texpression, treat result as signed short\n"
    "\t\t--esu\texpression, treat result as unsigned short\n"
    "\t\t--ed\texpression, treat result as signed integer\n"
    "\t\t--edu\texpression, treat result as unsigned integer\n"
    "\t\t--eld\texpression, treat result as signed long\n"
    "\t\t--eldu\texpression, treat result as unsigned long\n"
    "\ttarget\n"
    "\t\tWhat you're searching for.\n"
    "\nOptional arguments:\n"
    "\tcount\n"
    "\t\tHow many bytes iosdbg will search before aborting.\n"
    "\t\tWhen this argument is omitted, iosdbg aborts search on error.\n"
    "\nSyntax:\n"
    "\tmemory find <start> <count>? <type> <target>\n"
    "\n";

static const char *MEMORY_WRITE_COMMAND_DOCUMENTATION =
    "Write arbitrary data to debuggee memory.\n"
    "This command has three mandatory arguments and no optional arguments.\n"
    "\nMandatory arguments:\n"
    "\tlocation\n"
    "\t\tWhere to write to.\n"
    "\tdata\n"
    "\t\tWhat to write.\n"
    "\tcount\n"
    "\t\tThe size of your data.\n"
    "\nSyntax:\n"
    "\tmemory write location data count\n"
    "\n";

/*
 * Regexes
 */
static const char *DISASSEMBLE_COMMAND_REGEX =
    "(?<location>[\\w+\\-*\\/\\$()]+)\\s+(?<count>[\\w+\\-*\\/\\$()]+)";

static const char *EXAMINE_COMMAND_REGEX =
    "(?<location>[\\w+\\-*\\/\\$()]+)\\s+(?<count>[\\w+\\-*\\/\\$()]+)";

static const char *MEMORY_FIND_COMMAND_REGEX =
    "(?J)^(?<start>[\\w+\\-*\\/\\$()]+)\\s+"
    "((?<count>(0[xX])?[[:xdigit:]]+)\\s+)?"
    "(?<type>--(s|f|fd|fld|ec|ecu|es|esu|ed|edu|eld|eldu))\\s+"
    "(?(?=\")\"(?<target>.*)\"|(?<target>[\\w+\\-*\\/\\$()\\.]+))";

static const char *MEMORY_WRITE_COMMAND_REGEX =
    "^(?<location>[\\w+\\-*\\/\\$()]+)\\s+"
    "(?<data>[\\w+\\-*\\/\\$()]+)\\s+"
    "(?<size>(0[xX])?\\d+)";

/*
 * Regex groups
 */
static const char *DISASSEMBLE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "location", "count" };

static const char *EXAMINE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "location", "count" };

static const char *MEMORY_FIND_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "start", "count", "type", "target" };

static const char *MEMORY_WRITE_COMMAND_REGEX_GROUPS[MAX_GROUPS] =
    { "location", "data", "size" };

#endif
