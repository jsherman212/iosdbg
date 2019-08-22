#include <limits.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/stab.h>
#include <stdio.h>
#include <stdlib.h>

#include "../debuggee.h"
#include "../memutils.h"

int initialize_debuggee_dyld_all_image_infos(void){
    struct task_dyld_info dyld_info = {0};
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

    kern_return_t kret = task_info(debuggee->task, TASK_DYLD_INFO,
                (task_info_t)&dyld_info, &count);
    printf("%s: task_info says %s\n", __func__, mach_error_string(kret));

    if(kret){
        return 1;
    }

    printf("%s: all image info addr @ %#llx\n", __func__, dyld_info.all_image_info_addr);

    kret = read_memory_at_location(dyld_info.all_image_info_addr,
                &debuggee->dyld_all_image_infos,
                sizeof(struct dyld_all_image_infos));

    printf("%s: 1st read_memory_at_location says %s\n", __func__, mach_error_string(kret));


    if(kret){
        return 1;
    }

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

    printf("%s: 2nd read_memory_at_location says %s\n", __func__, mach_error_string(kret));

    if(kret){
        return 1;
    }


    debuggee->dyld_image_info_filePaths = calloc(sizeof(char *), 
            debuggee->dyld_all_image_infos.infoArrayCount);

    for(int i=0; i<debuggee->dyld_all_image_infos.infoArrayCount; i++){
        int count = PATH_MAX;
        char fpath[count];
        kret = read_memory_at_location(
                (unsigned long)debuggee->dyld_info_array[i].imageFilePath,
                fpath, count);

        //printf("%s: read_memory_at_location inside loop says %s\n",
          //      __func__, mach_error_string(kret));

        if(kret == KERN_SUCCESS){
            //printf("%s: image %d: fpath '%s'\n", __func__, i, fpath);

            size_t len = strlen(fpath) + 1;

            debuggee->dyld_image_info_filePaths[i] = malloc(len);
            strncpy(debuggee->dyld_image_info_filePaths[i], fpath, len);

            //printf("%s: image %d: path from array '%s'\n", __func__, i,
              //      debuggee->dyld_image_info_filePaths[i]);
        }

        unsigned long load_addr =
            (unsigned long)debuggee->dyld_info_array[i].imageLoadAddress;

        printf("%s: load addr for image %d: %#lx\n", __func__, i, load_addr);

        struct mach_header_64 image_hdr = {0};
        kret = read_memory_at_location(load_addr, &image_hdr, sizeof(image_hdr));

        //printf("%s: image %d: magic %#x filetype %#x ncmds %#x sizeofcmds %#x\n",
          //      __func__, i, image_hdr.magic, image_hdr.filetype, image_hdr.ncmds,
            //    image_hdr.sizeofcmds);
        struct load_command *cmd = malloc(image_hdr.sizeofcmds);
        struct load_command *cmdorig = cmd;
        struct segment_command_64 *linkedit_segcmd = NULL, *text_segcmd = NULL;

        //printf("%s: load commands should start at %#lx\n",
          //      __func__, load_addr + sizeof(image_hdr));

        kret = read_memory_at_location(load_addr + sizeof(image_hdr),
                cmd, image_hdr.sizeofcmds);

        //char *out = NULL;
        //dump_memory(load_addr, sizeof(image_hdr) + 0x40, &out);

        //printf("%s: mem dump around mach header:\n%s\n", __func__, out);


        //printf("%s: 3rd read_memory_at_location says %s, cmd->cmd %#x"
          //      " cmd->cmdsize %#x\n",
            //    __func__, mach_error_string(kret), cmd->cmd, cmd->cmdsize);


        struct symtab_command *symtab_cmd = NULL;
        struct dysymtab_command *dysymtab_cmd = NULL;

        // XXX left off on finding LC_SYMTAB
        for(int j=0; j<image_hdr.ncmds; j++){
            //printf("%s: cmd %p\n", __func__, cmd);
            if(cmd->cmd == LC_SYMTAB){
                printf("******%s: got LC_SYMTAB cmd for image %d\n",
                        __func__, i);

                symtab_cmd = malloc(cmd->cmdsize);
                memcpy(symtab_cmd, cmd, cmd->cmdsize);

                //break;
            }
            else if(cmd->cmd == LC_DYSYMTAB){
                dysymtab_cmd = malloc(cmd->cmdsize);
                memcpy(dysymtab_cmd, cmd, cmd->cmdsize);
            }
            else if(cmd->cmd == LC_SEGMENT_64){
                struct segment_command_64 *s = (struct segment_command_64 *)cmd;

                if(strcmp(s->segname, "__LINKEDIT") == 0){
                    printf("%s: got linkedit\n", __func__);
                    linkedit_segcmd = malloc(cmd->cmdsize);
                    memcpy(linkedit_segcmd, cmd, cmd->cmdsize);
                }
                else if(strcmp(s->segname, "__TEXT") == 0){
                    printf("%s: got text\n", __func__);
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


        // XXX XXX XXX FORMULA FOR SHARED CACHE STUFF, WILL HAVE TO FIND A WAY TO
        // DETECT IF AN IMAGE IS FROM THE SHARED CACHE - check if symtab_cmd->symoff and
        // symtab_cmd->stroff both fall in range of one of the dsc mappings
        // av = whatever mapping symtab_cmd->symoff and symtab_cmd->stroff fall into,
        //        VIRTUAL ADDRESS, LOWER BOUND
        // af = whatever mapping symtab_cmd->symoff and symtab_cmd->stroff fall into,
        //        FILE OFFSET, LOWER BOUND
        // b = symtab_cmd->symoff or symtab_cmd->stroff
        // c = shared cache slide
        // addr = av - (b - af) + c
        
        unsigned long symtab_addr = 0x1bc194000 +
            (symtab_cmd->symoff - 0x38194000) + scache_slide;
        unsigned long strtab_addr = 0x1bc194000 +
            (symtab_cmd->stroff - 0x38194000) + scache_slide;
        //symtab_addr = load_addr + symtab_cmd->symoff;
        //strtab_addr = load_addr + symtab_cmd->stroff;
        
        printf("%s: __LINKEDIT vmaddr %#llx symtab_addr %#lx strtab_addr %#lx\n",
                __func__, linkedit_segcmd->vmaddr, symtab_addr, strtab_addr);

        unsigned long symsize = symtab_cmd->nsyms * sizeof(struct nlist_64);

        
        size_t nscount = sizeof(struct nlist_64) * symtab_cmd->nsyms;
        struct nlist_64 *ns = malloc(nscount);

        unsigned long nlistaddr = symtab_addr;
        kret = read_memory_at_location(nlistaddr, ns, nscount);
        printf("%s: image %d: fifth kret says %s\n", __func__,
                i, mach_error_string(kret));

        if(kret == KERN_SUCCESS && debuggee->dyld_image_info_filePaths[i] && strcmp(debuggee->dyld_image_info_filePaths[i], "/usr/lib/system/libsystem_c.dylib") == 0){
          
        //if(kret == KERN_SUCCESS){
            for(int j=0; j<symtab_cmd->nsyms; j++){
                char str[PATH_MAX] = {0};

                //printf("%s: nlist %d: strx '%s' n_type %#x n_sect %#x n_desc %#x n_value %#llx\n",
                  //      __func__, j, strs + ns[j].n_un.n_strx, ns[j].n_type, ns[j].n_sect,
                    //    ns[j].n_desc, ns[j].n_value);
                //continue;
                kret = read_memory_at_location(strtab_addr + ns[j].n_un.n_strx,
                        str, PATH_MAX);

                if(kret == KERN_SUCCESS){
                    unsigned long slid_addr = 0;

                    if(ns[j].n_value != 0)
                        slid_addr = ns[j].n_value + (load_addr - text_segcmd->vmaddr);

                    printf("%s: nlist %d: strx '%s' n_type %#x n_sect %#x n_desc %#x"
                            " location %#lx\n",
                            __func__, j, str, ns[j].n_type, ns[j].n_sect,
                            ns[j].n_desc, slid_addr);
                }
                /*
                else{
                    printf("%s: nlist %d: strx %#x n_type %#x n_sect %#x n_desc %#x n_value %#llx\n",
                            __func__, j, ns[j].n_un.n_strx, ns[j].n_type, ns[j].n_sect,
                            ns[j].n_desc, ns[j].n_value);
                }
                */
            }
        }

        free(ns);

        free(symtab_cmd);
        free(dysymtab_cmd);
        //free(strs);
        free(linkedit_segcmd);
        free(text_segcmd);
    }

    int bkpthere = 0;

    return 0;
}
