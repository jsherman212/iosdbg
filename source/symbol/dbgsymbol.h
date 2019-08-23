#ifndef _DBGSYMBOL_H_
#define _DBGSYMBOL_H_

#include "../linkedlist.h"

struct dbg_sym_entry {
    char *imagename;
    unsigned int numsyms;
    unsigned int cursymarrsz;

    /* pointer into debuggee's address space */
    unsigned long strtab_addr;

    struct sym {
        //char *symname;
        /* pointer into debuggee's address space */
        unsigned int strtabidx;

        unsigned long symaddr_start;
        unsigned long symaddr_end;
    } **syms;
};

void add_symbol_to_entry(struct dbg_sym_entry *, int, int, unsigned long,
        unsigned long);
struct dbg_sym_entry *create_sym_entry(char *, unsigned int, unsigned long);
int get_symbol_info_from_address(struct linkedlist *, unsigned long, char **,
        char **, unsigned int *);

#endif
