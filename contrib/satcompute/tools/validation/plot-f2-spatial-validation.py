#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "matplotlib>=3.5,<4",
#   "numpy>=1.21,<3",
# ]
# ///
"""Plot real orbit-only F2 risk and exposure-normalized fault evidence."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import Normalize
from matplotlib.patches import Rectangle


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot the accepted long-horizon SatCompute F2 spatial validation"
    )
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--bins", type=Path, required=True)
    parser.add_argument("--events", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--minimum-bin-exposure",
        type=int,
        default=1000,
        help="mask empirical bins with fewer eligible satellite-seconds",
    )
    parser.add_argument(
        "--allow-unaccepted",
        action="store_true",
        help="allow diagnostic plots from short runs that did not pass acceptance",
    )
    return parser.parse_args()


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def validate_inputs(
    summary: dict[str, object],
    bins: list[dict[str, str]],
    events: list[dict[str, str]],
    *,
    require_accepted: bool,
) -> None:
    acceptance = summary["acceptance"]
    sampling = summary["sampling"]
    if not isinstance(acceptance, dict) or (
        require_accepted and not acceptance.get("all_passed")
    ):
        raise ValueError("refusing to plot an F2 run that did not pass acceptance")
    if not isinstance(sampling, dict):
        raise ValueError("summary sampling section is invalid")
    expected_events = int(sampling["actual_fault_count"])
    binned_events = sum(int(row["fault_count"]) for row in bins)
    if len(events) != expected_events or binned_events != expected_events:
        raise ValueError("summary, bin, and event fault totals do not agree")
    if not bins or not events:
        raise ValueError("accepted F2 evidence cannot be empty")


def risk_field(parameters: dict[str, object]) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    longitude = np.linspace(
        float(parameters["longitude_min_deg"]),
        float(parameters["longitude_max_deg"]),
        381,
    )
    latitude = np.linspace(
        float(parameters["latitude_min_deg"]),
        float(parameters["latitude_max_deg"]),
        221,
    )
    longitude_grid, latitude_grid = np.meshgrid(longitude, latitude)
    longitude_offset = (
        longitude_grid - float(parameters["hotspot_longitude_deg"])
    ) / float(parameters["sigma_longitude_deg"])
    latitude_offset = (
        latitude_grid - float(parameters["hotspot_latitude_deg"])
    ) / float(parameters["sigma_latitude_deg"])
    risk = np.exp(-0.5 * (longitude_offset**2 + latitude_offset**2))
    return longitude_grid, latitude_grid, risk


def empirical_grid(
    bins: list[dict[str, str]], minimum_exposure: int
) -> tuple[np.ndarray, np.ndarray, np.ma.MaskedArray]:
    longitude_edges = sorted(
        {float(row["longitude_min_deg"]) for row in bins}
        | {float(row["longitude_max_deg"]) for row in bins}
    )
    latitude_edges = sorted(
        {float(row["latitude_min_deg"]) for row in bins}
        | {float(row["latitude_max_deg"]) for row in bins}
    )
    longitude_index = {value: index for index, value in enumerate(longitude_edges[:-1])}
    latitude_index = {value: index for index, value in enumerate(latitude_edges[:-1])}
    rates = np.full((len(latitude_edges) - 1, len(longitude_edges) - 1), np.nan)
    for row in bins:
        exposure = int(row["eligible_exposure_s"])
        if exposure < minimum_exposure:
            continue
        latitude = latitude_index[float(row["latitude_min_deg"])]
        longitude = longitude_index[float(row["longitude_min_deg"])]
        rates[latitude, longitude] = float(row["fault_rate_per_million_exposure_s"])
    return np.asarray(longitude_edges), np.asarray(latitude_edges), np.ma.masked_invalid(rates)


def add_region_and_hotspot(
    axis: plt.Axes, parameters: dict[str, object], *, hotspot_color: str
) -> None:
    longitude_min = float(parameters["longitude_min_deg"])
    longitude_max = float(parameters["longitude_max_deg"])
    latitude_min = float(parameters["latitude_min_deg"])
    latitude_max = float(parameters["latitude_max_deg"])
    axis.add_patch(
        Rectangle(
            (longitude_min, latitude_min),
            longitude_max - longitude_min,
            latitude_max - latitude_min,
            fill=False,
            edgecolor="black",
            linewidth=1.0,
            linestyle="--",
        )
    )
    axis.scatter(
        [float(parameters["hotspot_longitude_deg"])],
        [float(parameters["hotspot_latitude_deg"])],
        marker="*",
        s=110,
        c=hotspot_color,
        edgecolors="black",
        linewidths=0.7,
        label="Reference hotspot",
        zorder=5,
    )
    axis.set_xlim(longitude_min, longitude_max)
    axis.set_ylim(latitude_min, latitude_max)
    axis.set_xlabel("Longitude (degrees)")
    axis.set_ylabel("Latitude (degrees)")
    axis.set_aspect("equal", adjustable="box")
    axis.grid(color="white", alpha=0.16, linewidth=0.5)


def plot(
    summary: dict[str, object],
    bins: list[dict[str, str]],
    events: list[dict[str, str]],
    output: Path,
    minimum_exposure: int,
) -> None:
    parameters = summary["f2_parameters"]
    run = summary["run"]
    sampling = summary["sampling"]
    spatial = summary["spatial_validation"]
    if not all(isinstance(section, dict) for section in (parameters, run, sampling, spatial)):
        raise ValueError("summary sections are invalid")

    longitude_grid, latitude_grid, risk = risk_field(parameters)
    longitude_edges, latitude_edges, empirical_rate = empirical_grid(
        bins, minimum_exposure
    )
    finite_rates = empirical_rate.compressed()
    if finite_rates.size == 0:
        raise ValueError("no empirical bin meets the exposure threshold")
    upper_rate = float(np.percentile(finite_rates, 98.0))
    if not math.isfinite(upper_rate) or upper_rate <= 0.0:
        upper_rate = float(np.max(finite_rates))

    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 9,
            "axes.titlesize": 10,
            "axes.labelsize": 9,
            "legend.fontsize": 8,
        }
    )
    figure, axes = plt.subplots(1, 2, figsize=(11.4, 5.1))

    risk_levels = np.linspace(0.0, 1.0, 21)
    risk_image = axes[0].contourf(
        longitude_grid,
        latitude_grid,
        risk,
        levels=risk_levels,
        cmap="viridis",
        norm=Normalize(0.0, 1.0),
    )
    threshold = float(parameters["spatial_risk_threshold"])
    threshold_contour = axes[0].contour(
        longitude_grid,
        latitude_grid,
        risk,
        levels=[threshold],
        colors=["white"],
        linewidths=1.4,
    )
    axes[0].clabel(
        threshold_contour,
        fmt={threshold: rf"NOTICE $w_{{F2}}={threshold:g}$"},
        inline=True,
        fontsize=8,
    )
    add_region_and_hotspot(axes[0], parameters, hotspot_color="#ffcc33")
    axes[0].set_title("a  Parametric F2 spatial risk field", loc="left", fontweight="bold")
    axes[0].legend(loc="upper right", frameon=True)
    risk_colorbar = figure.colorbar(risk_image, ax=axes[0], fraction=0.046, pad=0.03)
    risk_colorbar.set_label(r"Spatial risk $w_{F2}$")

    rate_image = axes[1].pcolormesh(
        longitude_edges,
        latitude_edges,
        empirical_rate,
        cmap="magma",
        shading="flat",
        vmin=0.0,
        vmax=upper_rate,
    )
    event_longitudes = np.asarray([float(event["longitude_deg"]) for event in events])
    event_latitudes = np.asarray([float(event["latitude_deg"]) for event in events])
    axes[1].scatter(
        event_longitudes,
        event_latitudes,
        s=4,
        facecolors="none",
        edgecolors="white",
        linewidths=0.25,
        alpha=0.4,
        label="Actual F2 fault",
        zorder=3,
    )
    add_region_and_hotspot(axes[1], parameters, hotspot_color="#38d6ff")
    axes[1].set_title(
        "b  Exposure-normalized simulated F2 faults",
        loc="left",
        fontweight="bold",
    )
    axes[1].legend(loc="upper right", frameon=True)
    rate_colorbar = figure.colorbar(rate_image, ax=axes[1], fraction=0.046, pad=0.03)
    rate_colorbar.set_label(r"Faults per $10^6$ eligible satellite-seconds")

    duration = int(run["duration_s"])
    fault_count = int(sampling["actual_fault_count"])
    expected = float(sampling["expected_fault_count_with_observed_recovery_suppression"])
    correlation = float(spatial["risk_rate_pearson_correlation"])
    figure.suptitle(
        "SatCompute F2 native-orbit spatial validation\n"
        f"{duration:,} s ({duration / 86400.0:.2f} days), "
        f"{fault_count:,} faults, conditional expectation {expected:.1f}, "
        f"bin risk-rate r={correlation:.3f}",
        fontsize=11,
        fontweight="bold",
        y=0.98,
    )
    figure.subplots_adjust(left=0.06, right=0.96, bottom=0.17, top=0.78, wspace=0.30)
    figure.text(
        0.5,
        0.035,
        f"{float(run['longitude_bin_deg']):g} x "
        f"{float(run['latitude_bin_deg']):g} degree bins; empirical bins require at least "
        f"{minimum_exposure:,} "
        "eligible satellite-seconds. White circles show sampled fault locations.",
        ha="center",
        va="bottom",
        fontsize=8,
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output, dpi=300, bbox_inches="tight", pad_inches=0.12, facecolor="white")
    plt.close(figure)


def main() -> None:
    args = parse_args()
    with args.summary.open(encoding="utf-8") as stream:
        summary = json.load(stream)
    bins = load_csv(args.bins)
    events = load_csv(args.events)
    validate_inputs(
        summary,
        bins,
        events,
        require_accepted=not args.allow_unaccepted,
    )
    plot(summary, bins, events, args.output, args.minimum_bin_exposure)
    print(f"PASS output={args.output}")


if __name__ == "__main__":
    main()
