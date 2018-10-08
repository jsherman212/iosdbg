CC=clang

iosdbg:
	$(CC) -arch arm64 -isysroot /var/theos/sdks/iPhoneOS9.3.sdk linkedlist.c breakpoint.c memutils.c matchcmd.c iosdbg.c mach_excUser.c mach_excServer.c -lreadline -lhistory -lncurses -o iosdbg	
