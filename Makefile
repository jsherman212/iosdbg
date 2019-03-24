SDK=/var/theos/sdks/iPhoneOS11.2.sdk
CC=clang
CFLAGS=-arch arm64 -isysroot $(SDK)
LDFLAGS=-lreadline7.0 -lhistory7.0 -lncurses -larmadillo -lpcre2-8.0
SRCDIR=source

OBJECT_FILES = $(SRCDIR)/argparse.o \
	$(SRCDIR)/breakpoint.o \
	$(SRCDIR)/convvar.o \
	$(SRCDIR)/dbgcmd.o \
	$(SRCDIR)/exception.o \
	$(SRCDIR)/expr.o \
	$(SRCDIR)/iosdbg.o \
	$(SRCDIR)/handlers.o \
	$(SRCDIR)/linkedlist.o \
	$(SRCDIR)/machthread.o \
	$(SRCDIR)/memutils.o \
	$(SRCDIR)/printutils.o \
	$(SRCDIR)/procutils.o \
	$(SRCDIR)/queue.o \
	$(SRCDIR)/servers.o \
	$(SRCDIR)/stack.o \
	$(SRCDIR)/trace.o \
	$(SRCDIR)/watchpoint.o

iosdbg : $(OBJECT_FILES)
	$(CC) -isysroot $(SDK) $(OBJECT_FILES) $(LDFLAGS) -o iosdbg

$(SRCDIR)/%.o : $(SRCDIR)/%.c $(SRCDIR)/%.h
	$(CC) $(CFLAGS) -c $< -o $@

clean :
	rm iosdbg $(OBJECT_FILES)
