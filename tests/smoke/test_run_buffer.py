"""§3.5 — Run buffer: the core edit → run → inspect loop."""

from pathlib import Path


def test_run_buffer_loads_definitions(trowel, fixture_files: Path):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(fixture_files / "defs.tur")})
    trowel.call("run.buffer")
    trowel.wait_idle(quiet_ms=400, timeout_ms=5000)
    trowel.send("smoke-x")
    hit = trowel.wait_output("42", timeout_ms=3000)
    assert "42" in hit["matched"]


def test_run_buffer_untitled_gets_scratched(trowel):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.type("(def smoke-untitled 99)")
    trowel.call("run.buffer")
    trowel.wait_idle(quiet_ms=400, timeout_ms=5000)
    trowel.send("smoke-untitled")
    hit = trowel.wait_output("99", timeout_ms=3000)
    assert "99" in hit["matched"]


def test_run_syntax_error_does_not_kill_repl(trowel, fixture_files: Path):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(fixture_files / "syntax_error.tur")})
    trowel.call("run.buffer")
    trowel.wait_idle(quiet_ms=400, timeout_ms=5000)
    assert trowel.call("repl.is_running")["running"] is True
    trowel.send("(+ 1 1)")
    hit = trowel.wait_output("2", timeout_ms=3000)
    assert "2" in hit["matched"]
