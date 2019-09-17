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

void *DSCDATA = NULL;
unsigned long DSCSZ = 0;

int PM = 0;

static int symoffcmp(const void *a, const void *b){
    struct sym *sa = *(struct sym **)a;
    struct sym *sb = *(struct sym **)b;

    if(!sa || !sb)
        return 0;

    return (long)sa->sym_func_start - (long)sb->sym_func_start;
}

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

static void get_cmds(void *dscdata, unsigned long image_load_addr,
        struct symtab_command **symtab_cmd_out,
        struct segment_command_64 **__text_seg_cmd_out,
        int *__text_segment_nsect_out,
        struct linkedit_data_command **__lc_fxn_start_cmd_out){
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
}

enum { DSC = 0, NON_DSC };

#define PUBG "/var/containers/Bundle/Application/B899DCE5-E05D-4CD2-B39E-AD15311610BF/ShadowTrackerExtra.app/ShadowTrackerExtra"

// XXX imagename param is only for testing
static int read_lc_fxn_starts(char *imagename, void *machodata,
        unsigned long image_load_addr,
        struct lc_fxn_starts_entry ***entries, int *entriescnt,
        unsigned int *entriescapa, unsigned long aslr_slide, int kind){
    struct segment_command_64 *__text = NULL;
    struct linkedit_data_command *lidc = NULL;

    get_cmds(machodata, image_load_addr, NULL, &__text, NULL, &lidc);

    //printf("%s: found LC_FUNCTION_STARTS dataoff %#x datasize %#x\n",
      //      __func__, lidc->dataoff, lidc->datasize);

    uint8_t *lcfxnstart_start = NULL, *lcfxnstart_end = NULL;

    if(kind == DSC){
        lcfxnstart_start = (uint8_t *)machodata + lidc->dataoff;
        lcfxnstart_end = lcfxnstart_start + lidc->datasize;
    }
    else{
        printf("%s: image loaded at %#lx, lcfxnstart file offset at %#x\n",
                __func__, image_load_addr, lidc->dataoff);
        // XXX seems to be more sane to read right from the file. Don't
        // have to deal with nonsense shifting in memory


        //unsigned long lcfxnoff = image_load_addr + lidc->dataoff;
        //printf("%s: lcfxnoff for non dsc image %#lx\n",
          //      __func__, lcfxnoff);
        lcfxnstart_start = malloc(lidc->datasize);

        if(copy_file_contents(imagename, lidc->dataoff, lcfxnstart_start,
                    lidc->datasize) == -1){
            printf("%s: warning: could not open file at '%s'\n",
                    __func__, imagename);
            return 1;
        }
        /*
        FILE *imgfile = fopen(imagename, "rb");

        if(!imgfile){
            printf("%s: warning: could not open file at '%s'\n",
                    __func__, imagename);
            return 1;
        }

        fseek(imgfile, lidc->dataoff, SEEK_SET);
        fread(lcfxnstart_start, sizeof(char), lidc->datasize, imgfile);

        fclose(imgfile);
        */
        
        lcfxnstart_end = lcfxnstart_start + lidc->datasize;
        /*kern_return_t kret =
            read_memory_at_location(lcfxnoff, lcfxnstart_start, lidc->datasize);
        //printf("%s: kret %s\n", __func__, mach_error_string(kret));
        lcfxnstart_end = lcfxnstart_start + lidc->datasize;
        
        char *buf = NULL;
        dump_memory(lcfxnoff, 0x50, &buf);
        printf("%s: memdump of lcfxnoff:\n%s\n", __func__, buf);
        free(buf);
        */
    }

    unsigned long total_fxn_len = 0;
    unsigned long len = 0;
    unsigned long nextfxnstartaddr = __text->vmaddr;

    uint8_t *p = lcfxnstart_start;
    int fxncnt = 0;

    while(p < lcfxnstart_end){
        unsigned long l = 0;
        unsigned long prevfxnlen = decode_uleb128(&p, &l);

        len += l;

        // XXX discard
        if(prevfxnlen == 0)
            continue;

        total_fxn_len += prevfxnlen;
        nextfxnstartaddr += prevfxnlen;

        /*
        if(strcmp(imagename, PUBG) == 0 && fxncnt < 50)
        printf("%s: fxn %d start @ %#lx, \n",
                __func__, fxncnt, nextfxnstartaddr + aslr_slide);
                */

        if(fxncnt >= FAST_POW_TWO(*entriescapa) - 1){
            (*entriescapa)++;
            struct lc_fxn_starts_entry **entries_rea = realloc(*entries,
                    CALC_ENTRIES_CAPACITY(*entriescapa));
            *entries = entries_rea;
        }

        struct lc_fxn_starts_entry **e = *entries;
        e[fxncnt] = malloc(sizeof(struct lc_fxn_starts_entry));
        e[fxncnt]->vmaddr = nextfxnstartaddr + aslr_slide;

        if(fxncnt == 0){
    //        printf("first fxn in bin, prev fxn len N/A\n");

            e[fxncnt]->len = FIRST_FXN_NO_LEN;
        }
        else{
      //      printf("prev fxn's len = %#lx\n", prevfxnlen);

            e[fxncnt - 1]->len = prevfxnlen;
        }

        fxncnt++;
    }

    //printf("%s: last fxn len is %#llx\n",
      //      __func__, __text->vmsize - total_fxn_len);

    if(fxncnt > 0)
        (*entries)[fxncnt - 1]->len = __text->vmsize - total_fxn_len;

    *entriescnt = fxncnt;

    if(kind == NON_DSC)
        free(lcfxnstart_start);

    return 0;
}

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

static int read_nlists(void *dscdata, char *imagename,
        unsigned long image_load_addr, struct nlist_64_wrapper ***nlists,
        int *nlistscnt, unsigned int *nlistscapa,
        struct my_dsc_mapping *mappings, int mappingcnt){
    if(is_dsc_image(image_load_addr, mappings, mappingcnt)){
        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;
        int __text_segment_nsect = 0;
        // XXX not zero inside iosdbg

        get_cmds(dscdata, image_load_addr, &symtab_cmd, &__text_seg_cmd,
                &__text_segment_nsect, NULL);

        if(!symtab_cmd || !__text_seg_cmd)
            return 1;

        unsigned long aslr_slide = image_load_addr - __text_seg_cmd->vmaddr;

        struct dsc_hdr *cache_hdr = (struct dsc_hdr *)dscdata;

        //printf("localsymoff %#llx\n", cache_hdr.localSymbolsOffset);

        struct dsc_local_syms_info *syminfos =
            (struct dsc_local_syms_info *)((uint8_t *)dscdata +
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
                (struct dsc_local_syms_entry *)((uint8_t *)dscdata + nextentryoff);

            void *nameptr = NULL;
            int result = get_dylib_path(dscdata, entry->dyliboff, &nameptr);

            if(result == NAME_OOB || result == NAME_NOT_FOUND)
                continue;

            char *cur_dylib_path = nameptr;

            if(strcmp(imagename, cur_dylib_path) != 0)
                continue;

            /*
               printf("Entry %d/%d: '%s': nliststartidx %#lx"
               " nlistcnt %#x\n",
               i+1, entriescnt, cur_dylib_path,
               nlist_offset, entry->nlistcnt);
            */
            unsigned long nlist_start = nlist_offset +
                (entry->nliststartidx * sizeof(struct nlist_64));
            int idx = 0;
            int nlistcnt = entry->nlistcnt;

            for(int j=0; j<nlistcnt; j++){
                unsigned long nextnlistoff = nlist_start +
                    (sizeof(struct nlist_64) * j);
                struct nlist_64 *nlist =
                    (struct nlist_64 *)((uint8_t *)dscdata + nextnlistoff);

                //printf("%s: nlist %p nlist->n_sect %#x\n",
                  //      __func__, nlist, nlist->n_sect);
                if(nlist->n_sect == __text_segment_nsect){
                    /* These come right from the DSC string table,
                     * not any dylib-specific string table.
                     */
                    unsigned long stroff = strings_offset + nlist->n_un.n_strx;
                    char *symname = (char *)((uint8_t *)dscdata + stroff);

                    if(idx >= FAST_POW_TWO(*nlistscapa) - 1){
                        (*nlistscapa)++;
                        struct nlist_64_wrapper **nlists_rea =
                            realloc(*nlists, CALC_NLISTS_ARR_CAPACITY(*nlistscapa));
                        *nlists = nlists_rea;
                    }

                   // printf("%s: nlist %p\n", __func__, nlist);
                    //nlist->n_value += aslr_slide;

                    (*nlists)[idx] = malloc(sizeof(struct nlist_64_wrapper));
                    (*nlists)[idx]->nlist = malloc(sizeof(struct nlist_64));
                    memcpy((*nlists)[idx]->nlist, nlist, sizeof(*nlist));
                    (*nlists)[idx]->nlist->n_value += aslr_slide;
                    (*nlists)[idx]->str = symname;

                    idx++;

                    /*
                       printf("from DSC: '%s': nlist %d/%d: strx '%s' - %#lx, n_type %#x"
                       " n_sect %#x n_desc %#x n_value %#llx\n",
                       cur_dylib_path, j+1, nlistcnt,
                       symname, stroff,
                       nlist->n_type, nlist->n_sect,
                       nlist->n_desc, nlist->n_value);
                       */
                }
            }

            // XXX now, go thru dylib's symtab to get the rest
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

                if(*symname != '<'){
                    if((nlist->n_type & N_TYPE) == N_SECT &&
                            nlist->n_sect == __text_segment_nsect){
                        /* However, these come right from the string table
                         * of the dylib we're currently on.
                         */

                        if(idx >= FAST_POW_TWO(*nlistscapa) - 1){
                            (*nlistscapa)++;
                            struct nlist_64_wrapper **nlists_rea =
                                realloc(*nlists, CALC_NLISTS_ARR_CAPACITY(*nlistscapa));
                            *nlists = nlists_rea;
                        }

                    //    nlist->n_value += aslr_slide;

                        (*nlists)[idx] = malloc(sizeof(struct nlist_64_wrapper));
                        (*nlists)[idx]->nlist = malloc(sizeof(struct nlist_64));
                        memcpy((*nlists)[idx]->nlist, nlist, sizeof(*nlist));
                        (*nlists)[idx]->nlist->n_value += aslr_slide;
                        (*nlists)[idx]->str = symname;

                        idx++;
/*
                           printf("from dylib: '%s': nlist %d/%d: "
                           " strx '%s' - %#lx, n_type %#x"
                           " n_sect %#x n_desc %#x n_value %#llx\n",
                           cur_dylib_path, j+1, nsyms,
                           symname, stroff,
                           nlist->n_type, nlist->n_sect,
                           nlist->n_desc, nlist->n_value);
                           */
                    }
                }
            }

            *nlistscnt = idx;

            // XXX done for this entry move on
            return 0;
        }
    }
    else{
        printf("%s: not a dsc image\n", __func__);
        
        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;
        int __text_seg_nsect = 0;
        // XXX not zero inside iosdbg

        get_cmds(dscdata, image_load_addr, &symtab_cmd, &__text_seg_cmd,
                &__text_seg_nsect, NULL);

        if(!symtab_cmd || !__text_seg_cmd)
            return 1;

        unsigned long aslr_slide = image_load_addr - __text_seg_cmd->vmaddr,
                      symtab_addr = symtab_cmd->symoff + image_load_addr,
                      strtab_addr = symtab_cmd->stroff + image_load_addr;

        size_t nscount = sizeof(struct nlist_64) * symtab_cmd->nsyms;
        struct nlist_64 *ns = malloc(nscount);

        kern_return_t kret = read_memory_at_location(symtab_addr, ns, nscount);

        if(kret != KERN_SUCCESS)
            return 1;

        int idx = 0;

        for(int j=0; j<symtab_cmd->nsyms; j++){
            struct nlist_64 nlist = ns[j];
            char str[PATH_MAX] = {0};
            kret = read_memory_at_location(strtab_addr + ns[j].n_un.n_strx,
                    str, PATH_MAX);

            if(kret != KERN_SUCCESS || !(*str) ||
                    (nlist.n_type & N_TYPE) != N_SECT ||
                    nlist.n_sect != __text_seg_nsect || nlist.n_value == 0){// ||
                    //strcmp(str, "__mh_execute_header") == 0){
                continue;
            }

            if(idx >= FAST_POW_TWO(*nlistscapa) - 1){
                (*nlistscapa)++;
                struct nlist_64_wrapper **nlists_rea =
                    realloc(*nlists, CALC_NLISTS_ARR_CAPACITY(*nlistscapa));
                *nlists = nlists_rea;
            }

            //    nlist->n_value += aslr_slide;

            (*nlists)[idx] = malloc(sizeof(struct nlist_64_wrapper));
            (*nlists)[idx]->nlist = malloc(sizeof(struct nlist_64));
            memcpy((*nlists)[idx]->nlist, &nlist, sizeof(nlist));
            (*nlists)[idx]->nlist->n_value += aslr_slide;
            //(*nlists)[idx]->str = symname;

            //printf("%s: '%s' at %#llx\n",
              //      __func__, str, (*nlists)[idx]->nlist->n_value);

            idx++;
        }

        free(ns);

        *nlistscnt = idx;
    }

    return 0;
}

static int nlistwcmp(const void *a, const void *b){
    struct nlist_64_wrapper *nwa = *(struct nlist_64_wrapper **)a;
    struct nlist_64_wrapper *nwb = *(struct nlist_64_wrapper **)b;

    //printf("%s: nwa %p nwb %p\n", __func__, nwa, nwb);

    if(!nwa || !nwb)
        return 0;

    struct nlist_64 *na = nwa->nlist;
    struct nlist_64 *nb = nwb->nlist;

    // XXX prevent overflow
    unsigned long navalue = na->n_value;
    unsigned long nbvalue = nb->n_value;

    //printf("navalue %#lx nbvalue %#lx\n", navalue, nbvalue);
    if(navalue < nbvalue)
        return -1;
    else if(navalue == nbvalue)
        return 0;
    else
        return 1;
}

static struct dbg_sym_entry *create_sym_entry_for_image(void *dscdata,
        char *imagename, unsigned long image_load_addr,
        struct my_dsc_mapping *mappings, int mappingcnt,
        struct symtab_command **symtab_cmd_out,
        struct segment_command_64 **__text_seg_cmd_out,
        int *__text_segment_nsect_out){
    struct dbg_sym_entry *entry = NULL;

    if(is_dsc_image(image_load_addr, mappings, mappingcnt)){
        //printf("%s: '%s' IS a shared cache image\n", __func__, imagename);

        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;

        int __text_segment_nsect = 0;

        get_cmds(dscdata, image_load_addr, &symtab_cmd, &__text_seg_cmd,
                &__text_segment_nsect, NULL);

        if(!symtab_cmd || !__text_seg_cmd)
            return NULL;

        unsigned long aslr_slide = image_load_addr - __text_seg_cmd->vmaddr;

        *__text_segment_nsect_out = __text_segment_nsect;

        entry = create_sym_entry_for_dsc_image(imagename);

        unsigned int lc_fxn_starts_capacity =
            CALC_ENTRIES_CAPACITY(STARTING_CAPACITY);
        int num_lc_fxn_starts_entries = 0;
        struct lc_fxn_starts_entry **lc_fxn_starts_entries =
            malloc(lc_fxn_starts_capacity);

        read_lc_fxn_starts(imagename, dscdata, image_load_addr, &lc_fxn_starts_entries,
                &num_lc_fxn_starts_entries, &lc_fxn_starts_capacity,
                aslr_slide, DSC);

        unsigned long real_entries_arr_sz =
            num_lc_fxn_starts_entries * sizeof(struct lc_fxn_starts_entry *);
        struct lc_fxn_starts_entry **lc_fxn_starts_entries_rea =
            realloc(lc_fxn_starts_entries, real_entries_arr_sz);
        lc_fxn_starts_entries = lc_fxn_starts_entries_rea;

        /*
        for(int i=0; i<num_lc_fxn_starts_entries; i++){
            struct lc_fxn_starts_entry *lce = lc_fxn_starts_entries[i];

            printf("'%s': fxn %d: [%#lx-%#lx]\n", fpath, i+1,
                    lce->vmaddr, lce->vmaddr + lce->len);

        }
        continue;
        */

        unsigned int nlists_capacity = CALC_NLISTS_ARR_CAPACITY(STARTING_CAPACITY);
        int num_nlists = 0;
        struct nlist_64_wrapper **nlists = malloc(nlists_capacity);

        read_nlists(dscdata, imagename, image_load_addr, &nlists, &num_nlists,
                &nlists_capacity, mappings, mappingcnt);

        unsigned long real_nlists_arr_sz = num_nlists * sizeof(struct nlist_64 *);
        struct nlist_64_wrapper **nlists_rea = realloc(nlists, real_nlists_arr_sz);
        nlists = nlists_rea;

        /* prep nlist array for binary searches */
        qsort(nlists, num_nlists, sizeof(struct nlist_64_wrapper *), nlistwcmp);

        /*
        for(int i=0; i<num_nlists; i++){
            struct nlist_64_wrapper *nl = nlists[i];
            printf("%s: nlist %d, n_value %#llx\n",
                    __func__, i+1, nl->nlist->n_value);
        }
        */

        // XXX add the symbols for this entry in the next loop
        for(int i=0; i<num_lc_fxn_starts_entries; i++){
            unsigned long vmaddr = lc_fxn_starts_entries[i]->vmaddr;
            int fxnlen = lc_fxn_starts_entries[i]->len;

            // XXX create an nlist "key" so this works correctly
            // XXX this shouldn't be so complicated
            struct nlist_64_wrapper nwkey = {0};
            struct nlist_64 nkey = {0};
            nkey.n_value = vmaddr;
            struct nlist_64 *nkeyp = &nkey;
            nwkey.nlist = nkeyp;
            struct nlist_64_wrapper *nwkey_p = &nwkey;

            struct nlist_64_wrapper **found_nw_dp = bsearch(&nwkey_p, nlists,
                    num_nlists, sizeof(struct nlist_64_wrapper *), nlistwcmp);

            // XXX symbol is present in the symbol table
            //if(found_nlist_dp){
            if(found_nw_dp){
                //struct nlist_64 *found_nlist = *found_nlist_dp;
                struct nlist_64_wrapper *found_nw = *found_nw_dp;

                // XXX add aslr slide to found_nw->nlist->n_value inside iosdbg
                add_symbol_to_entry(entry, 0, found_nw->nlist->n_value,
                        fxnlen, NAMED_SYM, found_nw->str);

                //printf("%s: got named symbol '%s' at %#llx\n",
                  //      __func__, found_nw->str, found_nw->nlist->n_value);
            }
            else{
                add_symbol_to_entry(entry, 0, vmaddr, fxnlen,
                        UNNAMED_SYM, NULL);

                //printf("%s: got unnamed symbol at %#lx\n", __func__, vmaddr);
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
    }
    else{
        printf("%s: '%s' IS NOT a shared cache image\n", __func__, imagename);

        struct symtab_command *symtab_cmd = NULL;
        struct segment_command_64 *__text_seg_cmd = NULL;

        int __text_segment_nsect = 0;

        get_cmds(dscdata, image_load_addr, &symtab_cmd, &__text_seg_cmd,
                &__text_segment_nsect, NULL);

        if(!symtab_cmd || !__text_seg_cmd)
            return NULL;

        unsigned long aslr_slide = image_load_addr - __text_seg_cmd->vmaddr;

        *__text_segment_nsect_out = __text_segment_nsect;

        int from_dsc = 0;
        unsigned long strtab_vmaddr = symtab_cmd->stroff + image_load_addr;
        entry = create_sym_entry(imagename, strtab_vmaddr, symtab_cmd->stroff,
                from_dsc);

        unsigned int lc_fxn_starts_capacity =
            CALC_ENTRIES_CAPACITY(STARTING_CAPACITY);
        int num_lc_fxn_starts_entries = 0;
        struct lc_fxn_starts_entry **lc_fxn_starts_entries =
            malloc(lc_fxn_starts_capacity);

        read_lc_fxn_starts(imagename, dscdata, image_load_addr, &lc_fxn_starts_entries,
                &num_lc_fxn_starts_entries, &lc_fxn_starts_capacity,
                aslr_slide, NON_DSC);

        unsigned long real_entries_arr_sz =
            num_lc_fxn_starts_entries * sizeof(struct lc_fxn_starts_entry *);
        struct lc_fxn_starts_entry **lc_fxn_starts_entries_rea =
            realloc(lc_fxn_starts_entries, real_entries_arr_sz);
        lc_fxn_starts_entries = lc_fxn_starts_entries_rea;

        /*
        for(int i=0; i<num_lc_fxn_starts_entries; i++){
            struct lc_fxn_starts_entry *lce = lc_fxn_starts_entries[i];

            printf("%s: fxn %d: [%#lx-%#lx]\n", __func__, -1,
                    lce->vmaddr, lce->vmaddr + lce->len);

        }*/
//        continue;

        unsigned int nlists_capacity = CALC_NLISTS_ARR_CAPACITY(STARTING_CAPACITY);
        int num_nlists = 0;
        struct nlist_64_wrapper **nlists = malloc(nlists_capacity);

        read_nlists(dscdata, imagename, image_load_addr, &nlists, &num_nlists,
                &nlists_capacity, mappings, mappingcnt);

        unsigned long real_nlists_arr_sz = num_nlists * sizeof(struct nlist_64 *);
        struct nlist_64_wrapper **nlists_rea = realloc(nlists, real_nlists_arr_sz);
        nlists = nlists_rea;

        /* prep nlist array for binary searches */
        qsort(nlists, num_nlists, sizeof(struct nlist_64_wrapper *), nlistwcmp);

        /*
        for(int i=0; i<num_nlists; i++){
            struct nlist_64_wrapper *nl = nlists[i];
            printf("%s: nlist %d, n_value %#llx\n",
                    __func__, i+1, nl->nlist->n_value);
        }
        */

        // XXX add the symbols for this entry in the next loop
        for(int i=0; i<num_lc_fxn_starts_entries; i++){
            unsigned long vmaddr = lc_fxn_starts_entries[i]->vmaddr;
            int fxnlen = lc_fxn_starts_entries[i]->len;

            // XXX create an nlist "key" so this works correctly
            struct nlist_64_wrapper nwkey = {0};
            struct nlist_64 nkey = {0};
            nkey.n_value = vmaddr;
            struct nlist_64 *nkeyp = &nkey;
            nwkey.nlist = nkeyp;
            struct nlist_64_wrapper *nwkey_p = &nwkey;

            struct nlist_64_wrapper **found_nw_dp = bsearch(&nwkey_p, nlists,
                    num_nlists, sizeof(struct nlist_64_wrapper *), nlistwcmp);

            // XXX symbol is present in the symbol table
            //if(found_nlist_dp){
            if(found_nw_dp){
                //struct nlist_64 *found_nlist = *found_nlist_dp;
                struct nlist_64_wrapper *found_nw = *found_nw_dp;

                // XXX add aslr slide to found_nw->nlist->n_value inside iosdbg
                // XXX these are for non DSC images, so strtabidx matters here
                add_symbol_to_entry(entry, found_nw->nlist->n_un.n_strx,
                        found_nw->nlist->n_value, fxnlen, NAMED_SYM, NULL);

                //printf("%s: got named symbol '%s' at %#llx\n",
                  //      __func__, found_nw->str, found_nw->nlist->n_value);
                //printf("%s: got named symbol at %#llx\n",
                  //      __func__, found_nw->nlist->n_value);
            }
            else{
                add_symbol_to_entry(entry, 0, vmaddr, fxnlen,
                        UNNAMED_SYM, NULL);
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
        int maxlen = PATH_MAX;
        char fpath[maxlen];
        memset(fpath, 0, maxlen);

        unsigned int bytes_read = 0;
        char read_byte = '\1';

        /* Read a byte at a time, arbitary large sizes for fpath
         * causes errors.
         */
        while(read_byte && bytes_read < maxlen && kret == KERN_SUCCESS){
         //   if(i==0)
           //     PM = 1;

            unsigned long imgpath =
                (unsigned long)debuggee->dyld_info_array[i].imageFilePath;
            kret = read_memory_at_location(imgpath + bytes_read,
                    &read_byte, sizeof(char));

            fpath[bytes_read] = read_byte;

            bytes_read++;
        }
        
        /*
        if(i==0){
            printf("%s: imgpath %#lx infoarray.imageFilePath %p\n",
                    __func__, imgpath, debuggee->dyld_info_array[i].imageFilePath);
            printf("%s: kret %s\n", __func__, mach_error_string(kret));
            char *buf = NULL;
            //PM = 1;
            dump_memory(imgpath, 0x50, &buf);
            printf("%s: imgname:\n%s\n", __func__, buf);
            free(buf);
        }
        */

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

    free(dsc_mappings);

    return 0;
}
