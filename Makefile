SDK=/var/theos/sdks/iPhoneOS11.2.sdk
CC=clang
CFLAGS=-arch arm64 -isysroot $(SDK)
LDFLAGS=-lreadline7.0 -lhistory7.0 -lncurses -larmadillo 
SRCDIR=source

OBJECT_FILES = $(SRCDIR)/breakpoint.o \
	$(SRCDIR)/dbgcmd.o \
	$(SRCDIR)/dbgutils.o \
	$(SRCDIR)/iosdbg.o \
	$(SRCDIR)/handlers.o \
	$(SRCDIR)/linkedlist.o \
	$(SRCDIR)/mach_excUser.o \
	$(SRCDIR)/mach_excServer.o \
	$(SRCDIR)/machthread.o \
	$(SRCDIR)/memutils.o \
	$(SRCDIR)/printutils.o \
	$(SRCDIR)/watchpoint.o

iosdbg : $(OBJECT_FILES)
	$(CC) -isysroot $(SDK) $(OBJECT_FILES) $(LDFLAGS) -o iosdbg

$(SRCDIR)/%.o : $(SRCDIR)/%.c $(SRCDIR)/%.h
	$(CC) $(CFLAGS) -c $< -o $@

clean :
	rm iosdbg $(OBJECT_FILES)
