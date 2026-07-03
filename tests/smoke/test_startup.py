"""§3.1 — startup, window, focus."""


def test_launches_with_empty_buffer(trowel):
    r = trowel.call("editor.get_text")
    assert r["text"] == ""
    assert r["modified"] is False
    assert trowel.call("repl.is_running")["running"] is True


def test_repl_prompt_appears_on_launch(trowel):
    hit = trowel.wait_output("turmeric>", timeout_ms=5000)
    assert hit["matched"] == "turmeric>"


def test_focus_toggle(trowel):
    trowel.call("window.focus", {"pane": "editor"})
    trowel.type("a")
    assert trowel.call("editor.get_text")["text"] == "a"
    # Focus terminal — editor.type still targets editor widget directly,
    # so we assert focus itself succeeded.
    assert trowel.call("window.focus", {"pane": "terminal"})["ok"] is True


def test_window_geometry_reports_fields(trowel):
    g = trowel.call("window.geometry")
    for k in ("x", "y", "w", "h", "splitter", "title", "file_path"):
        assert k in g, f"missing {k}"
    assert isinstance(g["splitter"], list) and len(g["splitter"]) == 2
    assert "Trowel" in g["title"]
