#ifndef _DIE_H_
#define _DIE_H_

int die_create_variable_or_parameter_desc(void *, void *, char **,
        void *, int);
void die_display(void *);
void die_display_die_tree_starting_from(void *);
int die_evaluate_location_description(void *, uint64_t, uint64_t *, void *);
int die_get_array_elem_size(void *, uint64_t *, void *);
int die_get_array_size_determined_at_runtime(void *, int *, void *);
int die_get_data_type_str(void *, char **, void *);
int die_get_encoding(void *, uint64_t *, void *);
int die_get_high_pc(void *, uint64_t *, void *);
int die_get_line_info_from_pc(void *, void *, uint64_t, char **, char **,
        uint64_t *, void *);
int die_get_low_pc(void *, uint64_t *, void *);
int die_get_members(void *, void *, void ***, int *, void *);
int die_get_member_offset(void *, uint64_t *, void *);
int die_get_name(void *, char **, void *);
int die_get_parameters(void *, void ***, int *, void *);
int die_get_parent(void *, void **, void *);
int die_get_pc_of_next_line(void *, void *, uint64_t, uint64_t *, void *);
int die_get_pc_values_from_lineno(void *, void *, uint64_t, uint64_t **,
        int *, void *);
int die_get_variables(void *, void *, void ***, int *, void *);
int die_get_variable_size(void *, uint64_t *, void *);
int die_is_member_of_struct_or_union(void *, int *, void *);
int die_lineno_to_pc(void *, void *, uint64_t *, uint64_t *, void *);
int die_pc_to_lineno(void *, void *, uint64_t, uint64_t *, void *);
int die_represents_array(void *, int *, void *);
int die_represents_pointer(void *, int *, void *);
int die_represents_struct(void *, int *, void *);
int die_represents_union(void *, int *, void *);
int die_search(void *, void *, int, void **, void *);
void die_tree_free(void *, void *, int);

/* Internal functions */
int initialize_and_build_die_tree_from_root_die(void *, void *, void **,
        void *);

#endif
