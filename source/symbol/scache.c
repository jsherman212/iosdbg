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
