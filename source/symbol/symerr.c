#include <stdlib.h>

#include "symerr.h"

static const char *const NO_ERROR_TABLE[] = {
    "No error (0)"
};

static const char *const GENERIC_ERROR_TABLE[] = {
    "No error (0)",
    "File not found (1 - generic error)",
    "Invalid parameter (2 - generic error)",
    "Invalid compilation unit pointer (3 - generic error)",
    "Invalid dwarfinfo pointer (4 - generic error)",
    "Invalid DIE pointer (5 - generic error)"
};

static const char *const SYM_ERROR_TABLE[] = {
    "No error (0)",
    "dwarf_init failed (1 - sym error)",
    "dwarf_siblingof_b failed (2 - sym error)",
    "dwarf_srclines failed (3 - sym error)"
};

static const char *const CU_ERROR_TABLE[] = {
    "No error (0)",
    "Compilation unit not found (1 - compilation unit error)",
    "dwarf_next_cu_header_d failed (2 - compilation unit error)"
};

static const char *const DIE_ERROR_TABLE[] = {
    "No error (0)",
    "Not a function DIE (1 - die error)",
    "DIE not found (2 - die error)",
    "Error getting line info (3 - die error)",
    "Not a compilation unit DIE (4 - die error)",
    "Next line not found (5 - die error)",
    "Line not found (6 - die error)",
    "No data type name (7 - die error)",
    "Not a struct or union DIE (8 - die error)",
    "No parent (9 - die error)",
    "Not a variable (10 - die error)"
};

static const size_t NO_ERROR_TABLE_LEN = sizeof(NO_ERROR_TABLE) / sizeof(const char *);
static const size_t GENERIC_ERROR_TABLE_LEN = sizeof(GENERIC_ERROR_TABLE) / sizeof(const char *);
static const size_t SYM_ERROR_TABLE_LEN = sizeof(SYM_ERROR_TABLE) / sizeof(const char *);
static const size_t CU_ERROR_TABLE_LEN = sizeof(CU_ERROR_TABLE) / sizeof(const char *);
static const size_t DIE_ERROR_TABLE_LEN = sizeof(DIE_ERROR_TABLE) / sizeof(const char *);

static const size_t TABLE_LENGTHS[] = {
    NO_ERROR_TABLE_LEN,
    GENERIC_ERROR_TABLE_LEN,
    SYM_ERROR_TABLE_LEN,
    CU_ERROR_TABLE_LEN,
    DIE_ERROR_TABLE_LEN
};

static const char *const *const ERROR_TABLES[] = {
    NO_ERROR_TABLE,
    GENERIC_ERROR_TABLE,
    SYM_ERROR_TABLE,
    CU_ERROR_TABLE,
    DIE_ERROR_TABLE
};

void errclear(sym_error_t *e){
    if(!e)
        return;

    e->error_kind = 0;
    e->error_id = 0;
    e = NULL;
}

const char *errmsg(sym_error_t e){
    if(e.error_kind > sizeof(ERROR_TABLES) / sizeof(ERROR_TABLES[0]))
        return "sym_error_t error kind out of bounds";

    if(e.error_kind > sizeof(TABLE_LENGTHS) / sizeof(size_t) ||
            e.error_id > TABLE_LENGTHS[e.error_kind]){
        return "sym_error_t error ID out of bounds";
    }

    return ERROR_TABLES[e.error_kind][e.error_id];
}

void errset(sym_error_t *e, unsigned ekind, unsigned eid){
    if(!e)
        return;

    e->error_kind = ekind;
    e->error_id = eid;
}
