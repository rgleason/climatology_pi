"""Run the bounded quantitative ENSO and baseline-modernisation study."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import heapq
import json
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy import stats
import xarray as xr

from enso import compare_legacy_table, load_classification


KNOTS_PER_MPS = 1.9438444924406
EARTH_RADIUS_NM = 3440.065
PHASES = ("neutral", "el_nino", "la_nina")


@dataclass(frozen=True)
class Route:
    name: str
    month: int
    start_lat: float
    start_lon: float
    end_lat: float
    end_lon: float
    rationale: str


ROUTES = (
    Route("Panama-Marquesas", 4, 8.9, -79.6, -9.8, -139.0,
          "eastern/central tropical Pacific; expected high ENSO sensitivity"),
    Route("California-Hawaii", 7, 34.0, -120.5, 21.3, -157.8,
          "subtropical northeast Pacific; potentially moderate sensitivity"),
    Route("Brisbane-Fiji", 11, -27.4, 153.2, -17.8, 177.4,
          "southwest Pacific warm season; potentially high sensitivity"),
    Route("Cape-Verde-Barbados", 1, 16.9, -25.0, 13.1, -59.6,
          "Atlantic trade-wind control route"),
    Route("Bermuda-Azores", 5, 32.3, -64.8, 38.5, -28.6,
          "North Atlantic subtropical passage; expected weak ENSO signal"),
    Route("Cape-Town-Fremantle", 12, -33.9, 18.4, -32.1, 115.7,
          "southern Indian Ocean passage; tests remote teleconnection"),
)


def angular_difference(a: float, b: float) -> float:
    return abs((a - b + 180.0) % 360.0 - 180.0)


def bearing(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dl = math.radians(((lon2 - lon1 + 180.0) % 360.0) - 180.0)
    y = math.sin(dl) * math.cos(p2)
    x = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
    return math.degrees(math.atan2(y, x)) % 360.0


def distance_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = p2 - p1
    dl = math.radians(((lon2 - lon1 + 180.0) % 360.0) - 180.0)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * EARTH_RADIUS_NM * math.asin(min(1.0, math.sqrt(a)))


def research_polar_speed(wind_knots: float, twa_deg: float) -> float:
    """Documented generic 35-foot cruiser proxy; never used by production."""
    angle = min(180.0, abs(twa_deg))
    if angle < 35.0:
        return 0.35
    angles = np.array([35, 45, 60, 90, 120, 150, 180], dtype=float)
    factors = np.array([0.22, 0.42, 0.62, 0.82, 0.90, 0.78, 0.62])
    factor = float(np.interp(angle, angles, factors))
    return max(0.35, min(8.5, factor * wind_knots))


def nearest_index(values: np.ndarray, value: float, longitude: bool = False) -> int:
    if longitude:
        value %= 360.0
        distance = np.abs((values - value + 180.0) % 360.0 - 180.0)
    else:
        distance = np.abs(values - value)
    return int(np.argmin(distance))


def ocean_mask(cache: Path) -> np.ndarray:
    with xr.open_dataset(cache / "ncep-land-sea-mask.nc") as dataset:
        return np.asarray(dataset.land.isel(time=0).values) < 0.5


def load_wind(cache: Path, classification: pd.DataFrame) -> xr.Dataset:
    with xr.open_dataset(cache / "ncep-uwnd-10m-monthly.nc") as source:
        u = source.uwnd.sel(time=slice("1950-01-01", "2025-12-31")).load()
    with xr.open_dataset(cache / "ncep-vwnd-10m-monthly.nc") as source:
        v = source.vwnd.sel(time=slice("1950-01-01", "2025-12-31")).load()
    dataset = xr.Dataset({"u": u, "v": v})
    lookup = classification.set_index("date")
    dates = pd.DatetimeIndex(dataset.time.values).to_period("M").to_timestamp()
    dataset = dataset.assign_coords(
        phase=("time", lookup.loc[dates, "phase"].to_numpy()),
        event_id=("time", lookup.loc[dates, "event_id"].to_numpy()),
        oni_c=("time", lookup.loc[dates, "oni_c"].to_numpy()),
    )
    return dataset


def weighted_fraction(mask: np.ndarray, lat: np.ndarray, ocean: np.ndarray) -> float:
    weights = np.cos(np.deg2rad(lat))[:, None] * ocean
    return float(np.sum(weights * mask) / np.sum(weights))


def fdr_bh(pvalues: np.ndarray, alpha: float = 0.05) -> np.ndarray:
    flat = pvalues.ravel()
    finite = np.isfinite(flat)
    values = flat[finite]
    order = np.argsort(values)
    ranked = values[order]
    thresholds = alpha * np.arange(1, len(ranked) + 1) / max(1, len(ranked))
    accepted = ranked <= thresholds
    cutoff = ranked[np.where(accepted)[0][-1]] if np.any(accepted) else -1.0
    result = np.zeros(flat.shape, dtype=bool)
    result[finite] = values <= cutoff
    return result.reshape(pvalues.shape)


def sample_counts(dataset: xr.Dataset) -> pd.DataFrame:
    records = []
    times = pd.DatetimeIndex(dataset.time.values)
    phases = np.asarray(dataset.phase.values)
    events = np.asarray(dataset.event_id.values)
    for month in range(1, 13):
        for phase in PHASES:
            selected = (times.month == month) & (phases == phase)
            selected_events = events[selected]
            if phase == "neutral":
                effective = int(np.sum(selected))
            else:
                effective = len(set(selected_events))
            records.append({"month": month, "phase": phase,
                            "sample_years": int(np.sum(selected)),
                            "independent_events": effective})
    return pd.DataFrame(records)


def global_signal(dataset: xr.Dataset, ocean: np.ndarray) -> pd.DataFrame:
    records = []
    times = pd.DatetimeIndex(dataset.time.values)
    phase_values = np.asarray(dataset.phase.values)
    lat = np.asarray(dataset.lat.values)
    for month in range(1, 13):
        neutral = (times.month == month) & (phase_values == "neutral")
        for phase in ("el_nino", "la_nina"):
            selected = (times.month == month) & (phase_values == phase)
            a_u = np.asarray(dataset.u.values[selected])
            a_v = np.asarray(dataset.v.values[selected])
            n_u = np.asarray(dataset.u.values[neutral])
            n_v = np.asarray(dataset.v.values[neutral])
            du = np.nanmean(a_u, axis=0) - np.nanmean(n_u, axis=0)
            dv = np.nanmean(a_v, axis=0) - np.nanmean(n_v, axis=0)
            magnitude = np.hypot(du, dv) * KNOTS_PER_MPS
            pooled = np.sqrt((np.nanvar(a_u, axis=0, ddof=1) + np.nanvar(n_u, axis=0, ddof=1) +
                              np.nanvar(a_v, axis=0, ddof=1) + np.nanvar(n_v, axis=0, ddof=1)) / 2)
            snr = np.hypot(du, dv) / np.where(pooled > 0, pooled, np.nan)
            p_u = stats.ttest_ind(a_u, n_u, axis=0, equal_var=False, nan_policy="omit").pvalue
            p_v = stats.ttest_ind(a_v, n_v, axis=0, equal_var=False, nan_policy="omit").pvalue
            component_bonferroni = np.minimum(1.0, 2.0 * np.minimum(p_u, p_v))
            accepted = fdr_bh(np.where(ocean, component_bonferroni, np.nan))
            marine_values = magnitude[ocean]
            records.append({
                "month": month, "phase": phase,
                "sample_years": int(np.sum(selected)),
                "median_vector_anomaly_kn": float(np.nanmedian(marine_values)),
                "p90_vector_anomaly_kn": float(np.nanpercentile(marine_values, 90)),
                "marine_area_fdr_005_pct": 100 * weighted_fraction(accepted, lat, ocean),
                "marine_area_snr_ge_0_5_pct": 100 * weighted_fraction(snr >= 0.5, lat, ocean),
            })
    return pd.DataFrame(records)


def corridor_mask(route: Route, lat: np.ndarray, lon: np.ndarray,
                  buffer_deg: float = 12.0) -> np.ndarray:
    # Points close to one of 41 great-circle-linear samples; adequate at this
    # coarse screening grid and explicit about its approximation.
    mask = np.zeros((len(lat), len(lon)), dtype=bool)
    for fraction in np.linspace(0, 1, 41):
        plat = route.start_lat + fraction * (route.end_lat - route.start_lat)
        delta = ((route.end_lon - route.start_lon + 180) % 360) - 180
        plon = (route.start_lon + fraction * delta) % 360
        lat_ok = np.abs(lat - plat) <= buffer_deg
        lon_distance = np.abs((lon - plon + 180) % 360 - 180)
        mask |= lat_ok[:, None] & (lon_distance[None, :] <= buffer_deg)
    return mask


def leave_one_event_out(dataset: xr.Dataset, ocean: np.ndarray) -> pd.DataFrame:
    records = []
    times = pd.DatetimeIndex(dataset.time.values)
    phases = np.asarray(dataset.phase.values)
    event_ids = np.asarray(dataset.event_id.values)
    lat = np.asarray(dataset.lat.values)
    lon = np.asarray(dataset.lon.values)
    u = np.asarray(dataset.u.values)
    v = np.asarray(dataset.v.values)
    for route in ROUTES:
        corridor = corridor_mask(route, lat, lon) & ocean
        for phase in ("el_nino", "la_nina"):
            phase_month = (times.month == route.month) & (phases == phase)
            for event in sorted(set(event_ids[phase_month])):
                held = phase_month & (event_ids == event)
                train_phase = phase_month & (event_ids != event)
                train_all = (times.month == route.month) & ~held
                opposite_name = "la_nina" if phase == "el_nino" else "el_nino"
                opposite = (times.month == route.month) & (phases == opposite_name)
                observed_u, observed_v = np.nanmean(u[held], axis=0), np.nanmean(v[held], axis=0)
                predictions = {
                    "correct_phase": (np.nanmean(u[train_phase], axis=0), np.nanmean(v[train_phase], axis=0)),
                    "all_years": (np.nanmean(u[train_all], axis=0), np.nanmean(v[train_all], axis=0)),
                    "opposite_phase": (np.nanmean(u[opposite], axis=0), np.nanmean(v[opposite], axis=0)),
                }
                for model, (pu, pv) in predictions.items():
                    error = np.hypot(pu - observed_u, pv - observed_v)[corridor] * KNOTS_PER_MPS
                    records.append({"route": route.name, "month": route.month,
                                    "held_phase": phase, "event_id": event,
                                    "model": model,
                                    "vector_rmse_kn": float(np.sqrt(np.nanmean(error ** 2))),
                                    "vector_mae_kn": float(np.nanmean(error))})
    return pd.DataFrame(records)


def dijkstra_route(u: np.ndarray, v: np.ndarray, lat: np.ndarray, lon: np.ndarray,
                   ocean: np.ndarray, route: Route) -> tuple[float, list[tuple[float, float]]]:
    start = (nearest_index(lat, route.start_lat), nearest_index(lon, route.start_lon, True))
    goal = (nearest_index(lat, route.end_lat), nearest_index(lon, route.end_lon, True))
    allowed = ocean.copy()
    # Reanalysis land masks can mark harbour endpoint cells as land.
    allowed[start] = True
    allowed[goal] = True
    queue = [(0.0, start)]
    cost = {start: 0.0}
    previous: dict[tuple[int, int], tuple[int, int]] = {}
    nlat, nlon = allowed.shape
    while queue:
        elapsed, node = heapq.heappop(queue)
        if elapsed != cost.get(node):
            continue
        if node == goal:
            break
        i, j = node
        for di, dj in ((-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1),
                       (1, -1), (1, 0), (1, 1)):
            ni, nj = i + di, (j + dj) % nlon
            if ni < 0 or ni >= nlat or not allowed[ni, nj]:
                continue
            segment = distance_nm(float(lat[i]), float(lon[j]), float(lat[ni]), float(lon[nj]))
            course = bearing(float(lat[i]), float(lon[j]), float(lat[ni]), float(lon[nj]))
            wind_from = math.degrees(math.atan2(-float(u[i, j]), -float(v[i, j]))) % 360
            wind_knots = math.hypot(float(u[i, j]), float(v[i, j])) * KNOTS_PER_MPS
            speed = research_polar_speed(wind_knots, angular_difference(course, wind_from))
            candidate = elapsed + segment / speed
            neighbour = (ni, nj)
            if candidate < cost.get(neighbour, math.inf):
                cost[neighbour] = candidate
                previous[neighbour] = node
                heapq.heappush(queue, (candidate, neighbour))
    if goal not in cost:
        return math.inf, []
    nodes = [goal]
    while nodes[-1] != start:
        nodes.append(previous[nodes[-1]])
    nodes.reverse()
    return cost[goal], [(float(lat[i]), float(((lon[j] + 180) % 360) - 180)) for i, j in nodes]


def route_experiments(dataset: xr.Dataset, ocean: np.ndarray,
                      output: Path) -> pd.DataFrame:
    times = pd.DatetimeIndex(dataset.time.values)
    phases = np.asarray(dataset.phase.values)
    lat, lon = np.asarray(dataset.lat.values), np.asarray(dataset.lon.values)
    u, v = np.asarray(dataset.u.values), np.asarray(dataset.v.values)
    records, paths = [], {}
    for route in ROUTES:
        month_mask = times.month == route.month
        route_results = []
        for phase in ("all_years",) + PHASES:
            selected = month_mask if phase == "all_years" else month_mask & (phases == phase)
            mean_u, mean_v = np.nanmean(u[selected], axis=0), np.nanmean(v[selected], axis=0)
            hours, path = dijkstra_route(mean_u, mean_v, lat, lon, ocean, route)
            distance = sum(distance_nm(*a, *b) for a, b in zip(path, path[1:])) if path else math.nan
            record = {"route": route.name, "month": route.month, "regime": phase,
                      "sample_years": int(np.sum(selected)), "passage_hours": hours,
                      "route_distance_nm": distance,
                      "rationale": route.rationale}
            records.append(record)
            route_results.append(record)
            paths[f"{route.name}:{phase}"] = path
        baseline = next(item for item in route_results if item["regime"] == "all_years")
        for item in route_results:
            item["eta_difference_hours_vs_all"] = item["passage_hours"] - baseline["passage_hours"]
            item["distance_difference_nm_vs_all"] = item["route_distance_nm"] - baseline["route_distance_nm"]
    (output / "route-paths.json").write_text(json.dumps(paths, indent=2) + "\n", encoding="utf-8")
    return pd.DataFrame(records)


def baseline_drift(dataset: xr.Dataset, ocean: np.ndarray) -> pd.DataFrame:
    periods = {"1981-2010": (1981, 2010), "1991-2020": (1991, 2020),
               "1996-2025": (1996, 2025), "1950-2025": (1950, 2025)}
    times = pd.DatetimeIndex(dataset.time.values)
    u, v = np.asarray(dataset.u.values), np.asarray(dataset.v.values)
    records = []
    for month in range(1, 13):
        fields = {}
        for label, (start, stop) in periods.items():
            selected = (times.month == month) & (times.year >= start) & (times.year <= stop)
            fields[label] = (np.nanmean(u[selected], axis=0), np.nanmean(v[selected], axis=0))
        reference = fields["1991-2020"]
        for label, field in fields.items():
            anomaly = np.hypot(field[0] - reference[0], field[1] - reference[1]) * KNOTS_PER_MPS
            records.append({"month": month, "period": label,
                            "reference": "1991-2020",
                            "marine_median_vector_difference_kn": float(np.nanmedian(anomaly[ocean])),
                            "marine_p90_vector_difference_kn": float(np.nanpercentile(anomaly[ocean], 90))})
    return pd.DataFrame(records)


def plot_maps(dataset: xr.Dataset, ocean: np.ndarray, output: Path) -> None:
    times = pd.DatetimeIndex(dataset.time.values)
    phases = np.asarray(dataset.phase.values)
    lon = ((np.asarray(dataset.lon.values) + 180) % 360) - 180
    order = np.argsort(lon)
    for month, phase in ((4, "el_nino"), (7, "el_nino"), (11, "la_nina"), (1, "el_nino")):
        selected = (times.month == month) & (phases == phase)
        neutral = (times.month == month) & (phases == "neutral")
        du = np.nanmean(dataset.u.values[selected], axis=0) - np.nanmean(dataset.u.values[neutral], axis=0)
        dv = np.nanmean(dataset.v.values[selected], axis=0) - np.nanmean(dataset.v.values[neutral], axis=0)
        magnitude = np.where(ocean, np.hypot(du, dv) * KNOTS_PER_MPS, np.nan)
        fig, ax = plt.subplots(figsize=(11, 4.8), constrained_layout=True)
        image = ax.pcolormesh(lon[order], dataset.lat.values, magnitude[:, order],
                              shading="auto", cmap="viridis", vmin=0, vmax=4)
        ax.set(xlabel="Longitude", ylabel="Latitude",
               title=f"{phase.replace('_', ' ').title()} - neutral monthly mean wind-vector anomaly; month {month}")
        fig.colorbar(image, ax=ax, label="knots (NCEP R1 screening experiment)")
        fig.savefig(output / f"wind-anomaly-month-{month:02d}-{phase}.png", dpi=140)
        plt.close(fig)


def summarize_loo(frame: pd.DataFrame) -> pd.DataFrame:
    pivot = frame.pivot_table(index=["route", "month", "held_phase", "event_id"],
                              columns="model", values="vector_rmse_kn").reset_index()
    pivot["skill_vs_all_pct"] = 100 * (pivot.all_years - pivot.correct_phase) / pivot.all_years
    pivot["skill_vs_opposite_pct"] = 100 * (pivot.opposite_phase - pivot.correct_phase) / pivot.opposite_phase
    rng = np.random.default_rng(20260825)
    records = []
    for keys, group in pivot.groupby(["route", "month", "held_phase"], sort=True):
        values = group.skill_vs_all_pct.to_numpy()
        boot = np.mean(rng.choice(values, size=(5000, len(values)), replace=True), axis=1)
        records.append({
            "route": keys[0], "month": keys[1], "held_phase": keys[2],
            "omitted_events": len(group),
            "median_skill_vs_all_pct": float(np.median(values)),
            "mean_skill_vs_all_pct": float(np.mean(values)),
            "mean_skill_bootstrap_ci025_pct": float(np.percentile(boot, 2.5)),
            "mean_skill_bootstrap_ci975_pct": float(np.percentile(boot, 97.5)),
            "positive_skill_fraction": float(np.mean(values > 0)),
            "median_skill_vs_opposite_pct": float(np.median(group.skill_vs_opposite_pct)),
        })
    return pd.DataFrame(records)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, default=Path("cache"))
    parser.add_argument("--output", type=Path, default=Path("results"))
    parser.add_argument("--enso-html", type=Path)
    parser.add_argument("--legacy-oni", type=Path,
                        default=Path(__file__).resolve().parents[2] / "data" / "elnino_years.txt")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    enso_path = args.enso_html or (args.cache / "oni-v6.html")
    classified = load_classification(enso_path)
    classified.to_csv(args.output / "enso-month-classification.csv", index=False)
    mismatch = compare_legacy_table(classified, args.legacy_oni)
    mismatch.to_csv(args.output / "production-oni-mismatches.csv", index=False)
    wind = load_wind(args.cache, classified)
    ocean = ocean_mask(args.cache)
    counts = sample_counts(wind)
    counts.to_csv(args.output / "enso-sample-counts.csv", index=False)
    signal = global_signal(wind, ocean)
    signal.to_csv(args.output / "global-wind-signal.csv", index=False)
    loo = leave_one_event_out(wind, ocean)
    loo.to_csv(args.output / "leave-one-event-out-detail.csv", index=False)
    summarize_loo(loo).to_csv(args.output / "leave-one-event-out-summary.csv", index=False)
    routes = route_experiments(wind, ocean, args.output)
    routes.to_csv(args.output / "route-proxy-results.csv", index=False)
    baseline_drift(wind, ocean).to_csv(args.output / "baseline-drift.csv", index=False)
    plot_maps(wind, ocean, args.output)
    metadata = {
        "schema": 1,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "screening_dataset": "NCEP/NCAR Reanalysis 1 monthly means",
        "analysis_period": ["1950-01", "2025-12"],
        "enso": ("CPC ERSSTv6 relative ONI (RONI)" if "roni" in enso_path.name.lower()
                 else "CPC ERSSTv6 ONI")
                + ", |0.5 C|, at least five consecutive overlapping seasons",
        "multiple_testing": "component-wise Welch tests; within-cell Bonferroni then global Benjamini-Hochberg FDR 0.05",
        "out_of_sample_unit": "complete qualified ENSO event",
        "route_model": "coarse-grid documented generic 35-foot-cruiser screening proxy; not production Weather Routing",
        "routes": [asdict(route) for route in ROUTES],
    }
    (args.output / "experiment-metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
