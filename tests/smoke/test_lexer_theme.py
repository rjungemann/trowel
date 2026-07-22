"""§3.7 — light-touch checks that the lexer & theme pipeline is wired up."""


def test_lexer_produces_nonzero_styles(trowel):
    # Put a keyword at pos 1 (after '(') — Scintilla should style it.
    trowel.call("editor.set_text", {"text": "(def x 1)"})
    styles = [trowel.call("editor.get_style_at", {"pos": p})["style"]
              for p in range(0, 9)]
    # At least one position must be styled to something other than default (0).
    assert any(s != 0 for s in styles), f"all styles default: {styles}"


def test_lexer_reports_style_after_reopen(trowel, tmp_path):
    f = tmp_path / "sample.tur"
    f.write_text("(def hi 42)\n")
    trowel.call("editor.open", {"path": str(f)})
    styles = [trowel.call("editor.get_style_at", {"pos": p})["style"]
              for p in range(0, 10)]
    assert any(s != 0 for s in styles)


# Rainbow bracket styles start at 40 (above Scintilla's predefined 32-39 range)
# and cycle through 7 colors; 47 flags an unmatched closer. Mirrors TurStyle in
# src/editor/turmeric_lexer.h.
RAINBOW0 = 40
RAINBOW_LAST = 46
BRACKET_ERROR = 47


def _style_at(trowel, pos):
    return trowel.call("editor.get_style_at", {"pos": pos})["style"]


def test_rainbow_brackets_color_by_depth(trowel):
    # (a (b) c): outer brackets at depth 0, inner pair at depth 1.
    trowel.call("editor.set_text", {"text": "(a (b) c)"})
    open_outer = _style_at(trowel, 0)   # '('
    open_inner = _style_at(trowel, 3)   # nested '('
    close_inner = _style_at(trowel, 5)  # ')' matching the nested one
    close_outer = _style_at(trowel, 8)  # ')' matching the outer one

    # Every bracket lands in the rainbow style range.
    for s in (open_outer, open_inner, close_inner, close_outer):
        assert RAINBOW0 <= s <= RAINBOW_LAST, f"not a rainbow style: {s}"

    # Matching pairs share a color; adjacent nesting levels differ.
    assert open_outer == close_outer
    assert open_inner == close_inner
    assert open_outer != open_inner


def test_rainbow_brackets_span_types(trowel):
    # Parens, square brackets, and curly braces all participate by depth.
    trowel.call("editor.set_text", {"text": "([{}])"})
    styles = [_style_at(trowel, p) for p in range(6)]
    for s in styles:
        assert RAINBOW0 <= s <= RAINBOW_LAST, f"not a rainbow style: {s}"
    # Outer '(' and its ')' match; each nesting level is a distinct color.
    assert styles[0] == styles[5]      # ( )
    assert styles[1] == styles[4]      # [ ]
    assert styles[2] == styles[3]      # { }
    assert len({styles[0], styles[1], styles[2]}) == 3


def test_rainbow_brackets_flag_unmatched_closer(trowel):
    trowel.call("editor.set_text", {"text": ")"})
    assert _style_at(trowel, 0) == BRACKET_ERROR
