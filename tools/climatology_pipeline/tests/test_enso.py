from climatology_pipeline.enso import parse_oni_html, render_legacy


def test_parse_complete_rows_and_drop_partial_current_year() -> None:
    row = " ".join(str(value / 10) for value in range(-6, 6))
    html = f"<table><tr><td>1950 {row}</td></tr><tr><td>2024 {row}</td></tr><tr><td>2026 0.1 0.2</td></tr></table>"
    result = parse_oni_html(html)
    assert sorted(result) == [1950, 2024]
    rendered = render_legacy(result)
    assert rendered.startswith("Year DJF JFM")
    assert len(rendered.splitlines()[1].split()) == 13
