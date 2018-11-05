SDK=/var/theos/sdks/iPhoneOS11.2.sdk
CC=clang
CFLAGS=-g -arch arm64 -isysroot $(SDK)
LDFLAGS=-lreadline7.0 -lhistory7.0 -lncurses

SOURCE_FILES = \
	breakpoint.c \
	dbgcmd.c \
	dbgutils.c \
	iosdbg.c \
	linkedlist.c \
	mach_excUser.c \
	mach_excServer.c \
	memutils.c \
	watchpoint.c

OBJECT_FILES = \
	breakpoint.o \
	dbgcmd.o \
	dbgutils.o \
	iosdbg.o \
	linkedlist.o \
	mach_excUser.o \
	mach_excServer.o \
	memutils.o \
	watchpoint.o

iosdbg : breakpoint.o dbgcmd.o dbgutils.o iosdbg.o linkedlist.o mach_excUser.o mach_excServer.o memutils.o watchpoint.o
	$(CC) -isysroot $(SDK) $(OBJECT_FILES) $(LDFLAGS) -o iosdbg

breakpoint.o : breakpoint.c breakpoint.h
	$(CC) $(CFLAGS) -c breakpoint.c

dbgcmd.o : dbgcmd.c dbgcmd.h
	$(CC) $(CFLAGS) -c dbgcmd.c

dbgutils.o : dbgutils.c dbgutils.h
	$(CC) $(CFLAGS) -c dbgutils.c

iosdbg.o : iosdbg.c iosdbg.h
	$(CC) $(CFLAGS) -c iosdbg.c

linkedlist.o : linkedlist.c linkedlist.h
	$(CC) $(CFLAGS) -c linkedlist.c

mach_excServer.o : mach_excServer.c
	$(CC) $(CFLAGS) -c mach_excServer.c

mach_excUser.o : mach_excUser.c mach_exc.h
	$(CC) $(CFLAGS) -c mach_excUser.c

memutils.o : memutils.c memutils.h
	$(CC) $(CFLAGS) -c memutils.c

watchpoint.o : watchpoint.c watchpoint.h
	$(CC) $(CFLAGS) -c watchpoint.c

clean :
	rm iosdbg $(OBJECT_FILES)
