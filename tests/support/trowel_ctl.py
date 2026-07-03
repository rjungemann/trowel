"""Minimal client for the Trowel control socket.

Usage:
    with TrowelCtl(socket_path) as ctl:
        ctl.call("ping")
        ctl.call("editor.type", {"text": "hello"})
"""

from __future__ import annotations

import itertools
import json
import socket
from typing import Any


class ControlError(RuntimeError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


class TrowelCtl:
    def __init__(self, path: str, timeout: float = 10.0) -> None:
        self._path = path
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.settimeout(timeout)
        self._sock.connect(path)
        self._buf = b""
        self._ids = itertools.count(1)

    def __enter__(self) -> "TrowelCtl":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass

    def call(self, cmd: str, args: dict[str, Any] | None = None) -> Any:
        req_id = next(self._ids)
        payload = {"id": req_id, "cmd": cmd, "args": args or {}}
        self._sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
        while True:
            line = self._readline()
            resp = json.loads(line)
            if resp.get("id") != req_id:
                # Reserved for async events (subscribe); ignore for now.
                continue
            if resp.get("ok"):
                return resp.get("result")
            err = resp.get("error") or {}
            raise ControlError(err.get("code", "error"), err.get("message", ""))

    def _readline(self) -> bytes:
        while b"\n" not in self._buf:
            chunk = self._sock.recv(4096)
            if not chunk:
                raise ConnectionError("control socket closed")
            self._buf += chunk
        line, _, self._buf = self._buf.partition(b"\n")
        return line
