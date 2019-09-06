#include <mach/mach.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/stab.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../debuggee.h"
#include "../linkedlist.h"
#include "../memutils.h"

#include "scache.h"

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

enum { NAME_OK, NAME_OOB, NAME_NOT_FOUND };

static int get_dylib_path(void *dscdata, unsigned int dylib_offset,
        void **nameptrout){
    struct mach_header_64 *dylib_hdr =
        (struct mach_header_64 *)((uint8_t *)dscdata + dylib_offset);

    struct load_command *cmd =
        (struct load_command *)((uint8_t *)dscdata + dylib_offset + sizeof(*dylib_hdr));

    struct load_command *cmdorig = cmd;
    struct dylib_command *dylib_cmd = NULL;

    for(int i=0; i<dylib_hdr->ncmds; i++){
        if(cmd->cmd == LC_ID_DYLIB){
            dylib_cmd = (struct dylib_command *)cmd;
            break;
        }

        unsigned long diff = (uint8_t *)cmd - (uint8_t *)dscdata;
        cmd = (struct load_command *)((uint8_t *)dscdata + diff + cmd->cmdsize);
    }

    unsigned int nameoff = dylib_cmd->dylib.name.offset;

    struct load_command *cmdend =
        (struct load_command *)((uint8_t *)cmdorig + dylib_hdr->sizeofcmds);
    unsigned long bytes_left = cmdend - cmd;

    if(nameoff > bytes_left)
        return NAME_OOB;

    if(dylib_cmd){
        *nameptrout = (uint8_t *)cmd + nameoff;
        return NAME_OK;
    }

    return NAME_NOT_FOUND;
}

struct dbg_sym_entry *create_sym_entry_for_dsc_image(char *imagename){
    int from_dsc = 1;
    struct dbg_sym_entry *entry = create_sym_entry(imagename, 0, 0, from_dsc);

    return entry;
}

void get_dsc_image_symbols(void *dscdata, char *imagename, unsigned long aslr_slide,
        struct dbg_sym_entry **sym_entry, struct symtab_command *symtab_cmd,
        int __text_segment_nsect){
    struct dsc_hdr *cache_hdr = (struct dsc_hdr *)dscdata;
    struct dsc_local_syms_info *syminfos =
        (struct dsc_local_syms_info *)((uint8_t *)dscdata + cache_hdr->localsymoff);

    unsigned long nlist_offset = syminfos->nlistoff + cache_hdr->localsymoff;
    unsigned long strings_offset = syminfos->stringsoff + cache_hdr->localsymoff;
    unsigned long entries_offset = syminfos->entriesoff + cache_hdr->localsymoff;

    // XXX this line should be somewhere else
    (*sym_entry)->strtab_fileaddr = strings_offset;

    int entriescnt = syminfos->entriescnt;

    for(int i=0; i<entriescnt; i++){
        unsigned long nextentryoff =
            entries_offset + (sizeof(struct dsc_local_syms_entry) * i);
        struct dsc_local_syms_entry *entry =
            (struct dsc_local_syms_entry *)((uint8_t *)dscdata + nextentryoff);

        void *nameptr = NULL;
        int result = get_dylib_path(dscdata, entry->dyliboff, &nameptr);

        if(result == NAME_OOB || result == NAME_NOT_FOUND)
            continue;

        char *cur_dylib_path = nameptr;

        if(strcmp(imagename, cur_dylib_path) != 0)
            continue;

        unsigned long nlist_start = nlist_offset +
            (entry->nliststartidx * sizeof(struct nlist_64));
        int idx = 0;
        int nlistcnt = entry->nlistcnt;

        for(int j=0; j<nlistcnt; j++){
            unsigned long nextnlistoff = nlist_start +
                (sizeof(struct nlist_64) * j);
            struct nlist_64 *nlist =
                (struct nlist_64 *)((uint8_t *)dscdata + nextnlistoff);

            if(nlist->n_sect == __text_segment_nsect){
                add_symbol_to_entry(*sym_entry, nlist->n_un.n_strx,
                        nlist->n_value + aslr_slide);

                /* These come right from the DSC string table,
                 * not any dylib-specific string table.
                 */
                (*sym_entry)->syms[idx]->use_dsc_dylib_strtab = 0;

                idx++;
            }
        }

        /* Go through the dylib's symtab to get the rest */
        unsigned int nsyms = symtab_cmd->nsyms;
        unsigned long stab_symoff = symtab_cmd->symoff;
        unsigned long stab_stroff = symtab_cmd->stroff;

        for(int j=0; j<nsyms; j++){
            unsigned long nextnlistoff = stab_symoff +
                (sizeof(struct nlist_64) * j);
            struct nlist_64 *nlist =
                (struct nlist_64 *)((uint8_t *)dscdata + nextnlistoff);

            unsigned long stroff = stab_stroff + nlist->n_un.n_strx;
            char *symname = (char *)((uint8_t *)dscdata + stroff);

            /* Skip <redacted> symbols */
            if(*symname != '<'){
                if((nlist->n_type & N_TYPE) == N_SECT &&
                        nlist->n_sect == __text_segment_nsect){
                    add_symbol_to_entry(*sym_entry, nlist->n_un.n_strx,
                            nlist->n_value + aslr_slide);

                    /* However, these come right from the string table
                     * of the dylib we're currently on.
                     */
                    (*sym_entry)->syms[idx]->use_dsc_dylib_strtab = 1;
                    (*sym_entry)->syms[idx]->dsc_dylib_strtab_fileoff =
                        symtab_cmd->stroff;

                    idx++;
                }
            }
        }

        return;
    }
}

struct my_dsc_mapping *get_dsc_mappings(void *dscdata, int *len){
    struct dsc_hdr *dsc_hdr = (struct dsc_hdr *)dscdata;
    struct my_dsc_mapping *dsc_mappings = calloc(1,
            sizeof(struct my_dsc_mapping) * dsc_hdr->mappingcnt);

    unsigned int msz = sizeof(struct dsc_mapping_info) * dsc_hdr->mappingcnt;
    struct dsc_mapping_info *mapping_infos = malloc(msz);

    memcpy(mapping_infos, (uint8_t *)dscdata + dsc_hdr->mappingoff, msz);

    for(int i=0; i<dsc_hdr->mappingcnt; i++){
        struct dsc_mapping_info info = mapping_infos[i];

        dsc_mappings[i].file_start = info.fileoff;
        dsc_mappings[i].file_end = dsc_mappings[i].file_start + info.size;
        dsc_mappings[i].vm_start = info.address +
            debuggee->dyld_all_image_infos.sharedCacheSlide;
        dsc_mappings[i].vm_end = dsc_mappings[i].vm_start + info.size;
    }

    free(mapping_infos);

    *len = dsc_hdr->mappingcnt;
    return dsc_mappings;
}

int is_dsc_image(unsigned long vmoffset, struct my_dsc_mapping *mappings,
        unsigned int mcnt){
    for(int i=0; i<mcnt; i++){
        struct my_dsc_mapping dscmap = mappings[i];

        if(vmoffset >= dscmap.vm_start && vmoffset < dscmap.vm_end)
            return 1;
    }

    return 0;
}
