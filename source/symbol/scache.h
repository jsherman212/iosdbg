#ifndef _SCACHE_H_
#define _SCACHE_H_

#include "dbgsymbol.h"

struct my_dsc_mapping {
    unsigned long file_start;
    unsigned long file_end;
    unsigned long vm_start;
    unsigned long vm_end;
};

struct dbg_sym_entry *create_sym_entry_for_dsc_image(char *);
struct my_dsc_mapping *get_dsc_mappings(int *);
//int is_dsc_image(unsigned long, unsigned long, struct my_dsc_mapping *,
  //      unsigned int, struct my_dsc_mapping *);
int is_dsc_image(unsigned long, struct my_dsc_mapping *, unsigned int);


#endif
