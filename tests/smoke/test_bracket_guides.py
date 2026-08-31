"""The active bracket-pair guide — track G of editor-intelligence.md.

The guide is computed from Scintilla's own style bytes, so none of this needs a
language server; only `test_guide_color_tracks_nesting_depth` cares about the
lexer at all, and it reads the colour back off the widget rather than
recomputing it.

Asserting on a computed pair without asserting on the *painted* range proves
nothing — the lesson lsp-support.md already recorded — so every test here goes
through `lsp.decorations`.
"""

from pathlib import Path

import pytest


def _open(trowel, path: Path):
    trowel.call("editor.open", {"path": str(path)})


def _text(trowel) -> str:
    return trowel.call("editor.get_text")["text"]


def _offset_of(trowel, needle: str, occurrence: int = 0) -> int:
    text = _text(trowel)
    idx = -1
    for _ in range(occurrence + 1):
        idx = text.index(needle, idx + 1)
    return len(text[:idx].encode("utf-8"))


def _caret_at(trowel, needle: str, extra: int = 0, occurrence: int = 0) -> int:
    pos = _offset_of(trowel, needle, occurrence) + extra
    trowel.call("editor.set_cursor", {"pos": pos})
    return pos


def _guide(trowel) -> dict:
    return trowel.call("lsp.decorations")


def _painted(trowel):
    """The single painted guide range, or None."""
    ranges = _guide(trowel)["bracket_guide_ranges"]
    assert len(ranges) <= 1, f"only the active pair is ever drawn: {ranges}"
    return ranges[0] if ranges else None


def _slice(trowel, rng) -> str:
    return _text(trowel).encode("utf-8")[rng["start"]:rng["end"]].decode("utf-8")


def test_single_line_pair_draws_between_the_brackets(trowel, fixture_files: Path):
    """Monaco draws single-line pairs and draws them between the brackets.

    `includeSingleLinePairs` is hardcoded true in Monaco's
    guidesTextModelPart.js, and the segment runs from the opener's *end* column
    to the closer's column. Matched rather than reinvented.
    """
    _open(trowel, fixture_files / "brackets.tur")
    # Caret inside `(+ 1 2)`.
    _caret_at(trowel, "(+ 1 2)", extra=3)

    rng = _painted(trowel)
    assert rng is not None
    assert _slice(trowel, rng) == "+ 1 2)"


def test_multi_line_pair_draws_on_the_closing_line(trowel, fixture_files: Path):
    """The segment sits under the closing line and terminates at the closer."""
    _open(trowel, fixture_files / "brackets.tur")
    # Caret inside the `(+ doubled\n 1)` form, which closes on the next line.
    _caret_at(trowel, "(+ doubled", extra=3)

    rng = _painted(trowel)
    assert rng is not None
    painted = _slice(trowel, rng)
    # One line only, ending on the closing bracket.
    assert "\n" not in painted
    assert painted.endswith(")")


def test_no_enclosing_pair_paints_nothing(trowel, fixture_files: Path):
    """Top level is the honest empty case, not a stale line.

    Monaco's active pair is undefined when nothing contains the caret, and with
    `bracketPairsHorizontal: 'active'` that means no horizontal guide at all.
    """
    _open(trowel, fixture_files / "brackets.tur")
    # Column 0 of a blank line between top-level forms.
    trowel.call("editor.set_cursor", {"pos": _offset_of(trowel, "(def single")})

    assert _painted(trowel) is None


def test_brackets_in_comments_and_strings_are_not_counted(trowel, fixture_files: Path):
    """The scan consults the style byte, so a `(` in a comment or a string is
    skipped without a second parser.

    The fixture's comment holds an unmatched `(` and its string holds both `(`
    and `]`. If either were counted, the pair found here would be wrong — and
    the guide would be drawn somewhere unrelated or not at all.
    """
    _open(trowel, fixture_files / "brackets.tur")
    # Caret inside the string's own `(def ... )` form, past both decoys.
    _caret_at(trowel, '"unclosed', extra=2)

    rng = _painted(trowel)
    assert rng is not None
    painted = _slice(trowel, rng)
    # The enclosing pair is the `(def bracket-text ...)` form, so the segment
    # ends at its closer — not at the `(` inside the comment above.
    assert painted.endswith(")")
    assert painted.startswith("def bracket-text")


def test_guide_moves_outward_as_the_caret_leaves_a_nested_form(
    trowel, fixture_files: Path
):
    """Depth is counted, not read off the style.

    Rainbow styles cycle mod 7, so a style comparison would match the wrong
    bracket on a deep form. Stepping out one level at a time is what catches
    that.
    """
    _open(trowel, fixture_files / "brackets.tur")

    _caret_at(trowel, "(- 3 4)", extra=3)
    innermost = _slice(trowel, _painted(trowel))
    assert innermost == "- 3 4)"

    _caret_at(trowel, "(* 2 (- 3 4))", extra=3)
    middle = _slice(trowel, _painted(trowel))
    assert middle == "* 2 (- 3 4))"

    _caret_at(trowel, "(+ 1 (* 2 (- 3 4)))", extra=3)
    outer = _slice(trowel, _painted(trowel))
    assert outer == "+ 1 (* 2 (- 3 4)))"


def test_guide_color_tracks_nesting_depth(trowel, fixture_files: Path):
    """The guide takes the pair's own colour, read back off the widget.

    Opener and closer share a style by construction, so this cannot drift out
    of sync with the parens it belongs to — but only an actual readback proves
    the colour reached Scintilla.
    """
    _open(trowel, fixture_files / "brackets.tur")

    _caret_at(trowel, "(+ 1 (* 2 (- 3 4)))", extra=3)
    outer_color = _guide(trowel)["bracket_guide_color"]

    _caret_at(trowel, "(* 2 (- 3 4))", extra=3)
    middle_color = _guide(trowel)["bracket_guide_color"]

    # Adjacent depths must not share a colour, or the guide says nothing about
    # which pair it belongs to.
    assert outer_color != middle_color


def test_guide_clears_when_the_caret_leaves_the_form(trowel, fixture_files: Path):
    """Transient by definition: the previous guide is cleared before painting."""
    _open(trowel, fixture_files / "brackets.tur")
    _caret_at(trowel, "(+ 1 2)", extra=3)
    assert _painted(trowel) is not None

    trowel.call("editor.set_cursor", {"pos": _offset_of(trowel, "(def single")})
    assert _painted(trowel) is None


def test_guide_is_absent_in_a_non_turmeric_buffer(trowel, fixture_files: Path):
    """Other languages have no rainbow depth styles, so the scan's style
    predicate would be reading bytes that mean something else."""
    _open(trowel, fixture_files / "sample.py")
    text = _text(trowel)
    trowel.call("editor.set_cursor", {"pos": len(text.encode("utf-8")) // 2})

    assert _guide(trowel)["bracket_guide_ranges"] == []


def test_guide_follows_an_edit_without_the_caret_moving(trowel, fixture_files: Path):
    """An edit moves the brackets around the caret even when the caret does not
    move, so the guide is recomputed on content change too."""
    _open(trowel, fixture_files / "brackets.tur")
    _caret_at(trowel, "(+ 1 2)", extra=3)
    before = _slice(trowel, _painted(trowel))
    assert before == "+ 1 2)"

    # Type a digit at the caret; the enclosing form grows by one byte.
    trowel.type("9")
    after = _slice(trowel, _painted(trowel))
    assert after != before
    assert after.endswith(")")


# --- the vertical guide (G2) -----------------------------------------------


def _vertical(trowel) -> dict:
    return _guide(trowel)["bracket_guide_vertical"]


def test_vertical_guide_spans_a_multi_line_pair(trowel, fixture_files: Path):
    """The companion line down the left edge of the enclosing form.

    Drawn as a viewport overlay rather than with Scintilla's own indentation
    guides, which sit at multiples of the indent width in a single colour — a
    lisp opener is rarely at a multiple of the indent width, so those would
    draw the wrong column in the wrong colour.
    """
    _open(trowel, fixture_files / "brackets.tur")
    _caret_at(trowel, "(+ doubled", extra=3)

    line = _vertical(trowel)
    assert line["visible"] is True
    # A real extent, top to bottom, at a sensible column.
    assert line["bottom"] > line["top"]
    assert line["x"] >= 0


def test_vertical_guide_is_absent_for_a_single_line_pair(trowel, fixture_files: Path):
    """No vertical extent to show; the horizontal segment already says it all."""
    _open(trowel, fixture_files / "brackets.tur")
    _caret_at(trowel, "(+ 1 2)", extra=3)

    assert _painted(trowel) is not None, "the horizontal segment should still be drawn"
    assert _vertical(trowel)["visible"] is False


def test_vertical_guide_clears_at_the_top_level(trowel, fixture_files: Path):
    _open(trowel, fixture_files / "brackets.tur")
    _caret_at(trowel, "(+ doubled", extra=3)
    assert _vertical(trowel)["visible"] is True

    trowel.call("editor.set_cursor", {"pos": _offset_of(trowel, "(def single")})
    assert _vertical(trowel)["visible"] is False
