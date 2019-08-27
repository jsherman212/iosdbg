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
    uint32_t mappingOffset;
    uint32_t mappingCount;
    uint32_t imagesOffset;
    uint32_t imagesCount;
    uint64_t dyldBaseAddress;
    uint64_t codeSignatureOffset;
    uint64_t codeSignatureSize;
    uint64_t slideInfoOffset;
    uint64_t slideInfoSize;
    uint64_t localSymbolsOffset;
    uint64_t localSymbolsSize;
    /*
       uint32_t	imagesCount;			// number of dyld_cache_image_info entries
       uint64_t	dyldBaseAddress;		// base address of dyld when cache was built
       uint64_t	codeSignatureOffset;	// file offset of code signature blob
       uint64_t	codeSignatureSize;		// size of code signature blob (zero means to end of file)
       uint64_t	slideInfoOffset;		// file offset of kernel slid info
       uint64_t	slideInfoSize;			// size of kernel slid info
       uint64_t	localSymbolsOffset;		// file offset of where local symbols are stored
       uint64_t	localSymbolsSize;
       */
};

struct dsc_local_syms_info {
    uint32_t nlistOffset;
    uint32_t nlistCount;
    uint32_t stringsOffset;
    uint32_t stringsSize;
    uint32_t entriesOffset;
    uint32_t entriesCount;
};
/*
   uint32_t	nlistOffset;		// offset into this chunk of nlist entries
   uint32_t	nlistCount;			// count of nlist entries
   uint32_t	stringsOffset;		// offset into this chunk of string pool
   uint32_t	stringsSize;		// byte count of string pool
   uint32_t	entriesOffset;		// offset into this chunk of array of dyld_cache_local_symbols_entry
   uint32_t	entriesCount;
   */

struct dsc_mapping_info {
    uint64_t address;
    uint64_t size;
    uint64_t fileOffset;
    uint32_t maxProt;
    uint32_t initProt;
};

struct dbg_sym_entry *create_sym_entry_for_dsc_image(char *imagename){

    return NULL;
}

struct my_dsc_mapping *get_dsc_mappings(int *len){
    struct dsc_hdr dsc_hdr = {0};
    unsigned long dsc_base_addr = debuggee->dyld_all_image_infos.sharedCacheBaseAddress;

    kern_return_t kret = read_memory_at_location(dsc_base_addr, &dsc_hdr, sizeof(dsc_hdr));

    //printf("%s: dsc hdr read_memory_at_location says %s\n", __func__, mach_error_string(kret));

    if(kret)
        return NULL;

    //printf("%s: dsc_hdr.mappingOffset %#x dsc_hdr.mappingCount %#x\n",
    //      __func__, dsc_hdr.mappingOffset, dsc_hdr.mappingCount);

    //struct my_dsc_mapping dsc_mappings[dsc_hdr.mappingCount];
    //memset(dsc_mappings, 0, dsc_hdr.mappingCount);

    struct my_dsc_mapping *dsc_mappings = calloc(1,
            sizeof(struct my_dsc_mapping) * dsc_hdr.mappingCount);
    //memset(dsc_mappings, 0, dsc_hdr.mappingCount);

    unsigned int msz = sizeof(struct dsc_mapping_info) * dsc_hdr.mappingCount;
    struct dsc_mapping_info *mapping_infos = malloc(msz);

    kret = read_memory_at_location(dsc_base_addr + dsc_hdr.mappingOffset,
            mapping_infos, msz);

    //  printf("%s: dsc mappings read_memory_at_location says %s\n", __func__, mach_error_string(kret));

    if(kret)
        return NULL;

    for(int i=0; i<dsc_hdr.mappingCount; i++){
        struct dsc_mapping_info info = mapping_infos[i];

        //    printf("%s: mapping info %d: address %#llx size %#llx fileOffset %#llx\n",
        //          __func__, i, info.address, info.size, info.fileOffset);

        dsc_mappings[i].file_start = info.fileOffset;
        dsc_mappings[i].file_end = dsc_mappings[i].file_start + info.size;
        dsc_mappings[i].vm_start = info.address +
            debuggee->dyld_all_image_infos.sharedCacheSlide;
        dsc_mappings[i].vm_end = dsc_mappings[i].vm_start + info.size;
    }

    free(mapping_infos);

    *len = dsc_hdr.mappingCount;
    return dsc_mappings;
}

/*
int is_dsc_image(unsigned long vmoffset, unsigned long fileoffset,
        struct my_dsc_mapping *mappings, unsigned int mcnt,
        struct my_dsc_mapping *which){
        */
int is_dsc_image(unsigned long vmoffset, struct my_dsc_mapping *mappings,
        unsigned int mcnt){
    // XXX remove this comment once I'm done
    /* We have to check this first because one of the dsc file mappings
     * starts at 0.
     */
    //int dscimage = 0;

    for(int i=0; i<mcnt; i++){
        struct my_dsc_mapping dscmap = mappings[i];
        //printf("%s: dscmap vm: %#lx-%#lx, file: %#lx-%#lx\n",
          //    __func__, dscmap.vm_start, dscmap.vm_end, dscmap.file_start,
           // dscmap.file_end);

        if(vmoffset >= dscmap.vm_start && vmoffset < dscmap.vm_end){
            return 1;
            //dscimage = 1;
            //break;
        }
    }

    //return dscimage;
    return 0;

    /*
    if(!dscimage)
        return 0;

    // printf("%s: fileoffset %#lx\n", __func__, fileoffset);
    for(int i=0; i<mcnt; i++){
        struct my_dsc_mapping dscmap = mappings[i];
        // printf("%s: dscmap vm: %#lx-%#lx, file: %#lx-%#lx\n",
        //       __func__, dscmap.vm_start, dscmap.vm_end, dscmap.file_start,
        //     dscmap.file_end);

        if(fileoffset >= dscmap.file_start && fileoffset < dscmap.file_end){
            *which = dscmap;
            return 1;
        }
    }

    return 0;
    */
}
