"""§ single-instance — a second launch forwards its files to the first.

On platforms without an OS-level "open document in the running app" mechanism
(everything except macOS, which uses LaunchServices/QEvent::FileOpen), Trowel
routes a second `trowel foo.tur` to the already-running instance via a
well-known control socket, then exits. These tests drive that with two real
processes.
"""

from __future__ import annotations

import glob
import os
import subprocess
import sys
import time
from pathlib import Path

import pytest

from conftest import BIN
from trowel_ctl import TrowelCtl

pytestmark = pytest.mark.skipif(
    sys.platform == "darwin",
    reason="single-instance routing is Linux-only; macOS uses LaunchServices",
)


def _env(runtime_dir: Path, home: Path) -> dict[str, str]:
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    # Keep the socket path well under the ~108-char AF_UNIX limit and isolate
    # this test's instance from any real user session.
    env["XDG_RUNTIME_DIR"] = str(runtime_dir)
    env["HOME"] = str(home)
    env["XDG_CONFIG_HOME"] = str(home / ".config")
    env["XDG_CACHE_HOME"] = str(home / ".cache")
    # A sanitizer build's LeakSanitizer would otherwise force exit 1; the
    # forwarding secondary never builds an eval env so it stays clean anyway,
    # but be explicit.
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    return env


def _wait_for_single_socket(runtime_dir: Path, timeout: float = 10.0) -> str:
    deadline = time.time() + timeout
    pattern = str(runtime_dir / "trowel" / "single*.sock")
    while time.time() < deadline:
        hits = glob.glob(pattern)
        if hits:
            return hits[0]
        time.sleep(0.05)
    raise TimeoutError(f"single-instance socket never appeared at {pattern}")


@pytest.fixture()
def primary(tmp_path: Path):
    if not BIN.exists():
        pytest.skip(f"trowel binary not built at {BIN}")
    # Short runtime dir under /tmp so the socket path fits AF_UNIX limits even
    # when tmp_path itself is deeply nested.
    runtime_dir = Path(f"/tmp/trowel-si-{os.getpid()}-{int(time.time()*1000) % 100000}")
    runtime_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    home = tmp_path / "home"
    home.mkdir()

    proc = subprocess.Popen(
        [str(BIN)],  # bare launch: no --control-socket, so single-instance engages
        env=_env(runtime_dir, home),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        sock = _wait_for_single_socket(runtime_dir)
        yield proc, runtime_dir, home, sock
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=2)
        for f in glob.glob(str(runtime_dir / "trowel" / "*")):
            try:
                os.unlink(f)
            except OSError:
                pass


def test_second_launch_forwards_file_and_exits(primary, tmp_path):
    proc, runtime_dir, home, sock = primary

    target = tmp_path / "forwarded.tur"
    target.write_text("(defn main [] 1)\n")

    # A second launch with a file: it should hand the file to the primary and
    # exit promptly, rather than opening its own window.
    result = subprocess.run(
        [str(BIN), str(target)],
        env=_env(runtime_dir, home),
        timeout=10,
    )
    assert result.returncode == 0, "secondary should forward and exit 0"

    # The primary is still alive and now shows the forwarded file.
    assert proc.poll() is None, "primary must keep running"
    with TrowelCtl(sock) as ctl:
        geom = ctl.call("window.geometry")
        assert geom["file_path"] == str(target)


def test_bare_second_launch_activates_and_exits(primary):
    proc, runtime_dir, home, sock = primary
    # Bare `trowel` with a running instance == "focus it and exit".
    result = subprocess.run(
        [str(BIN)],
        env=_env(runtime_dir, home),
        timeout=10,
    )
    assert result.returncode == 0
    assert proc.poll() is None
