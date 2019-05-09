![alt text](https://raw.githubusercontent.com/jsherman212/iosdbg/master/iosdbg8.png)

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

#### Theos
Skip this step if it's already installed on your computer. I have been using the iOS 9.3 SDK and (currently) the iOS 11.2 SDK to build this project. If you use a different SDK, edit the Makefile. I have been developing this debugger on an iPhone 6s on iOS 9.3.3, an iPhone 5s on iOS 10.3.2, an iPhone SE on iOS 12.0, and an iPhone X on iOS 12.1.

Theos is a cross-platform suite of tools capable of building iOS software without Xcode. Refer to this link for instructions on installing Theos on your computer: https://github.com/theos/theos/wiki/Installation-macOS

#### GNU readline 7.0
This project uses GNU readline 7.0. Compile it for `aarch64-apple-darwin`:

```
curl -O ftp://ftp.cwru.edu/pub/bash/readline-7.0.tar.gz
tar xvzf readline-7.0.tar.gz
cd readline-7.0
export CFLAGS='-arch arm64 -isysroot /path/to/your/iPhoneOS/sdk'
./configure --host=aarch64-apple-darwin
make
```

After you build it, you'll find `libreadline.a` and `libhistory.a` inside of the current working directory. Upload those files to your device at `/path/to/theos/sdks/your/sdk/usr/lib/`. Rename them to `libreadline7.0.a` and `libhistory7.0.a` and fakesign them with `ldid`.

#### pcre2
This project uses pcre2 10.32. Compile it for `aarch64-apple-darwin`:

```
export CFLAGS='-arch arm64 -isysroot /path/to/your/iPhoneOS/sdk'
./configure --host=aarch64-apple-darwin
make
```

You'll find `libpcre2-8.0.dylib` in `.libs`. Upload those files to your device at `/path/to/theos/sdks/your/sdk/usr/lib/` and fakesign it.

#### armadillo
I took a break from this project to write the disassembler for it. Head over to https://github.com/jsherman212/armadillo and follow the instructions for compiling it **on your jailbroken device**. Copy `source/armadillo.h` to `/path/to/your/iPhoneOS/sdk/usr/include`.

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