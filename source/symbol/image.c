#include <limits.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/stab.h>
#include <stdio.h>
#include <stdlib.h>

#include "dbgsymbol.h"

#include "../debuggee.h"
#include "../linkedlist.h"
#include "../memutils.h"

struct dsc_hdr {
    char magic[16];
    uint32_t mappingOffset;
    uint32_t mappingCount;
};

struct dsc_mapping_info {
    uint64_t address;
    uint64_t size;
    uint64_t fileOffset;
    uint32_t maxProt;
    uint32_t initProt;
};

struct my_dsc_mapping {
    unsigned long file_start;
    unsigned long file_end;
    unsigned long vm_start;
    unsigned long vm_end;
};

static int is_dsc_image(unsigned long vmoffset, unsigned long fileoffset,
        struct my_dsc_mapping *mappings, unsigned int mcnt,
        struct my_dsc_mapping *which){
    /* We have to check this first because one of the dsc file mappings
     * starts at 0.
     */
    int dscimage = 0;

    for(int i=0; i<mcnt; i++){
        struct my_dsc_mapping dscmap = mappings[i];
        //printf("%s: dscmap vm: %#lx-%#lx, file: %#lx-%#lx\n",
        //      __func__, dscmap.vm_start, dscmap.vm_end, dscmap.file_start,
        //    dscmap.file_end);

        if(vmoffset >= dscmap.vm_start && vmoffset < dscmap.vm_end){
            dscimage = 1;
            break;
        }
    }

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
}

static int cmp(const void *a, const void *b){
    struct sym *sa = *(struct sym **)a;
    struct sym *sb = *(struct sym **)b;
    if(!sa || !sb)
        return 0;
    /*
    if(!sa)
        return -1;
    if(!sb)
        return 1;
    */
    //printf("%s: sa addr start %#lx sb addr start %#lx\n", __func__,
      //      sa.symaddr_start, sb.symaddr_end);

    //long astart = (long)sa.symaddr_start;
    //long bstart = (long)sb.symaddr_start;
    long astart = (long)sa->symaddr_start;
    long bstart = (long)sb->symaddr_start;

    return (long)sa->symaddr_start - (long)sb->symaddr_start;
 //   return astart - bstart;

    /*
    if(astart < bstart)
        return -1;
    else if(astart == bstart)
        return 0;
    else
        return 1;
*/
    //return (long)sa.symaddr_start - (long)sb.symaddr_start;
}

static int cmp1(const void *a, const void *b){
    return *(int *)a - *(int *)b;
}

int initialize_debuggee_dyld_all_image_infos(void){


    struct task_dyld_info dyld_info = {0};
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

    kern_return_t kret = task_info(debuggee->task, TASK_DYLD_INFO,
            (task_info_t)&dyld_info, &count);
    //printf("%s: task_info says %s\n", __func__, mach_error_string(kret));

    if(kret){
        return 1;
    }

    printf("%s: all image info addr @ %#llx\n", __func__, dyld_info.all_image_info_addr);

    kret = read_memory_at_location(dyld_info.all_image_info_addr,
            &debuggee->dyld_all_image_infos,
            sizeof(struct dyld_all_image_infos));

    //printf("%s: 1st read_memory_at_location says %s\n", __func__, mach_error_string(kret));
    if(kret)
        return 1;

    unsigned long dyld_load_addr =
        (unsigned long)debuggee->dyld_all_image_infos.dyldImageLoadAddress;

    printf("%s: dyld loaded @ %#lx\n", __func__, dyld_load_addr);

    unsigned long scache_slide = debuggee->dyld_all_image_infos.sharedCacheSlide;

    printf("%s: shared cache slide: %#lx\n",
            __func__, debuggee->dyld_all_image_infos.sharedCacheSlide);
    printf("%s: shared cache base address: %#lx\n",
            __func__, debuggee->dyld_all_image_infos.sharedCacheBaseAddress);

    count = sizeof(struct dyld_image_info) *
        debuggee->dyld_all_image_infos.infoArrayCount;

    debuggee->dyld_info_array = malloc(count);

    kret = read_memory_at_location(
            (unsigned long)debuggee->dyld_all_image_infos.infoArray,
            debuggee->dyld_info_array, count);

    //printf("%s: 2nd read_memory_at_location says %s\n", __func__, mach_error_string(kret));

    if(kret)
        return 1;

    /* Read the mappings of dyld shared cache to differentiate
     * between cache images and other images.
     */
    struct dsc_hdr dsc_hdr = {0};
    unsigned long dsc_base_addr = debuggee->dyld_all_image_infos.sharedCacheBaseAddress;

    kret = read_memory_at_location(dsc_base_addr, &dsc_hdr, sizeof(dsc_hdr));

    //printf("%s: dsc hdr read_memory_at_location says %s\n", __func__, mach_error_string(kret));

    if(kret)
        return 1;

    //printf("%s: dsc_hdr.mappingOffset %#x dsc_hdr.mappingCount %#x\n",
    //      __func__, dsc_hdr.mappingOffset, dsc_hdr.mappingCount);

    struct my_dsc_mapping dsc_mappings[dsc_hdr.mappingCount];
    memset(dsc_mappings, 0, dsc_hdr.mappingCount);

    unsigned int msz = sizeof(struct dsc_mapping_info) * dsc_hdr.mappingCount;

    struct dsc_mapping_info *mapping_infos = malloc(msz);
    kret = read_memory_at_location(dsc_base_addr + dsc_hdr.mappingOffset,
            mapping_infos, msz);

    //  printf("%s: dsc mappings read_memory_at_location says %s\n", __func__, mach_error_string(kret));

    if(kret)
        return 1;

    for(int i=0; i<dsc_hdr.mappingCount; i++){
        struct dsc_mapping_info info = mapping_infos[i];

        //    printf("%s: mapping info %d: address %#llx size %#llx fileOffset %#llx\n",
        //          __func__, i, info.address, info.size, info.fileOffset);

        dsc_mappings[i].file_start = info.fileOffset;
        dsc_mappings[i].file_end = dsc_mappings[i].file_start + info.size;
        dsc_mappings[i].vm_start = info.address + scache_slide;
        dsc_mappings[i].vm_end = dsc_mappings[i].vm_start + info.size;
    }

    free(mapping_infos);

    debuggee->dyld_image_info_filePaths = calloc(sizeof(char *), 
            debuggee->dyld_all_image_infos.infoArrayCount);

    debuggee->symbols = linkedlist_new();

    for(int i=0; i<debuggee->dyld_all_image_infos.infoArrayCount; i++){
        int count = PATH_MAX;
        char fpath[count];
        kret = read_memory_at_location(
                (unsigned long)debuggee->dyld_info_array[i].imageFilePath,
                fpath, count);


        //printf("%s: read_memory_at_location inside loop says %s\n",
        //      __func__, mach_error_string(kret));

        if(kret == KERN_SUCCESS){
            size_t len = strlen(fpath) + 1;

            debuggee->dyld_image_info_filePaths[i] = malloc(len);
            strncpy(debuggee->dyld_image_info_filePaths[i], fpath, len);
        }

        unsigned long load_addr =
            (unsigned long)debuggee->dyld_info_array[i].imageLoadAddress;

        printf("%s: load addr for image %d: %#lx\n", __func__, i, load_addr);

        struct mach_header_64 image_hdr = {0};
        kret = read_memory_at_location(load_addr, &image_hdr, sizeof(image_hdr));

        if(kret)
            return 1;

        struct load_command *cmd = malloc(image_hdr.sizeofcmds);
        struct load_command *cmdorig = cmd;
        struct segment_command_64 *text_segcmd = NULL;
        //struct segment_command_64 *linkedit_segcmd = NULL, *text_segcmd = NULL;

        kret = read_memory_at_location(load_addr + sizeof(image_hdr),
                cmd, image_hdr.sizeofcmds);

        if(kret){
            free(cmd);
            return 1;
        }

        //printf("%s: 3rd read_memory_at_location says %s, cmd->cmd %#x"
        //      " cmd->cmdsize %#x\n",
        //    __func__, mach_error_string(kret), cmd->cmd, cmd->cmdsize);


        struct symtab_command *symtab_cmd = NULL;
        //struct dysymtab_command *dysymtab_cmd = NULL;

        // XXX left off on finding LC_SYMTAB
        for(int j=0; j<image_hdr.ncmds; j++){
            if(cmd->cmd == LC_SYMTAB){
                symtab_cmd = malloc(cmd->cmdsize);
                memcpy(symtab_cmd, cmd, cmd->cmdsize);
            }
            /* 
            else if(cmd->cmd == LC_DYSYMTAB){
                dysymtab_cmd = malloc(cmd->cmdsize);
                memcpy(dysymtab_cmd, cmd, cmd->cmdsize);
            }
            */
            else if(cmd->cmd == LC_SEGMENT_64){
                struct segment_command_64 *s = (struct segment_command_64 *)cmd;

                /*
                if(strcmp(s->segname, "__LINKEDIT") == 0){
                    //printf("%s: got linkedit\n", __func__);
                    linkedit_segcmd = malloc(cmd->cmdsize);
                    memcpy(linkedit_segcmd, cmd, cmd->cmdsize);
                }
                */
                if(strcmp(s->segname, "__TEXT") == 0){
                    //printf("%s: got text\n", __func__);
                    text_segcmd = malloc(cmd->cmdsize);
                    memcpy(text_segcmd, cmd, cmd->cmdsize);
                }
            }

            cmd = (struct load_command *)((uint8_t *)cmd + cmd->cmdsize);
        }

        free(cmdorig);

        printf("%s: image %d '%s': symoff %#x nsyms %#x stroff %#x strsize %#x\n",
                __func__, i, debuggee->dyld_image_info_filePaths[i],
                symtab_cmd->symoff, symtab_cmd->nsyms,
                symtab_cmd->stroff, symtab_cmd->strsize);

        unsigned long symtab_addr = 0, strtab_addr = 0;

        /* If symtab is a part of the shared cache, then stroff should be too */
        struct my_dsc_mapping apartof = {0};
        if(is_dsc_image(load_addr, symtab_cmd->symoff, dsc_mappings, dsc_hdr.mappingCount,
                    &apartof)){
            //printf("*******%s: dsc image!\n", __func__);
            symtab_addr = apartof.vm_start +
                (symtab_cmd->symoff - apartof.file_start);
            strtab_addr = apartof.vm_start +
                (symtab_cmd->stroff - apartof.file_start);
        }
        else{
            symtab_addr = load_addr + symtab_cmd->symoff;
            strtab_addr = load_addr + symtab_cmd->stroff;
        }

        free(symtab_cmd);

        printf("%s: symtab_addr %#lx strtab_addr %#lx\n",
                __func__, symtab_addr, strtab_addr);

        size_t nscount = sizeof(struct nlist_64) * symtab_cmd->nsyms;
        struct nlist_64 *ns = malloc(nscount);

        kret = read_memory_at_location(symtab_addr, ns, nscount);

        if(kret == KERN_SUCCESS){
            struct dbg_sym_entry *sym_entry = create_sym_entry(fpath,
                    symtab_cmd->nsyms, strtab_addr);

            for(int j=0; j<symtab_cmd->nsyms; j++){
                char str[PATH_MAX] = {0};
                kret = read_memory_at_location(strtab_addr + ns[j].n_un.n_strx,
                        str, PATH_MAX);

                if(kret == KERN_SUCCESS){
                    unsigned long slid_addr_start = 0;
                    unsigned long slid_addr_end = 0;

                    if(ns[j].n_value != 0 && strlen(str) > 0){
                        slid_addr_start = ns[j].n_value + (load_addr - text_segcmd->vmaddr);

                        add_symbol_to_entry(sym_entry, j, ns[j].n_un.n_strx,
                                slid_addr_start, slid_addr_end);
/*
                        if(i==0){
                            printf("%s: nlist %d: strx '%s' n_type %#x n_sect %#x n_desc %#x"
                                    " symbol location %#lx\n",
                                    __func__, j, str, ns[j].n_type, ns[j].n_sect,
                                    ns[j].n_desc, slid_addr_start);
                        }
                        */
                    }
                }
            }

            qsort(sym_entry->syms, sym_entry->cursymarrsz, sizeof(struct sym *), cmp);

            for(int i=0; i<sym_entry->cursymarrsz; i++){
    //            printf("%s: symbol %d: %p\n", __func__, i, sym_entry->syms[i]);
      //          continue;
                if(!sym_entry->syms[i])
                    continue;

                // XXX 8-23-19
                // XXX end of __TEXT segment is good for the end offset of the last
                // symbol because this array will be sorted
                // XXX figure out why duplicate symbols are added
                if(i != sym_entry->cursymarrsz-1){
                 /*   unsigned long next_sym_start = 0;
                    int j = i+1;
                    while(!sym_entry->syms[j++])
                        continue;
                        */
                    //if(sym_entry->syms[j]){
                        sym_entry->syms[i]->symaddr_end =
                            sym_entry->syms[i+1]->symaddr_start;


                        if(sym_entry->syms[i+1]->symaddr_start < load_addr)
                            sym_entry->syms[i]->symaddr_end =
                                (load_addr + (text_segcmd->fileoff + text_segcmd->filesize));

                    //}
                }
                else{
                    sym_entry->syms[i]->symaddr_end =
                        (load_addr - (text_segcmd->vmaddr + text_segcmd->vmsize));
                }
                
                //if(strcmp(sym_entry->imagename, "/usr/lib/system/libdyld.dylib") == 0){
                if(strcmp(sym_entry->imagename, "/private/var/mobile/testprogs/./params") == 0){
                    int len = 64;
                    char symname[len];
                    memset(symname, 0, len);
                    kern_return_t kret =
                        read_memory_at_location(sym_entry->strtab_addr +
                                sym_entry->syms[i]->strtabidx, symname, len);

                    //printf("%s: read_memory_at_location says %s\n", __func__,
                      //      mach_error_string(kret));
                    //printf("%s: symbol %d: [%#lx-%#lx] '%s'\n",
                      //      __func__, i, sym_entry->syms[i]->symaddr_start,
                        //    sym_entry->syms[i]->symaddr_end, symname);
                }
                
            }

            /*
            int l = 4;
            int *a = malloc(sizeof(int)*l);
            a[0] = 34;
            a[1] = 3;
            a[2] = 22;
            a[3] = 9;

            for(int i=0; i<l; i++)
                printf("%d ", a[i]);
            printf("\n");

            qsort(a, l, sizeof(int), cmp1);

            for(int i=0; i<l; i++)
                printf("%d ", a[i]);
            printf("\n");
            */
            linkedlist_add(debuggee->symbols, sym_entry);
        }


        free(ns);

        //free(symtab_cmd);
        //free(dysymtab_cmd);
        //free(strs);
        //free(linkedit_segcmd);
        free(text_segcmd);
    }

    int bkpthere = 0;

    return 0;
}
