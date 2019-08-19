#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libdwarf.h>

#include "../linkedlist.h"

#include "common.h"
#include "compunit.h"
#include "die.h"
#include "symerr.h"

int sym_init_with_dwarf_file(const char *file, dwarfinfo_t **_dwarfinfo,
        sym_error_t *e){
    int fd = open(file, O_RDONLY);

    if(fd < 0){
        errset(e, GENERIC_ERROR_KIND, GE_FILE_NOT_FOUND);
        return 1;
    }

    dwarfinfo_t *dwarfinfo = calloc(1, sizeof(dwarfinfo_t));
    Dwarf_Error d_error = NULL;
    int ret = dwarf_init(fd, DW_DLC_READ, NULL, NULL,
            &dwarfinfo->di_dbg, &d_error);

    if(ret != DW_DLV_OK){
        errset(e, SYM_ERROR_KIND, SYM_DWARF_INIT_FAILED);
        free(dwarfinfo);
        return 1;
    }

    dwarfinfo->di_compunits = linkedlist_new();
    dwarfinfo->di_numcompunits = 0;

    if(cu_load_compilation_units(dwarfinfo, e))
        return 1;

    *_dwarfinfo = dwarfinfo;

    return 0;
}

void sym_end(dwarfinfo_t **_dwarfinfo){
    if(!_dwarfinfo || !(*_dwarfinfo))
        return;

    dwarfinfo_t *dwarfinfo = *_dwarfinfo;

    struct node_t *current = dwarfinfo->di_compunits->front;

    while(current){
        void *cu = current->data;
        void *root_die = NULL;

        cu_get_root_die(cu, &root_die, NULL);

        current = current->next;

        die_tree_free(dwarfinfo->di_dbg, root_die, 0);
        linkedlist_delete(dwarfinfo->di_compunits, cu);
        cu_free(cu, NULL);
    }

    close(dwarfinfo->di_fd);

    Dwarf_Error d_error = NULL;
    int ret = dwarf_finish(dwarfinfo->di_dbg, &d_error);

    linkedlist_free(dwarfinfo->di_compunits);
    free(dwarfinfo);
}

int sym_display_compilation_units(dwarfinfo_t *dwarfinfo,
        sym_error_t *e){
    return cu_display_compilation_units(dwarfinfo, e);
}

int sym_find_compilation_unit_by_name(dwarfinfo_t *dwarfinfo, void **cuout,
        char *name, sym_error_t *e){
    return cu_find_compilation_unit_by_name(dwarfinfo, cuout, name, e);
}

int sym_get_compilation_unit_root_die(void *cu, void **dieout,
        sym_error_t *e){
    return cu_get_root_die(cu, dieout, e);
}

int sym_create_variable_or_parameter_die_desc(void *die, void *cu,
        char **desc, sym_error_t *e){
    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    return die_create_variable_or_parameter_desc(die, root_die, desc, e, 0);
}

void sym_display_die(void *die){
    die_display(die);
}

void sym_display_die_tree_starting_from(void *die){
    die_display_die_tree_starting_from(die);
}

int sym_evaluate_die_location_description(void *die, uint64_t pc,
        uint64_t *resultout, sym_error_t *e){
    return die_evaluate_location_description(die, pc, resultout, e);
}

int sym_find_die_by_name(void *cu, const char *name, void **dieout,
        sym_error_t *e){
    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    void *result = NULL;
    int ret = die_search(root_die, (void *)name, DIE_SEARCH_IF_NAME_MATCHES,
            &result, e);

    *dieout = result;
    return ret;
}

int sym_find_function_die_by_pc(void *cu, uint64_t pc, void **dieout,
        sym_error_t *e){
    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    void *result = NULL;
    int ret = die_search(root_die, (void *)pc, DIE_SEARCH_FUNCTION_BY_PC,
            &result, e);

    *dieout = result;
    return ret;
}

int sym_get_die_array_elem_size(void *die, uint64_t *elemszout, sym_error_t *e){
    return die_get_array_elem_size(die, elemszout, e);
}

int sym_get_die_array_size_determined_at_runtime(void *die, int *retval,
        sym_error_t *e){
    return die_get_array_size_determined_at_runtime(die, retval, e);
}

int sym_get_die_data_type_str(void *die, char **datatypeout, sym_error_t *e){
    return die_get_data_type_str(die, datatypeout, e);
}

int sym_get_die_encoding(void *die, uint64_t *encodingout, sym_error_t *e){
    return die_get_encoding(die, encodingout, e);
}

int sym_get_die_high_pc(void *die, uint64_t *highpcout, sym_error_t *e){
    return die_get_high_pc(die, highpcout, e);
}

int sym_get_die_low_pc(void *die, uint64_t *lowpcout, sym_error_t *e){
    return die_get_low_pc(die, lowpcout, e);
}

int sym_get_die_members(void *die, void *cu, void ***membersout,
        int *membersarrlen, sym_error_t *e){
    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    return die_get_members(die, root_die, membersout, membersarrlen, e);
}

int sym_get_die_name(void *die, char **dienameout, sym_error_t *e){
    return die_get_name(die, dienameout, e);
}

int sym_get_die_represents_array(void *die, int *retval, sym_error_t *e){
    return die_represents_array(die, retval, e);
}

int sym_get_die_represents_pointer(void *die, int *retval, sym_error_t *e){
    return die_represents_pointer(die, retval, e);
}

int sym_get_die_represents_struct(void *die, int *retval,
        sym_error_t *e){
    return die_represents_struct(die, retval, e);
}

int sym_get_die_represents_union(void *die, int *retval,
        sym_error_t *e){
    return die_represents_union(die, retval, e);
}

int sym_get_die_variable_size(void *die, uint64_t *sizeout, sym_error_t *e){
    return die_get_variable_size(die, sizeout, e);
}

int sym_get_function_die_parameters(void *die, void ***paramsout,
        int *lenout, sym_error_t *e){
    return die_get_parameters(die, paramsout, lenout, e);
}

int sym_get_parent_of_die(void *die, void **parentout, sym_error_t *e){
    return die_get_parent(die, parentout, e);
}

int sym_get_variable_dies(dwarfinfo_t *dwarfinfo, uint64_t pc,
        void ***vardies, int *len, sym_error_t *e){
    void *cu = NULL;
    if(cu_find_compilation_unit_by_pc(dwarfinfo, &cu, pc, e))
        return 1;

    void *fxndie = NULL;
    if(sym_find_function_die_by_pc(cu, pc, &fxndie, e))
        return 1;

    return die_get_variables(dwarfinfo->di_dbg, fxndie, vardies, len, e);
}

int sym_is_die_a_member_of_struct_or_union(void *die, int *retval,
        sym_error_t *e){
    return die_is_member_of_struct_or_union(die, retval, e);
}

int sym_get_line_info_from_pc(dwarfinfo_t *dwarfinfo, uint64_t pc,
        char **outsrcfilename, char **outsrcfunction,
        uint64_t *outsrcfilelineno, void **cudieout, sym_error_t *e){
    void *cu = NULL;
    if(cu_find_compilation_unit_by_pc(dwarfinfo, &cu, pc, e))
        return 1;

    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    int ret = die_get_line_info_from_pc(dwarfinfo->di_dbg, root_die, pc,
            outsrcfilename, outsrcfunction, outsrcfilelineno, e);

    *cudieout = root_die;
    return 0;
}

int sym_get_pc_of_next_line(dwarfinfo_t *dwarfinfo, uint64_t pc,
        uint64_t *next_line_pc, void **cudieout, sym_error_t *e){
    void *cu = NULL;
    if(cu_find_compilation_unit_by_pc(dwarfinfo, &cu, pc, e))
        return 1;

    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    int ret = die_get_pc_of_next_line(dwarfinfo->di_dbg, root_die, pc,
            next_line_pc, e);

    *cudieout = root_die;
    return ret;
}

int sym_get_pc_values_from_lineno(dwarfinfo_t *dwarfinfo, void *cu,
        uint64_t lineno, uint64_t **pcs, int *len, sym_error_t *e){
    if(!dwarfinfo){
        errset(e, GENERIC_ERROR_KIND, GE_INVALID_DWARFINFO);
        return 1;
    }

    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    return die_get_pc_values_from_lineno(dwarfinfo->di_dbg, root_die, lineno,
            pcs, len, e);
}

int sym_lineno_to_pc_a(dwarfinfo_t *dwarfinfo,
        char *srcfilename, uint64_t *srcfilelineno, uint64_t *pcout,
        sym_error_t *e){
    void *cu = NULL;
    if(cu_find_compilation_unit_by_name(dwarfinfo, &cu, srcfilename, e))
        return 1;

    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    return die_lineno_to_pc(dwarfinfo->di_dbg, root_die, srcfilelineno,
            pcout, e);
}

int sym_lineno_to_pc_b(dwarfinfo_t *dwarfinfo, void *cu,
        uint64_t *srcfilelineno, uint64_t *pcout, sym_error_t *e){
    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    return die_lineno_to_pc(dwarfinfo->di_dbg, root_die, srcfilelineno,
            pcout, e);
}

int sym_pc_to_lineno_a(dwarfinfo_t *dwarfinfo, uint64_t pc,
        uint64_t *srcfilelineno, sym_error_t *e){
    void *cu = NULL;
    if(cu_find_compilation_unit_by_pc(dwarfinfo, &cu, pc, e))
        return 1;

    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    return die_pc_to_lineno(dwarfinfo->di_dbg, root_die, pc, srcfilelineno, e);
}

int sym_pc_to_lineno_b(dwarfinfo_t *dwarfinfo, void *cu, uint64_t pc,
        uint64_t *srcfilelineno, sym_error_t *e){
    void *root_die = NULL;
    if(cu_get_root_die(cu, &root_die, e))
        return 1;

    return die_pc_to_lineno(dwarfinfo->di_dbg, root_die, pc, srcfilelineno, e);
}

const char *sym_strerror(sym_error_t e){
    return errmsg(e);
}

void sym_errclear(sym_error_t *e){
    errclear(e);
}
