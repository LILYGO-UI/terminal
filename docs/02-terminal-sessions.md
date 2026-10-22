# Terminal Sessions and Configuration

[English README](../README.md) | [中文 README](../README.zh-CN.md) |
[Architecture overview (Chinese)](00-overview.md)

## Shell Sessions

Terminal opens `/bin/sh` on a pseudo-terminal (PTY) and supplies `TERM=ansi`.
Commands share a live shell session, so directory changes, shell variables,
pipes, and interactive input behave as they do in a terminal. The default shell
is invoked with `-l` and uses its own startup files and prompt.

The screen size follows the available output area and selected font size, with
limits of 4 to 48 rows and 16 to 120 columns. Resizing updates both libtmt's
screen and the PTY window size through `TIOCSWINSZ`.

Closing Terminal or pressing `Restart` stops the existing shell. Shutdown sends
`SIGHUP` to the shell and its process group, closes the PTY, joins the reader,
and reaps the shell; a shell that does not exit promptly is killed. Restart
clears the screen and creates a new session. Shell state is not restored.

### Terminal Compatibility

libtmt interprets ANSI control sequences. The current view renders screen rows
as text and does not reproduce per-character ANSI colors or text attributes.
There is no scrollback history. Programs that require xterm extensions,
full-screen interfaces, or precise wide-character alignment need separate
compatibility checks.

## User Permissions

The shell inherits the UI process's user and group permissions. When launched
by an ordinary user, it runs as that user. When Launcher starts the application
as root, the shell also runs as root. No SSH or console login is required to
open or restart a shell.

The application is a local shell, not a remote SSH client or a sandbox.
Commands can access files and resources allowed by the launching process's
permissions.

### Shell Environment

The child inherits the application's environment, including `PATH`, `USER`,
and `LOGNAME`. Terminal sets `TERM=ansi`, `SHELL` to the configured executable,
and `HOME` to the first nonempty value in this order:

1. `LILYGO_UI_TERMINAL_HOME`.
2. The application's `HOME` environment variable.
3. The account home returned by `getpwuid(getuid())`.
4. `/`.

A nonempty `LILYGO_UI_TERMINAL_PS1` also overrides `PS1`. Other environment
variables remain inherited. Shell startup files can subsequently change the
environment. The shell initially changes to its configured home directory,
or `/` if that directory cannot be entered.

## Controls

| Control | Behavior |
| --- | --- |
| Touch keyboard | Switch between lowercase, uppercase, numbers, and symbols. |
| Special keys | Open the special-key window for terminal controls. |
| Exit button | Close the Terminal application. |
| `Ctrl` | Toggle a one-shot modifier; the next letter sends its control character. A second tap cancels it. |
| `C`, `D`, `L` | Send the letter normally, or `Ctrl+C`, `Ctrl+D`, `Ctrl+L` when Ctrl is armed. |
| `Esc`, `Tab`, `Enter` | Send escape, tab, and carriage return. |
| Arrow keys | Send standard ANSI cursor-key sequences. |
| Physical keyboard | Printable keys and standard editing/navigation keys are sent directly to the shell. `Shift+Tab` sends the ANSI back-tab sequence. |
| `BS` | Send the delete byte (`0x7f`) used for backspace. |
| `Clear` | Reset the local screen buffer without sending input or stopping the shell. |
| `Restart` | Stop the old shell and open a fresh session. |
| `A-`, `A+` | Select and save a 14, 18, or 22 px terminal font. |

Ctrl is consumed by the next non-Ctrl special-key action or nonempty text
input. `Ctrl+L` from the dedicated `L` button also clears the local screen.
`Ctrl+C` normally interrupts the foreground program; `Ctrl+D` is interpreted
by the running shell or program and can end the shell when the input line is
empty. Hardware keys are forwarded when the LVGL runtime supplies key events.

## Font Preferences

The default terminal font size is 18 px. The settings file is selected in this
order, ignoring empty environment variables:

| Condition | Settings file |
| --- | --- |
| `LILYGO_UI_TERMINAL_CONFIG` is set | The exact configured file path |
| `XDG_CONFIG_HOME` is set | `$XDG_CONFIG_HOME/lilygo-ui-terminal/settings.conf` |
| `HOME` is set | `$HOME/.config/lilygo-ui-terminal/settings.conf` |
| An account home can be resolved for the application's real UID | `<account-home>/.config/lilygo-ui-terminal/settings.conf` |
| No home can be resolved | `/tmp/.config/lilygo-ui-terminal/settings.conf` |

The file stores a level rather than a pixel size:

```ini
font_level=1
```

Levels `0`, `1`, and `2` correspond to 14, 18, and 22 px. Missing or invalid
values use the default; out-of-range integer levels are clamped to the nearest
supported level. Changes create parent directories as needed, write a sibling
`.tmp` file, and rename it into place. If saving fails, the new size still
applies to the current session and the status shows `Font setting not saved`.

Preferences belong to the UI process and use its environment and account home.
Overriding the shell's home directory does not change the settings file path.

## Runtime Overrides

Set overrides in the environment that launches the application. Setting them
inside an already-running terminal shell does not change the parent UI.

| Variable | Purpose |
| --- | --- |
| `LILYGO_UI_TERMINAL_CONFIG` | Nonempty path to the UI settings file. |
| `LILYGO_UI_TERMINAL_SHELL` | Nonempty executable path in place of `/bin/sh`; no command arguments or PATH lookup. |
| `LILYGO_UI_TERMINAL_HOME` | Nonempty home directory and initial working directory for the shell. |
| `LILYGO_UI_TERMINAL_PS1` | Nonempty prompt override; invokes the shell with `-i` instead of `-l`. |

The overrides are intended for tests and controlled deployments. A replacement
shell must accept the selected `-l` or `-i` option. An empty prompt override is
ignored: the shell uses `-l` and retains any inherited `PS1` until its startup
files change it.

For example, use a separate preferences file and deterministic prompt while
running the built host simulator:

```sh
LILYGO_UI_TERMINAL_CONFIG="$HOME/.config/lilygo-ui-terminal/demo.conf" \
LILYGO_UI_TERMINAL_PS1='terminal$ ' \
  ./build/host-simulator/lilygo-ui-terminal
```

## Troubleshooting

| Message or symptom | Action |
| --- | --- |
| `Shell exited` or `Shell terminated` | The shell has ended. Press `Restart` to create a new session. |
| `PTY unavailable` or `PTY setup failed` | Check PTY support and permissions; on Linux, verify that `/dev/ptmx` and the `devpts` mount are available. |
| `Shell exited` immediately after launch | Check that the configured shell executable exists and can be run by the UI process's user. |
| `Font setting not saved` | Check the UI process's settings path and directory permissions. Shell permissions do not determine who saves this file. |
| `Clear` leaves a blank screen | Clear only resets the local buffer. Enter another command or use the shell's redraw control. |
| A full-screen program looks incorrect | Check its support for `TERM=ansi` and the compatibility limits above. |

Build and dependency troubleshooting belongs in the
[README build instructions](../README.md#build-and-test).
