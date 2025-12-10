#compdef skhd

# Zsh completion for Simple Hotkey Daemon v0.3.9 for macOS:
# https://github.com/koekeishiya/skhd

local arguments=(
  '(--install-service)--install-service[Install launchd service file into ~/Library/LaunchAgents/com.koekeishiya.skhd.plist]'
  '(--uninstall-service)--uninstall-service[Remove launchd service file ~/Library/LaunchAgents/com.koekeishiya.skhd.plist]'
  '(--start-service)--start-service[Run skhd as a service through launchd]'
  '(--restart-service)--restart-service[Restart skhd service]'
  '(--stop-service)--stop-service[Stop skhd service from running]'
  '(--verbose -V)'{--verbose,-V}'[Output debug information]'
  '(--profile -P)'{--profile,-P}'[Output profiling information]'
  '(--version -v)'{--version,-v}'[Print version number to stdout]'
  '(--config -c)'{--config,-c}'[Specify location of config file]:file:_files'
  '(--observe -o)'{--observe,-o}'[Output keycode and modifiers of event. Ctrl+C to quit]'
  '(--reload -r)'{--reload,-r}'[Signal a running instance of skhd to reload its config file]'
  '(--no-hotload -h)'{--no-hotload,-h}'[Disable system for hotloading config file]'
  '(--key -k)'{--key,-k}'[Synthesize a keypress (same syntax as when defining a hotkey)]:keysym'
  '(--text -t)'{--text,-t}'[Synthesize a line of text]:text'
)

_arguments -S : $arguments
