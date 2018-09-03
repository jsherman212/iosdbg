CC=clang

iosdbg: linenoise.c iosdbg.c
	$(CC) -arch arm64 -isysroot /var/theos/sdks/iPhoneOS9.3.sdk linenoise.c iosdbg.c -o iosdbg	
