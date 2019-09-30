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

#define LL_FOREACH(list, var) \
    for(struct node *var = list->front; \
            var; \
            var = var->next)

enum {
    DIE_SEARCH_IF_NAME_MATCHES,
    DIE_SEARCH_FUNCTION_BY_PC,
    DIE_SEARCH_IF_DIE_OFFSET_MATCHES
};

enum {
    LOCATION_EXPRESSION = 0,
    LOCATION_LIST_ENTRY,
    LOCATION_LIST_ENTRY_SPLIT
};

#endif
