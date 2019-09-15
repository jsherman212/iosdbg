#ifndef _DBGSYMBOL_H_
#define _DBGSYMBOL_H_

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

#define STARTING_CAPACITY (9)
#define FAST_POW_TWO(x) (1 << (x))
#define CALC_SYM_CAPACITY(x) (sizeof(struct sym *) * FAST_POW_TWO(x))
#define CALC_ENTRIES_CAPACITY(x) (sizeof(struct lc_fxn_starts_entry *) * FAST_POW_TWO(x))
#define CALC_NLISTS_ARR_CAPACITY(x) (sizeof(struct nlist_64_wrapper) * FAST_POW_TWO(x))

#define FIRST_FXN_NO_LEN (-1)

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

    /* dylib-specific file offset of the string table inside of the DSC */
   // unsigned long dsc_dylib_strtab_fileoff;

    // XXX non-NULL if the entry that owns this sym comes from the dyld shared cache,
    // as we can mmap the dsc and strdup this pointer to save a lot of memory
    char *dsc_symname;

    // XXX when it's unnamed, strtabidx is not used and unnamed_sym_num is,
    // when it's named, strtabidx is used and unnamed_sym_num isn't
    union {
        int strtabidx;
        int unnamed_sym_num;
    };

    unsigned int sym_func_len;

    //char use_dsc_dylib_strtab;
};

struct dbg_sym_entry {
    char *imagename;
    struct sym **syms;

    /* pointer into debuggee's address space */
    // XXX if zero, this is a shared cache image
    unsigned long strtab_vmaddr;

    /* file offset for the start of the string table */
//    unsigned long strtab_fileaddr;

    unsigned int cursymarrsz;

    /* which power of 2 the symbol array's capacity equals */
    char symarr_capacity_pow;
    char from_dsc;
};

struct nlist_64_wrapper {
    struct nlist_64 *nlist;
    char *str;
};

struct lc_fxn_starts_entry {
    /* function start address */
    unsigned long vmaddr;
    /* length of function, -1 if first entry in LC_FUNCTION_STARTS */
    int len;
};

enum {
    UNNAMED_SYM = 0, NAMED_SYM = 1
};

void add_symbol_to_entry(struct dbg_sym_entry *, int, unsigned long,
        unsigned int, int, char *);
struct dbg_sym_entry *create_sym_entry(char *, unsigned long, unsigned long, int);
int get_symbol_info_from_address(struct linkedlist *, unsigned long, char **,
        char **, unsigned int *);
void sym_desc(struct dbg_sym_entry *, struct sym *);

#endif
