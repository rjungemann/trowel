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
