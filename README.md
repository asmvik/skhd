> **NOTE**: If you are having issues with skhd, or are looking for feature expansion and future development,  
you may want to check out [this backwards compatible skhd-port](https://github.com/jackielii/skhd.zig) written in Zig by [Jackie Li](https://github.com/jackielii).  
(I am not personally involved in this re-write.)

**This repository is in maintenance mode; only critical issues that affect the core functionality of the software will be taken care of.** 

**skhd** is a simple hotkey daemon for macOS that focuses on responsiveness and performance.
Hotkeys are defined in a text file through a simple DSL. **skhd** is able to hotload its config file, meaning that hotkeys can be edited and updated live while **skhd** is running.

**skhd** uses a pid-file to make sure that only one instance is running at any moment in time. This also allows for the ability to trigger
a manual reload of the config file by invoking `skhd --reload` at any time while an instance of **skhd** is running. The pid-file is saved
as `/tmp/skhd_$USER.pid` and so the user that is running **skhd** must have write permission to said path.
When running as a service (through launchd) log files can be found at `/tmp/skhd_$USER.out.log` and `/tmp/skhd_$USER.err.log`.

list of features

| feature                    | skhd |
|:--------------------------:|:----:|
| hotload config file        | [x]  |
| hotkey passthrough         | [x]  |
| modal hotkey-system        | [x]  |
| application specific hotkey| [x]  |
| blacklist applications     | [x]  |
| use media-keys as hotkey   | [x]  |
| synthesize a key-press     | [x]  |

### Install

The first time **skhd** is run, it will request access to the accessibility API.

After access has been granted, the application must be restarted.

*Secure Keyboard Entry* must be disabled for **skhd** to receive key-events.

**Homebrew**:

Requires xcode-8 command-line tools.

      brew install asmvik/formulae/skhd
      skhd --start-service

**Source**:

Requires xcode-8 command-line tools.

      git clone https://github.com/asmvik/skhd
      make install      # release version
      make              # debug version

### Usage

```
--install-service: Install launchd service file into ~/Library/LaunchAgents/com.asmvik.skhd.plist
    skhd --install-service

--uninstall-service: Remove launchd service file ~/Library/LaunchAgents/com.asmvik.skhd.plist
    skhd --uninstall-service

--start-service: Run skhd as a service through launchd
    skhd --start-service

--restart-service: Restart skhd service
    skhd --restart-service

--stop-service: Stop skhd service from running
    skhd --stop-service

-V | --verbose: Output debug information
    skhd -V

-P | --profile: Output profiling information
    skhd -P

-v | --version: Print version number to stdout
    skhd -v

-c | --config: Specify location of config file
    skhd -c ~/.skhdrc

-o | --observe: Output keycode and modifiers of event. Ctrl+C to quit
    skhd -o

-r | --reload: Signal a running instance of skhd to reload its config file
    skhd -r

-h | --no-hotload: Disable system for hotloading config file
    skhd -h

-k | --key: Synthesize a keypress (same syntax as when defining a hotkey)
    skhd -k "shift + alt - 7"

-t | --text: Synthesize a line of text
    skhd -t "hello, worldシ"
```

### Configuration

The default configuration file is located at one of the following places (in order):

 - `$XDG_CONFIG_HOME/skhd/skhdrc`
 - `$HOME/.config/skhd/skhdrc`
 - `$HOME/.skhdrc`

A different location can be specified with the *--config | -c* argument.

A sample config is available [here](https://github.com/asmvik/skhd/blob/master/examples/skhdrc)

A list of all built-in modifier and literal keywords can be found [here](https://github.com/asmvik/skhd/issues/1)

A hotkey is written according to the following rules:
```
hotkey       = <mode> '<' <action> | <action>

mode         = 'name of mode' | <mode> ',' <mode>

action       = <keysym> '[' <proc_map_lst> ']' | <keysym> '->' '[' <proc_map_lst> ']'
               <keysym> ':' <command>          | <keysym> '->' ':' <command>
               <keysym> ';' <mode>             | <keysym> '->' ';' <mode>

keysym       = <mod> '-' <key> | <key>

mod          = 'modifier keyword' | <mod> '+' <mod>

key          = <literal> | <keycode>

literal      = 'single letter or built-in keyword'

keycode      = 'apple keyboard kVK_<Key> values (0x3C)'

proc_map_lst = * <proc_map>

proc_map     = <string> ':' <command> | <string>     '~' |
               '*'      ':' <command> | '*'          '~'

string       = '"' 'sequence of characters' '"'

command      = command is executed through '$SHELL -c' and
               follows valid shell syntax. if the $SHELL environment
               variable is not set, it will default to '/bin/bash'.
               when bash is used, the ';' delimeter can be specified
               to chain commands.

               to allow a command to extend into multiple lines,
               prepend '\' at the end of the previous line.

               an EOL character signifies the end of the bind.

->           = keypress is not consumed by skhd

*            = matches every application not specified in <proc_map_lst>

~            = application is unbound and keypress is forwarded per usual, when specified in a <proc_map>
```

A mode is declared according to the following rules:
```

mode_decl = '::' <name> '@' ':' <command> | '::' <name> ':' <command> |
            '::' <name> '@'               | '::' <name>

name      = desired name for this mode,

@         = capture keypresses regardless of being bound to an action

command  = command is executed through '$SHELL -c' and
           follows valid shell syntax. if the $SHELL environment
           variable is not set, it will default to '/bin/bash'.
           when bash is used, the ';' delimeter can be specified
           to chain commands.

           to allow a command to extend into multiple lines,
           prepend '\' at the end of the previous line.

           an EOL character signifies the end of the bind.
```

General options that configure the behaviour of **skhd**:
```
# specify a file that should be included as an additional config-file.
# treated as an absolutepath if the filename begins with '/' otherwise
# the file is relative to the path of the config-file it was loaded from.

.load "/Users/Koe/.config/partial_skhdrc"
.load "partial_skhdrc"

# prevents skhd from monitoring events for listed processes.

.blacklist [
    "terminal"
    "qutebrowser"
    "kitty"
    "google chrome"
]
```
