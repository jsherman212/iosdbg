#ifndef _DBGSYMBOL_H_
#define _DBGSYMBOL_H_

#include "../array.h"
#include "../linkedlist.h"

struct dsc_hdr {
    char magic[16];
    unsigned int mappingoff;
    unsigned int mappingcnt;
    char pad[48];
    unsigned long localsymoff;
    unsigned long localsymsz;
};

struct dsc_local_syms_info {
    unsigned int nlistoff;
    unsigned int nlistcnt;
    unsigned int stringsoff;
    unsigned int stringssz;
    unsigned int entriesoff;
    unsigned int entriescnt;
};

struct dsc_local_syms_entry {
    unsigned int dyliboff;
    unsigned int nliststartidx;
    unsigned int nlistcnt;
};

struct dsc_mapping_info {
    unsigned long address;
    unsigned long size;
    unsigned long fileoff;
    char pad[8];
};

/* If the symbol isn't named, we'll make dsc_symname a special value to save
 * memory. This applies for non-DSC symbols as well. The following macro
 * serves to ease confusion that will come about from implementing it
 * this way. But better that than wasting hundreds of MB of memory by adding
 * another member to struct sym.
 */
#define UNNAMED_SYMBOL ((char *)1)
#define IS_UNNAMED_SYMBOL(x) (x->dsc_symname == UNNAMED_SYMBOL)

struct sym {
    /* pointer into debuggee's address space, the start of this function */
    unsigned long sym_func_start;

    char *dsc_symname;

    /* When a symbol is unnamed, unnamed_sym_num is used.
     * When a symbol is named, strtabidx is used.
     * Using a union saves memory.
     */
    union {
        int strtabidx;
        int unnamed_sym_num;
    };

    unsigned int sym_func_len;
};

struct dbg_sym_entry {
    char *imagename;

    struct array *syms;

    unsigned long load_addr;

    /* pointer into debuggee's address space */
    unsigned long strtab_vmaddr;

    /* unfortunate... */
    char from_dsc;
};

enum {
    UNNAMED_SYM = 0, NAMED_SYM = 1
};

void add_symbol_to_entry(struct dbg_sym_entry *, int, unsigned long,
        unsigned int, int, char *);
void create_frame_string(unsigned long, char **);
struct dbg_sym_entry *create_sym_entry(unsigned long, unsigned long, int);
void destroy_all_symbol_entries(void);
int get_symbol_info_from_address(struct linkedlist *, unsigned long, char **,
        char **, unsigned int *);
void reset_unnamed_sym_cnt(void);

#endif
