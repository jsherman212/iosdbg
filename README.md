# iosdbg

A work in progress, native debugger built for jailbroken 64 bit iOS devices capable of debugging any 64 bit process (except the kernel). Inspired by GDB and LLDB.

| iOS Version |	Supported? |
| ----------- |:---------: |
| iOS 7			| Unknown  |
| iOS 8			| Unknown  |
| iOS 9			| Yes	   |
| iOS 10		| Unknown  |
| iOS 11+		| Unknown  |

## Getting started

#### Theos
Skip this step if it's already installed on your device. I have been using the iOS 9.3 SDK to build this project. If you use a different SDK, edit the Makefile.

Theos is a cross-platform suite of tools capable of building iOS software without Xcode. Refer to this link for instructions on installing Theos on your jailbroken iOS device: https://github.com/theos/theos/wiki/Installation-iOS

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

After you build it, you'll find `libreadline.a` and `libhistory.a` inside of the current working directory. Upload those files to your device at `/path/to/theos/sdks/your/sdk/usr/lib/`. After that, rename them to `libreadline7.0.a` and `libhistory7.0.a`.

#### iosdbg
You're set to compile iosdbg. SSH into your device as `root` and run these commands:

```
cd /var/mobile
git clone https://github.com/jsherman212/iosdbg.git iosdbg
cd iosdbg
make -B
ldid -Sent.xml ./iosdbg
chmod +x ./iosdbg
```

If you're on iOS 11 and above you'll need to copy it to /usr/bin/:
`cp ./iosdbg /usr/bin/iosdbg`

If all went well, you should be good to go. If you're below iOS 11, you can run it in your current working directory with `./iosdbg`. Otherwise, you'll have to run it with `iosdbg`. Attach to your program with its PID or executable name and have fun.

## Commands
My goal with this project is to have a reliable debugger with basic debugger functionality. Breakpoints, watchpoints (to come), and little conveniences like command completion and ASLR being automatically accounted for. Commands implemented in this commit are:

`attach`
Attach to a program given its PID or executable name.

`aslr`
View the debuggee's ASLR slide. ASLR is automatically accounted for when setting breakpoints and viewing memory (a feature I'm still fixing up), so you do not need to add ASLR to every address you type.

`backtrace` (alias: `bt`)
Unwind the stack.

`break` (alias: `b`)
Set a breakpoint. Again, ASLR will be accounted for.

`continue` (alias: `c`)
Resume tthe debuggee's execution.

`delete <breakpoint ID>` (alias: `d`)
Delete a breakpoint by specifing its ID.

`detach`
Detach from the debuggee.

`help <command name>`
View command description. Does not autocomplete the argument.

`kill`
Kill the debuggee.

`quit` (alias: `q`)
Quit iosdbg.

`regs gen <optional register arg0> ...`
Show general purpose registers. If no arguments are given, all of them are shown. If you only want to show specific ones, list them.

`regs float <register arg0> ...`
Same as `regs gen` but shows a floating point register. Only supports single precision registers (`S` registers) for now. The argument is not optional.

You can view what a command does with `help command`. However, most descriptions are incomplete. I'll touch up on them soon.

## Contributing
Do not expect me to accept contributions. I learn the best when I am able to make mistakes and correct them, not by accepting someone else's solution or fix. I enjoy something the most when I figure it out and implement it myself. However, I am open to suggestions.

This is the first project I have used git and make with. My Makefile is the result of putting something together quickly. I plan to brush up on it soon.# iosdbg
