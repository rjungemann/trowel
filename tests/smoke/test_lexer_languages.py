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
