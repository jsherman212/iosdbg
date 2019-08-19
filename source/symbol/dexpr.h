#ifndef _DEXPR_H_
#define _DEXPR_H_

void add_additional_location_description(Dwarf_Half, void **, void *, int);
void *copy_locdesc(void *);
void *create_location_description(Dwarf_Small, uint64_t, uint64_t,
        Dwarf_Small, Dwarf_Unsigned, Dwarf_Unsigned, Dwarf_Unsigned,
        Dwarf_Unsigned);
char *decode_location_description(void *, void *, uint64_t, uint64_t *);
void describe_location_description(void *, int, int, int, int, int *);
void *get_next_location_description(void *);
void initialize_die_loclists(void ***, int);
int is_locdesc_in_bounds(void *, uint64_t);
void loc_free(void *);

#endif
