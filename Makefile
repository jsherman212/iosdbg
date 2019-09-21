SDK=~/theos/sdks/iPhoneOS11.2.sdk
IPHONESDK=/var/theos/sdks/iPhoneOS10.3.sdk
CC=clang
CFLAGS=-arch arm64 -isysroot $(SDK) -pedantic -Wno-gnu-case-range
LDFLAGS=-arch arm64 -lreadline8.0 -lhistory8.0 -lncurses -larmadillo -lpcre2-8.0 -ldwarf-20190529 -lz -miphoneos-version-min=12.2 -rpath $(IPHONESDK)/usr/lib
SRC=source
CMDSRC=$(SRC)/cmd
DISASSRC=$(SRC)/disas
SYMSRC=$(SRC)/symbol

ROOT_OBJECT_FILES = $(patsubst $(SRC)/%.c,$(SRC)/%.o,$(wildcard $(SRC)/*.c))
CMD_OBJECT_FILES = $(patsubst $(CMDSRC)/%.c,$(CMDSRC)/%.o,$(wildcard $(CMDSRC)/*.c))
DISAS_OBJECT_FILES = $(patsubst $(DISASSRC)/%.c,$(DISASSRC)/%.o,$(wildcard $(DISASSRC)/*.c))
SYM_OBJECT_FILES = $(patsubst $(SYMSRC)/%.c,$(SYMSRC)/%.o,$(wildcard $(SYMSRC)/*.c))
CRITICAL_HEADER_FILES = $(SRC)/debuggee.h $(CMDSRC)/cmd.h

iosdbg : $(ROOT_OBJECT_FILES) $(CMD_OBJECT_FILES) $(DISAS_OBJECT_FILES) $(SYM_OBJECT_FILES)
	$(CC) -isysroot $(SDK) $(ROOT_OBJECT_FILES) $(CMD_OBJECT_FILES) $(DISAS_OBJECT_FILES) $(SYM_OBJECT_FILES) $(LDFLAGS) -o iosdbg
	dsymutil ./iosdbg

$(SRC)/%.o : $(SRC)/%.c $(SRC)/%.h $(CRITICAL_HEADER_FILES)
	$(CC) $(CFLAGS) -c $< -o $@

CMD_SOURCES = $(wildcard $(SRC)/cmd/*.c)

cmds : $(CMD_SOURCES) $(CRITICAL_HEADER_FILES)
	cd $(CMDSRC)
	$(MAKE)

DISAS_SOURCES = $(wildcard $(SRC)/disas/*.c)

disas : $(DISAS_SOURCES)
	cd $(DISASSRC)
	$(MAKE)

SYM_SOURCES = $(wildcard $(SRC)/symbol/*.c)

symbol : $(SYMBOL_OBJECT_FILES)
	cd $(SYMSRC)
	$(MAKE)

BUILD-DEVICE=pink
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
	rm iosdbg $(ROOT_OBJECT_FILES) $(CMD_OBJECT_FILES) $(SYM_OBJECT_FILES)
