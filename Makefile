CC=clang

iosdbg:
	$(CC) -arch arm64 -isysroot /var/theos/sdks/iPhoneOS9.3.sdk linkedlist.c breakpoint.c memutils.c dbgcmd.c dbgutils.c iosdbg.c mach_excUser.c mach_excServer.c -lreadline7.0 -lhistory7.0 -lncurses -o iosdbg	
