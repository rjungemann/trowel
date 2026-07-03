"""§3.8 — clean shutdown."""


def test_repl_quit_exits(trowel):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.send(":quit")
    result = trowel.call("wait.process_exit", {"timeout_ms": 3000})
    assert result["exit_code"] == 0
    assert trowel.call("repl.is_running")["running"] is False
