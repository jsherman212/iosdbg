#include <limits.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/stab.h>
#include <stdio.h>
#include <stdlib.h>

#include "dbgsymbol.h"
#include "scache.h"

#include "../debuggee.h"
#include "../linkedlist.h"
#include "../memutils.h"

static int symoffcmp(const void *a, const void *b){
    struct sym *sa = *(struct sym **)a;
    struct sym *sb = *(struct sym **)b;

    if(!sa || !sb)
        return 0;

    return (long)sa->symaddr_start - (long)sb->symaddr_start;
}

static void get_cmds(unsigned long image_load_addr,
        struct symtab_command **symtab_cmd_out,
        struct segment_command_64 **__text_seg_cmd_out){
    struct mach_header_64 image_hdr = {0};
    kern_return_t kret =
        read_memory_at_location(image_load_addr, &image_hdr, sizeof(image_hdr));

    if(kret)
        return;

    struct load_command *cmd = malloc(image_hdr.sizeofcmds);
    struct load_command *cmdorig = cmd;

    kret = read_memory_at_location(image_load_addr + sizeof(image_hdr),
            cmd, image_hdr.sizeofcmds);

    if(kret){
        free(cmd);
        return;
    }

    //printf("%s: 3rd read_memory_at_location says %s, cmd->cmd %#x"
    //      " cmd->cmdsize %#x\n",
    //    __func__, mach_error_string(kret), cmd->cmd, cmd->cmdsize);


    struct symtab_command *symtab_cmd = NULL;
    struct segment_command_64 *__text_seg_cmd = NULL;

    for(int i=0; i<image_hdr.ncmds; i++){
        if(cmd->cmd == LC_SYMTAB){
            symtab_cmd = malloc(cmd->cmdsize);
            memcpy(symtab_cmd, cmd, cmd->cmdsize);
        }
        else if(cmd->cmd == LC_SEGMENT_64){
            struct segment_command_64 *s = (struct segment_command_64 *)cmd;

            if(strcmp(s->segname, "__TEXT") == 0){
                __text_seg_cmd = malloc(cmd->cmdsize);
                memcpy(__text_seg_cmd, cmd, cmd->cmdsize);
            }
        }

        if(symtab_cmd && __text_seg_cmd)
            break;

        cmd = (struct load_command *)((uint8_t *)cmd + cmd->cmdsize);
    }

    free(cmdorig);

    *symtab_cmd_out = symtab_cmd;
    *__text_seg_cmd_out = __text_seg_cmd;
}

// XXX if image is a part of shared cache, call create_sym_entry_for_dsc_image
// otherwise write the logic here
static struct dbg_sym_entry *create_sym_entry_for_image(char *imagename,
        unsigned long image_load_addr, struct my_dsc_mapping *mappings,
        int mappingcnt, struct symtab_command **symtab_cmd_out,
        struct segment_command_64 **__text_seg_cmd_out){
    struct dbg_sym_entry *entry = NULL;

    if(is_dsc_image(image_load_addr, mappings, mappingcnt)){
        // XXX remember I wrote a function to get the dylib's name
        // from the struct dylib load command
        printf("%s: '%s' IS a shared cache image\n", __func__, imagename);

        entry = create_sym_entry_for_dsc_image(imagename);

        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;

        get_cmds(image_load_addr, &symtab_cmd, &__text_seg_cmd);

        unsigned long slide = image_load_addr - __text_seg_cmd->vmaddr;
        printf("%s: slide for '%s' is %#lx\n", __func__, imagename, slide);
        get_dsc_image_symbols(imagename, slide, &entry, symtab_cmd);

        qsort(entry->syms, entry->cursymarrsz,
                sizeof(struct sym *), symoffcmp);

        // XXX sorted by default
        for(int i=0; i<entry->cursymarrsz; i++){
            if(i != entry->cursymarrsz-1){
                entry->syms[i]->symaddr_end =
                    entry->syms[i+1]->symaddr_start;
            }
            else{
                entry->syms[i]->symaddr_end = image_load_addr +
                    __text_seg_cmd->vmsize;
            }
        }

        free(symtab_cmd);
        free(__text_seg_cmd);
    }
    else{
        printf("%s: '%s' IS NOT a shared cache image\n", __func__, imagename);

        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;

        get_cmds(image_load_addr, &symtab_cmd, &__text_seg_cmd);

        if(!symtab_cmd || !__text_seg_cmd)
            return NULL;

        printf("%s: got symtab & __text commands\n", __func__);
        int from_dsc = 0;
        //unsigned long symtab_vmaddr = symtab_cmd->symoff + image_load_addr,
        //             strtab_vmaddr = symtab_cmd->stroff + image_load_addr;
        unsigned long strtab_vmaddr = symtab_cmd->stroff + image_load_addr;
        entry = create_sym_entry(imagename, strtab_vmaddr, symtab_cmd->stroff,
                from_dsc);

        *symtab_cmd_out = symtab_cmd;
        *__text_seg_cmd_out = __text_seg_cmd;

        //free(symtab_cmd);
        //symtab_cmd = NULL;
    }

    return entry;
}

/*
   static int stroffcmp(const void *a, const void *b){
   struct sym *sa = *(struct sym **)a;
   struct sym *sb = *(struct sym **)b;

   if(!sa || !sb)
   return 0;

   return (long)(sa->strtab_fileaddr + sa->strtabidx) -
   (long)(sb->strtab_fileaddr + sb->strtabidx);

   }

   static int cmp1(const void *a, const void *b){
   return *(int *)a - *(int *)b;
   }
   */

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

    struct dyld_all_image_infos dyld_all_image_infos = debuggee->dyld_all_image_infos;
    //int bkpthere1 = 0;

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

    unsigned long scache_vmbase = debuggee->dyld_all_image_infos.sharedCacheBaseAddress;

    count = sizeof(struct dyld_image_info) *
        debuggee->dyld_all_image_infos.infoArrayCount;

    debuggee->dyld_info_array = malloc(count);

    kret = read_memory_at_location(
            (unsigned long)debuggee->dyld_all_image_infos.infoArray,
            debuggee->dyld_info_array, count);

    //printf("%s: 2nd read_memory_at_location says %s\n", __func__, mach_error_string(kret));

    if(kret)
        return 1;

    //printf("%s: dsc_hdr.mappingOffset %#x dsc_hdr.mappingCount %#x\n",
    //      __func__, dsc_hdr.mappingOffset, dsc_hdr.mappingCount);

    /* Read the mappings of dyld shared cache to differentiate
     * between cache images and other images.
     */
    int num_dsc_mappings = 0;
    struct my_dsc_mapping *dsc_mappings = get_dsc_mappings(&num_dsc_mappings);

    printf("%s: dsc_mappings %p num %d\n", __func__, dsc_mappings, num_dsc_mappings);

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

        //printf("%s: Image %d: '%s'\n", __func__, i, fpath);
        unsigned long image_load_address =
            (unsigned long)debuggee->dyld_info_array[i].imageLoadAddress;

        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;
        struct dbg_sym_entry *entry = create_sym_entry_for_image(fpath,
                image_load_address, dsc_mappings, num_dsc_mappings,
                &symtab_cmd, &__text_seg_cmd);

        if(!entry)
            continue;

        unsigned long symtab_addr = 0, strtab_addr = 0;

        if(entry->from_dsc){
            free(symtab_cmd);
            free(__text_seg_cmd);
            //free(ns);

            linkedlist_add(debuggee->symbols, entry);

            continue;
        }
        else{
            symtab_addr = symtab_cmd->symoff + image_load_address;
            strtab_addr = symtab_cmd->stroff + image_load_address;

            printf("%s: symtab_addr %#lx strtab_addr %#lx\n",
                    __func__, symtab_addr, strtab_addr);

            size_t nscount = sizeof(struct nlist_64) * symtab_cmd->nsyms;
            struct nlist_64 *ns = malloc(nscount);

            kret = read_memory_at_location(symtab_addr, ns, nscount);

            if(kret != KERN_SUCCESS)
                continue;

            for(int j=0; j<symtab_cmd->nsyms; j++){
                char str[PATH_MAX] = {0};
                kret = read_memory_at_location(strtab_addr + ns[j].n_un.n_strx,
                        str, PATH_MAX);
                str[PATH_MAX - 1] = '\0';

                if(kret == KERN_SUCCESS){
                    unsigned long slid_addr_start = 0;
                    unsigned long slid_addr_end = 0;

                    if(ns[j].n_value != 0 && strlen(str) > 0){
                        slid_addr_start = ns[j].n_value +
                            (image_load_address - __text_seg_cmd->vmaddr);

                        // XXX is this correct?
                        if((ns[j].n_type & N_TYPE) == N_SECT){
                            add_symbol_to_entry(entry, j, ns[j].n_un.n_strx,
                                    slid_addr_start, slid_addr_end);

                            if(i==0){
                                printf("%s: nlist %d: strx '%s' n_type %#x "
                                        " n_sect %#x n_desc %#x"
                                        " symbol location %#lx\n",
                                        __func__, j, str, ns[j].n_type & N_TYPE,
                                        ns[j].n_sect, ns[j].n_desc, slid_addr_start);
                            }
                        }
                    }
                }
            }

            free(ns);
        }



        //        continue;
        free(symtab_cmd);
        //      free(__text_seg_cmd);

        qsort(entry->syms, entry->cursymarrsz,
                sizeof(struct sym *), symoffcmp);

        for(int i=0; i<entry->cursymarrsz; i++){
            // XXX 8-23-19
            // XXX end of __TEXT segment is good for the end offset of the last
            // symbol because this array will be sorted
            // XXX figure out why duplicate symbols are added
            if(i != entry->cursymarrsz-1){
                entry->syms[i]->symaddr_end =
                    entry->syms[i+1]->symaddr_start;
            }
            else{
                entry->syms[i]->symaddr_end = image_load_address +
                    __text_seg_cmd->vmsize;
            }


            if(strcmp(entry->imagename, "/private/var/mobile/testprogs/./params") == 0){
                int len = 64;
                char symname[len];
                memset(symname, 0, len);
                kern_return_t kret =
                    read_memory_at_location(entry->strtab_vmaddr +
                            entry->syms[i]->strtabidx, symname, len);

                //printf("%s: read_memory_at_location says %s\n", __func__,
                //      mach_error_string(kret));
                printf("%s: symbol %d: [%#lx-%#lx] '%s'\n",
                        __func__, i, entry->syms[i]->symaddr_start,
                        entry->syms[i]->symaddr_end, symname);
            }
        }

        free(__text_seg_cmd);

        linkedlist_add(debuggee->symbols, entry);
    }


    free(dsc_mappings);

    return 0;
}
