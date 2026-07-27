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
