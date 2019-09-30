#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbgsymbol.h"
#include "sym.h"

#include "../debuggee.h"
#include "../memutils.h"
#include "../strext.h"

static int UNNAMED_SYM_CNT = 1;

/* arg1 represents strtabidx or unnamed_sym_num depending on kind */
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

    if(kind == UNNAMED_SYM){
        entry->syms[entry->cursymarrsz]->dsc_symname = UNNAMED_SYMBOL;
        entry->syms[entry->cursymarrsz]->unnamed_sym_num = UNNAMED_SYM_CNT++;
    }
    else if(kind == NAMED_SYM){
        entry->syms[entry->cursymarrsz]->strtabidx = arg1;
    }

    entry->syms[entry->cursymarrsz]->sym_func_start = vmaddr_start;
    entry->syms[entry->cursymarrsz]->sym_func_len = fxnlen;

    entry->cursymarrsz++;
}

void create_frame_string(unsigned long vmaddr, char **frstr){
    /* first, get symbol name */
    char *imgname = NULL, *symname = NULL;
    unsigned int symdist = 0;

    /* If we can't get the symbol name, there's no need to continue
     * and try to get line of the source file we're at.
     */
    if(get_symbol_info_from_address(debuggee->symbols,
            vmaddr, &imgname, &symname, &symdist)){
        return;
    }

    concat(frstr, "%s`%s", imgname, symname);

    free(imgname);
    free(symname);

    /* second, see if we can figure out the line number we're at */
    /* if we can't, then we substitute that for how far we are into the fxn */
    if(!debuggee->has_dwarf_debug_info()){
        if(symdist > 0)
            concat(frstr, " + %#lx", symdist);

        return;
    }

    char *pc_srcfile = NULL, *pc_srcfunc = NULL;
    uint64_t pc_srcfileline = 0;
    void *root_die = NULL;

    if(sym_get_line_info_from_pc(debuggee->dwarfinfo,
                vmaddr - debuggee->aslr_slide, &pc_srcfile, &pc_srcfunc,
                &pc_srcfileline, &root_die, NULL)){
        if(symdist > 0)
            concat(frstr, " + %#lx", symdist);

        return;
    }

    if(!pc_srcfile)
        return;

    /* we only care about what's after the last slash */
    char *pcsf = pc_srcfile;
    char *lastslash = strrchr(pc_srcfile, '/');

    if(lastslash)
        pcsf = lastslash + 1;

    concat(frstr, " at %s:%lld", pcsf, pc_srcfileline);

    free(pc_srcfile);
    free(pc_srcfunc);
}

struct dbg_sym_entry *create_sym_entry(unsigned long strtab_vmaddr,
        unsigned long strtab_fileaddr, int from_dsc){
    struct dbg_sym_entry *entry = malloc(sizeof(struct dbg_sym_entry));

    entry->symarr_capacity_pow = STARTING_CAPACITY;
    entry->cursymarrsz = 0;
    entry->strtab_vmaddr = strtab_vmaddr;
    entry->syms = malloc(CALC_SYM_CAPACITY(STARTING_CAPACITY));
    entry->from_dsc = from_dsc;

    return entry;
}

void destroy_all_symbol_entries(void){
    struct node *current = debuggee->symbols->front;

    while(current){
        struct dbg_sym_entry *entry = current->data;

        free(entry->imagename);
        entry->imagename = NULL;

        for(int i=0; i<entry->cursymarrsz; i++){
            free(entry->syms[i]);
            entry->syms[i] = NULL;
        }

        free(entry->syms);
        entry->syms = NULL;

        current = current->next;

        free(entry);
        entry = NULL;
    }
}

enum { SYM = 0, GC };

struct goodcombo {
    struct dbg_sym_entry *entry;
    struct sym *sym;
};

/* find the symbol with the closest function that starts before vmaddr */
int bsearch_lc(void *arg0, unsigned long vmaddr, int lo, int hi, int which){
    if(lo == hi){
        unsigned long val = ((struct sym **)arg0)[lo]->sym_func_start;

        if(which == GC)
            val = ((struct goodcombo **)arg0)[lo]->sym->sym_func_start;

        return val > vmaddr ? -1 : lo;
    }

    if((hi - 1) == lo){
        unsigned long hival = ((struct sym **)arg0)[hi]->sym_func_start;
        unsigned long loval = ((struct sym **)arg0)[lo]->sym_func_start;

        if(which == GC){
            hival = ((struct goodcombo **)arg0)[hi]->sym->sym_func_start;
            loval = ((struct goodcombo **)arg0)[lo]->sym->sym_func_start;
        }

        if(vmaddr >= hival)
            return hi;
        else if(vmaddr >= loval)
            return lo;

        return -1;
    }

    int mid = (lo + hi) / 2;
    unsigned long midval = ((struct sym **)arg0)[mid]->sym_func_start;

    if(which == GC)
        midval = ((struct goodcombo **)arg0)[mid]->sym->sym_func_start;

    if(vmaddr < midval)
        return bsearch_lc(arg0, vmaddr, lo, mid - 1, which);

    return bsearch_lc(arg0, vmaddr, mid, hi, which);

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
    int num_good_combos = 0;
    struct goodcombo **good_combos = malloc(sizeof(struct goodcombo *));
    good_combos[num_good_combos] = NULL;

    for(struct node *current = symlist->front;
            current;
            current = current->next){
        struct dbg_sym_entry *entry = current->data;

        if(entry->cursymarrsz == 0 || vmaddr < entry->load_addr)
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

        /* Figure out the closest symbol to vmaddr for each entry */
        int lo = 0, hi = entry->cursymarrsz - 1;
        int bestsymidx = bsearch_lc(entry->syms, vmaddr, lo, hi, SYM);

        if(bestsymidx != -1){
            struct goodcombo **good_combos_rea = realloc(good_combos,
                    ++num_good_combos * sizeof(struct goodcombo *));
            good_combos = good_combos_rea;
            good_combos[num_good_combos - 1] = malloc(sizeof(struct goodcombo));
            good_combos[num_good_combos - 1]->entry = entry;
            good_combos[num_good_combos - 1]->sym = entry->syms[bestsymidx];
        }
    }

    /* could happen if a thread is stopped at a bad address */
    if(num_good_combos == 0)
        return 1;

    /* Prep good combo array for binary search */
    qsort(good_combos, num_good_combos, sizeof(struct goodcombo *), goodcombocmp);

    /* out of all the candidates we have, which is the closest to vmaddr? */
    int bestcomboidx = bsearch_lc(good_combos, vmaddr, 0, num_good_combos - 1, GC);

    struct goodcombo *bestcombo = good_combos[bestcomboidx];

    struct dbg_sym_entry *best_entry = bestcombo->entry;
    struct sym *best_sym = bestcombo->sym;

    char *symname = NULL;

    if(best_entry->from_dsc){
        if(!IS_UNNAMED_SYMBOL(best_sym))
            concat(&symname, "%s", best_sym->dsc_symname);
        else{
            concat(&symname, "iosdbg_unnamed_symbol%d",
                    best_sym->unnamed_sym_num);
        }
    }
    else{
        if(!IS_UNNAMED_SYMBOL(best_sym)){
            int maxlen = 512;
            symname = malloc(maxlen);

            unsigned long stroff = best_entry->strtab_vmaddr + best_sym->strtabidx;
            read_memory_at_location(stroff, symname, maxlen);
        }
        else{
            concat(&symname, "iosdbg_unnamed_symbol%d",
                    best_sym->unnamed_sym_num);
        }
    }

    for(int i=0; i<num_good_combos; i++)
        free(good_combos[i]);

    free(good_combos);

    if(imgnameout)
        *imgnameout = strdup(best_entry->imagename);
    
    if(symnameout)
        *symnameout = symname;
    else
        free(symname);

    if(distfromsymstartout)
        *distfromsymstartout = vmaddr - best_sym->sym_func_start;

    return 0;
}

void reset_unnamed_sym_cnt(void){
    UNNAMED_SYM_CNT = 1;
}
