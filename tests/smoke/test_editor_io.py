"""§3.3 — editor file I/O."""

from pathlib import Path


def test_open_file(trowel, fixture_files: Path):
    trowel.call("editor.open", {"path": str(fixture_files / "hello.tur")})
    r = trowel.call("editor.get_text")
    assert "greeting" in r["text"]
    assert r["path"].endswith("hello.tur")
    assert r["modified"] is False


def test_save_marks_clean(trowel, tmp_path: Path):
    target = tmp_path / "written.tur"
    trowel.type("(def x 1)")
    assert trowel.call("editor.get_text")["modified"] is True
    trowel.call("editor.save_as", {"path": str(target)})
    assert target.exists()
    assert "(def x 1)" in target.read_text()
    assert trowel.call("editor.get_text")["modified"] is False


def test_save_as_updates_path(trowel, tmp_path: Path):
    target = tmp_path / "renamed.tur"
    trowel.type("(def y 2)")
    trowel.call("editor.save_as", {"path": str(target)})
    r = trowel.call("editor.get_text")
    assert r["path"].endswith("renamed.tur")
    title = trowel.call("window.geometry")["title"]
    assert "renamed.tur" in title
