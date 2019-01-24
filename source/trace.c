#include "trace.h"

int initialize_ktrace_buffer(void){
	int mib[3];

	mib[0] = CTL_KERN;
	mib[1] = KERN_KDEBUG;
	mib[2] = KERN_KDSETUP;

	size_t needed = 0;

	return sysctl(mib, 3, NULL, &needed, NULL, 0);
}

int get_kbufinfo_buffer(kbufinfo_t *out){
	int mib[3];

	mib[0] = CTL_KERN;
	mib[1] = KERN_KDEBUG;
	mib[2] = KERN_KDGETBUF;

	size_t needed = sizeof(*out);

	return sysctl(mib, 3, out, &needed, NULL, 0);
}

int read_ktrace_buffer(kd_buf **out, size_t *needed){
	int mib[3];

	mib[0] = CTL_KERN;
	mib[1] = KERN_KDEBUG;
	mib[2] = KERN_KDREADTR;

	*out = malloc(*needed);

	return sysctl(mib, 3, *out, needed, NULL, 0);
}

int reset_ktrace_buffers(void){
	int mib[3];

	mib[0] = CTL_KERN;
	mib[1] = KERN_KDEBUG;
	mib[2] = KERN_KDREMOVE;

	size_t needed = 0;

	return sysctl(mib, 3, NULL, &needed, NULL, 0);
}

typedef struct {
	unsigned int type;
	unsigned int value1;
	unsigned int value2;
	unsigned int value3;
	unsigned int value4;
} kd_regtype;

int set_kdebug_trace_pid(int pid, int value){
	int mib[3];

	mib[0] = CTL_KERN;
	mib[1] = KERN_KDEBUG;
	mib[2] = KERN_KDPIDTR;

	kd_regtype kdregtype = { KDBG_TYPENONE, pid, value, 0, 0 };

	size_t needed = sizeof(kdregtype);

	return sysctl(mib, 3, &kdregtype, &needed, NULL, 0);
}

int set_kdebug_trace_excluded_pid(int pid, int value){
	int mib[3];

	mib[0] = CTL_KERN;
	mib[1] = KERN_KDEBUG;
	mib[2] = KERN_KDPIDEX;

	kd_regtype kdregtype = { KDBG_TYPENONE, pid, value, 0, 0 };

	size_t needed = sizeof(kdregtype);

	return sysctl(mib, 3, &kdregtype, &needed, NULL, 0);
}

int kdebug_wait(void){
	int mib[3];

	mib[0] = CTL_KERN;
	mib[1] = KERN_KDEBUG;
	mib[2] = KERN_KDBUFWAIT;

	size_t needed;

	return sysctl(mib, 3, NULL, &needed, NULL, 0);
}

int set_kdebug_enabled(int value){
	int mib[4];

	mib[0] = CTL_KERN;
	mib[1] = KERN_KDEBUG;
	mib[2] = KERN_KDENABLE;
	mib[3] = value;

	return sysctl(mib, 4, NULL, 0, NULL, 0);
}

void cleanup(void){
	reset_ktrace_buffers();
	set_kdebug_enabled(0);

	debuggee->currently_tracing = 0;
}

void *trace(void *arg){
	while(1){
		initialize_ktrace_buffer();

		if(debuggee->pid == -1 || stop){
			cleanup();
			pthread_exit(NULL);
		}

		int err = set_kdebug_trace_pid(debuggee->pid, 1);

		/* Target process died. */
		if(err < 0){
			cleanup();
			pthread_exit(NULL);
		}

		/* Don't want the kernel tracing the above events. */
		set_kdebug_enabled(1);

		if(debuggee->pid == -1 || stop){
			cleanup();
			pthread_exit(NULL);
		}

		/* Let the kernel wake up the buffer. See bsd/kern/kdebug.c
		 * @ kernel_debug_internal
		 */
		kdebug_wait();

		if(debuggee->pid == -1 || stop){
			cleanup();
			pthread_exit(NULL);
		}

		kbufinfo_t kbufinfo;
		get_kbufinfo_buffer(&kbufinfo);

		size_t numbuffers = kbufinfo.nkdbufs * sizeof(kd_buf);

		kd_buf *kdbuf;

		/* Read kernel trace buffer. */
		read_ktrace_buffer(&kdbuf, &numbuffers);

		for(int i=0; i<numbuffers; i++){
			kd_buf current = kdbuf[i];

			/* bsd/kern/kdebug.c: kernel_debug_internal */
			int code = current.debugid & ~KDBG_FUNC_MASK;
			unsigned int etype = current.debugid & KDBG_EVENTID_MASK;
			unsigned int stype = current.debugid & KDBG_CSC_MASK;

			char *event = NULL;

			int idx = (code & 0xfff) / 4;

			if(stype == BSC_SysCall){
				if(code > 0x40c0824)
					idx = (code & ~0xff00000) / 4;

				event = bsd_syscalls[idx];
			}
			else if(stype == MACH_SysCall)
				event = mach_traps[idx];
			else if(stype == MACH_Msg){
				idx = (code & ~0xff000000) / 4;

				event = mach_messages[idx];

				if(!event)
					continue;
			}
			else
				continue;

			char *tidstr = NULL;
			asprintf(&tidstr, "[0x%-6.6llx] ", current.arg5);

			char *calling, *returning;

			asprintf(&calling, "%s%-10s", tidstr, "Calling:");
			asprintf(&returning, "%s%-10s", tidstr, "Returning:");

			if(current.debugid & DBG_FUNC_START)
				printf("\e[42m\e[30m%-*s\e[0m %-35.35s", (int)strlen(calling), calling, event);

			if(current.debugid & DBG_FUNC_END)
				printf("\e[46m\e[30m%-*s\e[0m %-35.35s", (int)strlen(returning), returning, event);

			free(calling);
			free(returning);

			char *arg1desc, *arg2desc, *arg3desc, *arg4desc;

			asprintf(&arg1desc, "\e[32marg1\e[0m = 0x%16.16llx", current.arg1);
			asprintf(&arg2desc, "\e[94marg2\e[0m = 0x%16.16llx", current.arg2);
			asprintf(&arg3desc, "\e[38;5;208marg3\e[0m = 0x%16.16llx", current.arg3);
			asprintf(&arg4desc, "\e[38;5;124marg4\e[0m = 0x%16.16llx", current.arg4);

			printf("%1s%s%2s%s%2s%s%2s%s\n", "", arg1desc, "", arg2desc, "", arg3desc, "", arg4desc);

			free(arg1desc);
			free(arg2desc);
			free(arg3desc);
			free(arg4desc);
		}

		/* Reset the kernel buffers and go again. */
		reset_ktrace_buffers();
		initialize_ktrace_buffer();
		set_kdebug_enabled(0);

		free(kdbuf);
	}

	return NULL;
}

void start_trace(void){
	debuggee->currently_tracing = 1;

	rl_already_prompted = 1;
	rl_on_new_line();

	stop = 0;

	printf("Press Ctrl+C to stop tracing\n");
	
	if(debuggee->interrupted)
		printf("Warning: debuggee is currently suspended, type c and hit enter to continue\n");

	pthread_t trace_thread;
	pthread_create(&trace_thread, NULL, trace, NULL);
}

void stop_trace(void){
	rl_already_prompted = 0;

	stop = 1;
}
