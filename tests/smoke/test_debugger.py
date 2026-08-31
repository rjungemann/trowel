"""Phase 1 of debugger-support.md — headless debug session.

Drives `debug.start` / `debug.status` / `debug.stop` through the control
socket against a real fixture. Phase 1 is "run the current file under the
interpreter with captured output": no breakpoints, no stack, no stepping.
The test proves transport, lifecycle, and teardown before any UI is built on
top of it.
"""

from pathlib import Path

import pytest

from trowel_ctl import ControlError

# A debug session spawns a real `tur dap` subprocess that runs a real program.
DEBUG_MS = 15000


def _open(trowel, path: Path) -> None:
    trowel.call("editor.open", {"path": str(path)})


def test_debug_runs_a_program_and_reports_its_exit_code(trowel, fixture_files: Path):
    """The happy path: launch, run to completion, see the exit code."""
    _open(trowel, fixture_files / "trace_trivial.tur")
    trowel.call("debug.start", {"timeout_ms": DEBUG_MS})

    # Poll until the session reports terminated. The program runs to
    # completion asynchronously; debug.status reflects the live state.
    import time
    deadline = time.time() + DEBUG_MS / 1000.0
    last = {}
    while time.time() < deadline:
        last = trowel.call("debug.status")
        if last.get("state") in ("terminated", "idle") and not last.get("running"):
            break
        time.sleep(0.05)
    assert last.get("running") is False, last
    # trace_trivial's main evaluates `(+ 1 2)` and returns 3; `tur dap`
    # propagates the program's real return value (constraint 10), so the exit
    # code is 3, not a fake 0 from a user stop.
    assert last.get("exit_code") == 3, last


def test_debug_captures_debuggee_output(trowel, fixture_files: Path):
    """`output` events stream into the session's output buffer."""
    # A program that prints something visible.
    prog = fixture_files / "debug_print.tur"
    prog.write_text('(defn main [] : int\n  (println "hello-from-debuggee")\n  0)\n')
    try:
        _open(trowel, prog)
        trowel.call("debug.start", {"timeout_ms": DEBUG_MS})
        import time
        deadline = time.time() + DEBUG_MS / 1000.0
        out = ""
        while time.time() < deadline:
            st = trowel.call("debug.status")
            out = st.get("output", "")
            if "hello-from-debuggee" in out:
                break
            if not st.get("running"):
                break
            time.sleep(0.05)
        assert "hello-from-debuggee" in out, st
    finally:
        prog.unlink(missing_ok=True)


def test_debug_stop_kills_a_running_session(trowel, fixture_files: Path):
    """stop_on_entry pauses; debug.stop then reports not-running."""
    # A program that loops so it stays alive long enough to stop.
    prog = fixture_files / "debug_loop.tur"
    prog.write_text(
        '(defn main [] : int\n  (let [^mut i 0]\n'
        '    (while (< i 1000000)\n      (set! i (+ i 1))))\n  0)\n')
    try:
        _open(trowel, prog)
        # stop_on_entry so we are paused (and thus stoppable) immediately.
        trowel.call("debug.start", {"stop_on_entry": True, "timeout_ms": DEBUG_MS})
        import time
        # Wait for paused.
        deadline = time.time() + 5.0
        paused = False
        while time.time() < deadline:
            st = trowel.call("debug.status")
            if st.get("state") == "paused":
                paused = True
                break
            time.sleep(0.05)
        assert paused, st

        trowel.call("debug.stop")
        st = trowel.call("debug.status")
        assert st.get("running") is False, st
        # A user-stopped session reports -1, not a fake 0 (constraint 10).
        assert st.get("exit_code") == -1, st
    finally:
        prog.unlink(missing_ok=True)


def test_debug_refuses_a_non_turmeric_file(trowel, fixture_files: Path):
    """The gate mirrors Run Buffer: a .py is not debuggable."""
    _open(trowel, fixture_files / "sample.py")
    with pytest.raises(ControlError) as exc:
        trowel.call("debug.start")
    assert exc.value.code == "not_debuggable"


# --- Phase 3: breakpoints and stepping -----------------------------------

def _wait_state(trowel, target, timeout=8.0):
    """Poll debug.status until `state` matches `target` or time out."""
    import time
    deadline = time.time() + timeout
    while time.time() < deadline:
        st = trowel.call("debug.status")
        if st.get("state") == target:
            return st
        time.sleep(0.05)
    return trowel.call("debug.status")


def test_breakpoint_toggle_round_trips_through_the_model(trowel, fixture_files: Path):
    """debug.breakpoint.toggle adds then removes; debug.breakpoints reads it."""
    path = fixture_files / "trace_trivial.tur"
    _open(trowel, path)
    r = trowel.call("debug.breakpoint.toggle", {"path": str(path), "line": 7})
    assert r["added"] is True
    bps = trowel.call("debug.breakpoints", {"path": str(path)})["breakpoints"]
    assert len(bps) == 1
    assert bps[0]["line"] == 7
    assert bps[0]["enabled"] is True
    r = trowel.call("debug.breakpoint.toggle", {"path": str(path), "line": 7})
    assert r["added"] is False
    bps = trowel.call("debug.breakpoints", {"path": str(path)})["breakpoints"]
    assert len(bps) == 0


def test_breakpoint_stops_a_running_program(trowel, fixture_files: Path):
    """A breakpoint set before launch causes a `stopped` event."""
    # A program with a loop so it would run past the breakpoint without one.
    prog = fixture_files / "debug_bp.tur"
    prog.write_text(
        '(defn main [] : int\n'
        '  (let [^mut i 0]\n'
        '    (while (< i 100)\n'
        '      (set! i (+ i 1))))\n'
        '  0)\n')
    try:
        _open(trowel, prog)
        # Set a breakpoint on the loop body line (4: the set!).
        trowel.call("debug.breakpoint.toggle", {"path": str(prog), "line": 5})
        trowel.call("debug.start", {"timeout_ms": DEBUG_MS})
        st = _wait_state(trowel, "paused", timeout=8.0)
        assert st["state"] == "paused", st
        trowel.call("debug.stop")
    finally:
        prog.unlink(missing_ok=True)


def test_step_over_advances_and_stays_paused(trowel, fixture_files: Path):
    """stop_on_entry pauses; step_over keeps us paused."""
    prog = fixture_files / "debug_step.tur"
    prog.write_text(
        '(defn main [] : int\n'
        '  (let [^mut i 0]\n'
        '    (set! i (+ i 1))\n'
        '    (set! i (+ i 1)))\n'
        '  0)\n')
    try:
        _open(trowel, prog)
        trowel.call("debug.start", {"stop_on_entry": True, "timeout_ms": DEBUG_MS})
        st = _wait_state(trowel, "paused", timeout=8.0)
        assert st["state"] == "paused", st
        trowel.call("debug.step", {"kind": "over"})
        st = _wait_state(trowel, "paused", timeout=5.0)
        assert st["state"] == "paused", st
        trowel.call("debug.continue")
        _wait_state(trowel, "idle", timeout=8.0)
    finally:
        prog.unlink(missing_ok=True)


def test_continue_runs_to_completion(trowel, fixture_files: Path):
    """stop_on_entry then continue reaches the end and exits."""
    _open(trowel, fixture_files / "trace_trivial.tur")
    trowel.call("debug.start", {"stop_on_entry": True, "timeout_ms": DEBUG_MS})
    st = _wait_state(trowel, "paused", timeout=8.0)
    assert st["state"] == "paused", st
    trowel.call("debug.continue")
    st = _wait_state(trowel, "idle", timeout=8.0)
    assert st["state"] in ("idle", "terminated"), st
    assert st.get("running") is False
