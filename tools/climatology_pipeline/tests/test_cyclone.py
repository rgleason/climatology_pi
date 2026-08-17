from __future__ import annotations

from io import StringIO

from climatology_pipeline.cyclone import convert_ibtracs
from climatology_pipeline.legacy import decode_cyclones, encode_cyclones


HEADER = "SID,SEASON,BASIN,SUBBASIN,ISO_TIME,NATURE,LAT,LON,WMO_WIND,WMO_PRES,WMO_AGENCY,TRACK_TYPE,USA_STATUS,USA_WIND,USA_PRES\n"
UNITS = ",Year,,,,,degrees_north,degrees_east,kts,mb,,,,kts,mb\n"


def test_ibtracs_main_six_hour_rows_and_homogeneous_usa_wind() -> None:
    rows = "".join((
        "1995001N10020,1995,NA,NA,1995-01-01 00:00:00,TS,10,-45,40,995,hurdat_atl,main,HU,65,980\n",
        # Three-hourly row is deliberately excluded from the legacy atlas.
        "1995001N10020,1995,NA,NA,1995-01-01 03:00:00,TS,10.1,-45.1,42,994,hurdat_atl,main,HU,66,979\n",
        "1995001N10020,1995,NA,NA,1995-01-01 06:00:00,ET,11,-44,35,998,hurdat_atl,main,EX,,\n",
        "1995001N10020,1995,NA,NA,1995-01-01 12:00:00,TS,12,-43,40,996,hurdat_atl,spur-other,TS,45,990\n",
    ))
    theatres, stats = convert_ibtracs(StringIO(HEADER + UNITS + rows))
    assert len(theatres["atl"]) == 1
    points = theatres["atl"][0].points
    assert [point.hour for point in points] == [0, 6]
    assert points[0].wind_knots == 65  # not the incompatible WMO_WIND value 40
    assert points[1].wind_knots == 0 and points[1].pressure_hpa == 0
    assert [point.state for point in points] == ["*", "E"]
    assert stats.accepted_rows == 2
    assert stats.skipped_non_main == 1
    assert stats.skipped_time == 1


def test_southern_track_round_trip_and_longitude_convention() -> None:
    row = "1995001S10090,1995,SI,MM,1995-01-01 00:00:00,TS,-12,90,35,1000,reunion,main,TS,40,998\n"
    theatres, _ = convert_ibtracs(StringIO(HEADER + UNITS + row))
    track = theatres["she"][0]
    assert track.points[0].longitude_tenths == -2700
    payload = encode_cyclones([track], southern=True)
    assert decode_cyclones(payload, southern=True) == (track,)


def test_state_mapping_keeps_subtropical_and_disturbance_distinct() -> None:
    rows = "".join((
        "1995001N10020,1995,EP,MM,1995-01-01 00:00:00,SS,10,200,,,atcf,main,SS,40,1000\n",
        "1995001N10020,1995,EP,MM,1995-01-01 06:00:00,DS,10,201,,,atcf,main,XX,,\n",
    ))
    theatres, _ = convert_ibtracs(StringIO(HEADER + UNITS + rows))
    assert [point.state for point in theatres["epa"][0].points] == ["S", "D"]
