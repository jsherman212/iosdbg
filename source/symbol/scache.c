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
    uint8_t		uuid[16];				// unique value for each shared cache file
	uint64_t	cacheType;				// 0 for development, 1 for production
	uint32_t	branchPoolsOffset;		// file offset to table of uint64_t pool addresses
	uint32_t	branchPoolsCount;	    // number of uint64_t entries
	uint64_t	accelerateInfoAddr;		// (unslid) address of optimization info
	uint64_t	accelerateInfoSize;		// size of optimization info
	uint64_t	imagesTextOffset;		// file offset to first dyld_cache_image_text_info
	uint64_t	imagesTextCount;
};

struct dsc_local_syms_info {
    uint32_t nlistOffset;
    uint32_t nlistCount;
    uint32_t stringsOffset;
    uint32_t stringsSize;
    uint32_t entriesOffset;
    uint32_t entriesCount;
};

struct dsc_local_syms_entry {
	uint32_t	dylibOffset;		// offset in cache file of start of dylib
	uint32_t	nlistStartIndex;	// start index of locals for this dylib
	uint32_t	nlistCount;			// number of local symbols for this dylib
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

static char *get_dylib_path(FILE *dscfile, unsigned int dylib_offset){
    struct mach_header_64 dylib_hdr = {0};

    fseek(dscfile, dylib_offset, SEEK_SET);
    fread(&dylib_hdr, sizeof(char), sizeof(dylib_hdr), dscfile);

    struct load_command *cmd = malloc(dylib_hdr.sizeofcmds);
    struct load_command *cmdorig = cmd;
    struct dylib_command *dylib_cmd = NULL;

    fseek(dscfile, dylib_offset + sizeof(dylib_hdr), SEEK_SET);
    fread(cmd, sizeof(char), dylib_hdr.sizeofcmds, dscfile);

    for(int i=0; i<dylib_hdr.ncmds; i++){
        if(cmd->cmd == LC_ID_DYLIB){
            dylib_cmd = malloc(cmd->cmdsize);
            memcpy(dylib_cmd, cmd, cmd->cmdsize);

            break;
        }

        cmd = (struct load_command *)((uint8_t *)cmd + cmd->cmdsize);
    }

    char *name = NULL;
    unsigned int nameoff = dylib_cmd->dylib.name.offset;

    struct load_command *cmdend =
        (struct load_command *)((uint8_t *)cmdorig + dylib_hdr.sizeofcmds);
    unsigned long bytes_left = cmdend - cmd;

    if(nameoff > bytes_left){
        free(cmdorig);
        free(dylib_cmd);
        return strdup("Name out of bounds");
    }

    if(dylib_cmd)
        name = strdup((const char *)((uint8_t *)cmd + nameoff));

    free(cmdorig);
    free(dylib_cmd);

    return name;
}

struct dbg_sym_entry *create_sym_entry_for_dsc_image(char *imagename){
    int from_dsc = 1;
    struct dbg_sym_entry *entry = create_sym_entry(imagename, 0, 0, from_dsc);

    return entry;
}

void get_dsc_image_symbols(char *imagename, unsigned long aslr_slide,
        struct dbg_sym_entry **sym_entry, struct symtab_command *symtab_cmd){
    // XXX no hardcode on master
    char *dscpath = "/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64";

    FILE *dscfile = fopen(dscpath, "rb");

    if(!dscfile){
        printf("%s: couldn't open dsc\n", __func__);
        return;
    }

    struct dsc_hdr cache_hdr = {0};
    fread(&cache_hdr, sizeof(char), sizeof(cache_hdr), dscfile);

    printf("localSymbolsOffset %#llx\n", cache_hdr.localSymbolsOffset);

    struct dsc_local_syms_info syminfos = {0};
    fseek(dscfile, cache_hdr.localSymbolsOffset, SEEK_SET);
    fread(&syminfos, sizeof(char), sizeof(syminfos), dscfile);

    syminfos.nlistOffset += cache_hdr.localSymbolsOffset;
    syminfos.stringsOffset += cache_hdr.localSymbolsOffset;
    syminfos.entriesOffset += cache_hdr.localSymbolsOffset;

    printf("nlistOffset %#x nlistCount %#x stringsOffset %#x stringsSize %#x "
            "entriesOffset %#x entriesCount %#x\n",
            syminfos.nlistOffset, syminfos.nlistCount, syminfos.stringsOffset,
            syminfos.stringsSize, syminfos.entriesOffset,
            syminfos.entriesCount);


    // XXX this line should be somewhere else
    (*sym_entry)->strtab_fileaddr = syminfos.stringsOffset;
    
    for(int i=0; i<syminfos.entriesCount; i++){
        fseek(dscfile,
                syminfos.entriesOffset +
                (sizeof(struct dsc_local_syms_entry) * i), SEEK_SET);
        struct dsc_local_syms_entry entry = {0};
        fread(&entry, sizeof(char), sizeof(entry), dscfile);

        uint64_t nlist_start = syminfos.nlistOffset +
            (entry.nlistStartIndex * sizeof(struct nlist_64));

        char *cur_dylib_path = get_dylib_path(dscfile, entry.dylibOffset);

        /*
        printf("Entry %d/%d: dylibOffset %#x nlistStartIndex %#llx"
                " nlistCount %#x\n",
                i+1, syminfos.entriesCount, entry.dylibOffset,
                nlist_start, entry.nlistCount);
        */
        if(strcmp(imagename, cur_dylib_path) != 0){
            free(cur_dylib_path);
            continue;
        }

        /*
           printf("Entry %d/%d: '%s': nlistStartIndex %#llx"
           " nlistCount %#x\n",
           i+1, syminfos.entriesCount, cur_dylib_path,
           nlist_start, entry.nlistCount);
           */

        int idx = 0;

        for(int j=0; j<entry.nlistCount; j++){
        //while(idx < entry.nlistCount){
            struct nlist_64 nlist = {0};

            fseek(dscfile, nlist_start + (sizeof(nlist) * j), SEEK_SET);
            fread(&nlist, sizeof(char), sizeof(nlist), dscfile);

            unsigned long stroff = nlist.n_un.n_strx + syminfos.stringsOffset;

            add_symbol_to_entry(*sym_entry, idx, nlist.n_un.n_strx,
                    nlist.n_value + aslr_slide, 0);
            (*sym_entry)->syms[idx]->dsc_use_stroff = 0;
            idx++;
            /*
            enum { len = 512 };
            char str[len] = {0};
            fseek(dscfile, stroff, SEEK_SET);
            fread(str, sizeof(char), len, dscfile);
            str[len - 1] = '\0';

            printf("Entry %d: '%s': nlist %d/%d: strx '%s' - %#lx, n_type %#x"
                    " n_sect %#x n_desc %#x n_value %#llx\n",
                    i, cur_dylib_path, j+1, entry.nlistCount,
                    str, stroff,
                    nlist.n_type, nlist.n_sect,
                    nlist.n_desc, nlist.n_value);
        */
        }


        free(cur_dylib_path);

        // XXX now, go thru dylib's symtab to get the rest
        unsigned int nsyms = symtab_cmd->nsyms;
        unsigned int limit = idx + nsyms;

        for(int j=0; j<nsyms; j++){
            struct nlist_64 nlist = {0};

            fseek(dscfile, symtab_cmd->symoff + (sizeof(nlist) * j), SEEK_SET);
            fread(&nlist, sizeof(char), sizeof(nlist), dscfile);

            unsigned long stroff = symtab_cmd->stroff + nlist.n_un.n_strx;

            enum { len = 512 };
            char symname[len] = {0};

            fseek(dscfile, stroff, SEEK_SET);
            fread(symname, sizeof(char), len, dscfile);

            symname[len - 1] = '\0';

            if(strcmp(symname, "<redacted>") != 0){
                if((nlist.n_type & N_TYPE) == N_SECT){
                    add_symbol_to_entry(*sym_entry, idx, nlist.n_un.n_strx,
                            nlist.n_value + aslr_slide, 0);


                    //(*sym_entry)->strtab_fileaddr = symtab_cmd->stroff;
                    (*sym_entry)->syms[idx]->dsc_use_stroff = 1;
                    (*sym_entry)->syms[idx]->stroff_fileaddr = symtab_cmd->stroff;

                    idx++;
                }
            }
            else{
                //printf("%s: not adding redacted\n", __func__);
            }
        }
    }

    fclose(dscfile);
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
