CC=clang
CFLAGS=-arch arm64 -isysroot /var/theos/sdks/iPhoneOS9.3.sdk
LDFLAGS=-lreadline7.0 -lhistory7.0 -lncurses

iosdbg:
	$(CC) $(CFLAGS) linkedlist.c breakpoint.c memutils.c dbgcmd.c dbgutils.c iosdbg.c mach_excUser.c mach_excServer.c $(LDFLAGS) -o iosdbg	
