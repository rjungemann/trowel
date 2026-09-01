"""§3.11 — per-window session persistence.

Each test quits through File > Quit (persistence lives in closeEvent) and
relaunches against the same settings, so these assert what actually
survives a restart.
"""


def tab_sets(client):
    """Sorted tab-path sets, one per window — order-independent."""
    lst = client.call("window.list")
    return sorted(sorted(w["tabs"]) for w in lst["windows"])


def test_quit_with_two_windows_restores_both(trowel_session, fixture_files):
    hello, defs = fixture_files / "hello.tur", fixture_files / "defs.tur"

    t = trowel_session.launch([str(hello)])
    t.call("window.new")
    t.call("editor.open", {"path": str(defs)})
    assert t.call("window.list")["count"] == 2
    trowel_session.quit(t)

    t2 = trowel_session.launch()
    assert t2.call("window.list")["count"] == 2
    # Each window keeps its own tabs rather than the sets being merged.
    assert tab_sets(t2) == sorted([[str(hello)], [str(defs)]])


def test_closing_a_window_drops_it_from_the_session(trowel_session, fixture_files):
    hello, defs = fixture_files / "hello.tur", fixture_files / "defs.tur"

    t = trowel_session.launch([str(hello)])
    t.call("window.new")
    t.call("editor.open", {"path": str(defs)})
    # Close the active (defs) window, leaving the hello one.
    t.call("menu.invoke", {"path": ["File", "Close Window"]})
    assert t.call("window.list")["count"] == 1
    trowel_session.quit(t)

    t2 = trowel_session.launch()
    assert t2.call("window.list")["count"] == 1
    assert tab_sets(t2) == [[str(hello)]]


def test_single_window_session_round_trips(trowel_session, fixture_files):
    hello, defs = fixture_files / "hello.tur", fixture_files / "defs.tur"

    t = trowel_session.launch([str(hello)])
    t.call("editor.open", {"path": str(defs)})
    trowel_session.quit(t)

    t2 = trowel_session.launch()
    assert t2.call("window.list")["count"] == 1
    # Tab order is preserved, and the active tab is restored.
    assert t2.call("window.list")["windows"][0]["tabs"] == [str(hello), str(defs)]
    assert t2.call("editor.get_text")["path"] == str(defs)


def test_legacy_single_window_settings_migrate(trowel_session, fixture_files):
    """A pre-multi-window settings file becomes one restored window."""
    hello, defs = fixture_files / "hello.tur", fixture_files / "defs.tur"

    ini = trowel_session.settings_ini
    ini.parent.mkdir(parents=True, exist_ok=True)
    ini.write_text(
        "[General]\n"
        f"openBuffers={hello}, {defs}\n"
        "activeBuffer=1\n"
        "replVisible=true\n"
    )

    t = trowel_session.launch()
    assert t.call("window.list")["count"] == 1
    assert t.call("window.list")["windows"][0]["tabs"] == [str(hello), str(defs)]
    assert t.call("editor.get_text")["path"] == str(defs)
    trowel_session.quit(t)

    # The legacy top-level keys give way to the windows array.
    text = ini.read_text()
    assert not any(line.startswith("openBuffers=") for line in text.splitlines())
    assert "[windows]" in text


def test_closing_the_last_window_keeps_its_geometry(trowel_session):
    """Open, resize, close, reopen — and it must still be the resized size.

    `closeEvent` calls `WindowManager::forget(this)` *before* `persistAll()`,
    so the closing window is already out of the registry when the session is
    rewritten. With another window open that is exactly right — closing one
    drops it. With this the *last* window, "what survives" is nothing, so
    `persistAll` wrote a zero-length `windows` array and erased the whole
    session on the way out: geometry, splitter, open buffers, breakpoints.
    Every other restore test quits through File > Quit, which snapshots first,
    so none of them covered the ordinary way a session ends.

    The size is kept well inside the offscreen platform's virtual screen.
    Qt's `restoreGeometry` clamps to the available screen, so asking for
    1040x660 here comes back 798 wide and looks like a restore failure when it
    is a screen-fit one.
    """
    t = trowel_session.launch()
    t.call("window.geometry", {"width": 760, "height": 560})
    t.call("window.set_splitter", {"sizes": [500, 260]})
    before = t.call("window.geometry")
    assert (before["w"], before["h"]) == (760, 560), before

    # Close the one and only window. Not File > Quit — that path snapshots
    # first and is already covered above.
    t.call("menu.invoke", {"path": ["File", "Close Window"]})

    t2 = trowel_session.launch()
    after = t2.call("window.geometry")
    assert (after["w"], after["h"]) == (760, 560), after
    # The splitter is part of the same session blob, so it rides along.
    assert after["splitter"] == before["splitter"], (before, after)


def test_closing_the_last_window_keeps_its_tabs(trowel_session, fixture_files):
    """The same erasure took the open buffers with it."""
    hello = fixture_files / "hello.tur"
    t = trowel_session.launch([str(hello)])
    assert t.call("window.list")["windows"][0]["tabs"] == [str(hello)]

    t.call("menu.invoke", {"path": ["File", "Close Window"]})

    t2 = trowel_session.launch()
    assert tab_sets(t2) == [[str(hello)]]
