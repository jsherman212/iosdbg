#ifndef _DBGSYMBOL_H_
#define _DBGSYMBOL_H_

#include "../linkedlist.h"

struct dbg_sym_entry {
    char *imagename;
    //unsigned int numsyms;
    unsigned int cursymarrsz;

    /* pointer into debuggee's address space */
    unsigned long strtab_vmaddr;

    unsigned long strtab_fileaddr;


    struct sym {
        unsigned int strtabidx;

        /* pointers into debuggee's address space */
        unsigned long symaddr_start;
        // XXX instead of this, maybe unsigned int length to save space?
        unsigned long symaddr_end;

        unsigned long stroff_fileaddr;
        char dsc_use_stroff;
    } **syms;

    char from_dsc;
};

void add_symbol_to_entry(struct dbg_sym_entry *, int, int, unsigned long,
        unsigned long);
struct dbg_sym_entry *create_sym_entry(char *, unsigned long, unsigned long, int);
int get_symbol_info_from_address(struct linkedlist *, unsigned long, char **,
        char **, unsigned int *);

#endif
