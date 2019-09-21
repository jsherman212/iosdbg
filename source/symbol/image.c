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
#include "../strext.h"

void *DSCDATA = NULL;
unsigned long DSCSZ = 0;

static unsigned long decode_uleb128(uint8_t **p, unsigned long *len){
    unsigned long val = 0;
    unsigned int shift = 0;

    const uint8_t *orig_p = *p;

    int more = 1;
    while(more){
        uint8_t byte = *(*p)++;
        val |= ((byte & 0x7f) << shift);
        shift += 7;

        if(byte < 0x80)
            break;
    }

    *len = (unsigned long)(*p - orig_p);

    return val;
}

static int get_cmds(unsigned long image_load_addr,
        struct symtab_command **symtab_cmd_out,
        struct segment_command_64 **__text_seg_cmd_out,
        int *__text_segment_nsect_out,
        struct linkedit_data_command **__lc_fxn_start_cmd_out){
    struct mach_header_64 image_hdr = {0};
    kern_return_t kret =
        read_memory_at_location(image_load_addr, &image_hdr, sizeof(image_hdr));

    if(kret)
        return 1;

    struct load_command *cmd = malloc(image_hdr.sizeofcmds);
    struct load_command *cmdorig = cmd;

    kret = read_memory_at_location(image_load_addr + sizeof(image_hdr),
            cmd, image_hdr.sizeofcmds);

    if(kret){
        free(cmd);
        return 1;
    }

    struct symtab_command *symtab_cmd = NULL;
    struct segment_command_64 *__text_seg_cmd = NULL;
    struct linkedit_data_command *lc_fxn_start_cmd = NULL;

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
        else if(cmd->cmd == LC_FUNCTION_STARTS){
            lc_fxn_start_cmd = malloc(cmd->cmdsize);
            memcpy(lc_fxn_start_cmd, cmd, cmd->cmdsize);
        }

        if(symtab_cmd && __text_seg_cmd && lc_fxn_start_cmd)
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

    if(symtab_cmd_out)
        *symtab_cmd_out = symtab_cmd;
    else
        free(symtab_cmd);

    if(__text_seg_cmd_out)
        *__text_seg_cmd_out = __text_seg_cmd;
    else
        free(__text_seg_cmd);

    if(__text_segment_nsect_out)
        *__text_segment_nsect_out = section_num;

    if(__lc_fxn_start_cmd_out)
        *__lc_fxn_start_cmd_out = lc_fxn_start_cmd;
    else
        free(lc_fxn_start_cmd);

    return 0;
}

enum { DSC = 0, NON_DSC };

struct lc_fxn_starts_entry {
    /* function start address */
    unsigned long vmaddr;
    /* length of function, -1 if first entry in LC_FUNCTION_STARTS */
    int len;
};

static int read_lc_fxn_starts(char *imagename, unsigned long image_load_addr,
        struct lc_fxn_starts_entry ***entries, int *entriescnt,
        unsigned int *entriescapa, unsigned long aslr_slide, int kind){
    struct segment_command_64 *__text = NULL;
    struct linkedit_data_command *lidc = NULL;

    if(get_cmds(image_load_addr, NULL, &__text, NULL, &lidc))
        return 1;

    uint8_t *lcfxnstart_start = NULL, *lcfxnstart_end = NULL;

    if(kind == DSC){
        lcfxnstart_start = (uint8_t *)DSCDATA + lidc->dataoff;
        lcfxnstart_end = lcfxnstart_start + lidc->datasize;
    }
    else{
        lcfxnstart_start = malloc(lidc->datasize);

        if(copy_file_contents(imagename, lidc->dataoff, lcfxnstart_start,
                    lidc->datasize) == -1){
            return 1;
        }
        
        lcfxnstart_end = lcfxnstart_start + lidc->datasize;
    }

    unsigned long total_fxn_len = 0,
                  len = 0,
                  nextfxnstartaddr = __text->vmaddr;

    uint8_t *p = lcfxnstart_start;
    int fxncnt = 0;

    while(p < lcfxnstart_end){
        unsigned long l = 0;
        unsigned long prevfxnlen = decode_uleb128(&p, &l);

        len += l;

        if(prevfxnlen == 0)
            continue;

        total_fxn_len += prevfxnlen;
        nextfxnstartaddr += prevfxnlen;

        if(fxncnt >= FAST_POW_TWO(*entriescapa) - 1){
            (*entriescapa)++;
            struct lc_fxn_starts_entry **entries_rea = realloc(*entries,
                    CALC_ENTRIES_CAPACITY(*entriescapa));
            *entries = entries_rea;
        }

        struct lc_fxn_starts_entry **e = *entries;
        e[fxncnt] = malloc(sizeof(struct lc_fxn_starts_entry));
        e[fxncnt]->vmaddr = nextfxnstartaddr + aslr_slide;

        if(fxncnt == 0)
            e[fxncnt]->len = FIRST_FXN_NO_LEN;
        else
            e[fxncnt - 1]->len = prevfxnlen;

        fxncnt++;
    }

    if(fxncnt > 0)
        (*entries)[fxncnt - 1]->len = __text->vmsize - total_fxn_len;

    *entriescnt = fxncnt;

    if(kind == NON_DSC)
        free(lcfxnstart_start);

    return 0;
}

enum { NAME_OK = 0, NAME_OOB, NAME_NOT_FOUND };

static int get_dylib_path(unsigned int dylib_offset, void **nameptrout){
    struct mach_header_64 *dylib_hdr =
        (struct mach_header_64 *)((uint8_t *)DSCDATA + dylib_offset);

    struct load_command *cmd =
        (struct load_command *)((uint8_t *)DSCDATA + dylib_offset +
                sizeof(*dylib_hdr));

    struct load_command *cmdorig = cmd;
    struct dylib_command *dylib_cmd = NULL;

    for(int i=0; i<dylib_hdr->ncmds; i++){
        if(cmd->cmd == LC_ID_DYLIB){
            dylib_cmd = (struct dylib_command *)cmd;
            break;
        }

        unsigned long diff = (uint8_t *)cmd - (uint8_t *)DSCDATA;
        cmd = (struct load_command *)((uint8_t *)DSCDATA + diff + cmd->cmdsize);
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

struct nlist_64_wrapper {
    struct nlist_64 *nlist;
    char *str;
};

static void add_nlist_wrapper(struct nlist_64_wrapper ***nlist_wrappers,
        struct nlist_64 *nlist, unsigned int *nlists_wrapper_capa, int idx,
        unsigned long aslr_slide, char *str){
    if(idx >= FAST_POW_TWO(*nlists_wrapper_capa) - 1){
        (*nlists_wrapper_capa)++;
        struct nlist_64_wrapper **nlists_rea =
            realloc(*nlist_wrappers, CALC_NLISTS_ARR_CAPACITY(*nlists_wrapper_capa));
        *nlist_wrappers = nlists_rea;
    }

    (*nlist_wrappers)[idx] = malloc(sizeof(struct nlist_64_wrapper));
    (*nlist_wrappers)[idx]->nlist = malloc(sizeof(struct nlist_64));
    memcpy((*nlist_wrappers)[idx]->nlist, nlist, sizeof(*nlist));
    (*nlist_wrappers)[idx]->nlist->n_value += aslr_slide;
    (*nlist_wrappers)[idx]->str = str;
}

struct dsc_local_symentry_wrapper {
    struct dsc_local_syms_entry *entry;
    char *dylib_path;
};

static int wrappercmp_q(const void *a, const void *b){
    struct dsc_local_symentry_wrapper *wa
        = *(struct dsc_local_symentry_wrapper **)a;
    struct dsc_local_symentry_wrapper *wb
        = *(struct dsc_local_symentry_wrapper **)b;

    if(!wa || !wb)
        return 0;

    return strcmp(wa->dylib_path, wb->dylib_path);
}

static int read_nlists(char *imagename, unsigned long image_load_addr,
        struct nlist_64_wrapper ***nlist_wrappers,
        int *nlists_wrapper_cnt, unsigned int *nlists_wrapper_capa,
        struct my_dsc_mapping *mappings, int mappingcnt, int dsc_image,
        struct symtab_command *symtab_cmd,
        struct segment_command_64 *__text_seg_cmd,
        int __text_segment_nsect,
        struct dsc_local_symentry_wrapper **dsc_local_sym_entries,
        int num_dsc_local_sym_entries){
    unsigned long aslr_slide = image_load_addr - __text_seg_cmd->vmaddr;

    int idx = 0;

    if(dsc_image){
        struct dsc_hdr *cache_hdr = (struct dsc_hdr *)DSCDATA;

        struct dsc_local_syms_info *syminfos =
            (struct dsc_local_syms_info *)((uint8_t *)DSCDATA +
                    cache_hdr->localsymoff);

        unsigned long nlist_offset = syminfos->nlistoff +
            cache_hdr->localsymoff;
        unsigned long strings_offset = syminfos->stringsoff +
            cache_hdr->localsymoff;
        unsigned long entries_offset = syminfos->entriesoff +
            cache_hdr->localsymoff;

        struct dsc_local_symentry_wrapper wkey = {0};
        wkey.dylib_path = imagename;
        struct dsc_local_symentry_wrapper *wkey_p = &wkey;
        struct dsc_local_symentry_wrapper **entry_wrapper =
            bsearch(&wkey_p, dsc_local_sym_entries, num_dsc_local_sym_entries,
                    sizeof(struct dsc_local_symentry_wrapper *), wrappercmp_q);

        if(!entry_wrapper)
            return 1;

        struct dsc_local_syms_entry *entry = (*entry_wrapper)->entry;

        if(!entry)
            return 1;

        unsigned long nlist_start = nlist_offset +
            (entry->nliststartidx * sizeof(struct nlist_64));
        int nlistcnt = entry->nlistcnt;

        /* These come right from the DSC string table,
         * not any dylib-specific string table.
         */
        for(int j=0; j<nlistcnt; j++){
            unsigned long nextnlistoff = nlist_start +
                (sizeof(struct nlist_64) * j);
            struct nlist_64 *nlist =
                (struct nlist_64 *)((uint8_t *)DSCDATA + nextnlistoff);

            if(nlist->n_sect == __text_segment_nsect){
                unsigned long stroff = strings_offset + nlist->n_un.n_strx;
                char *symname = (char *)((uint8_t *)DSCDATA + stroff);

                add_nlist_wrapper(nlist_wrappers, nlist,
                        nlists_wrapper_capa, idx, aslr_slide, symname);

                idx++;
            }
        }

        /* These come right from the string table
         * of the dylib we're currently on.
         */
        unsigned int nsyms = symtab_cmd->nsyms;
        unsigned long stab_symoff = symtab_cmd->symoff;
        unsigned long stab_stroff = symtab_cmd->stroff;

        for(int j=0; j<nsyms; j++){
            unsigned long nextnlistoff = stab_symoff +
                (sizeof(struct nlist_64) * j);
            struct nlist_64 *nlist =
                (struct nlist_64 *)((uint8_t *)DSCDATA + nextnlistoff);

            unsigned long stroff = stab_stroff + nlist->n_un.n_strx;
            char *symname = (char *)((uint8_t *)DSCDATA + stroff);

            /* don't add <redacted> symbols */
            if(*symname != '<'){
                if((nlist->n_type & N_TYPE) == N_SECT &&
                        nlist->n_sect == __text_segment_nsect){

                    add_nlist_wrapper(nlist_wrappers, nlist,
                            nlists_wrapper_capa, idx, aslr_slide, symname);

                    idx++;
                }
            }
        }
    }
    else{
        unsigned long symtab_addr = symtab_cmd->symoff + image_load_addr,
                      strtab_addr = symtab_cmd->stroff + image_load_addr;

        size_t nscount = sizeof(struct nlist_64) * symtab_cmd->nsyms;
        struct nlist_64 *ns = malloc(nscount);

        kern_return_t kret = read_memory_at_location(symtab_addr, ns, nscount);

        if(kret != KERN_SUCCESS)
            return 1;

        for(int j=0; j<symtab_cmd->nsyms; j++){
            struct nlist_64 nlist = ns[j];
            enum { maxlen = 512 };
            char str[maxlen] = {0};
            kret = read_memory_at_location(strtab_addr + ns[j].n_un.n_strx,
                    str, maxlen);

            if(kret != KERN_SUCCESS || !(*str) ||
                    (nlist.n_type & N_TYPE) != N_SECT ||
                    nlist.n_sect != __text_segment_nsect || nlist.n_value == 0){
                continue;
            }

            add_nlist_wrapper(nlist_wrappers, &nlist, nlists_wrapper_capa,
                    idx, aslr_slide, NULL);

            idx++;
        }

        free(ns);
    }

    *nlists_wrapper_cnt = idx;

    return 0;
}

static int nlistwcmp(const void *a, const void *b){
    struct nlist_64_wrapper *nwa = *(struct nlist_64_wrapper **)a;
    struct nlist_64_wrapper *nwb = *(struct nlist_64_wrapper **)b;

    if(!nwa || !nwb)
        return 0;

    struct nlist_64 *na = nwa->nlist;
    struct nlist_64 *nb = nwb->nlist;

    unsigned long navalue = na->n_value;
    unsigned long nbvalue = nb->n_value;

    if(navalue < nbvalue)
        return -1;
    else if(navalue == nbvalue)
        return 0;
    else
        return 1;
}

static struct dbg_sym_entry *create_sym_entry_for_image(char *imagename, 
        unsigned long image_load_addr, struct my_dsc_mapping *mappings,
        int mappingcnt, struct symtab_command **symtab_cmd_out,
        struct segment_command_64 **__text_seg_cmd_out,
        int *__text_segment_nsect_out,
        struct dsc_local_symentry_wrapper **dsc_local_sym_entries,
        int dsc_local_sym_entries_cnt){
    int dsc_image = is_dsc_image(image_load_addr, mappings, mappingcnt);

    struct symtab_command *symtab_cmd = NULL;
    struct segment_command_64 *__text_seg_cmd = NULL;

    int __text_segment_nsect = 0;

    if(get_cmds(image_load_addr, &symtab_cmd, &__text_seg_cmd,
            &__text_segment_nsect, NULL)){
        return NULL;
    }

    unsigned long aslr_slide = image_load_addr - __text_seg_cmd->vmaddr;

    *__text_segment_nsect_out = __text_segment_nsect;

    struct dbg_sym_entry *entry = NULL;

    if(dsc_image)
        entry = create_sym_entry_for_dsc_image();
    else{
        int from_dsc = 0;
        unsigned long strtab_vmaddr = symtab_cmd->stroff + image_load_addr;

        entry = create_sym_entry(strtab_vmaddr, symtab_cmd->stroff, from_dsc);
    }

    unsigned int lc_fxn_starts_capacity =
        CALC_ENTRIES_CAPACITY(STARTING_CAPACITY);
    int num_lc_fxn_starts_entries = 0;
    struct lc_fxn_starts_entry **lc_fxn_starts_entries =
        malloc(lc_fxn_starts_capacity);

    if(read_lc_fxn_starts(imagename, image_load_addr, &lc_fxn_starts_entries,
            &num_lc_fxn_starts_entries, &lc_fxn_starts_capacity,
            aslr_slide, dsc_image ? DSC : NON_DSC)){
        free(entry);
        free(symtab_cmd);
        free(__text_seg_cmd);

        return NULL;
    }

    unsigned long real_entries_arr_sz =
        num_lc_fxn_starts_entries * sizeof(struct lc_fxn_starts_entry *);
    struct lc_fxn_starts_entry **lc_fxn_starts_entries_rea =
        realloc(lc_fxn_starts_entries, real_entries_arr_sz);
    lc_fxn_starts_entries = lc_fxn_starts_entries_rea;

    unsigned int nlists_capacity = CALC_NLISTS_ARR_CAPACITY(STARTING_CAPACITY);
    int num_nlists = 0;
    struct nlist_64_wrapper **nlists = malloc(nlists_capacity);

    if(read_nlists(imagename, image_load_addr, &nlists, &num_nlists,
            &nlists_capacity, mappings, mappingcnt, dsc_image,
            symtab_cmd, __text_seg_cmd, __text_segment_nsect,
            dsc_local_sym_entries, dsc_local_sym_entries_cnt)){
        free(entry);
        free(symtab_cmd);
        free(__text_seg_cmd);

        return NULL;
    }

    unsigned long real_nlists_arr_sz = num_nlists * sizeof(struct nlist_64 *);
    struct nlist_64_wrapper **nlists_rea = realloc(nlists, real_nlists_arr_sz);
    nlists = nlists_rea;

    /* prep nlist array for binary searches */
    qsort(nlists, num_nlists, sizeof(struct nlist_64_wrapper *), nlistwcmp);

    /* add the symbols for the current entry */
    for(int i=0; i<num_lc_fxn_starts_entries; i++){
        unsigned long vmaddr = lc_fxn_starts_entries[i]->vmaddr;

        struct nlist_64 nlist_for_wrapper = {0};
        nlist_for_wrapper.n_value = vmaddr;

        struct nlist_64_wrapper nwkey = {0};
        nwkey.nlist = &nlist_for_wrapper;

        struct nlist_64_wrapper *nwkey_p = &nwkey;

        struct nlist_64_wrapper **found_nw_dp = bsearch(&nwkey_p, nlists,
                num_nlists, sizeof(struct nlist_64_wrapper *), nlistwcmp);

        int fxnlen = lc_fxn_starts_entries[i]->len;

        /* this symbol is named */
        if(found_nw_dp){
            struct nlist_64_wrapper *found_nw = *found_nw_dp;

            add_symbol_to_entry(entry,
                    dsc_image ? 0 : found_nw->nlist->n_un.n_strx,
                    found_nw->nlist->n_value, fxnlen, NAMED_SYM,
                    dsc_image ? found_nw->str : NULL);
        }
        else{
            add_symbol_to_entry(entry, 0, vmaddr, fxnlen, UNNAMED_SYM, NULL);
        }
    }

    for(int i=0; i<num_lc_fxn_starts_entries; i++)
        free(lc_fxn_starts_entries[i]);
    free(lc_fxn_starts_entries);
    lc_fxn_starts_entries = NULL;

    for(int i=0; i<num_nlists; i++){
        free(nlists[i]->nlist);
        free(nlists[i]);
    }

    free(nlists);
    nlists = NULL;

    return entry;
}

static int stash_dsc_local_syms_entries(
        struct dsc_local_symentry_wrapper ***wrappers_out,
        int *wrapperscnt, unsigned int *wrapperscapa){
    struct dsc_hdr *cache_hdr = (struct dsc_hdr *)DSCDATA;

    struct dsc_local_syms_info *syminfos =
        (struct dsc_local_syms_info *)((uint8_t *)DSCDATA +
                cache_hdr->localsymoff);

    unsigned long nlist_offset = syminfos->nlistoff +
        cache_hdr->localsymoff;
    unsigned long strings_offset = syminfos->stringsoff +
        cache_hdr->localsymoff;
    unsigned long entries_offset = syminfos->entriesoff +
        cache_hdr->localsymoff;

    int entriescnt = syminfos->entriescnt;

    for(int i=0; i<entriescnt; i++){
        unsigned long nextentryoff =
            entries_offset + (sizeof(struct dsc_local_syms_entry) * i);
        struct dsc_local_syms_entry *entry =
            (struct dsc_local_syms_entry *)((uint8_t *)DSCDATA + nextentryoff);

        void *nameptr = NULL;
        int result = get_dylib_path(entry->dyliboff, &nameptr);

        if(i >= FAST_POW_TWO(*wrapperscapa) - 1){
            (*wrapperscapa)++;
            struct dsc_local_symentry_wrapper **wrappers_rea =
                realloc(*wrappers_out,
                        CALC_DSC_LOCAL_SYM_ENTRIES_ARR_CAPACITY(*wrapperscapa));
            *wrappers_out = wrappers_rea;
        }

        (*wrappers_out)[i] = malloc(sizeof(struct dsc_local_symentry_wrapper));
        (*wrappers_out)[i]->entry = entry;
        (*wrappers_out)[i]->dylib_path = nameptr;
    }

    *wrapperscnt = entriescnt;

    return 0;
}

int initialize_debuggee_dyld_all_image_infos(void){
    struct task_dyld_info dyld_info = {0};
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

    kern_return_t kret = task_info(debuggee->task, TASK_DYLD_INFO,
            (task_info_t)&dyld_info, &count);

    if(kret)
        return 1;

    kret = read_memory_at_location(dyld_info.all_image_info_addr,
            &debuggee->dyld_all_image_infos,
            sizeof(struct dyld_all_image_infos));

    struct dyld_all_image_infos dyld_all_image_infos =
        debuggee->dyld_all_image_infos;

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
        get_dsc_mappings(DSCDATA, &num_dsc_mappings);

    debuggee->symbols = linkedlist_new();

    /* Stash the local symbols entries from the dyld shared cache
     * so we can bsearch them.
     */
    unsigned int dsc_local_symentries_wrapper_capa =
        CALC_DSC_LOCAL_SYM_ENTRIES_ARR_CAPACITY(STARTING_CAPACITY);
    int dsc_local_sym_entries_cnt = 0;
    struct dsc_local_symentry_wrapper **wrappers =
        malloc(dsc_local_symentries_wrapper_capa);

    stash_dsc_local_syms_entries(&wrappers, &dsc_local_sym_entries_cnt,
            &dsc_local_symentries_wrapper_capa);

    int wrappers_real_sz =
        sizeof(struct dsc_local_symentry_wrapper *) * dsc_local_sym_entries_cnt;
    struct dsc_local_symentry_wrapper **wrappers_rea =
        realloc(wrappers, wrappers_real_sz);
    wrappers = wrappers_rea;

    qsort(wrappers, dsc_local_sym_entries_cnt,
            sizeof(struct dsc_local_symentry_wrapper *), wrappercmp_q);

    //clock_t start = clock(), end;

    for(int i=0; i<debuggee->dyld_all_image_infos.infoArrayCount; i++){
        int maxlen = PATH_MAX;
        char fpath[maxlen];
        memset(fpath, 0, maxlen);

        unsigned int bytes_read = 0;
        char read_byte = '\1';

        /* Read a byte at a time, arbitary large sizes for fpath
         * causes errors.
         */
        while(read_byte && bytes_read < maxlen && kret == KERN_SUCCESS){
            unsigned long imgpath =
                (unsigned long)debuggee->dyld_info_array[i].imageFilePath;
            kret = read_memory_at_location(imgpath + bytes_read,
                    &read_byte, sizeof(char));

            fpath[bytes_read] = read_byte;

            bytes_read++;
        }

        unsigned long image_load_address =
            (unsigned long)debuggee->dyld_info_array[i].imageLoadAddress;

        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;
        int __text_seg_nsect = 0;

        struct dbg_sym_entry *entry = create_sym_entry_for_image(fpath,
                image_load_address, dsc_mappings, num_dsc_mappings,
                &symtab_cmd, &__text_seg_cmd, &__text_seg_nsect,
                wrappers, dsc_local_sym_entries_cnt);

        if(!entry)
            continue;

        /* we only care about the last part of fpath */
        char *lastslash = strrchr(fpath, '/');
        char *path = fpath;

        if(lastslash)
            path = lastslash + 1;

        entry->imagename = strdup(path);

        linkedlist_add(debuggee->symbols, entry);

        free(symtab_cmd);
        free(__text_seg_cmd);
    }

    /*
    end = clock();

    double seconds = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("%s: took %f seconds to create all the entries\n",
            __func__, seconds);
    */
    free(dsc_mappings);

    for(int i=0; i<dsc_local_sym_entries_cnt; i++)
        free(wrappers[i]);
    
    free(wrappers);

    return 0;
}
