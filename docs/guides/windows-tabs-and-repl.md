# Windows, Tabs & the REPL

How Trowel decides where a file opens, what closing things does, and where
each window's REPL is rooted.

## Where a file opens

| Situation | What happens |
| --- | --- |
| No window open | Opens in a **new window** |
| A window is open | Opens as a **new tab** in that window |
| The file is already open in that window | **Focuses** its existing tab — no duplicate |
| File dropped onto a window | **Replaces the current tab** |

This holds however you open the file — from the **File** menu, the `trowel`
command line, Finder, or a file manager. On macOS a second `trowel foo.tur`
is routed to the running app rather than starting a new copy; on Linux the
same thing happens over a per-session socket.

Opening several files at once gives you one tab each.

### Drag and drop

Dropping a file **replaces the tab you are looking at**, rather than adding
one. This is deliberate, and differs from VS Code — it makes drag-and-drop a
way to *swap* what you are working on instead of accumulating tabs.

Some details worth knowing:

- If the current tab has unsaved changes, you are asked first; cancelling
  leaves the tab alone.
- Dropping a file already open in that window focuses its tab instead, so a
  drop never leaves you with the same file twice.
- Dropping **several** files replaces the current tab with the first and
  opens the rest as new tabs.
- Dropping a **directory** turns the current tab into a directory browser.

## Windows and tabs

| Shortcut | Action |
| --- | --- |
| `Ctrl+N` | New tab |
| `Ctrl+Shift+N` | New window |
| `Ctrl+W` | Close tab |
| `Ctrl+Shift+W` | Close window |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous tab |
| `Ctrl+Q` | Quit |

On macOS, `Ctrl` is `Cmd`.

A **new window** starts empty, with its own tabs and its own REPL. The
**Window** menu lists everything open and switches between them.

Closing the **last tab** in a window leaves a fresh empty tab — the window
stays. To close the window itself use `Ctrl+Shift+W` or its close button.

On macOS, closing the last window leaves Trowel running in the dock, the way
most Mac apps behave; clicking the dock icon or opening a document gives you
a window back. On Linux and Windows, closing the last window exits.

## Sessions

Quitting remembers **every window** that was open — its tabs, which tab was
active, its size and position, and whether its REPL pane was showing. The
next launch puts them all back.

Closing a window on purpose removes it from that memory, so it will not
reappear next time. Unsaved (`Untitled`) buffers are not restored, since
there is no file to point at.

Starting Trowel *with* a file — `trowel foo.tur`, or opening a document from
Finder — skips session restore and gives you a window for that file.

## The REPL

Each window runs its **own** `tur repl`, so two windows are two independent
sessions. Where a REPL starts depends on how its window came to be:

- A window **opened on a file** roots its REPL in that file's directory — so
  the REPL follows the project you are working in.
- A **new, empty window** roots its REPL at your home directory — a scratch
  REPL to experiment in.

Each REPL says where it landed when it starts, so you never have to guess:

```
[trowel] tur repl started in ~/projects/foo
```

The working directory is fixed when the REPL starts. It deliberately does
**not** follow you as you switch tabs: a REPL that silently changed
directory underneath you would break relative paths and anything you had in
flight.

When you do want to move it, **Restart REPL** (`Ctrl+Shift+R`) restarts the
REPL in the current file's directory. Restarting clears REPL state — any
definitions you have evaluated are gone — which is why it is an explicit
action rather than something that happens on its own.

## See also

- [Keyboard shortcuts](keyboard-shortcuts.md) — the full key map.
