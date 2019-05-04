SDK=~/theos/sdks/iPhoneOS11.2.sdk
IPHONESDK=/var/theos/sdks/iPhoneOS11.2.sdk
CC=clang
CFLAGS=-arch arm64 -isysroot $(SDK)
LDFLAGS=-arch arm64 -lreadline7.0 -lhistory7.0 -lncurses -larmadillo -lpcre2-8.0 -miphoneos-version-min=12.0
SRC=source
CMDSRC=$(SRC)/cmd

ROOT_OBJECT_FILES = $(patsubst $(SRC)/%.c,$(SRC)/%.o,$(wildcard $(SRC)/*.c))
CMD_OBJECT_FILES = $(patsubst $(CMDSRC)/%.c,$(CMDSRC)/%.o,$(wildcard $(CMDSRC)/*.c))
CRITICAL_HEADER_FILES = $(SRC)/debuggee.h $(CMDSRC)/cmd.h

iosdbg : $(ROOT_OBJECT_FILES) $(CMD_OBJECT_FILES)
	$(CC) -isysroot $(SDK) $(ROOT_OBJECT_FILES) $(CMD_OBJECT_FILES) $(LDFLAGS) -o iosdbg
	dsymutil ./iosdbg

$(SRC)/%.o : $(SRC)/%.c $(SRC)/%.h $(CRITICAL_HEADER_FILES)
	$(CC) $(CFLAGS) -c $< -o $@

CMD_SOURCES = $(wildcard $(SRC)/cmd/*.c)

cmds : $(CMD_SOURCES) $(CRITICAL_HEADER_FILES)
	cd $(SRC)/cmd
	$(MAKE)

deploy:
	$(MAKE)
	scp -P 2222 iosdbg root@localhost:/var/mobile/ios-projects/iosdbg/v2
	scp -r -P 2222 iosdbg.dSYM root@localhost:/var/mobile/ios-projects/iosdbg/v2
	ssh -t -p2222 root@localhost 'cd /var/mobile/ios-projects/iosdbg/v2' \
		'&& ldid -Sent.xml ./iosdbg' \
		'&& chmod +x ./iosdbg'

.PHONY: clean
clean:
	rm iosdbg $(ROOT_OBJECT_FILES) $(CMD_OBJECT_FILES)
