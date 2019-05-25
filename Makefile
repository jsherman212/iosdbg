SDK=~/theos/sdks/iPhoneOS11.2.sdk
IPHONESDK=/var/theos/sdks/iPhoneOS10.3.sdk
CC=clang
CFLAGS=-g -arch arm64 -isysroot $(SDK) -pedantic
LDFLAGS=-arch arm64 -lreadline7.0 -lhistory7.0 -lncurses -larmadillo -lpcre2-8.0 -miphoneos-version-min=12.0 -fsanitize=address -rpath $(IPHONESDK)/usr/lib
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

BUILD-DEVICE=iosdragon
BUILD-PATH=/var/mobile/iosdbg-dev

deploy:
	$(MAKE)
	ssh -t -p2222 $(BUILD-DEVICE) 'if test -f "$(BUILD-PATH)/iosdbg"; then' \
		'rm $(BUILD-PATH)/iosdbg;' \
		'fi'
	scp -P 2222 iosdbg ent.xml $(BUILD-DEVICE):$(BUILD-PATH)
	scp -r -P 2222 iosdbg.dSYM $(BUILD-DEVICE):$(BUILD-PATH)
	ssh -t -p2222 $(BUILD-DEVICE) 'cd $(BUILD-PATH)' \
		'&& ldid -Sent.xml -P ./iosdbg' \
		'&& chmod +x ./iosdbg'

.PHONY: clean
clean:
	rm iosdbg $(ROOT_OBJECT_FILES) $(CMD_OBJECT_FILES)
