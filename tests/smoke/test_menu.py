"""§3.6 — menu & shortcut equivalence."""

import pytest
from trowel_ctl import ControlError


def test_menu_invoke_run_buffer(trowel):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.type("(def smoke-menu 11)")
    trowel.call("menu.invoke", {"path": ["Run", "Run Buffer"]})
    trowel.wait_idle(quiet_ms=400, timeout_ms=5000)
    trowel.send("smoke-menu")
    hit = trowel.wait_output("11", timeout_ms=3000)
    assert "11" in hit["matched"]


def test_menu_unknown_action_errors(trowel):
    with pytest.raises(ControlError) as ei:
        trowel.call("menu.invoke", {"path": ["Nope", "Nada"]})
    assert ei.value.code == "no_action"


def test_menu_restart_repl(trowel):
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("menu.invoke", {"path": ["Run", "Restart REPL"]})
    trowel.wait_output("Turmeric v", timeout_ms=5000)
