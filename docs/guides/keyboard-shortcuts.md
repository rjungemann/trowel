# Keyboard Shortcuts

All Trowel shortcuts are registered in `src/app/main_window.cpp` (`setupMenus()`).
On macOS, `Ctrl` maps to `Cmd` for the platform-standard sequences
(`QKeySequence::New`, `Save`, `Quit`, etc.).

## File

| Shortcut | Action |
| --- | --- |
| `Ctrl+N` | New buffer |
| `Ctrl+O` | Open file… |
| `Ctrl+Shift+O` | Open directory… |
| `Ctrl+S` | Save |
| `Ctrl+Shift+S` | Save As… |
| `Ctrl+W` | Close current tab |
| `Ctrl+Q` / `Alt+F4` | Quit |

## View

| Shortcut | Action |
| --- | --- |
| `Ctrl+,` | Pick font… |
| `Ctrl+Tab` | Next tab |
| `Ctrl+Shift+Tab` | Previous tab |

## Run

| Shortcut | Action |
| --- | --- |
| `Ctrl+R` | Run buffer (evaluate file) |
| `Ctrl+Shift+E` | Run selection |
| `Ctrl+Shift+R` | Restart REPL |
| `Ctrl+E` | Focus editor |
| `Ctrl+T` | Focus REPL |
| ``Ctrl+` `` | Toggle focus between editor and REPL |

## Directory view

Active when the directory list has focus.

| Shortcut | Action |
| --- | --- |
| `Left` | Navigate to parent directory |
| `Right` | Activate current item |
| `Esc` | Exit path-edit mode |

## Terminal / REPL

The REPL pane forwards keystrokes to the underlying PTY, so shortcuts follow
standard terminal conventions:

- `Ctrl+A`–`Ctrl+Z` are sent as control characters (e.g. `Ctrl+C` = `\x03`).
- `Ctrl+C` / `Cmd+C` copies when a selection exists.
- `Ctrl+V` / `Cmd+V` pastes from the clipboard.
- Arrow keys, `Home`, `End`, `Delete`, `Tab`, and `Enter` emit VT100 escape
  sequences.

## Editor (Scintilla)

The editor pane is a [Scintilla](https://www.scintilla.org/) widget and inherits
its full built-in keymap (cursor movement, selection, indentation, undo/redo,
etc.). For the complete list see the upstream reference:

- **SciTE / Scintilla key bindings:** <https://www.scintilla.org/SciTEDoc.html#KeyBindings>
- **Scintilla default key definitions:** <https://www.scintilla.org/ScintillaDoc.html#KeyDefinitions>
