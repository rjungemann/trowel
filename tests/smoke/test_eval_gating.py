"""§3.5b — evaluation is gated by file type.

Run/evaluate is a Turmeric-only action. Non-Turmeric documents grey it out, and
a `build.tur` retargets it at the project build rather than loading the manifest
into the REPL as if it were a script.

`menu.invoke` is the probe: it refuses a disabled action with `action_disabled`,
which is exactly the state under test.
"""

from pathlib import Path

import pytest
from trowel_ctl import ControlError

RUN_BUFFER = ["Run", "Run Buffer"]
RUN_SELECTION = ["Run", "Run Selection"]
BUILD_PROJECT = ["Run", "Build Project"]


def test_untitled_buffer_is_runnable(trowel):
    # An untitled buffer is a Turmeric scratch buffer, so it stays enabled.
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("menu.invoke", {"path": RUN_BUFFER})


def test_turmeric_file_is_runnable(trowel, tmp_path: Path):
    src = tmp_path / "ok.tur"
    src.write_text("(def gate-ok 1)\n")
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(src)})
    trowel.call("menu.invoke", {"path": RUN_BUFFER})


@pytest.mark.parametrize("name", ["notes.md", "data.json", "sample.c", "plain.txt"])
def test_non_turmeric_file_disables_evaluation(trowel, tmp_path: Path, name: str):
    src = tmp_path / name
    src.write_text("hello\n")
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(src)})

    with pytest.raises(ControlError) as ei:
        trowel.call("menu.invoke", {"path": RUN_BUFFER})
    assert ei.value.code == "action_disabled"

    with pytest.raises(ControlError) as ei:
        trowel.call("menu.invoke", {"path": RUN_SELECTION})
    assert ei.value.code == "action_disabled"


def test_gate_follows_the_active_tab(trowel, tmp_path: Path):
    tur = tmp_path / "a.tur"
    tur.write_text("(def gate-tab 1)\n")
    md = tmp_path / "b.md"
    md.write_text("# nope\n")

    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(tur)})
    trowel.call("editor.open", {"path": str(md)})

    # Markdown tab is active — off.
    with pytest.raises(ControlError):
        trowel.call("menu.invoke", {"path": RUN_BUFFER})

    # Switching back to the Turmeric tab turns it on again.
    trowel.call("menu.invoke", {"path": ["View", "Previous Tab"]})
    trowel.call("menu.invoke", {"path": RUN_BUFFER})


def test_save_as_to_non_turmeric_disables_evaluation(trowel, tmp_path: Path):
    src = tmp_path / "a.tur"
    src.write_text("(def gate-saveas 1)\n")
    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(src)})
    trowel.call("menu.invoke", {"path": RUN_BUFFER})

    # Rename it out of Turmeric territory; the gate has to follow the path.
    trowel.call("editor.save_as", {"path": str(tmp_path / "a.txt")})
    with pytest.raises(ControlError) as ei:
        trowel.call("menu.invoke", {"path": RUN_BUFFER})
    assert ei.value.code == "action_disabled"


def test_build_tur_becomes_a_project_build(trowel, tmp_path: Path):
    project = tmp_path / "proj"
    (project / "src").mkdir(parents=True)
    (project / "build.tur").write_text('(defpackage :name "proj" :version "0.1.0")\n')

    trowel.wait_output("turmeric>", timeout_ms=5000)
    trowel.call("editor.open", {"path": str(project / "build.tur")})

    # The buffer-eval entry is gone: the action has retitled itself.
    with pytest.raises(ControlError) as ei:
        trowel.call("menu.invoke", {"path": RUN_BUFFER})
    assert ei.value.code == "no_action"

    # Selection eval means nothing for a manifest.
    with pytest.raises(ControlError) as ei:
        trowel.call("menu.invoke", {"path": RUN_SELECTION})
    assert ei.value.code == "action_disabled"

    # And the project build is offered in its place. It reaches the terminal
    # rather than the REPL, so assert on the banner the runner prints.
    trowel.call("menu.invoke", {"path": BUILD_PROJECT})
    hit = trowel.wait_output("tur build", timeout_ms=5000)
    assert "tur build" in hit["matched"]
