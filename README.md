![alt text](https://raw.githubusercontent.com/jsherman212/iosdbg/master/iosdbg9.png)

# iosdbg

A work in progress, native debugger built for *jailbroken* 64 bit iOS devices capable of debugging any 64 bit process (except the kernel and itself). Includes an in-house arm64 disassembler and expression evaluator.

| iOS Version |	Supported? |
| ----------- |:---------: |
| iOS 7			| Unknown  |
| iOS 8			| Unknown  |
| iOS 9			| Yes	   |
| iOS 10		| Yes	   |
| iOS 11		| Unknown  |
| iOS 12		| Yes	   |

## Getting started

#### If you're jailbroken with Unc0ver on iOS 12, you'll need to set CS_GET_TASK_ALLOW (0x4) in the csflags of the program you want to debug, as of March 6th, 2019. Otherwise, use beta 50 or later and turn on "Enable get-task-allow" and "Set CS_DEBUGGED" and jailbreak. If you're using Chimera, you should be fine.

Optional: if you're compiling on device, add MCApollo's repo in Cydia: https://mcapollo.github.io/Public/

#### Theos
Skip this step if it's already installed on your computer. I have been using the iOS 9.3 SDK and (currently) the iOS 11.2 SDK to build this project. If you use a different SDK, edit the Makefile. I have been developing this debugger on an iPhone 6s on iOS 9.3.3, an iPhone 5s on iOS 10.3.2, an iPhone SE on iOS 12.0, and an iPhone X on iOS 12.1.

Theos is a cross-platform suite of tools capable of building iOS software without Xcode. Refer to this link for instructions on installing Theos on your computer: https://github.com/theos/theos/wiki/Installation-macOS or your iDevice: 

#### GNU readline 8.0
This project uses GNU readline 8.0. Compile it for `aarch64-apple-darwin`:

```
curl -O ftp://ftp.cwru.edu/pub/bash/readline-8.0.tar.gz
tar xvzf readline-8.0.tar.gz
cd readline-8.0
export CFLAGS='-arch arm64 -isysroot /path/to/your/iPhoneOS/sdk'
./configure --host=aarch64-apple-darwin
make
```

After you build it, you'll find `libreadline.a` and `libhistory.a` inside of the current working directory. Upload those files to your device at `/path/to/theos/sdks/your/sdk/usr/lib/`. Rename them to `libreadline8.0.a` and `libhistory8.0.a` and fakesign them with `ldid`. If you're compiling on a computer, copy those files to `/path/to/theos/sdks/your/sdk/usr/lib/`. Create a new directory called `readline` at `/path/to/theos/sdks/your/sdk/usr/include` and copy (or upload to your device) `chardefs.h`, `history.h`, `keymaps.h`, `readline.h`, `rlstdc.h`, `rltypedefs.h`, and `tilde.h` there.

#### pcre2
This project uses pcre2 10.32. Compile it for `aarch64-apple-darwin`:

```
export CFLAGS='-arch arm64 -isysroot /path/to/your/iPhoneOS/sdk'
./configure --host=aarch64-apple-darwin
make
```

You'll find `libpcre2-8.0.dylib` in `.libs`. Upload it to your device at `/path/to/theos/sdks/your/sdk/usr/lib/` and fakesign it. If you're compiling on a computer, copy it to `/path/to/theos/sdks/your/sdk/usr/lib/`. Copy (or upload to your device) `pcre2.h` to `/path/to/theos/sdks/your/sdk/usr/include`.

Alternatively, if you're compiling on device, you can add MCApollo's repo in Cydia and install `pcre2`. Make sure the version is 10.32.

#### armadillo
I took a break from this project to write the disassembler for it. Head over to https://github.com/jsherman212/armadillo and follow the instructions for compiling it **on your jailbroken device**. Copy `source/armadillo.h` to `/path/to/theos/sdks/your/sdk/usr/include`.

### Source Level Debugging
I took another break from this project to write a wrapper around libdwarf to support C language source level debugging. You can find the implementation in `source/symbol`. This requires libdwarf and its dependencies. To build each for `aarch64-apple-darwin` on your computer:

#### libdwarf-20190529
```
curl -O https://www.prevanders.net/libdwarf-20190529.tar.gz
tar xvzf libdwarf-20190529.tar.gz
cd libdwarf-20190529
export CFLAGS='-arch arm64 -isysroot /path/to/your/iPhoneOS/sdk'
./configure --host=aarch64-apple-darwin --disable-libelf
cp libdwarf/libdwarf.h.in libdwarf/libdwarf.h
```

You'll find `libdwarf.a` at `libdwarf/.libs`. Rename it to `libdwarf-20190529.a` and upload it to your device at `/path/to/theos/sdks/your/sdk/usr/lib/` and fakesign it. If you're compiling on a computer, copy it to `/path/to/theos/sdks/your/sdk/usr/lib/`. Copy (or upload to your device) `libdwarf.h` and `dwarf.h` to `/path/to/theos/sdks/your/sdk/usr/include/`.

#### zlib
The SDKs from Theos should already ship with zlib.

#### iosdbg
You're set to compile iosdbg. On your computer:

```
git clone https://github.com/jsherman212/iosdbg.git iosdbg
cd iosdbg
make SDK=yoursdk
```

After that, copy the `iosdbg` executable to your iOS device. Then SSH into your iOS device and run these commands:

iOS 11 and below:
```
ldid -Sent.xml ./iosdbg
```

iOS 12 and above:
```
ldid -P -Sent.xml ./iosdbg
```

The following applies to all iOS versions:
```
chmod +x ./iosdbg
```

If you're on iOS 11 and above you'll need to copy it to /usr/bin/:
`cp ./iosdbg /usr/bin/iosdbg`

If all went well, you should be good to go. If you're below iOS 11, you can run it in your current working directory with `./iosdbg`. Otherwise, you'll have to run it with `iosdbg`. Attach to your program with its PID or executable name and have fun.

## Commands
**You only need to type enough characters in the command for iosdbg to unambiguously identify it**. You can view detailed documentation for a command with the `help` command. If you type `help` by itself, you'll be shown all top level commands. Include `!` at the beginning of your input to execute a shell command.


## ASLR
When I started this project I wanted some commands (`breakpoint set`, `memory read`, etc) to automatically add the ASLR slide to relieve the user the burden of doing it themselves. However, I could not find a good middle ground. The ASLR slide is now stored in the convenience variable `$ASLR`. This way, it can be included in expressions, ex: `breakpoint set 0x100007edc+$ASLR`.


## Contributing
While I may not accept contributions, I am open to suggestions.

This is the first project I have used git and make with.
