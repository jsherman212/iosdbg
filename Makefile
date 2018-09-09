CC=clang

iosdbg:
	$(CC) -arch arm64 -isysroot /var/theos/sdks/iPhoneOS9.3.sdk linenoise.c linkedlist.c breakpoint.c memutils.c iosdbg.c mach_excUser.c mach_excServer.c -o iosdbg	