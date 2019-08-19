#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdwarf.h>

#include "../linkedlist.h"

#include "common.h"
#include "die.h"
#include "symerr.h"

typedef struct {
    Dwarf_Unsigned cu_header_len;
    Dwarf_Unsigned cu_abbrev_offset;
    Dwarf_Half cu_address_size;
    Dwarf_Unsigned cu_next_header_offset;

    void *cu_root_die;
} compunit_t;

int cu_display_compilation_units(dwarfinfo_t *dwarfinfo, sym_error_t *e){
    if(!dwarfinfo){
        errset(e, GENERIC_ERROR_KIND, GE_INVALID_DWARFINFO);
        return 1;
    }

    int cnt = 1;

    LL_FOREACH(dwarfinfo->di_compunits, current){
        compunit_t *cu = current->data;

        char *cuname = NULL;
        die_get_name(cu->cu_root_die, &cuname, NULL);

        printf("Compilation unit %d/%d:\n"
                "\tcu_header_len: %#llx\n"
                "\tcu_abbrev_offset: %#llx\n"
                "\tcu_address_size: %#x\n"
                "\tcu_next_header_offset: %#llx\n"
                "\tcu_diename: '%s'\n",
                cnt++, dwarfinfo->di_numcompunits,
                cu->cu_header_len, cu->cu_abbrev_offset,
                cu->cu_address_size, cu->cu_next_header_offset,
                cuname);
    }
    
    return 0;
}

int cu_find_compilation_unit_by_name(dwarfinfo_t *dwarfinfo,
        compunit_t **cuout, char *name, sym_error_t *e){
    if(!dwarfinfo){
        errset(e, GENERIC_ERROR_KIND, GE_INVALID_DWARFINFO);
        return 1;
    }

    if(!name){
        errset(e, GENERIC_ERROR_KIND, GE_INVALID_PARAMETER);
        return 1;
    }

    LL_FOREACH(dwarfinfo->di_compunits, current){
        compunit_t *cu = current->data;

        char *cuname = NULL;
        die_get_name(cu->cu_root_die, &cuname, NULL);

        if(strcmp(cuname, name) == 0){
            *cuout = cu;
            return 0;
        }
    }

    errset(e, CU_ERROR_KIND, CU_CU_NOT_FOUND);
    return 1;
}

int cu_find_compilation_unit_by_pc(dwarfinfo_t *dwarfinfo,
        compunit_t **cuout, uint64_t pc, sym_error_t *e){
    if(!dwarfinfo){
        errset(e, GENERIC_ERROR_KIND, GE_INVALID_DWARFINFO);
        return 1;
    }

    if(!cuout){
        errset(e, GENERIC_ERROR_KIND, GE_INVALID_PARAMETER);
        return 1;
    }

    LL_FOREACH(dwarfinfo->di_compunits, current){
        compunit_t *cu = current->data;

        Dwarf_Unsigned lpc = 0, hpc = 0;
        die_get_low_pc(cu->cu_root_die, &lpc, NULL);
        die_get_high_pc(cu->cu_root_die, &hpc, NULL);
        
        if(pc >= lpc && pc < hpc){
            *cuout = cu;
            return 0;
        }
    }

    errset(e, CU_ERROR_KIND, CU_CU_NOT_FOUND);
    return 1;
}

int cu_free(compunit_t *cu, sym_error_t *e){
    if(!cu){
        errset(e, GENERIC_ERROR_KIND, GE_INVALID_CU_POINTER);
        return 1;
    }

    free(cu->cu_root_die);
    free(cu);

    return 0;
}

int cu_get_address_size(compunit_t *cu, Dwarf_Half *addrsize,
        sym_error_t *e){
    if(!cu){
        errset(e, GENERIC_ERROR_KIND, GE_INVALID_CU_POINTER);
        return 1;
    }

    *addrsize = cu->cu_address_size;
    return 0;
}

int cu_get_root_die(compunit_t *cu, void **dieout, sym_error_t *e){
    if(!cu){
        errset(e, GENERIC_ERROR_KIND, GE_INVALID_CU_POINTER);
        return 1;
    }

    *dieout = cu->cu_root_die;
    return 0;
}

int cu_load_compilation_units(dwarfinfo_t *dwarfinfo, sym_error_t *e){
    for(;;){
        compunit_t *cu = calloc(1, sizeof(compunit_t));
        Dwarf_Half ver, len_sz, ext_sz, hdr_type;
        Dwarf_Sig8 sig;
        Dwarf_Unsigned typeoff;
        Dwarf_Error d_error;
        int is_info = 1;

        int ret = dwarf_next_cu_header_d(dwarfinfo->di_dbg,
                is_info, &cu->cu_header_len, &ver, &cu->cu_abbrev_offset,
                &cu->cu_address_size, &len_sz, &ext_sz, &sig,
                &typeoff, &cu->cu_next_header_offset, &hdr_type,
                &d_error);

        if(ret == DW_DLV_ERROR){
            dwarf_dealloc(dwarfinfo->di_dbg, d_error, DW_DLA_ERROR);
            errset(e, CU_ERROR_KIND, CU_DWARF_NEXT_CU_HEADER_D_FAILED);
            free(cu);
            return 1;
        }

        if(ret == DW_DLV_NO_ENTRY){
            free(cu);
            return 0;
        }

        void *root_die = NULL;
        if(initialize_and_build_die_tree_from_root_die(dwarfinfo, cu,
                &root_die, e)){
            free(cu);
            return 1;
        }

        cu->cu_root_die = root_die;
        linkedlist_add(dwarfinfo->di_compunits, cu);

        dwarfinfo->di_numcompunits++;
    }

    return 0;
}
