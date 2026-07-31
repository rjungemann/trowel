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


def test_run_sweet_buffer(trowel, fixture_files: Path):
    # A clean, saved .tur.sweet file loads in place; `tur` picks the
    # sweet-expression reader off the extension.
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(fixture_files / "sweet_hello.tur.sweet")})
    trowel.call("run.buffer")
    trowel.wait_idle(quiet_ms=400, timeout_ms=5000)
    trowel.send("sweet-x")
    hit = trowel.wait_output("=> 7", timeout_ms=3000)
    assert "7" in hit["matched"]


def test_run_dirty_sweet_buffer_keeps_sweet_extension(trowel, fixture_files: Path):
    # The dirty path writes a scratch file instead, which only parses as sweet
    # if it inherits the .tur.sweet suffix — the extension is the only signal,
    # since a `#lang sweet-exp` header is a parse error.
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(fixture_files / "sweet_hello.tur.sweet")})
    trowel.call("editor.set_text", {"text": "def sweet-dirty 21\n"})
    trowel.call("run.buffer")
    trowel.wait_idle(quiet_ms=400, timeout_ms=5000)
    trowel.send("sweet-dirty")
    hit = trowel.wait_output("=> 21", timeout_ms=3000)
    assert "21" in hit["matched"]


def test_run_selection_keeps_the_lang_directive(trowel, tmp_path: Path):
    # The selection starts below line 1, so the `#lang` line is not in it and
    # has to be re-attached. This uses a *layer* (`stringed`, which enables the
    # `#s"..."` literal) rather than a base dialect on purpose: the scratch
    # file's extension can carry the sweet base, but no extension can express a
    # layer, so only re-attaching the directive itself makes this pass.
    src = tmp_path / "sel.tur"
    src.write_text('#lang turmeric stringed\n(def ignored 1)\n(def sel-s #s"hi")\n')
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(src)})
    body = src.read_text()
    start = body.index("(def sel-s")
    trowel.call("editor.set_selection", {"start": start, "end": len(body)})
    trowel.call("run.selection")
    trowel.wait_idle(quiet_ms=400, timeout_ms=5000)
    trowel.send("sel-s")
    trowel.wait_idle(quiet_ms=400, timeout_ms=5000)
    # Asserted against the rendered screen rather than wait_output: the REPL
    # wraps string results in ANSI colour codes, so `=> "hi"` is contiguous
    # only after the terminal has interpreted them.
    screen = trowel.call("repl.get_screen", {"lines": 40})["text"]
    assert '=> "hi"' in screen, screen


def test_run_syntax_error_does_not_kill_repl(trowel, fixture_files: Path):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(fixture_files / "syntax_error.tur")})
    trowel.call("run.buffer")
    trowel.wait_idle(quiet_ms=400, timeout_ms=5000)
    assert trowel.call("repl.is_running")["running"] is True
    trowel.send("(+ 1 1)")
    hit = trowel.wait_output("2", timeout_ms=3000)
    assert "2" in hit["matched"]
