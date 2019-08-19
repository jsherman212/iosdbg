#ifndef _COMMON_H_
#define _COMMON_H_

#include <libdwarf.h>

typedef struct {
    /* Our DWARF file */
    int di_fd;

    Dwarf_Debug di_dbg;

    struct linkedlist *di_compunits;
    int di_numcompunits;
} dwarfinfo_t;

#define dprintf(fmt, ...) do { \
    printf("%s:%s:%d: " fmt, __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
    } while(0)

#define LL_FOREACH(list, var) \
    for(struct node_t *var = list->front; \
            var; \
            var = var->next)

enum {
    DIE_SEARCH_IF_NAME_MATCHES,
    DIE_SEARCH_FUNCTION_BY_PC,
    DIE_SEARCH_IF_DIE_OFFSET_MATCHES
};

static void write_spaces(int count){
    for(int i=0; i<count; i++)
        putchar(' ');
}

static void write_tabs(int cnt){
    for(int i=0; i<cnt; i++)
        putchar('\t');
}

#define BLACK "\033[30m"
#define GREEN "\033[32m"
#define RED "\033[31m"
#define CYAN "\033[36m"
#define MAGENTA "\033[35m"
#define YELLOW "\033[33m"
#define LIGHT_GREEN "\033[92m"
#define LIGHT_MAGENTA "\033[95m"
#define LIGHT_RED "\033[91m"
#define LIGHT_YELLOW "\033[93m"
#define LIGHT_BLUE "\033[94m" // XXX purple?
#define RESET "\033[39m"

#define YELLOW_BG "\033[43m"
#define GREEN_BG "\033[42m"
#define RED_BG "\033[41m"
#define BLUE_BG "\033[44m"
#define WHITE_BG "\033[107m"
#define MAGENTA_BG "\033[45m"
#define LIGHT_YELLOW_BG "\033[103m"
#define LIGHT_RED_BG "\033[101m"
#define LIGHT_GREEN_BG "\033[102m"
#define RESET_BG "\033[49m"

enum {
    LOCATION_EXPRESSION = 0,
    LOCATION_LIST_ENTRY,
    LOCATION_LIST_ENTRY_SPLIT
};

#endif
