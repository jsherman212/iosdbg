SDK=~/theos/sdks/iPhoneOS11.2.sdk
IPHONESDK=/var/theos/sdks/iPhoneOS11.2.sdk
CC=clang
CFLAGS=-g -arch arm64 -isysroot $(SDK)
LDFLAGS=-arch arm64 -lreadline7.0 -lhistory7.0 -lncurses -larmadillo -lpcre2-8.0 -miphoneos-version-min=12.0 -fsanitize=address -rpath $(IPHONESDK)/usr/lib
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
	dsymutil ./iosdbg

$(SRCDIR)/%.o : $(SRCDIR)/%.c $(SRCDIR)/%.h
	$(CC) $(CFLAGS) -c $< -o $@

deploy:
	make iosdbg
	scp -P 2222 iosdbg root@localhost:/var/mobile/ios-projects/iosdbg/v2
	scp -r -P 2222 iosdbg.dSYM root@localhost:/var/mobile/ios-projects/iosdbg/v2
	ssh -t -p2222 root@localhost 'cd /var/mobile/ios-projects/iosdbg/v2' \
		'&& ldid -Sent.xml ./iosdbg' \
		'&& chmod +x ./iosdbg'

clean:
	rm iosdbg $(OBJECT_FILES)
