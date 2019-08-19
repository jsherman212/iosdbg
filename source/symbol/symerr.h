#ifndef _SYMERR_H_
#define _SYMERR_H_

typedef struct {
    unsigned error_kind;
    unsigned error_id;
} sym_error_t;

enum {
    NO_ERROR_KIND = 0,
    GENERIC_ERROR_KIND,
    SYM_ERROR_KIND,
    CU_ERROR_KIND,
    DIE_ERROR_KIND
};

enum {
    GE_NO_ERROR = 0,
    GE_FILE_NOT_FOUND,
    GE_INVALID_PARAMETER,
    GE_INVALID_CU_POINTER,
    GE_INVALID_DWARFINFO,
    GE_INVALID_DIE
};

enum {
    SYM_NO_ERROR = 0,
    SYM_DWARF_INIT_FAILED,
    SYM_DWARF_SIBLING_OF_B_FAILED,
    SYM_DWARF_SRCLINES_FAILED
};

enum {
    CU_NO_ERROR = 0,
    CU_CU_NOT_FOUND,
    CU_DWARF_NEXT_CU_HEADER_D_FAILED,
};

enum {
    DIE_NO_ERROR = 0,
    DIE_NOT_FUNCTION_DIE,
    DIE_DIE_NOT_FOUND,
    DIE_COULD_NOT_GET_LINE_INFO,
    DIE_NOT_COMPILE_UNIT_DIE,
    DIE_NEXT_LINE_NOT_FOUND,
    DIE_LINE_NOT_FOUND,
    DIE_NO_DATA_TYPE_NAME,
    DIE_NOT_STRUCT_OR_UNION,
    DIE_NO_PARENT,
    DIE_NOT_VARIABLE_DIE
};

void errclear(sym_error_t *);
const char *errmsg(sym_error_t);
void errset(sym_error_t *, unsigned, unsigned);

#endif
