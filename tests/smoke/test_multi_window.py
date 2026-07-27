"""§3.9 — multiple windows.

Covers the window registry and File > New Window. The open-routing rules
themselves (a: no window -> new window, b: window -> new tab) are asserted
here only in their (b) form, which is what a live instance can exercise.
"""


def test_launches_with_one_window(trowel):
    lst = trowel.call("window.list")
    assert lst["count"] == 1
    assert lst["windows"][0]["active"] is True


def test_new_window_creates_a_blank_sibling(trowel, fixture_files):
    sample = fixture_files / "hello.tur"
    trowel.call("editor.open", {"path": str(sample)})
    assert trowel.call("editor.get_text")["path"] == str(sample)

    trowel.call("window.new")

    lst = trowel.call("window.list")
    assert lst["count"] == 2
    # The new window is blank and active; the original keeps its file.
    active = [w for w in lst["windows"] if w["active"]]
    assert len(active) == 1
    assert active[0]["file_path"] == ""
    assert sorted(w["file_path"] for w in lst["windows"]) == ["", str(sample)]


def test_new_window_via_file_menu(trowel):
    trowel.call("menu.invoke", {"path": ["File", "New Window"]})
    assert trowel.call("window.list")["count"] == 2


def test_open_routes_into_the_active_window_as_a_tab(trowel, fixture_files):
    """Requirement (b): with a window open, a file opens as a tab in it."""
    trowel.call("window.new")
    assert trowel.call("window.list")["count"] == 2

    sample = fixture_files / "hello.tur"
    trowel.call("editor.open", {"path": str(sample)})

    # No third window was spawned, and the active one now shows the file.
    assert trowel.call("window.list")["count"] == 2
    assert trowel.call("editor.get_text")["path"] == str(sample)


def test_reopening_an_open_file_focuses_its_tab(trowel, fixture_files):
    """The duplicate-files rule: focus the existing tab, don't add a second."""
    hello, defs = fixture_files / "hello.tur", fixture_files / "defs.tur"
    trowel.call("editor.open", {"path": str(hello)})
    trowel.call("editor.open", {"path": str(defs)})
    assert trowel.call("window.list")["windows"][0]["tab_count"] == 2

    trowel.call("editor.open", {"path": str(hello)})

    # A duplicate tab would also have made hello active, so the tab count is
    # what actually distinguishes focusing from re-opening.
    assert trowel.call("editor.get_text")["path"] == str(hello)
    assert trowel.call("window.list")["windows"][0]["tab_count"] == 2


def test_closing_last_window_leaves_app_alive(trowel):
    """macOS: the app outlives its windows, which is what makes rule (a) reachable."""
    import sys
    if sys.platform != "darwin":
        import pytest
        pytest.skip("quitOnLastWindowClosed is only disabled on macOS")

    trowel.call("menu.invoke", {"path": ["File", "Close Window"]})
    assert trowel.call("window.list")["count"] == 0

    # A query must report no_window rather than conjuring a window to answer.
    from trowel_ctl import ControlError
    try:
        trowel.call("editor.get_text")
        raise AssertionError("expected no_window")
    except ControlError as e:
        assert "no_window" in str(e)
    assert trowel.call("window.list")["count"] == 0


def test_open_with_no_window_creates_one(trowel, fixture_files):
    """Requirement (a): no window open + a file opened -> a new window."""
    import sys
    if sys.platform != "darwin":
        import pytest
        pytest.skip("requires the macOS windowless-app lifecycle")

    trowel.call("menu.invoke", {"path": ["File", "Close Window"]})
    assert trowel.call("window.list")["count"] == 0

    sample = fixture_files / "hello.tur"
    trowel.call("editor.open", {"path": str(sample)})

    lst = trowel.call("window.list")
    assert lst["count"] == 1
    assert lst["windows"][0]["file_path"] == str(sample)
    # The file lands in the fresh window's buffer, with no stray Untitled tab.
    assert lst["windows"][0]["tab_count"] == 1


def test_window_menu_lists_and_switches_windows(trowel, fixture_files):
    hello, defs = fixture_files / "hello.tur", fixture_files / "defs.tur"
    trowel.call("editor.open", {"path": str(hello)})
    trowel.call("window.new")
    trowel.call("editor.open", {"path": str(defs)})

    titles = [w["title"] for w in trowel.call("window.list")["windows"]]
    hello_title = next(t for t in titles if "hello.tur" in t)

    # Switching through the menu makes that window the command target.
    trowel.call("menu.invoke", {"path": ["Window", hello_title]})
    assert trowel.call("editor.get_text")["path"] == str(hello)


def test_window_menu_entries_track_titles(trowel, fixture_files):
    """Menu entries must follow the title, not the placeholder a window had
    before it loaded anything."""
    hello = fixture_files / "hello.tur"
    trowel.call("editor.open", {"path": str(hello)})

    title = trowel.call("window.list")["windows"][0]["title"]
    assert "hello.tur" in title
    # Resolving it proves the menu was relisted after the title changed.
    assert trowel.call("menu.invoke", {"path": ["Window", title]})["ok"] is True


def test_each_window_gets_its_own_repl(trowel):
    assert trowel.call("repl.is_running")["running"] is True
    trowel.call("window.new")
    # The new (now active) window answers with its own running REPL.
    assert trowel.call("repl.is_running")["running"] is True
    assert trowel.call("window.list")["count"] == 2
