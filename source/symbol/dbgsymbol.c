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
    struct sym *sym = malloc(sizeof(struct sym));

    if(symname)
        sym->dsc_symname = symname;

    if(kind == NAMED_SYM)
        sym->strtabidx = arg1;
    else if(kind == UNNAMED_SYM){
        sym->dsc_symname = UNNAMED_SYMBOL;
        sym->unnamed_sym_num = UNNAMED_SYM_CNT++;
    }

    sym->sym_func_start = vmaddr_start;
    sym->sym_func_len = fxnlen;

    array_insert(entry->syms, sym);
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

    entry->strtab_vmaddr = strtab_vmaddr;
    entry->syms = array_new();
    entry->from_dsc = from_dsc;

    return entry;
}

void destroy_all_symbol_entries(void){
    if(!debuggee->symbols)
        return;

    struct node *current = debuggee->symbols->front;

    while(current){
        struct dbg_sym_entry *entry = current->data;

        free(entry->imagename);
        entry->imagename = NULL;

        for(int i=0; i<entry->syms->len; i++){
            free(entry->syms->items[i]);
            entry->syms->items[i] = NULL;
        }

        array_destroy(&entry->syms);

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
int bsearch_lc(struct array *arg0, unsigned long vmaddr, int lo, int hi, int which){
    if(lo == hi){
        unsigned long val = ((struct sym *)(arg0->items[lo]))->sym_func_start;

        if(which == GC)
            val = ((struct goodcombo *)(arg0->items[lo]))->sym->sym_func_start;

        return val > vmaddr ? -1 : lo;
    }

    if((hi - 1) == lo){
        unsigned long hival = ((struct sym *)(arg0->items[hi]))->sym_func_start;
        unsigned long loval = ((struct sym *)(arg0->items[lo]))->sym_func_start;

        if(which == GC){
            hival = ((struct goodcombo *)(arg0->items[hi]))->sym->sym_func_start;
            loval = ((struct goodcombo *)(arg0->items[lo]))->sym->sym_func_start;
        }

        if(vmaddr >= hival)
            return hi;
        else if(vmaddr >= loval)
            return lo;

        return -1;
    }

    int mid = (lo + hi) / 2;
    unsigned long midval = ((struct sym *)(arg0->items[mid]))->sym_func_start;

    if(which == GC)
        midval = ((struct goodcombo *)(arg0->items[mid]))->sym->sym_func_start;

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
    struct array *good_combos = array_new();

    for(struct node *current = symlist->front;
            current;
            current = current->next){
        struct dbg_sym_entry *entry = current->data;

        if(entry->syms->len == 0 || vmaddr < entry->load_addr)
            continue;

        if(entry->syms->len == 1){
            struct goodcombo *gc = malloc(sizeof(struct goodcombo));
            gc->entry = entry;
            gc->sym = (struct sym *)entry->syms->items[0];

            array_insert(good_combos, gc);

            continue;
        }

        /* figure out the closest symbol to vmaddr for each entry */
        int lo = 0, hi = entry->syms->len - 1;
        int bestsymidx = bsearch_lc(entry->syms, vmaddr, lo, hi, SYM);

        if(bestsymidx != -1){
            struct goodcombo *gc = malloc(sizeof(struct goodcombo));
            gc->entry = entry;
            gc->sym = (struct sym *)entry->syms->items[bestsymidx];

            array_insert(good_combos, gc);
        }
    }

    /* could happen if a thread is stopped at a bad address */
    if(good_combos->len == 0)
        return 1;

    /* Prep good combo array for binary search */
    array_qsort(good_combos, goodcombocmp);

    /* out of all the candidates we have, which is the closest to vmaddr? */
    int bestcomboidx = bsearch_lc(good_combos, vmaddr, 0, good_combos->len - 1, GC);

    struct goodcombo *bestcombo =
        (struct goodcombo *)good_combos->items[bestcomboidx];

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

    int num_good_combos = good_combos->len;

    for(int i=0; i<num_good_combos; i++)
        free(good_combos->items[i]);

    array_destroy(&good_combos);

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
