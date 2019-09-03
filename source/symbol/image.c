#include <limits.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/stab.h>
#include <stdio.h>
#include <stdlib.h>

#include "dbgsymbol.h"
#include "scache.h"

#include "../dbgio.h"
#include "../debuggee.h"
#include "../linkedlist.h"
#include "../memutils.h"

static int symoffcmp(const void *a, const void *b){
    struct sym *sa = *(struct sym **)a;
    struct sym *sb = *(struct sym **)b;

    if(!sa || !sb)
        return 0;

    return (long)sa->sym_func_start - (long)sb->sym_func_start;
}

static void get_cmds(void *dscdata, unsigned long image_load_addr,
        struct symtab_command **symtab_cmd_out,
        struct segment_command_64 **__text_seg_cmd_out,
        int *__text_segment_nsect_out){
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

    struct symtab_command *symtab_cmd = NULL;
    struct segment_command_64 *__text_seg_cmd = NULL;

    int section_num = 1;

    for(int i=0; i<image_hdr.ncmds; i++){
        if(cmd->cmd == LC_SYMTAB){
            symtab_cmd = malloc(cmd->cmdsize);
            memcpy(symtab_cmd, cmd, cmd->cmdsize);
        }
        else if(cmd->cmd == LC_SEGMENT_64){
            unsigned int textval1 = *(unsigned int *)"__TE";
            unsigned short textval2 = *(unsigned short *)"XT";

            struct segment_command_64 *s = (struct segment_command_64 *)cmd;

            unsigned int segval1 = *(unsigned int *)s->segname;
            unsigned short segval2 =
                *(unsigned short *)(s->segname + sizeof(unsigned int));

            if(segval1 == textval1 && segval2 == textval2){
                __text_seg_cmd = malloc(cmd->cmdsize);
                memcpy(__text_seg_cmd, cmd, cmd->cmdsize);
            }
        }

        if(symtab_cmd && __text_seg_cmd)
            break;

        /* We don't want to include symbols not defined in __TEXT.
         * See https://developer.apple.com/documentation/kernel/nlist_64/1583960-n_sect?language=objc
         * for more info.
         * Skip __PAGEZERO also, it isn't anywhere physically, it's just a
         * part of the mach-o binary.
         */
        if(cmd->cmd == LC_SEGMENT_64 && !__text_seg_cmd){
            unsigned int pgval1 = *(unsigned int *)"__PA";
            unsigned short pgval2 = *(unsigned short *)"GE";

            struct segment_command_64 *s = (struct segment_command_64 *)cmd;

            unsigned int segval1 = *(unsigned int *)s->segname;
            unsigned short segval2 =
                *(unsigned short *)(s->segname + sizeof(unsigned int));

            if(pgval1 != segval1 && pgval2 != segval2)
                section_num++;
        }

        cmd = (struct load_command *)((uint8_t *)cmd + cmd->cmdsize);
    }

    free(cmdorig);

    *symtab_cmd_out = symtab_cmd;
    *__text_seg_cmd_out = __text_seg_cmd;
    *__text_segment_nsect_out = section_num;
}

static struct dbg_sym_entry *create_sym_entry_for_image(void *dscdata, char *imagename,
        unsigned long image_load_addr, struct my_dsc_mapping *mappings,
        int mappingcnt, struct symtab_command **symtab_cmd_out,
        struct segment_command_64 **__text_seg_cmd_out,
        int *__text_segment_nsect_out){
    struct dbg_sym_entry *entry = NULL;

    if(is_dsc_image(image_load_addr, mappings, mappingcnt)){
        entry = create_sym_entry_for_dsc_image(imagename);

        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;
        int __text_segment_nsect = 0;

        get_cmds(dscdata, image_load_addr, &symtab_cmd, &__text_seg_cmd,
                &__text_segment_nsect);

        if(!symtab_cmd || !__text_seg_cmd)
            return NULL;

        *__text_segment_nsect_out = __text_segment_nsect;

        unsigned long slide = image_load_addr - __text_seg_cmd->vmaddr;
        get_dsc_image_symbols(dscdata, imagename, slide, &entry, symtab_cmd,
                __text_segment_nsect);

        struct sym **sym_rea = realloc(entry->syms,
                sizeof(struct sym *) * entry->cursymarrsz);
        entry->syms = sym_rea;

        if(entry->cursymarrsz > 1){
            qsort(entry->syms, entry->cursymarrsz,
                    sizeof(struct sym *), symoffcmp);

            int oneless = entry->cursymarrsz - 1;

            for(int i=0; i<oneless; i++){
                unsigned int len = entry->syms[i+1]->sym_func_start -
                    entry->syms[i]->sym_func_start;
                entry->syms[i]->sym_func_len = len;
            }

            unsigned int len =
                (__text_seg_cmd->vmaddr +  __text_seg_cmd->vmsize) -
                entry->syms[oneless]->sym_func_start;
            entry->syms[oneless]->sym_func_len = len;
        }
        else if(entry->cursymarrsz > 0){
            unsigned int len =
                (__text_seg_cmd->vmaddr + __text_seg_cmd->vmsize) -
                entry->syms[0]->sym_func_start;
            entry->syms[0]->sym_func_len = len;
        }
    }
    else{
        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;
        int __text_segment_nsect = 0;

        get_cmds(dscdata, image_load_addr, &symtab_cmd, &__text_seg_cmd,
                &__text_segment_nsect);

        if(!symtab_cmd || !__text_seg_cmd)
            return NULL;

        *__text_segment_nsect_out = __text_segment_nsect;

        int from_dsc = 0;
        unsigned long strtab_vmaddr = symtab_cmd->stroff + image_load_addr;

        entry = create_sym_entry(imagename, strtab_vmaddr, symtab_cmd->stroff,
                from_dsc);

        *symtab_cmd_out = symtab_cmd;
        *__text_seg_cmd_out = __text_seg_cmd;
    }

    return entry;
}

int initialize_debuggee_dyld_all_image_infos(void *dscdata){
    struct task_dyld_info dyld_info = {0};
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

    kern_return_t kret = task_info(debuggee->task, TASK_DYLD_INFO,
            (task_info_t)&dyld_info, &count);

    if(kret)
        return 1;

    kret = read_memory_at_location(dyld_info.all_image_info_addr,
            &debuggee->dyld_all_image_infos,
            sizeof(struct dyld_all_image_infos));

    struct dyld_all_image_infos dyld_all_image_infos = debuggee->dyld_all_image_infos;

    count = sizeof(struct dyld_image_info) *
        debuggee->dyld_all_image_infos.infoArrayCount;

    debuggee->dyld_info_array = malloc(count);

    kret = read_memory_at_location(
            (unsigned long)debuggee->dyld_all_image_infos.infoArray,
            debuggee->dyld_info_array, count);

    if(kret)
        return 1;

    /* Read the mappings of dyld shared cache to differentiate
     * between cache images and other images.
     */
    int num_dsc_mappings = 0;
    struct my_dsc_mapping *dsc_mappings =
        get_dsc_mappings(dscdata, &num_dsc_mappings);

    debuggee->symbols = linkedlist_new();

    for(int i=0; i<debuggee->dyld_all_image_infos.infoArrayCount; i++){
        int count = PATH_MAX;
        char fpath[count];

        kret = read_memory_at_location(
                (unsigned long)debuggee->dyld_info_array[i].imageFilePath,
                fpath, count);

        unsigned long image_load_address =
            (unsigned long)debuggee->dyld_info_array[i].imageLoadAddress;

        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;
        int __text_seg_nsect = 0;

        struct dbg_sym_entry *entry = create_sym_entry_for_image(dscdata, fpath,
                image_load_address, dsc_mappings, num_dsc_mappings,
                &symtab_cmd, &__text_seg_cmd, &__text_seg_nsect);

        if(!entry)
            continue;

        unsigned long symtab_addr = 0, strtab_addr = 0;

        if(entry->from_dsc){
            linkedlist_add(debuggee->symbols, entry);

            free(symtab_cmd);
            free(__text_seg_cmd);

            continue;
        }
        else{
            symtab_addr = symtab_cmd->symoff + image_load_address;
            strtab_addr = symtab_cmd->stroff + image_load_address;

            size_t nscount = sizeof(struct nlist_64) * symtab_cmd->nsyms;
            struct nlist_64 *ns = malloc(nscount);

            kret = read_memory_at_location(symtab_addr, ns, nscount);

            if(kret != KERN_SUCCESS)
                continue;

            for(int j=0; j<symtab_cmd->nsyms; j++){
                struct nlist_64 nlist = ns[j];
                char str[PATH_MAX] = {0};
                kret = read_memory_at_location(strtab_addr + ns[j].n_un.n_strx,
                        str, PATH_MAX);

                if(kret != KERN_SUCCESS || !(*str) ||
                        (nlist.n_type & N_TYPE) != N_SECT ||
                        nlist.n_sect != __text_seg_nsect || nlist.n_value == 0){
                    continue;
                }

                unsigned long slid_addr_start = 0;
                unsigned long slid_addr_end = 0;

                slid_addr_start = nlist.n_value +
                    (image_load_address - __text_seg_cmd->vmaddr);

                add_symbol_to_entry(entry,nlist.n_un.n_strx, slid_addr_start);
            }

            free(ns);
        }

        if(entry->cursymarrsz > 1){
            qsort(entry->syms, entry->cursymarrsz,
                    sizeof(struct sym *), symoffcmp);

            int oneless = entry->cursymarrsz - 1;

            for(int i=0; i<oneless; i++){
                unsigned int len = entry->syms[i+1]->sym_func_start -
                    entry->syms[i]->sym_func_start;
                entry->syms[i]->sym_func_len = len;
            }

            unsigned int len =
                (__text_seg_cmd->vmaddr +  __text_seg_cmd->vmsize) -
                entry->syms[oneless]->sym_func_start;
            entry->syms[oneless]->sym_func_len = len;
        }
        else if(entry->cursymarrsz > 0){
            unsigned int len =
                (__text_seg_cmd->vmaddr + __text_seg_cmd->vmsize) -
                entry->syms[0]->sym_func_start;
            entry->syms[0]->sym_func_len = len;
        }

        linkedlist_add(debuggee->symbols, entry);

        free(symtab_cmd);
        free(__text_seg_cmd);
    }

    free(dsc_mappings);

    return 0;
}
