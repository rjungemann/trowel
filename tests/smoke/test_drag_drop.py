"""§3.10 — drag and drop (requirement d).

`window.drop` synthesizes a real drag/drop against the window, so these
exercise MainWindow::dragEnterEvent/dropEvent rather than calling the
open path directly.
"""

import pytest

from trowel_ctl import ControlError


def tabs(trowel):
    return trowel.call("window.list")["windows"][0]["tabs"]


def test_drop_replaces_the_current_tab(trowel, fixture_files):
    hello, defs = fixture_files / "hello.tur", fixture_files / "defs.tur"
    trowel.call("editor.open", {"path": str(hello)})
    assert tabs(trowel) == [str(hello)]

    trowel.call("window.drop", {"paths": [str(defs)]})

    # Replace, not add — the tab count staying at 1 is the whole point.
    assert tabs(trowel) == [str(defs)]
    assert trowel.call("editor.get_text")["path"] == str(defs)


def test_drop_of_several_files_replaces_then_adds(trowel, fixture_files):
    hello, defs = fixture_files / "hello.tur", fixture_files / "defs.tur"
    trowel.call("editor.open", {"path": str(hello)})

    trowel.call("window.drop", {"paths": [str(defs), str(hello)]})

    # First replaced the current tab; the rest opened as additional tabs.
    assert tabs(trowel) == [str(defs), str(hello)]


def test_drop_onto_empty_tab_fills_it(trowel, fixture_files):
    hello = fixture_files / "hello.tur"
    assert tabs(trowel) == [""]  # fresh Untitled

    trowel.call("window.drop", {"paths": [str(hello)]})

    assert tabs(trowel) == [str(hello)]


def test_drop_of_already_open_file_does_not_duplicate(trowel, fixture_files):
    hello, defs = fixture_files / "hello.tur", fixture_files / "defs.tur"
    trowel.call("editor.open", {"path": str(hello)})
    trowel.call("editor.open", {"path": str(defs)})
    before = tabs(trowel)

    trowel.call("window.drop", {"paths": [str(hello)]})

    # Focus-existing-tab wins over replace, so no tab is lost or duplicated.
    assert tabs(trowel) == before
    assert trowel.call("editor.get_text")["path"] == str(hello)


def test_drop_without_local_files_is_rejected(trowel):
    before = tabs(trowel)
    with pytest.raises(ControlError):
        trowel.call("window.drop", {"paths": []})
    assert tabs(trowel) == before


def test_editor_commands_refuse_on_a_directory_tab(trowel, fixture_files):
    """A non-editor tab must produce `no_editor`, not a crash.

    MainWindow::editorView() is null for a directory browser; the control
    handlers used to dereference it, which killed the process.
    """
    trowel.call("window.drop", {"paths": [str(fixture_files)]})
    assert tabs(trowel) == [str(fixture_files)]

    for cmd, args in [
        ("editor.get_text", None),
        ("editor.get_cursor", None),
        ("editor.set_text", {"text": "x"}),
        ("editor.type", {"text": "x"}),
        ("editor.save", None),
        ("window.focus", {"pane": "editor"}),
    ]:
        with pytest.raises(ControlError, match="no_editor"):
            trowel.call(cmd, args)

    # Still alive and answering.
    assert trowel.call("ping")["pong"] is True
