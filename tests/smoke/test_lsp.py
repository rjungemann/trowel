"""Language server integration — diagnostics, completion, hover.

Every wait goes through `wait.diagnostics` or a handler-side timeout; no test
sleeps. See docs/plans/smoke-tests.md.

Timeouts stay under the control socket's own 10s read timeout (see
tests/support/trowel_ctl.py) — a longer `timeout_ms` would surface as a socket
timeout rather than the handler's `timeout` error.
"""

from pathlib import Path

import pytest

from trowel_ctl import ControlError

# The first request pays for process spawn + initialize + a full compile.
WAIT_MS = 8000


def _require_server(trowel) -> dict:
    status = trowel.call("lsp.status")
    if not status["enabled"]:
        pytest.skip("language server disabled in settings")
    if not status["server_path"]:
        pytest.skip("no `tur` binary available to run `tur lsp`")
    return status


def _open_and_analyze(trowel, path: Path, min_count: int = 0) -> dict:
    """Open `path` and block until the server publishes a batch for it."""
    trowel.call("editor.open", {"path": str(path)})
    return trowel.call("wait.diagnostics", {"min_count": min_count, "timeout_ms": WAIT_MS})


def test_status_reports_a_server_binary(trowel):
    status = _require_server(trowel)
    assert status["server_path"].endswith("tur")
    assert status["state"] in {"stopped", "starting", "ready"}


def test_syntax_error_produces_a_diagnostic(trowel, fixture_files: Path):
    _require_server(trowel)
    r = _open_and_analyze(trowel, fixture_files / "syntax_error.tur", min_count=1)

    assert r["count"] >= 1
    first = r["diagnostics"][0]
    assert first["message"]
    assert first["severity"] in (1, 2, 3, 4)
    # The fixture is a single unterminated line, so the range must land on it.
    assert first["start_line"] == 0


def test_diagnostic_is_painted_in_the_editor(trowel, fixture_files: Path):
    """The squiggle and gutter marker actually reach Scintilla.

    lsp.decorations reads back out of the widget, so this fails if
    setDiagnostics stops painting even while the manager still holds the data.
    """
    _require_server(trowel)
    r = _open_and_analyze(trowel, fixture_files / "syntax_error.tur", min_count=1)
    first = r["diagnostics"][0]

    d = trowel.call("lsp.decorations")
    assert d["error_ranges"], d
    assert d["error_marker_lines"] == [first["start_line"]]

    # The squiggle must be non-empty and sit inside the document.
    start, end = d["error_ranges"][0]["start"], d["error_ranges"][0]["end"]
    assert end > start
    assert end <= len(trowel.call("editor.get_text")["text"].encode("utf-8"))


def test_decorations_clear_when_the_error_is_fixed(trowel, fixture_files: Path):
    _require_server(trowel)
    _open_and_analyze(trowel, fixture_files / "syntax_error.tur", min_count=1)
    assert trowel.call("lsp.decorations")["error_ranges"]

    trowel.call("editor.set_text", {"text": '(def fixed "ok")\n'})
    trowel.call("wait.diagnostics", {"min_count": 0, "max_count": 0, "timeout_ms": WAIT_MS})

    d = trowel.call("lsp.decorations")
    assert d["error_ranges"] == []
    assert d["error_marker_lines"] == []


def test_clean_file_has_no_diagnostics(trowel, fixture_files: Path):
    _require_server(trowel)
    # min_count 0 still waits for a real publish — wait.diagnostics checks
    # hasPublishedFor, so this cannot pass before the server has analyzed.
    r = _open_and_analyze(trowel, fixture_files / "hello.tur")
    assert r["count"] == 0


def test_diagnostics_clear_when_the_error_is_fixed(trowel, fixture_files: Path):
    _require_server(trowel)
    assert _open_and_analyze(trowel, fixture_files / "syntax_error.tur", min_count=1)["count"] >= 1

    trowel.call("editor.set_text", {"text": '(def fixed "ok")\n'})
    # max_count 0 blocks until the repaired buffer publishes an empty batch —
    # without it the already-published error batch would satisfy min_count 0
    # immediately and the assert would race.
    r = trowel.call("wait.diagnostics",
                    {"min_count": 0, "max_count": 0, "timeout_ms": WAIT_MS})
    assert r["count"] == 0


def _completions_at_end(trowel, extra_text: str = "") -> dict:
    """Append `extra_text`, park the caret at the end, and complete there."""
    text = trowel.call("editor.get_text")["text"] + extra_text
    trowel.call("editor.set_text", {"text": text})
    trowel.call("editor.set_cursor", {"pos": len(text.encode("utf-8"))})
    return trowel.call("lsp.completions", {"timeout_ms": WAIT_MS})


def test_completion_returns_symbols(trowel, fixture_files: Path):
    _require_server(trowel)
    _open_and_analyze(trowel, fixture_files / "defs.tur")

    # Offset 0 is the obvious thing a client does — ask for completions right
    # after opening a file. It used to return nothing (the server derived its
    # prefix with a helper built for hover, which stepped right and filtered
    # every candidate away), which was indistinguishable from "no matches".
    # Fixed in Turmeric v0.32.2; pinned here so it stays fixed.
    trowel.call("editor.set_cursor", {"pos": 0})
    r = trowel.call("lsp.completions", {"timeout_ms": WAIT_MS})
    assert r["count"] > 0
    assert all(isinstance(label, str) and label for label in r["labels"])


def test_completion_survives_an_unbalanced_buffer(trowel, fixture_files: Path):
    """Typing `(` unbalances the buffer — completion must still work.

    Not parsing is the *normal* state mid-keystroke, and the server builds its
    symbol index from a successful compile. It used to return nothing here,
    which meant completion went silent exactly when it was wanted. v0.32.2
    retains the last good index (and falls back to stdlib for a file that has
    never parsed), so the buffer's own defs still come back.
    """
    _require_server(trowel)
    _open_and_analyze(trowel, fixture_files / "defs.tur")

    r = _completions_at_end(trowel, "\n(smoke")
    assert any("smoke-" in label for label in r["labels"]), r["labels"][:20]


def test_completion_includes_document_symbols(trowel, fixture_files: Path):
    _require_server(trowel)
    _open_and_analyze(trowel, fixture_files / "defs.tur")

    # Caret inside `smoke-x` in `(def smoke-x 42)`. Completing the symbol under
    # the caret surfaces the buffer's own defs; the unfiltered list is capped
    # at 200 server-side and would bury them.
    trowel.call("editor.set_cursor", {"pos": 5})
    r = trowel.call("lsp.completions", {"timeout_ms": WAIT_MS})
    assert any("smoke-" in label for label in r["labels"]), r["labels"][:20]


def test_hover_reports_the_symbol(trowel, fixture_files: Path):
    _require_server(trowel)
    _open_and_analyze(trowel, fixture_files / "hello.tur")

    # Caret inside `greeting` in `(def greeting "hello, world")`.
    trowel.call("editor.set_cursor", {"pos": 7})
    r = trowel.call("lsp.hover", {"timeout_ms": WAIT_MS})
    assert "greeting" in r["text"]


def test_unsaved_buffer_is_skipped(trowel):
    _require_server(trowel)
    trowel.type("(def x 1)")
    with pytest.raises(ControlError) as excinfo:
        trowel.call("wait.diagnostics", {"min_count": 0, "timeout_ms": 2000})
    assert excinfo.value.code == "no_uri"


def test_non_turmeric_buffer_is_skipped(trowel, fixture_files: Path):
    _require_server(trowel)
    trowel.call("editor.open", {"path": str(fixture_files / "sample.json")})
    # A JSON buffer is never registered, so no batch ever arrives for it.
    with pytest.raises(ControlError) as excinfo:
        trowel.call("wait.diagnostics", {"min_count": 0, "timeout_ms": 1500})
    assert excinfo.value.code == "timeout"


def test_restart_recovers(trowel, fixture_files: Path):
    _require_server(trowel)
    assert _open_and_analyze(trowel, fixture_files / "syntax_error.tur", min_count=1)["count"] >= 1

    trowel.call("lsp.restart")
    trowel.call("editor.set_text", {"text": "(def still broken (\n"})
    r = trowel.call("wait.diagnostics", {"min_count": 1, "timeout_ms": WAIT_MS})
    assert r["count"] >= 1
