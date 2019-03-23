#ifndef _TRACE_H_
#define _TRACE_H_

#include <sys/types.h>

typedef struct {
    /* number of events that can fit in the buffers */
    int nkdbufs;
    /* set if trace is disabled */
    int nolog;
    /* kd_ctrl_page.flags */
    unsigned int flags;
    /* number of threads in thread map */
    int nkdthreads;
    /* the owning pid */
    int bufid;
} kbufinfo_t;

#if defined(__arm64__)
typedef uint64_t kd_buf_argtype;
#else
typedef uintptr_t kd_buf_argtype;
#endif

typedef struct {
    uint64_t timestamp;
    kd_buf_argtype arg1;
    kd_buf_argtype arg2;
    kd_buf_argtype arg3;
    kd_buf_argtype arg4;
    kd_buf_argtype arg5; /* the thread ID */
    uint32_t debugid;
/*
 * Ensure that both LP32 and LP64 variants of arm64 use the same kd_buf
 * structure.
 */
#if defined(__LP64__) || defined(__arm64__)
    uint32_t cpuid;
    kd_buf_argtype unused;
#endif
} kd_buf;

typedef struct {
    unsigned int type;
    unsigned int value1;
    unsigned int value2;
    unsigned int value3;
    unsigned int value4;
} kd_regtype;

#define KDBG_CSC_MASK   (0xffff0000)
#define KDBG_FUNC_MASK    (0x00000003)
#define KDBG_EVENTID_MASK (0xfffffffc)

#define BSC_SysCall 0x040c0000
#define MACH_SysCall    0x010c0000
#define MACH_Msg 0xff000000

/* function qualifiers  */
#define DBG_FUNC_START 1
#define DBG_FUNC_END   2

#define KDBG_TYPENONE   0x80000

void start_trace(void);
void stop_trace(void);

/* Wait for the trace thread to be finished with
 * processing everything before this function returns.
 */
void wait_for_trace(void);

#endif
