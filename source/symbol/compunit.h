#ifndef _COMPUNIT_H_
#define _COMPUNIT_H_

int cu_display_compilation_units(void *, void *);
int cu_find_compilation_unit_by_name(void *, void **, char *, void *);
int cu_find_compilation_unit_by_pc(void *, void **, uint64_t, void *);
int cu_free(void *, void *);
int cu_get_address_size(void *, unsigned short *, void *);
int cu_get_root_die(void *, void **, void *);
int cu_load_compilation_units(void *, void *); 

#endif
