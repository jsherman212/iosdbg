#ifndef _DBGSYMBOL_H_
#define _DBGSYMBOL_H_

#include "../linkedlist.h"

#define STARTING_CAPACITY (9)
#define FAST_POW_TWO(x) (1 << (x))
#define CALC_SYM_CAPACITY(x) (sizeof(struct sym *) * FAST_POW_TWO(x))

struct sym {
    /* pointer into debuggee's address space, the start of this function */
    unsigned long sym_func_start;

    /* dylib-specific file offset of the string table inside of the DSC */
    unsigned long dsc_dylib_strtab_fileoff;

    unsigned int strtabidx;
    unsigned int sym_func_len;

    char use_dsc_dylib_strtab;
};

struct dbg_sym_entry {
    char *imagename;
    struct sym **syms;

    /* pointer into debuggee's address space */
    unsigned long strtab_vmaddr;

    /* file offset for the start of the string table */
    unsigned long strtab_fileaddr;

    unsigned int cursymarrsz;

    /* which power of 2 the symbol array's capacity equals */
    char symarr_capacity_pow;
    char from_dsc;
};

void add_symbol_to_entry(struct dbg_sym_entry *, int, unsigned long);
struct dbg_sym_entry *create_sym_entry(char *, unsigned long, unsigned long, int);
int get_symbol_info_from_address(struct linkedlist *, unsigned long, char **,
        char **, unsigned int *);

#endif
