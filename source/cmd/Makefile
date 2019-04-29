SDK=~/theos/sdks/iPhoneOS11.2.sdk
CC=clang
CFLAGS=-g -arch arm64 -isysroot $(SDK)

%.o : %.c %.h
	$(CC) $(CFLAGS) -c $<

.PHONY: clean
clean:
	rm *.o
