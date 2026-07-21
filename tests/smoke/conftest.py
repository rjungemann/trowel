"""Fixtures for the Trowel smoke suite.

Each test drives a real Trowel process through the control socket. Tests
never sleep — every wait uses `wait.*` with an explicit timeout.
"""

from __future__ import annotations

import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator

import pytest

REPO = Path(__file__).resolve().parents[2]


def _resolve_bin() -> Path:
    """Locate the built trowel binary for the host platform.

    Honors $TROWEL_BIN, then falls back to the per-preset build layout used by
    the Justfile: a macOS .app bundle on Darwin, a plain ELF binary elsewhere.
    """
    override = os.environ.get("TROWEL_BIN")
    if override:
        return Path(override)
    if sys.platform == "darwin":
        for preset in ("macos-debug", "macos-release"):
            cand = REPO / "build" / preset / "trowel.app" / "Contents" / "MacOS" / "trowel"
            if cand.exists():
                return cand
        preset = "macos-debug"
        return REPO / "build" / preset / "trowel.app" / "Contents" / "MacOS" / "trowel"
    for preset in ("linux-debug", "linux-release"):
        cand = REPO / "build" / preset / "trowel"
        if cand.exists():
            return cand
    return REPO / "build" / "linux-debug" / "trowel"


BIN = _resolve_bin()
FIXTURES = Path(__file__).parent / "fixtures"
ARTIFACTS = Path(__file__).parent / "artifacts"

sys.path.insert(0, str(REPO / "tests" / "support"))
from trowel_ctl import TrowelCtl, ControlError  # noqa: E402


# ---------- process launcher ----------


@dataclass
class TrowelProc:
    proc: subprocess.Popen
    socket_path: str
    stdout_path: Path
    home: Path

    def stop(self) -> None:
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=2)


def _wait_for_socket(path: str, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                s.connect(path)
                s.close()
                return
            except OSError:
                pass
        time.sleep(0.05)
    raise TimeoutError(f"control socket never appeared at {path}")


def _launch_trowel(tmp_path: Path) -> TrowelProc:
    if not BIN.exists():
        pytest.skip(f"trowel binary not built at {BIN}")

    sock = f"/tmp/trowel-smoke-{uuid.uuid4().hex}.sock"
    home = tmp_path / "home"
    home.mkdir(exist_ok=True)
    stdout_path = tmp_path / "trowel.stdout"

    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["HOME"] = str(home)
    env["XDG_CONFIG_HOME"] = str(home / ".config")
    env["XDG_CACHE_HOME"] = str(home / ".cache")
    # Force English so REPL banners are predictable.
    env.setdefault("LC_ALL", "en_US.UTF-8")

    proc = subprocess.Popen(
        [str(BIN), f"--control-socket-path={sock}"],
        stdout=stdout_path.open("wb"),
        stderr=subprocess.STDOUT,
        env=env,
    )
    try:
        _wait_for_socket(sock)
    except Exception:
        proc.terminate()
        raise
    return TrowelProc(proc=proc, socket_path=sock, stdout_path=stdout_path, home=home)


# ---------- helpers used inside fixtures ----------


class Client:
    """Convenience wrapper around TrowelCtl with dump-on-failure."""

    def __init__(self, ctl: TrowelCtl, name_hint: str) -> None:
        self._ctl = ctl
        self._name_hint = name_hint

    def call(self, cmd: str, args: dict[str, Any] | None = None) -> Any:
        return self._ctl.call(cmd, args)

    # focused shorthands (§2 harness)
    def type(self, text: str) -> None: self.call("editor.type", {"text": text})
    def press(self, key: str, mods: list[str] | None = None) -> None:
        args: dict[str, Any] = {"key": key}
        if mods: args["mods"] = mods
        self.call("editor.press", args)
    def send(self, text: str) -> None: self.call("repl.send", {"text": text})
    def repl_press(self, key: str, mods: list[str] | None = None) -> None:
        args: dict[str, Any] = {"key": key}
        if mods: args["mods"] = mods
        self.call("repl.press", args)

    def wait_output(self, pattern: str, timeout_ms: int = 3000, regex: bool = False) -> dict:
        return self.call("wait.repl_output",
                         {"pattern": pattern, "regex": regex, "timeout_ms": timeout_ms})
    def wait_idle(self, quiet_ms: int = 200, timeout_ms: int = 3000) -> None:
        self.call("wait.repl_idle", {"quiet_ms": quiet_ms, "timeout_ms": timeout_ms})

    def dump(self) -> dict:
        return {
            "geometry": self.call("window.geometry"),
            "editor": self.call("editor.get_text"),
            "cursor": self.call("editor.get_cursor"),
            "repl_running": self.call("repl.is_running"),
            "screen": self.call("repl.get_screen", {"lines": 40}),
        }


# ---------- fixtures ----------


@pytest.fixture(scope="function")
def fresh_trowel(tmp_path: Path, request) -> Iterator[Client]:
    tp = _launch_trowel(tmp_path)
    ctl = TrowelCtl(tp.socket_path)
    client = Client(ctl, request.node.name)
    request.node._trowel_client = client
    request.node._trowel_proc = tp
    try:
        yield client
    finally:
        try: ctl.close()
        except Exception: pass
        tp.stop()


# Session fixture disabled by default — most tests want a fresh REPL and
# QSettings. Kept as a function-scoped alias so tests can prefer the same
# name if we introduce a session-scoped variant later.
@pytest.fixture(scope="function")
def trowel(fresh_trowel: Client) -> Client:
    return fresh_trowel


@pytest.fixture
def fixture_files() -> Path:
    return FIXTURES


# ---------- diagnostics on failure ----------


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    if rep.when != "call" or not rep.failed:
        return
    client: Client | None = getattr(item, "_trowel_client", None)
    proc: TrowelProc | None = getattr(item, "_trowel_proc", None)
    ARTIFACTS.mkdir(exist_ok=True)
    base = ARTIFACTS / item.name
    try:
        if client is not None:
            (base.with_suffix(".state.json")).write_text(
                json.dumps(client.dump(), indent=2, default=str))
    except Exception as e:
        (base.with_suffix(".state.err")).write_text(f"dump failed: {e!r}\n")
    if proc is not None and proc.stdout_path.exists():
        shutil.copy(proc.stdout_path, base.with_suffix(".trowel.log"))
