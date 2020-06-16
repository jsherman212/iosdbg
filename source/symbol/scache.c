#include <mach-o/loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../debuggee.h"

#include "scache.h"

struct dbg_sym_entry *create_sym_entry_for_dsc_image(void){
    int from_dsc = 1;
    struct dbg_sym_entry *entry = create_sym_entry(0, 0, from_dsc);

    return entry;
}

struct my_dsc_mapping *get_dsc_mappings(void *dscdata, int *len){
    if(!dscdata)
        return NULL;

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
