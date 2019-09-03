#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbgsymbol.h"

#include "../memutils.h"

void add_symbol_to_entry(struct dbg_sym_entry *entry, int strtabidx,
        unsigned long vmaddr_start){
    if(entry->cursymarrsz >= FAST_POW_TWO(entry->symarr_capacity_pow) - 1){
        entry->symarr_capacity_pow++;
        struct sym **sym_rea = realloc(entry->syms,
                CALC_SYM_CAPACITY(entry->symarr_capacity_pow));
        entry->syms = sym_rea;
    }

    entry->syms[entry->cursymarrsz] = malloc(sizeof(struct sym));
    entry->syms[entry->cursymarrsz]->strtabidx = strtabidx;
    entry->syms[entry->cursymarrsz]->sym_func_start = vmaddr_start;
    entry->syms[entry->cursymarrsz]->sym_func_len = 0;

    entry->cursymarrsz++;
}

struct dbg_sym_entry *create_sym_entry(char *imagename,
        unsigned long strtab_vmaddr, unsigned long strtab_fileaddr,
        int from_dsc){
    struct dbg_sym_entry *entry = malloc(sizeof(struct dbg_sym_entry));

    char *name = NULL;

    if(imagename)
        name = strdup(imagename);
    else
        name = strdup("unknown");

    entry->imagename = name;
    entry->symarr_capacity_pow = STARTING_CAPACITY;
    entry->cursymarrsz = 0;
    entry->strtab_vmaddr = strtab_vmaddr;
    entry->strtab_fileaddr = strtab_fileaddr;
    entry->syms = malloc(CALC_SYM_CAPACITY(STARTING_CAPACITY));
    entry->from_dsc = from_dsc;

    return entry;
}

int get_symbol_info_from_address(struct linkedlist *symlist,
        unsigned long vmaddr, char **imgnameout, char **symnameout,
        unsigned int *distfromsymstartout){
    struct dbg_sym_entry *best_entry = NULL;
    int best_symbol_idx = 0;

    unsigned long diff = 0;

    for(struct node_t *current = symlist->front;
            current;
            current = current->next){
        int cnt = 1;
        struct dbg_sym_entry *entry = current->data;
        //printf("%s: searching entry %d: '%s'\n", __func__, cnt++, entry->imagename);

        for(int i=0; i<entry->cursymarrsz; i++){
            unsigned long end = entry->syms[i]->sym_func_start +
                (unsigned long)entry->syms[i]->sym_func_len;

            if(!(vmaddr >= entry->syms[i]->sym_func_start && vmaddr < end))
                continue;

            if(diff == 0 || vmaddr - entry->syms[i]->sym_func_start < diff){
                best_entry = entry;
                best_symbol_idx = i;

                diff = vmaddr - entry->syms[i]->sym_func_start;
            }
        }
    }

    if(!best_entry)
        return 1;

    enum { len = 512 };
    char symname[len] = {0};

    if(best_entry->from_dsc){
        // XXX don't hardcode on master
        FILE *dscfptr =
            fopen("/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64", "rb");

        if(!dscfptr){
            printf("%s: couldn't open shared cache\n", __func__);
            return 1;
        }

        struct sym *best_sym = best_entry->syms[best_symbol_idx];
        unsigned long using_strtab = best_entry->strtab_fileaddr;

        if(best_sym->use_dsc_dylib_strtab)
            using_strtab = best_sym->dsc_dylib_strtab_fileoff;

        unsigned long file_stroff =
            using_strtab + best_sym->strtabidx;

        fseek(dscfptr, file_stroff, SEEK_SET);
        fread(symname, sizeof(char), len, dscfptr);
        symname[len - 1] = '\0';

        //printf("%s: got symname '%s' from file offset %#lx\n", __func__, symname,
        //       file_stroff);

        fclose(dscfptr);
    }
    else{
        kern_return_t kret =
            read_memory_at_location(best_entry->strtab_vmaddr +
                    best_entry->syms[best_symbol_idx]->strtabidx, symname, len);

        // printf("%s: kret %s for addr %#lx\n", __func__, mach_error_string(kret),
        //       best_entry->strtab_vmaddr + best_entry->syms[best_symbol_idx]->strtabidx);
    }

    *imgnameout = strdup(best_entry->imagename);
    *symnameout = strdup(symname);
    *distfromsymstartout = vmaddr - best_entry->syms[best_symbol_idx]->sym_func_start;

    return 0;
}
