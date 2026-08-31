"""T1 of editor-intelligence.md — record a trace and explain what came back.

T1's whole purpose is to answer §5.3's three questions against real fixtures
*before* any UI is built on top of the answers. Each case gets its own test
because each has to produce a **specific** message rather than an empty panel.
A low step count is routinely not about the program at all — under per-line
stepping it usually means the source is densely written — and a user who sees
a bare step count concludes the feature is broken.

The exit code is deliberately never asserted on. `tur trace` propagates the
traced program's own return value — a `main` returning 144 exits 144 — so it
cannot distinguish success from failure. See TraceSummary's header comment.

Step counts are asserted as ranges, never as exact values: Turmeric
`e7140c97c` changed the recorder from per-line to per-expression granularity,
which multiplies every count. The tests read `granularity` off the summary and
branch where the meaning actually differs.
"""

from pathlib import Path

import pytest

from trowel_ctl import ControlError

# A recording spawns a real subprocess that runs a real program.
TRACE_MS = 20000

TRACE_MENU = ["Run", "Trace Buffer"]


def _trace(trowel, path: Path) -> dict:
    trowel.call("editor.open", {"path": str(path)})
    return trowel.call("trace.run", {"timeout_ms": TRACE_MS})


def test_a_loop_records_a_plausible_step_count(trowel, fixture_files: Path):
    """The good case: an interpreted `while` in `main` records every iteration."""
    r = _trace(trowel, fixture_files / "trace_loop.tur")

    assert r["outcome"] == "recorded"
    assert r["parsed"] is True
    # The fixture loops 200 times; the exact multiple is the tracer's business,
    # but it must be in the hundreds rather than in the single digits.
    assert r["steps"] > 100, r
    assert r["enters"] >= 1
    assert r["truncated"] is False
    assert "Recorded" in r["explanation"]


def test_a_trivial_program_records_little_and_says_why(trowel, fixture_files: Path):
    """A short recording that really is about the program.

    `main` exists, runs, and evaluates three expressions. That has to read
    differently from "no main" and from "did not compile", and differently
    again from a truncated recording — it is the whole run.
    """
    r = _trace(trowel, fixture_files / "trace_trivial.tur")

    assert r["outcome"] == "short_recording"
    assert 0 < r["steps"] < 16, r
    assert r["enters"] >= 1
    # Never blames type annotations: that explanation was wrong and is gone.
    assert "annotation" not in r["explanation"]
    assert "compiles" not in r["explanation"]


def test_the_same_program_spread_over_lines_records_far_more(
    trowel, fixture_files: Path
):
    """The paired fixture that proves layout is the variable.

    `trace_dense.tur` and `trace_spread.tur` are the same `fib`, identically
    annotated, differing only in where the newlines fall. Under the per-line
    stepping of v0.42.0 one recorded 2 steps at depth 1 and the other 531 at
    depth 10 — fidelity depended on formatting, which is the bug.

    From Turmeric `e7140c97c` (v0.42.1) they agree exactly. Both eras are
    asserted so this test documents the fix rather than merely surviving it.
    """
    dense = _trace(trowel, fixture_files / "trace_dense.tur")
    spread = _trace(trowel, fixture_files / "trace_spread.tur")

    if spread["granularity"] == "per expression":
        # Layout stops mattering: identical steps, enters and call depth.
        assert dense["steps"] == spread["steps"], (dense, spread)
        assert dense["enters"] == spread["enters"]
        assert dense["peak_depth"] == spread["peak_depth"] > 1
        return

    assert spread["steps"] > dense["steps"] * 50, (dense, spread)
    # Call depth is the sharper signal: the dense spelling never sees the
    # recursion at all.
    assert dense["peak_depth"] == 1
    assert spread["peak_depth"] > 1


def test_a_file_with_no_main_says_so(trowel, fixture_files: Path):
    """§5.3.2. Records nothing while still printing the program's output."""
    r = _trace(trowel, fixture_files / "trace_nomain.tur")

    assert r["outcome"] == "no_main"
    assert r["steps"] == 0
    assert r["enters"] == 0
    # The program still ran — its output was captured even though no step was.
    assert r["output_bytes"] > 0
    assert "main" in r["explanation"]


def test_a_file_that_does_not_compile_reports_the_error(trowel, fixture_files: Path):
    """§5.3.3, and the case that most needs the text rather than the exit code.

    `tur trace` prints `trace: 0 steps, …` for a file that failed to compile,
    so a client keying off the summary alone would report this identically to
    the no-main case. The error line is what tells them apart.
    """
    r = _trace(trowel, fixture_files / "trace_broken.tur")

    assert r["outcome"] == "compile_error"
    assert "does not compile" in r["explanation"]
    # Explicitly *not* misreported as the no-main case, which is what would
    # happen if the step count were consulted first.
    assert r["outcome"] != "no_main"


def test_the_four_cases_are_mutually_distinguishable(trowel, fixture_files: Path):
    """The gate T1 exists to satisfy: each case produces a *specific* message.

    Asserting the four explanations are pairwise distinct is what stops a later
    refactor from collapsing two of them into one generic sentence.
    """
    outcomes = {}
    for name in ("trace_loop", "trace_trivial", "trace_nomain", "trace_broken"):
        r = _trace(trowel, fixture_files / f"{name}.tur")
        outcomes[name] = (r["outcome"], r["explanation"])

    labels = [o for o, _ in outcomes.values()]
    assert len(set(labels)) == 4, outcomes
    explanations = [e for _, e in outcomes.values()]
    assert len(set(explanations)) == 4, outcomes


def test_trace_is_disabled_in_a_non_turmeric_buffer(trowel, fixture_files: Path):
    trowel.call("editor.open", {"path": str(fixture_files / "sample.py")})
    with pytest.raises(ControlError) as ei:
        trowel.call("menu.invoke", {"path": TRACE_MENU})
    assert ei.value.code == "action_disabled"


def test_trace_refuses_an_unsaved_buffer(trowel):
    """`tur trace` reads from disk, so an unsaved buffer would trace nothing —
    or worse, a stale previous version."""
    trowel.type("(defn main [] : int 0)")
    r = trowel.call("trace.run", {"timeout_ms": TRACE_MS})
    assert r["outcome"] == "failed"
    assert "Save" in r["explanation"]
