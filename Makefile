SDK=/var/theos/sdks/iPhoneOS11.2.sdk
CC=clang
CFLAGS=-g -arch arm64 -isysroot $(SDK)
LDFLAGS=-lreadline7.0 -lhistory7.0 -lncurses -larmadillo -lpcre2-8.0 -fsanitize=address
SRC=source

OBJECT_FILES = $(SRC)/argparse.o \
			   $(SRC)/audit.o \
			   $(SRC)/breakpoint.o \
			   $(SRC)/completer.o \
			   $(SRC)/convvar.o \
			   $(SRC)/dbgcmd.o \
			   $(SRC)/dbgops.o \
			   $(SRC)/docfunc.o \
			   $(SRC)/exception.o \
			   $(SRC)/expr.o \
			   $(SRC)/iosdbg.o \
			   $(SRC)/handlers.o \
			   $(SRC)/linkedlist.o \
			   $(SRC)/machthread.o \
			   $(SRC)/memutils.o \
			   $(SRC)/printutils.o \
			   $(SRC)/procutils.o \
			   $(SRC)/queue.o \
			   $(SRC)/servers.o \
			   $(SRC)/sigcmd.o \
			   $(SRC)/sigsupport.o \
			   $(SRC)/stack.o \
			   $(SRC)/strext.o \
			   $(SRC)/trace.o \
			   $(SRC)/watchpoint.o

iosdbg : $(OBJECT_FILES)
	$(CC) -isysroot $(SDK) $(OBJECT_FILES) $(LDFLAGS) -o iosdbg

$(SRCDIR)/%.o : $(SRCDIR)/%.c $(SRCDIR)/%.h
	$(CC) $(CFLAGS) -c $< -o $@

clean :
	rm iosdbg $(OBJECT_FILES)
