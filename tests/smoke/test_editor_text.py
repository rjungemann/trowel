"""§3.2 — editor text & cursor."""


def test_type_inserts_text(trowel):
    trowel.type("hello")
    r = trowel.call("editor.get_text")
    assert r["text"] == "hello"
    c = trowel.call("editor.get_cursor")
    assert c["pos"] == 5
    assert c["line"] == 0 and c["col"] == 5


def test_arrow_navigation(trowel):
    trowel.type("abc\ndef")
    trowel.press("Up")
    trowel.press("Home")
    trowel.press("Right")
    c = trowel.call("editor.get_cursor")
    assert (c["line"], c["col"]) == (0, 1)


def test_selection_by_setter(trowel):
    trowel.type("hello world")
    trowel.call("editor.set_selection", {"start": 6, "end": 11})
    s = trowel.call("editor.get_selection")
    assert s["text"] == "world"


def test_backspace_removes_char(trowel):
    trowel.type("abc")
    trowel.press("Backspace")
    assert trowel.call("editor.get_text")["text"] == "ab"


def test_set_text_replaces_buffer(trowel):
    trowel.type("keep? no.")
    trowel.call("editor.set_text", {"text": "fresh"})
    assert trowel.call("editor.get_text")["text"] == "fresh"


def test_set_cursor_by_pos(trowel):
    trowel.type("0123456789")
    trowel.call("editor.set_cursor", {"pos": 3})
    assert trowel.call("editor.get_cursor")["pos"] == 3


def test_set_cursor_by_line_col(trowel):
    trowel.type("aaa\nbbb\nccc")
    trowel.call("editor.set_cursor", {"line": 1, "col": 2})
    c = trowel.call("editor.get_cursor")
    assert (c["line"], c["col"]) == (1, 2)


def test_newline_creates_second_line(trowel):
    trowel.type("(defn foo ()")
    trowel.press("Return")
    c = trowel.call("editor.get_cursor")
    assert c["line"] == 1
