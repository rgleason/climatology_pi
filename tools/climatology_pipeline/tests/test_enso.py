from climatology_pipeline.enso import (apply_episode_rule, parse_oni_html,
                                      render_legacy)


def test_parse_complete_rows_and_drop_partial_current_year() -> None:
    row = " ".join(str(value / 10) for value in range(-6, 6))
    html = f"<table><tr><td>1950 {row}</td></tr><tr><td>2024 {row}</td></tr><tr><td>2026 0.1 0.2</td></tr></table>"
    result = parse_oni_html(html)
    assert sorted(result) == [1950, 2024]
    rendered = render_legacy(result)
    assert rendered.startswith("Year DJF JFM")
    assert len(rendered.splitlines()[1].split()) == 13


def test_parser_keeps_decorated_final_cell_separate_from_markup() -> None:
    values = ["-1.3", "-1.2", "-1.1", "-1.1", "-1.1", "-0.9",
              "-0.6", "-0.6", "-0.6", "-0.6", "-0.7"]
    row = "<td>1950</td>" + "".join(f"<td>{value}</td>" for value in values)
    row += '<td><span class="negative">-0.8</span></td>'
    other = row.replace("1950", "2024", 1)
    parsed = parse_oni_html(f"<table><tr>{row}</tr><tr>{other}</tr></table>")
    assert parsed[1950][-1] == -0.8


def test_episode_rule_neutralises_short_excursion_across_year_boundary() -> None:
    rows = {
        2023: (0.0,) * 10 + (0.6, 0.7),
        2024: (0.8, 0.9, 0.0, 0.0, 0.0, -0.5, -0.6, -0.7, -0.8, -0.9, 0.0, 0.0),
    }
    result = apply_episode_rule(rows)
    assert result[2023][-2:] == (0.0, 0.0)
    assert result[2024][:2] == (0.0, 0.0)
    assert result[2024][5:10] == rows[2024][5:10]
