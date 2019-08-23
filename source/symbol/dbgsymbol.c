#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbgsymbol.h"

#include "../memutils.h"

void add_symbol_to_entry(struct dbg_sym_entry *entry, int idx, int strtabidx,
        unsigned long vmaddr_start, unsigned long vmaddr_end){
    //return;
    if(!entry)
        return;

    //printf("%s: adding symbol with strtab idx %#x with addr %#lx at idx %d\n",
      //      __func__, strtabidx, vmaddr_start, idx);

    if(!entry->syms)
        entry->syms = malloc(sizeof(struct sym *) * ++entry->cursymarrsz);
    else{
        struct sym **sym_rea = realloc(entry->syms,
                sizeof(struct sym *) * ++entry->cursymarrsz);
        //printf("%s: sym_rea %p current array sz %d\n", __func__, sym_rea,
          //      entry->cursymarrsz);
        entry->syms = sym_rea;
    }
//    printf("%s: entry->syms %p sym arr size %d\n", __func__,
  //          entry->syms, entry->cursymarrsz);

    //entry->syms[idx] = malloc(sizeof(struct sym));
    entry->syms[entry->cursymarrsz - 1] = malloc(sizeof(struct sym));
    
    
    //printf("%s: sizeof(**entry->syms) = %zu\n", __func__, sizeof(**entry->syms));
    //printf("%s: entry->syms[%d] = %p\n", __func__, idx, entry->syms[idx]);
    //entry->syms[idx]->symname = strdup(symname);
    entry->syms[entry->cursymarrsz - 1]->strtabidx = strtabidx;
    entry->syms[entry->cursymarrsz - 1]->symaddr_start = vmaddr_start;
    entry->syms[entry->cursymarrsz - 1]->symaddr_end = vmaddr_end;
    //entry->syms[entry->cursymarrsz] = NULL;
}

struct dbg_sym_entry *create_sym_entry(char *imagename, unsigned int nsyms,
        unsigned long strtab_addr){
    //return NULL;
    printf("%s: nsyms %u\n", __func__, nsyms);
    struct dbg_sym_entry *entry = malloc(sizeof(struct dbg_sym_entry));

    entry->imagename = strdup(imagename);
    entry->numsyms = nsyms;
    entry->cursymarrsz = 0;
    entry->strtab_addr = strtab_addr;
    entry->syms = NULL;
    //entry->syms = malloc(sizeof(*entry->syms) * ++entry->cursymarrsz);
    //entry->syms[entry->cursymarrsz - 1] = NULL;
    
    //entry->syms = calloc(entry->numsyms, sizeof(*entry->syms));
    //entry->syms = malloc(entry->numsyms * sizeof(*entry->syms));
    //printf("%s: entry->syms %p sizeof(*entry->syms) = %zu\n", __func__, entry->syms,
      //      sizeof(*entry->syms));

   /* for(int i=0; i<entry->numsyms; i++){
        printf("%s: name '%s' allocating memory for spot %d\n", __func__,
                imagename, i);
        entry->syms[i] = malloc(sizeof(**entry->syms));
    }*/


    return entry;
}

int get_symbol_info_from_address(struct linkedlist *symlist,
        unsigned long vmaddr, char **imgnameout, char **symnameout,
        unsigned int *distfromsymstartout){
    struct dbg_sym_entry *best_entry = NULL;
    int best_symbol_idx = 0;

    unsigned long diff = 0;

    for(struct node_t *current = symlist->front;
            current;
            current = current->next){
        struct dbg_sym_entry *entry = current->data;

        for(int i=0; i<entry->cursymarrsz/*numsyms*/; i++){
            //printf("%s: entry->syms[i] %p\n", __func__, entry->syms[i]);
            if(strcmp(entry->imagename, "/private/var/mobile/testprogs/./params") == 0){
                int len = 64;
                char symname[len];
                memset(symname, 0, len);

                kern_return_t kret =
                    read_memory_at_location(entry->strtab_addr + entry->syms[i]->strtabidx,
                            symname, len);
/*
                printf("%s: sym name '%s' entry->syms[i]->symaddr_start %#lx "
                        " entry->syms[i]->symaddr_end %#lx\n",
                        __func__, symname, entry->syms[i]->symaddr_start,
                        entry->syms[i]->symaddr_end);
                        */
            }
            
            if(!(vmaddr >= entry->syms[i]->symaddr_start &&
                        vmaddr < entry->syms[i]->symaddr_end)){
                continue;
            }

            /*
            printf("%s: vmaddr %#lx vmaddr - entry->syms[i]->symaddr_start %#lx"
                    " diff %#lx\n",
                    __func__, vmaddr, vmaddr - entry->syms[i]->symaddr_start, diff);
    */
            if(diff == 0 || vmaddr - entry->syms[i]->symaddr_start < diff){
                best_entry = entry;
                best_symbol_idx = i;
                
                diff = vmaddr - entry->syms[i]->symaddr_start;

                //printf("****%s: updated best entry %p and idx %d\n",
                  //      __func__, best_entry, best_symbol_idx);
            }


            /*
            printf("%s: potential: symaddr_start %#lx symaddr_end %#lx "
                   " strtabidx %#x\n",
                   __func__, entry->syms[i]->symaddr_start,
                   entry->syms[i]->symaddr_end, entry->syms[i]->strtabidx);
                   */
            /*
            int len = 64;
            char symname[len];
            memset(symname, 0, len);

            kern_return_t kret =
                read_memory_at_location(entry->strtab_addr + entry->syms[i]->strtabidx,
                    symname, len);

            //printf("%s: read_memory_at_location says %s\n", __func__,
              //      mach_error_string(kret));

            *imgnameout = strdup(entry->imagename);
            *symnameout = strdup(symname);
            *distfromsymstartout = vmaddr - entry->syms[i]->symaddr_start;
            */
            //if(entry->syms[i]->symaddr_start == 0x1c3944b90)
              //  return 0;
            //return 0;
        }
    }

    if(!best_entry)
        return 1;

    int len = 64;
    char symname[len];
    memset(symname, 0, len);

    kern_return_t kret =
        read_memory_at_location(best_entry->strtab_addr +
                best_entry->syms[best_symbol_idx]->strtabidx, symname, len);

    *imgnameout = strdup(best_entry->imagename);
    *symnameout = strdup(symname);
    *distfromsymstartout = vmaddr - best_entry->syms[best_symbol_idx]->symaddr_start;

    return 0;
}
