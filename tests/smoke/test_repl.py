"""§3.4 — REPL interaction."""


def test_repl_echoes_result(trowel):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.send("(+ 1 2)")
    hit = trowel.wait_output("3", timeout_ms=3000)
    assert "3" in hit["matched"]


def test_repl_multiple_expressions(trowel):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.send("(+ 40 2)")
    trowel.wait_output("42", timeout_ms=3000)
    trowel.send("(- 10 3)")
    hit = trowel.wait_output("7", timeout_ms=3000)
    assert "7" in hit["matched"]


def test_repl_restart_changes_state(trowel):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("repl.restart")
    # After restart, the banner reappears.
    trowel.wait_output("Turmeric v", timeout_ms=5000)
    assert trowel.call("repl.is_running")["running"] is True


def test_repl_survives_syntax_error(trowel):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    # Unbalanced close-paren — a complete but invalid expression.
    trowel.send(")))")
    trowel.wait_idle(quiet_ms=300, timeout_ms=3000)
    # A real expression should still evaluate afterwards.
    trowel.send("(+ 5 5)")
    hit = trowel.wait_output("10", timeout_ms=3000)
    assert "10" in hit["matched"]


def test_repl_ctrl_c_returns_prompt(trowel):
    """Ctrl-C on a running REPL should get back to a prompt."""
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.repl_press("c", mods=["ctrl"])
    trowel.wait_idle(quiet_ms=300, timeout_ms=3000)
    # Ensure REPL is still alive and responds.
    trowel.send("(+ 1 1)")
    hit = trowel.wait_output("2", timeout_ms=3000)
    assert "2" in hit["matched"]
