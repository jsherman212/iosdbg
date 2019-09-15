#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbgsymbol.h"

#include "../memutils.h"
#include "../strext.h"

// XXX inside iosdbg, reset this on detach
static int UNNAMED_SYM_CNT = 0;

// XXX get rid of the idx param
// XXX arg1 - strtabidx or unnamed_sym_num depending on kind
void add_symbol_to_entry(struct dbg_sym_entry *entry, int arg1,
        unsigned long vmaddr_start, unsigned int fxnlen, int kind,
        char *symname){
    if(entry->cursymarrsz >= FAST_POW_TWO(entry->symarr_capacity_pow) - 1){
        entry->symarr_capacity_pow++;
        struct sym **sym_rea = realloc(entry->syms,
                CALC_SYM_CAPACITY(entry->symarr_capacity_pow));
        entry->syms = sym_rea;
    }

    entry->syms[entry->cursymarrsz] = malloc(sizeof(struct sym));

    if(symname)
        entry->syms[entry->cursymarrsz]->dsc_symname = symname;
    //else
      //  entry->syms[entry->cursymarrsz]->dsc_symname = UNNAMED_SYMBOL;

    if(kind == UNNAMED_SYM){
        entry->syms[entry->cursymarrsz]->dsc_symname = UNNAMED_SYMBOL;
        entry->syms[entry->cursymarrsz]->unnamed_sym_num = UNNAMED_SYM_CNT++;
    }
    else if(kind == NAMED_SYM)
        entry->syms[entry->cursymarrsz]->strtabidx = arg1;

    //entry->syms[entry->cursymarrsz]->strtabidx = strtabidx;
    entry->syms[entry->cursymarrsz]->sym_func_start = vmaddr_start;
    entry->syms[entry->cursymarrsz]->sym_func_len = fxnlen;

    entry->cursymarrsz++;
}

struct dbg_sym_entry *create_sym_entry(char *imagename,
        unsigned long strtab_vmaddr, unsigned long strtab_fileaddr,
        int from_dsc){
    struct dbg_sym_entry *entry = malloc(sizeof(struct dbg_sym_entry));

    char *name = NULL;

    if(imagename)
        name = strdup(imagename);
    else
        name = strdup("unknown");

    entry->imagename = name;
    entry->symarr_capacity_pow = STARTING_CAPACITY;
    entry->cursymarrsz = 0;
    entry->strtab_vmaddr = strtab_vmaddr;
    //entry->strtab_fileaddr = strtab_fileaddr;
    entry->syms = malloc(CALC_SYM_CAPACITY(STARTING_CAPACITY));
    entry->from_dsc = from_dsc;

    return entry;
}

// XXX find the symbol with the closest function that starts before vmaddr
// XXX return idx of that smallest closest elem or -1 if not found
int bsearch_sym_lc(struct sym **syms, unsigned long vmaddr, int lo, int hi){
    if(lo == hi){
        //printf("%s: lo sym func start %#lx vmaddr %#lx\n",
          //      __func__, syms[lo]->sym_func_start, vmaddr);
        if(syms[lo]->sym_func_start > vmaddr)
            return -1;

        return lo;
    }

    if((hi - 1) == lo){
        //printf("arr[lo] %d vmaddr %d\n", arr[lo], vmaddr);
        if(vmaddr >= syms[hi]->sym_func_start)
            return hi;
        else if(vmaddr >= syms[lo]->sym_func_start)
            return lo;

        return -1;
    }

    int mid = (lo + hi) / 2;
    //printf("lo %d hi %d mid %d\n", lo, hi, mid);
    //int midval = arr[mid];
    struct sym *midsym = syms[mid];

    if(vmaddr < midsym->sym_func_start)
        return bsearch_sym_lc(syms, vmaddr, lo, mid - 1);

    return bsearch_sym_lc(syms, vmaddr, mid, hi);
}

struct goodcombo {
    struct dbg_sym_entry *entry;
    struct sym *sym;
};

// XXX find the symbol with the closest function that starts before vmaddr
// XXX return idx of that smallest closest elem or -1 if not found
int bsearch_gc_lc(struct goodcombo **gcs, unsigned long vmaddr, int lo, int hi){
    if(lo == hi){
        //printf("%s: lo sym func start %#lx vmaddr %#lx\n",
          //      __func__, syms[lo]->sym_func_start, vmaddr);
        if(gcs[lo]->sym->sym_func_start > vmaddr)
            return -1;

        return lo;
    }

    if((hi - 1) == lo){
        //printf("arr[lo] %d vmaddr %d\n", arr[lo], vmaddr);
        if(vmaddr >= gcs[hi]->sym->sym_func_start)
            return hi;
        else if(vmaddr >= gcs[lo]->sym->sym_func_start)
            return lo;

        return -1;
    }

    int mid = (lo + hi) / 2;
    //printf("lo %d hi %d mid %d\n", lo, hi, mid);
    //int midval = arr[mid];
    //struct sym *midsym = syms[mid];
    struct goodcombo *midgc = gcs[mid];

    if(vmaddr < midgc->sym->sym_func_start)
        return bsearch_gc_lc(gcs, vmaddr, lo, mid - 1);

    return bsearch_gc_lc(gcs, vmaddr, mid, hi);
}

static int goodcombocmp(const void *a, const void *b){
    struct goodcombo *gca = *(struct goodcombo **)a;
    struct goodcombo *gcb = *(struct goodcombo **)b;

    if(!gca || !gcb)
        return 0;

    /* we could overflow if we just return gcasf - gcbsf */
    long gcasf = (long)gca->sym->sym_func_start;
    long gcbsf = (long)gcb->sym->sym_func_start;

    if(gcasf < gcbsf)
        return -1;
    else if(gcasf == gcbsf)
        return 0;
    else
        return 1;
}

int get_symbol_info_from_address(struct linkedlist *symlist,
        unsigned long vmaddr, char **imgnameout, char **symnameout,
        unsigned int *distfromsymstartout){
    struct dbg_sym_entry *best_entry = NULL;
    struct sym *best_sym = NULL;

    int best_symbol_idx = 0;

    unsigned long diff = 0;
    int cnt = 1;

    int num_good_combos = 0;
    struct goodcombo **good_combos = malloc(sizeof(struct goodcombo *));
    good_combos[num_good_combos] = NULL;

    for(struct node_t *current = symlist->front;
            current;
            current = current->next){
        struct dbg_sym_entry *entry = current->data;
      //  printf("%s: searching entry %d: '%s' for vmaddr %#lx\n",
        //        __func__, cnt++, entry->imagename, vmaddr);

        if(entry->cursymarrsz == 0)
            continue;

        if(entry->cursymarrsz == 1){
            struct goodcombo **good_combos_rea = realloc(good_combos,
                    ++num_good_combos * sizeof(struct goodcombo *));
            good_combos = good_combos_rea;
            good_combos[num_good_combos - 1] = malloc(sizeof(struct goodcombo));
            good_combos[num_good_combos - 1]->entry = entry;
            good_combos[num_good_combos - 1]->sym = entry->syms[0];

            continue;
        }

        /*
        if(strcmp(entry->imagename, "/private/var/mobile/testprogs/./params") == 0){
            for(int i=0; i<entry->cursymarrsz; i++){
                sym_desc(entry, entry->syms[i]);
            }
        }
        */

        /* Figure out the closest symbol to vmaddr for each entry */
        int lo = 0, hi = entry->cursymarrsz - 1;
        int bestsymidx = bsearch_sym_lc(entry->syms, vmaddr, lo, hi);
        //best_symbol_idx = bsearch_lc(entry->syms, vmaddr, lo, hi);

        //if((best_sym = bsearchrange(entry, NULL, 0, vmaddr, NULL))){
        if(bestsymidx != -1){
            struct goodcombo **good_combos_rea = realloc(good_combos,
                    ++num_good_combos * sizeof(struct goodcombo *));
            good_combos = good_combos_rea;
            good_combos[num_good_combos - 1] = malloc(sizeof(struct goodcombo));
            good_combos[num_good_combos - 1]->entry = entry;
            good_combos[num_good_combos - 1]->sym = entry->syms[bestsymidx];
        }
    }

    /* Prep good combo array for binary search */
    qsort(good_combos, num_good_combos, sizeof(struct goodcombo *), goodcombocmp);

    for(int i=0; i<num_good_combos; i++){
        struct goodcombo *gc = good_combos[i];
        printf("%s: combo %d: ", __func__, i);
        sym_desc(gc->entry, gc->sym);
    }
    printf("%s: done printing good combos\n\n", __func__);
    /* Out of all the candidates we have, which is the closest to vmaddr? */
    int bestcomboidx = bsearch_gc_lc(good_combos, vmaddr, 0, num_good_combos - 1);

    /* Nothing found. This is fine, could be an unnamed function. */
    //if(bestcomboidx == -1)
      //  return 1;

    struct goodcombo *bestcombo = good_combos[bestcomboidx];

    printf("%s: final best combo:\n", __func__);
    sym_desc(bestcombo->entry, bestcombo->sym);
    printf("%s: done printing best combo\n\n", __func__);
    
    best_entry = bestcombo->entry;
    best_sym = bestcombo->sym;

    char *symname = NULL;

    if(best_entry->from_dsc){
        if(!IS_UNNAMED_SYMBOL(best_sym))
            concat(&symname, "%s", best_sym->dsc_symname);
        else{
            concat(&symname, "iosdbg$$unnamed_symbol%d",
                    best_sym->unnamed_sym_num);
        }
    }
    else{
        if(!IS_UNNAMED_SYMBOL(best_sym)){
            //printf("%s: best_sym is named\n", __func__);
            int maxlen = 512;
            symname = malloc(maxlen);

            unsigned long stroff = best_entry->strtab_vmaddr + best_sym->strtabidx;
            kern_return_t kret = read_memory_at_location(stroff, symname, maxlen);

            //printf("%s: kret %s\n", __func__, mach_error_string(kret));

            //concat(&symname, "%s", best_sym->dsc_symname);
        }
        else{
            concat(&symname, "iosdbg$$unnamed_symbol%d",
                    best_sym->unnamed_sym_num);
        }
    }

    for(int i=0; i<num_good_combos; i++)
        free(good_combos[i]);

    free(good_combos);

    *imgnameout = strdup(best_entry->imagename);
    *symnameout = symname;
    *distfromsymstartout = vmaddr - best_sym->sym_func_start;

    return 0;
}

void sym_desc(struct dbg_sym_entry *entry, struct sym *sym){
    unsigned long start = sym->sym_func_start;
    unsigned long end = start + sym->sym_func_len;

    char *symname = NULL;

    if(entry->from_dsc){
        if(!IS_UNNAMED_SYMBOL(sym))
            concat(&symname, "%s", sym->dsc_symname);
        else{
            concat(&symname, "iosdbg$$unnamed_symbol%d", sym->unnamed_sym_num);
        }

        printf("DSC image '%s': [%#lx-%#lx]: '%s'\n",
                entry->imagename, start, end, symname);

        free(symname);

        return;
    }

    if(!IS_UNNAMED_SYMBOL(sym)){
        //printf("%s: best_sym is named\n", __func__);
        int maxlen = 512;
        symname = malloc(maxlen);

        unsigned long stroff = entry->strtab_vmaddr + sym->strtabidx;
        kern_return_t kret = read_memory_at_location(stroff, symname, maxlen);

        //printf("%s: kret %s\n", __func__, mach_error_string(kret));

        //concat(&symname, "%s", best_sym->dsc_symname);
    }
    else{
        concat(&symname, "iosdbg$$unnamed_symbol%d", sym->unnamed_sym_num);
    }

    printf("non-DSC image '%s': [%#lx-%#lx]:", entry->imagename, start, end);

    //if(kret == KERN_SUCCESS)
        printf(" '%s'\n", symname);
    //else
      //  printf(" could not get symname from vmaddr %#lx: %s\n",
        //        entry->strtab_vmaddr + sym->strtabidx, mach_error_string(kret));

}
