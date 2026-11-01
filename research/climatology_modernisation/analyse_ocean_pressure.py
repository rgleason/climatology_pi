"""Analyse pressure, SST and current ENSO composites and out-of-sample skill."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd
from scipy import stats
import xarray as xr

from enso import load_classification
from run_study import (KNOTS_PER_MPS, PHASES, ROUTES, corridor_mask, fdr_bh,
                       weighted_fraction)


def phase_arrays(year: np.ndarray, month: np.ndarray,
                 classification: pd.DataFrame) -> tuple[np.ndarray, np.ndarray]:
    lookup = classification.set_index(["year", "month"])
    keys = list(zip(year.astype(int), month.astype(int)))
    return (np.array([lookup.loc[key, "phase"] for key in keys], dtype=object),
            np.array([lookup.loc[key, "event_id"] for key in keys], dtype=object))


def scalar_signal(values: np.ndarray, year: np.ndarray, month: np.ndarray,
                  phases: np.ndarray, lat: np.ndarray, valid: np.ndarray,
                  variable: str, unit: str) -> pd.DataFrame:
    records = []
    for calendar_month in range(1, 13):
        neutral = (month == calendar_month) & (phases == "neutral")
        for phase in ("el_nino", "la_nina"):
            selected = (month == calendar_month) & (phases == phase)
            a, b = values[selected], values[neutral]
            difference = np.nanmean(a, axis=0) - np.nanmean(b, axis=0)
            pooled = np.sqrt((np.nanvar(a, axis=0, ddof=1) + np.nanvar(b, axis=0, ddof=1)) / 2)
            snr = np.abs(difference) / np.where(pooled > 0, pooled, np.nan)
            pvalue = stats.ttest_ind(a, b, axis=0, equal_var=False, nan_policy="omit").pvalue
            accepted = fdr_bh(np.where(valid, pvalue, np.nan))
            absolute = np.abs(difference[valid])
            records.append({
                "variable": variable, "unit": unit, "month": calendar_month,
                "phase": phase, "sample_years": int(np.sum(selected)),
                "median_absolute_anomaly": float(np.nanmedian(absolute)),
                "p90_absolute_anomaly": float(np.nanpercentile(absolute, 90)),
                "valid_area_fdr_005_pct": 100 * weighted_fraction(accepted, lat, valid),
                "valid_area_snr_ge_0_5_pct": 100 * weighted_fraction(snr >= 0.5, lat, valid),
            })
    return pd.DataFrame(records)


def vector_signal(u: np.ndarray, v: np.ndarray, month: np.ndarray,
                  phases: np.ndarray, lat: np.ndarray,
                  valid: np.ndarray) -> pd.DataFrame:
    records = []
    for calendar_month in range(1, 13):
        neutral = (month == calendar_month) & (phases == "neutral")
        for phase in ("el_nino", "la_nina"):
            selected = (month == calendar_month) & (phases == phase)
            au, av, bu, bv = u[selected], v[selected], u[neutral], v[neutral]
            du, dv = np.nanmean(au, axis=0) - np.nanmean(bu, axis=0), np.nanmean(av, axis=0) - np.nanmean(bv, axis=0)
            magnitude = np.hypot(du, dv) * KNOTS_PER_MPS
            pooled = np.sqrt((np.nanvar(au, axis=0, ddof=1) + np.nanvar(bu, axis=0, ddof=1) +
                              np.nanvar(av, axis=0, ddof=1) + np.nanvar(bv, axis=0, ddof=1)) / 2)
            snr = np.hypot(du, dv) / np.where(pooled > 0, pooled, np.nan)
            pu = stats.ttest_ind(au, bu, axis=0, equal_var=False, nan_policy="omit").pvalue
            pv = stats.ttest_ind(av, bv, axis=0, equal_var=False, nan_policy="omit").pvalue
            accepted = fdr_bh(np.where(valid, np.minimum(1, 2 * np.minimum(pu, pv)), np.nan))
            records.append({
                "variable": "GODAS 5 m current vector", "unit": "knots",
                "month": calendar_month, "phase": phase,
                "sample_years": int(np.sum(selected)),
                "median_absolute_anomaly": float(np.nanmedian(magnitude[valid])),
                "p90_absolute_anomaly": float(np.nanpercentile(magnitude[valid], 90)),
                "valid_area_fdr_005_pct": 100 * weighted_fraction(accepted, lat, valid),
                "valid_area_snr_ge_0_5_pct": 100 * weighted_fraction(snr >= 0.5, lat, valid),
            })
    return pd.DataFrame(records)


def vector_loo(u: np.ndarray, v: np.ndarray, year: np.ndarray, month: np.ndarray,
               phases: np.ndarray, events: np.ndarray, lat: np.ndarray,
               lon: np.ndarray, valid: np.ndarray) -> pd.DataFrame:
    records = []
    for route in ROUTES:
        corridor = corridor_mask(route, lat, lon) & valid
        if not np.any(corridor):
            continue
        for phase in ("el_nino", "la_nina"):
            phase_month = (month == route.month) & (phases == phase)
            for event in sorted(set(events[phase_month])):
                held = phase_month & (events == event)
                training = phase_month & (events != event)
                all_years = (month == route.month) & ~held
                if not np.any(held) or np.sum(training) < 3:
                    continue
                observed = (np.nanmean(u[held], axis=0), np.nanmean(v[held], axis=0))
                for model, selected in (("correct_phase", training), ("all_years", all_years)):
                    pu, pv = np.nanmean(u[selected], axis=0), np.nanmean(v[selected], axis=0)
                    error = np.hypot(pu - observed[0], pv - observed[1])[corridor] * KNOTS_PER_MPS
                    records.append({"route": route.name, "month": route.month,
                                    "held_phase": phase, "event_id": event,
                                    "model": model,
                                    "vector_rmse_kn": float(np.sqrt(np.nanmean(error ** 2)))})
    detail = pd.DataFrame(records)
    pivot = detail.pivot_table(index=["route", "month", "held_phase", "event_id"],
                               columns="model", values="vector_rmse_kn").reset_index()
    pivot["skill_vs_all_pct"] = 100 * (pivot.all_years - pivot.correct_phase) / pivot.all_years
    return pivot.groupby(["route", "month", "held_phase"]).agg(
        omitted_events=("event_id", "count"),
        median_skill_vs_all_pct=("skill_vs_all_pct", "median"),
        mean_skill_vs_all_pct=("skill_vs_all_pct", "mean"),
        positive_skill_fraction=("skill_vs_all_pct", lambda x: float(np.mean(x > 0))),
    ).reset_index()


def analyse_pressure(cache: Path, classification: pd.DataFrame) -> pd.DataFrame:
    with xr.open_dataset(cache / "ncep-slp-monthly.nc") as source:
        field = source.slp.sel(time=slice("1950-01-01", "2025-12-31")).load()
    dates = pd.DatetimeIndex(field.time.values)
    phases, _ = phase_arrays(dates.year.to_numpy(), dates.month.to_numpy(), classification)
    values = np.asarray(field.values)
    valid = np.all(np.isfinite(values), axis=0)
    return scalar_signal(values, dates.year.to_numpy(), dates.month.to_numpy(), phases,
                         np.asarray(field.lat.values), valid, "NCEP R1 sea-level pressure", "hPa")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, default=Path("cache"))
    parser.add_argument("--output", type=Path, default=Path("results"))
    parser.add_argument(
        "--enso-html", type=Path,
        help="CPC ONI/RONI HTML table (defaults to CACHE/oni-v6.html)",
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    classification = load_classification(args.enso_html or args.cache / "oni-v6.html")
    analyse_pressure(args.cache, classification).to_csv(args.output / "global-pressure-signal.csv", index=False)

    with np.load(args.cache / "oisst-monthly-2deg.npz") as source:
        sst, lat = source["sst"], source["lat"]
        year, month = source["year"], source["month"]
    phases, _ = phase_arrays(year, month, classification)
    valid = np.mean(np.isfinite(sst), axis=0) > 0.9
    scalar_signal(sst, year, month, phases, lat, valid,
                  "NOAA OISST v2 high-resolution SST (2 degree sample)", "degC").to_csv(
                      args.output / "global-sst-signal.csv", index=False)

    godas_path = sorted(args.cache.glob("godas-current-1deg-*.npz"))[-1]
    with np.load(godas_path) as source:
        u, v, lat, lon = source["u"], source["v"], source["lat"], source["lon"]
        year, month = source["year"], source["month"]
    phases, events = phase_arrays(year, month, classification)
    valid = (np.mean(np.isfinite(u), axis=0) > 0.8) & (np.mean(np.isfinite(v), axis=0) > 0.8)
    vector_signal(u, v, month, phases, lat, valid).to_csv(
        args.output / "global-current-signal.csv", index=False)
    vector_loo(u, v, year, month, phases, events, lat, lon, valid).to_csv(
        args.output / "current-leave-one-event-out-summary.csv", index=False)


if __name__ == "__main__":
    main()
