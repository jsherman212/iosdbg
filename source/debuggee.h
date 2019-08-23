#ifndef _DEBUGGEE_H_
#define _DEBUGGEE_H_

#include <mach/mach.h>
#include <mach-o/dyld_images.h>
#include <sys/types.h>

struct debuggee {
    /* Task port for the debuggee. */
    mach_port_t task;

    /* PID of the debuggee. */
    pid_t pid;

    /* dwarfinfo pointer for the debuggee. */
    void *dwarfinfo;

    /* dyld_all_image_infos pointer for the debuggee. */
    struct dyld_all_image_infos dyld_all_image_infos;

    /* Array of images dyld loaded into the debuggee. */
    struct dyld_image_info *dyld_info_array;

    /* List of symbols for the debuggee. */
    struct linkedlist *symbols;

    /* Array of imageFilePath strings, corresponds with dyld_all_image_infos. */
    char **dyld_image_info_filePaths;

    /* If this variable is non-zero, tracing is not supported. */
    int tracing_disabled;

    /* Whether or not we are currently tracing. */
    int currently_tracing;

    /* How many breakpoints are set. */
    int num_breakpoints;

    /* How many watchpoints are set. */
    int num_watchpoints;

    /* The debuggee's name. */
    char *debuggee_name;

    /* How many hardware breakpoints the device supports. */
    int num_hw_bps;

    /* How many hardware watchpoints the device supports. */
    int num_hw_wps;

    /* Count of threads for the debuggee. */
    mach_msg_type_number_t thread_count;

    /* Port to get exceptions from the debuggee. */
    mach_port_t exception_port;

    struct {
        mach_msg_type_number_t count;
#define MAX_EXCEPTION_PORTS 16
        exception_mask_t masks[MAX_EXCEPTION_PORTS];
        exception_handler_t ports[MAX_EXCEPTION_PORTS];
        exception_behavior_t behaviors[MAX_EXCEPTION_PORTS];
        thread_state_flavor_t flavors[MAX_EXCEPTION_PORTS];
    } original_exception_ports;

    /* List of breakpoints on the debuggee. */
    struct linkedlist *breakpoints;

    /* List of watchpoints on the debuggee. */
    struct linkedlist *watchpoints;

    /* List of threads on the debuggee. */
    struct linkedlist *threads;

    /* The debuggee's ASLR slide. */
    unsigned long aslr_slide;

    /* The function pointer to find the debuggee's ASLR slide. */
    unsigned long (*find_slide)(void);

    /* The function pointer to restore original exception ports. */
    kern_return_t (*restore_exception_ports)(void);

    /* The function pointer to task_resume. */
    kern_return_t (*resume)(void);

    /* The function pointer to set up exception handling. */
    kern_return_t (*setup_exception_handling)(char **);

    /* The function pointer to deallocate needed ports on detach. */
    kern_return_t (*deallocate_ports)(char **);

    /* The function pointer to task_suspend. */
    kern_return_t (*suspend)(void);

    /* The function pointer to update the list of the debuggee's threads. */
    kern_return_t (*get_threads)(thread_act_port_array_t *,
            mach_msg_type_number_t *, char **);

    /* The function pointer to figure out if source level debugging is available. */
    int (*has_debug_info)(void);

    /* The function pointer to figure out if the debuggee is currently suspended. */
    int (*suspended)(void);
};

/* This structure represents what we are currently debugging. */
extern struct debuggee *debuggee;

#endif
