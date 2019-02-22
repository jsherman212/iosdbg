![alt text](https://raw.githubusercontent.com/jsherman212/iosdbg/master/iosdbg6.png)

# iosdbg

A work in progress, native debugger built for *jailbroken* 64 bit iOS devices capable of debugging any 64 bit process (except the kernel and itself). Inspired by GDB and LLDB.

| iOS Version |	Supported? |
| ----------- |:---------: |
| iOS 7			| Unknown  |
| iOS 8			| Unknown  |
| iOS 9			| Yes	   |
| iOS 10		| Yes	   |
| iOS 11		| Unknown  |
| iOS 12		| Yes	   |

## Getting started

#### Instructions for compiling and running iosdbg on iOS 12 will come when a proper jailbreak is released.

#### Theos
Skip this step if it's already installed on your device. I have been using the iOS 9.3 SDK and (currently) the iOS 11.2 SDK to build this project. If you use a different SDK, edit the Makefile.

Theos is a cross-platform suite of tools capable of building iOS software without Xcode. Refer to this link for instructions on installing Theos on your jailbroken iOS device: https://github.com/theos/theos/wiki/Installation-iOS

#### GNU readline 7.0
This project uses GNU readline 7.0. Compile it for `aarch64-apple-darwin` on a non-iOS device:

```
curl -O ftp://ftp.cwru.edu/pub/bash/readline-7.0.tar.gz
tar xvzf readline-7.0.tar.gz
cd readline-7.0
export CFLAGS='-arch arm64 -isysroot /path/to/your/iPhoneOS/sdk'
./configure --host=aarch64-apple-darwin
make
```

After you build it, you'll find `libreadline.a` and `libhistory.a` inside of the current working directory. Upload those files to your device at `/path/to/theos/sdks/your/sdk/usr/lib/`. After that, rename them to `libreadline7.0.a` and `libhistory7.0.a`.

#### armadillo
I took a break from this project to write the disassembler for it. Head over to https://github.com/jsherman212/armadillo and follow the instructions for compiling it **on your jailbroken device**. Copy `source/armadillo.h` to `/path/to/your/iPhoneOS/sdk/usr/include`.

#### iosdbg
You're set to compile iosdbg. SSH into your device as `root` and run these commands:

```
cd /var/mobile
git clone https://github.com/jsherman212/iosdbg.git iosdbg
cd iosdbg
make
ldid -Sent.xml ./iosdbg
chmod +x ./iosdbg
```

If you're on iOS 11 and above you'll need to copy it to /usr/bin/:
`cp ./iosdbg /usr/bin/iosdbg`

If all went well, you should be good to go. If you're below iOS 11, you can run it in your current working directory with `./iosdbg`. Otherwise, you'll have to run it with `iosdbg`. Attach to your program with its PID or executable name and have fun.

## Commands
My goal with this project is to have a reliable debugger with basic functionality. Breakpoints, watchpoints, and little conveniences like command completion and ASLR being automatically accounted for. **You only need to type enough characters in the command for iosdbg to unambiguously identify it**. Commands implemented are:

### `attach`
Attach to a program given its PID or executable name. Syntax: `attach <(PID|{--waitfor} progname)>`

Include `--waitfor` to wait for a process and attach to it when it launches.

### `aslr`
View the debuggee's ASLR slide.

### `backtrace` (alias: `bt`)
Unwind the stack.

### `break` (alias: `b`)
Set a breakpoint. After all hardware breakpoints have been used, iosdbg defaults to software breakpoints. Syntax: `(b|break) <location>`

Pass `--no-aslr` to keep ASLR from being added.

### `continue` (alias: `c`)
Resume the debuggee's execution.

### `disassemble` (alias: `dis`)
Disassemble debuggee memory. Syntax: `(dis|disassemble) <location> <numlines>`

Pass `--no-aslr` to keep ASLR from being added.

### `delete` (alias: `d`)
Delete a breakpoint or a watchpoint by specifing its ID. Syntax: `(d|delete) <type> {ID}`

Type can be `b` for breakpoint or `w` for watchpoint. If you do not include `ID`, you will be given an option to delete all breakpoints or watchpoints, depending on what you gave for `<type>`.

### `detach`
Detach from the debuggee.

### `examine` (alias: `x`)
Examine memory at a location. Syntax: `(x|examine) (location|$register) <count>`

If you want to view a register, prefix it with '$'. In this case, ASLR will never be accounted for.

If you want your amount interpreted as hex, use `0x`. Pass `--no-aslr` to keep ASLR from being added.

### `help`
View command description. Syntax: `help <command name>`

Does not auto-complete the argument.

### `kill`
Kill the debuggee.

### `quit` (alias: `q`)
Quit iosdbg.

### `regs gen`
Show general purpose registers. Syntax: `regs gen {reg1 reg2 ...}`

The argument is optional. All general purpose registers are dumped if there is no argument.

You can list as many general purpose registers as you want.

### `regs float`
Same as `regs gen` but shows floating point register(s). Syntax: `regs float <reg1 reg2 ...>`

You can list as many floating point registers as you want.

### `set`
Modify debuggee memory, registers, or a convenience variable for iosdbg. Syntax: `set <(*offset|$register|$variable)=value>`

You must prefix an offset with a `*`  and register or convenience variable with `$`.

If you want to modify one of the 128 bit V registers, format value as follows: `"{byte1 byte2 byte3 byte4 byte5 byte6 byte7 byte8 byte9 byte10 byte11 byte12 byte13 byte14 byte15 byte16}"`. Bytes do not have to be in hex.

If you want your value to be interpreted as hex, use `0x`. Pass `--no-aslr` to prevent ASLR from being added.

### `show`
Show convenience variables. Syntax: `show {convenience variable}`

You can exclude `{convenience variable}` to print all of them, or include it to only print a specific one. Convenience variables automatically managed by iosdbg are:

`$_`: set by the `examine` command to the last address examined.

`$__`: set by the `examine` command to the value found in the last address examined.

`$_exitcode`: when the debuggee terminates normally, iosdbg sets this variable to its exit code, and resets `$_exitsignal` to void.

`$_exitsignal`: when the debuggee dies due to a signal, iosdbg sets this variable to the signal number, and resets `$_exitcode` to void.

#### **To force iosdbg to never add ASLR to expressions, set the convenience variable `$NO_ASLR_OVERRIDE` to any value.**

### `stepi`
Step into the next machine instruction. The debuggee must be suspended.

### `thread list`
List threads belonging to the debuggee.

### `thread select`
Select a different thread to focus on while debugging. Syntax: `thread select <thread ID>`

Default focused thread is thread #1. If the focused thread goes away, the first available thread is selected automatically. When an exception is caused, focus goes to the thread that caused it.

### `trace`
Trace system calls, mach system calls, and mach traps of the debuggee. Press Ctrl+C to stop.

### `unset`
Set a convenience variable's value to `void`. Syntax: `unset <convenience variable> ...`

You can include as many convenience variables as you wish.

### `watch` (alias: `w`)
Set a read, write, or read-write watchpoint. Syntax: `(w|watch) {type} <addr> <size>`

Type can be `--r` (read), `--w` (write), or `--rw` (read/write). It is optional, so if nothing is given, iosdbg defaults to `--w`. ASLR is not accounted for.

##### You can view what a command does with `help <command>`.


## Contributing
While I may not accept contributions, I am open to suggestions.

This is the first project I have used git and make with.
