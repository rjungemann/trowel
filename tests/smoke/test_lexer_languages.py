"""Per-file-type highlighting: each language gets its own style band, and
Markdown delegates fenced-code bodies to the right guest scanner.

Style IDs mirror the enums in src/editor/lexers.h. Each language owns a
contiguous block, so most assertions can check "is this in the right band"
rather than pinning an exact slot.
"""

from pathlib import Path

import pytest

# --- style bands (src/editor/lexers.h) --------------------------------------

TUR_DEFINE = 14
TUR_CBLOCK = 21
TUR_NEOTERIC = 25
TUR_IDENT = 26
TUR_SWEET_MARKER = 28
TUR_BAND = range(0, 31)
RAINBOW_BAND = range(40, 48)

C_DEFAULT, C_COMMENT, C_DOC, C_PREPROC, C_KEYWORD, C_TYPE = 48, 49, 50, 51, 52, 53
C_STRING, C_ESCAPE, C_CHAR, C_NUMBER, C_OPERATOR, C_IDENT = 54, 55, 56, 57, 58, 59
C_BAND = range(48, 60)

MD_DEFAULT, MD_HEADING, MD_EMPHASIS, MD_STRONG, MD_CODESPAN = 64, 65, 66, 67, 68
MD_FENCE, MD_CODEBLOCK, MD_LINKTEXT, MD_LINKURL = 69, 70, 71, 72
MD_BLOCKQUOTE, MD_LISTMARKER, MD_RULE, MD_HTML, MD_ESCAPE = 73, 74, 75, 76, 77
MD_BAND = range(64, 78)

JSON_DEFAULT, JSON_KEY, JSON_STRING, JSON_ESCAPE = 80, 81, 82, 83
JSON_NUMBER, JSON_LITERAL, JSON_OPERATOR, JSON_ERROR = 84, 85, 86, 87

JUST_DEFAULT, JUST_COMMENT, JUST_RECIPE, JUST_DEP, JUST_PARAM = 91, 92, 93, 94, 95
JUST_ASSIGN, JUST_INTERP, JUST_BACKTICK, JUST_KEYWORD = 96, 97, 98, 99
JUST_STRING, JUST_NUMBER, JUST_BODY, JUST_ATTR, JUST_OP = 100, 101, 102, 103, 104


# --- helpers ----------------------------------------------------------------


def style_at(trowel, pos):
    return trowel.call("editor.get_style_at", {"pos": pos})["style"]


class Doc:
    """A fixture opened in Trowel, with offset lookup by substring."""

    def __init__(self, trowel, path: Path):
        self.trowel = trowel
        self.text = path.read_text()
        trowel.call("editor.open", {"path": str(path)})

    def offset(self, needle: str, after: str | None = None) -> int:
        start = self.text.index(after) if after else 0
        idx = self.text.index(needle, start)
        assert idx >= 0, f"{needle!r} not in fixture"
        return idx

    def style_of(self, needle: str, after: str | None = None) -> int:
        return style_at(self.trowel, self.offset(needle, after))


@pytest.fixture
def md(trowel, fixture_files):
    return Doc(trowel, fixture_files / "sample.md")


# --- Markdown ---------------------------------------------------------------


def test_markdown_block_constructs(md):
    assert md.style_of("# Heading one") == MD_HEADING
    assert md.style_of("- list item") == MD_LISTMARKER
    assert md.style_of("`inline code span`") == MD_CODESPAN
    assert md.style_of("*emphasis*") == MD_EMPHASIS
    assert md.style_of("**strong**") == MD_STRONG
    assert md.style_of("[link]") == MD_LINKTEXT
    assert md.style_of("(https://example.com)") == MD_LINKURL


def test_markdown_prose_is_not_lexed_as_turmeric(md):
    # The whole point of the change: prose in a .md file must land in the
    # Markdown band, not be styled by the Turmeric lexer.
    assert md.style_of("After the fenced block.") == MD_DEFAULT
    assert md.style_of("Some *emphasis*") == MD_DEFAULT


def test_markdown_fence_delimiters(md):
    assert md.style_of("```turmeric") == MD_FENCE


# --- the nested case: markdown -> turmeric -> C -----------------------------


def test_turmeric_fence_body_is_turmeric(md):
    assert md.style_of("def hi") == TUR_DEFINE


def test_inner_c_block_inside_turmeric_fence_is_c(md):
    # ```c opened *inside* a ```turmeric fence: the body is C, not Turmeric
    # and not flat code.
    assert md.style_of("int answer") == C_TYPE
    assert md.style_of("42;") == C_NUMBER


def test_equal_length_inner_fence_does_not_close_outer_block(md):
    # The inner ``` closes the C block only. Turmeric that follows it is still
    # inside the outer fence and must still highlight as Turmeric.
    assert md.style_of("def bye") == TUR_DEFINE


def test_outer_fence_closes_and_prose_resumes(md):
    # ...and once the real closing fence lands, we're back to markdown.
    assert md.style_of("After the fenced block.") == MD_DEFAULT


def test_four_backtick_fence_survives_inner_three_backticks(md):
    assert md.style_of("def outer") == TUR_DEFINE
    assert md.style_of("long wide") == C_TYPE
    assert md.style_of("def still-turmeric") == TUR_DEFINE
    assert md.style_of("Done.") == MD_DEFAULT


# --- C ----------------------------------------------------------------------


def test_c_file_highlights_as_c(trowel, fixture_files):
    doc = Doc(trowel, fixture_files / "sample.c")
    assert doc.style_of("#include") == C_PREPROC
    assert doc.style_of("/* A block comment. */") == C_COMMENT
    assert doc.style_of("int main") == C_TYPE
    assert doc.style_of("return") == C_KEYWORD
    assert doc.style_of('"hello') == C_STRING
    assert doc.style_of("\\n") == C_ESCAPE
    assert doc.style_of("0;") == C_NUMBER


# --- inline C in a plain .tur file ------------------------------------------


def test_turmeric_inline_c_block_delegates_to_c(trowel):
    # Multi-line ``` blocks in a .tur buffer: the fence stays CBlock-colored,
    # the body is scanned as C. (This input also used to hang the lexer.)
    trowel.call("editor.set_text",
                {"text": "(def x 1)\n```c\nint y = 2;\n```\n(def z 3)\n"})
    text = "(def x 1)\n```c\nint y = 2;\n```\n(def z 3)\n"
    assert style_at(trowel, text.index("```c")) == TUR_CBLOCK
    assert style_at(trowel, text.index("int y")) == C_TYPE
    assert style_at(trowel, text.index("2;")) == C_NUMBER
    assert style_at(trowel, text.index("def z")) == TUR_DEFINE


# --- JSON -------------------------------------------------------------------


def test_json_file_highlights_as_json(trowel, fixture_files):
    doc = Doc(trowel, fixture_files / "sample.json")
    assert doc.style_of('"name"') == JSON_KEY
    assert doc.style_of('"trowel"') == JSON_STRING
    assert doc.style_of("10,") == JSON_NUMBER
    assert doc.style_of("true") == JSON_LITERAL
    assert doc.style_of("null") == JSON_LITERAL
    # Braces take the shared rainbow palette when rainbow brackets are on.
    assert style_at(trowel, 0) in RAINBOW_BAND


# --- Justfile ---------------------------------------------------------------


def test_justfile_highlights_as_just(trowel, fixture_files):
    doc = Doc(trowel, fixture_files / "Justfile")
    assert doc.style_of("# Build everything") == JUST_COMMENT
    assert doc.style_of("preset :=") == JUST_ASSIGN
    assert doc.style_of('"macos-debug"') == JUST_STRING
    assert doc.style_of("build:") == JUST_RECIPE
    assert doc.style_of("configure\n") == JUST_DEP
    assert doc.style_of("{{preset}}") == JUST_INTERP


# --- sweet expressions ------------------------------------------------------


@pytest.fixture
def sweet(trowel, fixture_files):
    return Doc(trowel, fixture_files / "sweet_syntax.tur.sweet")


def test_sweet_markers_are_styled(sweet):
    # The two markers the reader actually accepts: GROUP/SPLIT `$`, and a lone
    # `\` SPLIT on its own line.
    assert sweet.style_of("$") == TUR_SWEET_MARKER
    assert sweet.style_of("\\") == TUR_SWEET_MARKER


def test_sweet_keywords_highlight_without_parens(sweet):
    # Keyword styling is position-independent, so a paren-less `def` line reads
    # the same as `(def ...)`.
    assert sweet.style_of("def sweet-plain") == TUR_DEFINE
    assert sweet.style_of("defn sweet-add") == TUR_DEFINE


def test_sweet_neoteric_and_curly_infix(sweet):
    assert sweet.style_of("sweet-add(1 2)") == TUR_NEOTERIC
    assert sweet.style_of("{3 + 4}") in RAINBOW_BAND


def test_dollar_is_not_a_marker_in_plain_turmeric(trowel):
    # `$` is a legal symbol character, so outside sweet mode it must stay an
    # identifier rather than pick up the marker color.
    text = "(def x $)\n"
    trowel.call("editor.set_text", {"text": text})
    assert style_at(trowel, text.index("$")) != TUR_SWEET_MARKER


def test_dollar_prefixed_symbol_is_not_a_marker(trowel, tmp_path):
    # `$foo` is one symbol; only a standalone `$` is the GROUP marker.
    f = tmp_path / "dollar.tur.sweet"
    f.write_text("def $named 1\n")
    trowel.call("editor.open", {"path": str(f)})
    doc = Doc(trowel, f)
    assert doc.style_of("$named") == TUR_IDENT


def test_markdown_sweet_fence_delegates_to_sweet(md):
    assert md.style_of("def fenced-sweet") == TUR_DEFINE
    assert md.style_of("$", after="def fenced-sweet") == TUR_SWEET_MARKER


# --- #lang directive --------------------------------------------------------
#
# Trowel mirrors the toolchain's own precedence (elab_toplevel.c): an extension
# that names a non-default reader wins, otherwise the `#lang` line decides.


def lang_doc(trowel, tmp_path, name, body):
    f = tmp_path / name
    f.write_text(body)
    trowel.call("editor.open", {"path": str(f)})
    return Doc(trowel, f)


def test_lang_sweet_in_tur_file_selects_sweet(trowel, tmp_path):
    # `.tur` is the default reader, so the directive decides.
    doc = lang_doc(trowel, tmp_path, "a.tur", "#lang turmeric/sweet\ndef x $ + 1 2\n")
    assert doc.style_of("$") == TUR_SWEET_MARKER


def test_legacy_sweet_exp_alias_still_selects_sweet(trowel, tmp_path):
    doc = lang_doc(trowel, tmp_path, "b.tur", "#lang sweet-exp\ndef x $ + 1 2\n")
    assert doc.style_of("$") == TUR_SWEET_MARKER


def test_lang_turmeric_does_not_make_a_tur_file_sweet(trowel, tmp_path):
    doc = lang_doc(trowel, tmp_path, "c.tur", "#lang turmeric\n(def x $)\n")
    assert doc.style_of("$") != TUR_SWEET_MARKER


def test_sweet_extension_beats_a_plain_lang_directive(trowel, tmp_path):
    # `.tur.sweet` already names a non-default reader, so it wins and the
    # directive is only a redundant hint — matching how the file would run.
    doc = lang_doc(trowel, tmp_path, "d.tur.sweet",
                   "#lang turmeric\ndef x $ + 1 2\n")
    assert doc.style_of("$") == TUR_SWEET_MARKER


def test_bare_sweet_extension_is_plain_turmeric(trowel, tmp_path):
    # Turmeric's reader_type_from_extension only knows `.tur.sweet`; a bare
    # `.sweet` runs as ordinary Turmeric, so it must highlight that way.
    doc = lang_doc(trowel, tmp_path, "e.sweet", "def x $ + 1 2\n")
    assert doc.style_of("$") != TUR_SWEET_MARKER


def test_bare_sweet_extension_honors_lang_directive(trowel, tmp_path):
    doc = lang_doc(trowel, tmp_path, "f.sweet",
                   "#lang turmeric/sweet\ndef x $ + 1 2\n")
    assert doc.style_of("$") == TUR_SWEET_MARKER


def test_lang_line_after_shebang_is_honored(trowel, tmp_path):
    doc = lang_doc(trowel, tmp_path, "g.tur",
                   "#!/usr/bin/env tur\n#lang turmeric/sweet\ndef x $ + 1 2\n")
    assert doc.style_of("$") == TUR_SWEET_MARKER


def test_lang_layers_do_not_disturb_the_base(trowel, tmp_path):
    # Layer tokens follow the base name and don't change the reader.
    doc = lang_doc(trowel, tmp_path, "h.tur",
                   "#lang turmeric/sweet stringed\ndef x $ + 1 2\n")
    assert doc.style_of("$") == TUR_SWEET_MARKER


def test_lang_directive_is_ignored_below_line_one(trowel, tmp_path):
    doc = lang_doc(trowel, tmp_path, "i.tur",
                   "(def a 1)\n#lang turmeric/sweet\ndef x $ + 1 2\n")
    assert doc.style_of("$") != TUR_SWEET_MARKER


def test_typing_a_lang_line_switches_language_live(trowel):
    # Untitled buffer: no extension to go on, so the directive is the only
    # signal — and it has to take effect without a save.
    plain = "def x $ + 1 2\n"
    trowel.call("editor.set_text", {"text": plain})
    assert style_at(trowel, plain.index("$")) != TUR_SWEET_MARKER

    switched = "#lang turmeric/sweet\ndef x $ + 1 2\n"
    trowel.call("editor.set_text", {"text": switched})
    assert style_at(trowel, switched.index("$")) == TUR_SWEET_MARKER


def test_lang_line_in_a_markdown_file_is_not_a_directive(trowel, tmp_path):
    # Only Turmeric-family extensions consult the directive; elsewhere it is
    # just text.
    # (`#lang` is not a heading either — ATX headings need a space after the
    # hash — so this asserts the Markdown band rather than a specific style.)
    doc = lang_doc(trowel, tmp_path, "j.md", "#lang turmeric/sweet\n")
    assert doc.style_of("#lang turmeric/sweet") in MD_BAND


# --- dispatch ---------------------------------------------------------------


def test_unknown_extension_falls_back_to_turmeric(trowel, tmp_path):
    f = tmp_path / "notes.txt"
    f.write_text("(def hi 42)\n")
    trowel.call("editor.open", {"path": str(f)})
    assert style_at(trowel, 1) == TUR_DEFINE


def test_language_switches_when_saved_under_a_new_extension(trowel, tmp_path):
    src = tmp_path / "notes.tur"
    src.write_text("# Heading one\n")
    trowel.call("editor.open", {"path": str(src)})
    # As Turmeric, a leading '#' is not a heading.
    assert style_at(trowel, 0) != MD_HEADING

    trowel.call("editor.save_as", {"path": str(tmp_path / "notes.md")})
    assert style_at(trowel, 0) == MD_HEADING
