#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "matplotlib>=3.5,<4",
#   "numpy>=1.21,<3",
# ]
# ///
"""Plot publication-ready F2 model risk and long-horizon fault evidence."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LinearSegmentedColormap, Normalize, PowerNorm
from matplotlib.patches import Rectangle


VIEW_MARGIN_DEG = 10.0
RISK_FIELD_RESOLUTION_DEG = 0.5
FAULT_COUNT_DISPLAY_GAMMA = 0.65
PAPER_BLUE_LOW_TO_HIGH = (
    "#F4F9FE",
    "#D2E3F3",
    "#AACFE5",
    "#68ACD5",
    "#3888C0",
    "#105CA4",
    "#08336E",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot accepted long-horizon SatCompute F2 spatial validation"
    )
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--bins", type=Path, required=True)
    parser.add_argument("--events", type=Path, required=True)
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="PNG output path; matching SVG and PDF files are also written",
    )
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
    parameters = summary["f2_parameters"]
    if not isinstance(acceptance, dict) or (
        require_accepted and not acceptance.get("all_passed")
    ):
        raise ValueError("refusing to plot an F2 run that did not pass acceptance")
    if not isinstance(sampling, dict) or not isinstance(parameters, dict):
        raise ValueError("summary sampling or F2 parameter section is invalid")
    asymmetric_keys = {
        "sigma_longitude_west_deg",
        "sigma_longitude_east_deg",
        "sigma_latitude_deg",
    }
    if not asymmetric_keys.issubset(parameters):
        raise ValueError("summary does not describe the asymmetric F2 model")
    expected_events = int(sampling["actual_fault_count"])
    binned_events = sum(int(row["fault_count"]) for row in bins)
    if len(events) != expected_events or binned_events != expected_events:
        raise ValueError("summary, bin, and event fault totals do not agree")
    if not bins or not events:
        raise ValueError("accepted F2 evidence cannot be empty")


def publication_colormap() -> LinearSegmentedColormap:
    return LinearSegmentedColormap.from_list(
        "satcompute_f2_blue",
        PAPER_BLUE_LOW_TO_HIGH,
        N=256,
    )


def geographic_view(parameters: dict[str, object]) -> tuple[float, float, float, float]:
    """Return the configured risk rectangle with a fixed display-only margin."""
    return (
        float(parameters["longitude_min_deg"]) - VIEW_MARGIN_DEG,
        float(parameters["longitude_max_deg"]) + VIEW_MARGIN_DEG,
        float(parameters["latitude_min_deg"]) - VIEW_MARGIN_DEG,
        float(parameters["latitude_max_deg"]) + VIEW_MARGIN_DEG,
    )


def risk_field(parameters: dict[str, object]) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    longitude_min, longitude_max, latitude_min, latitude_max = geographic_view(
        parameters
    )
    longitude_samples = (
        int(round((longitude_max - longitude_min) / RISK_FIELD_RESOLUTION_DEG)) + 1
    )
    latitude_samples = (
        int(round((latitude_max - latitude_min) / RISK_FIELD_RESOLUTION_DEG)) + 1
    )
    longitude = np.linspace(longitude_min, longitude_max, longitude_samples)
    latitude = np.linspace(latitude_min, latitude_max, latitude_samples)
    longitude_grid, latitude_grid = np.meshgrid(longitude, latitude)
    longitude_delta = longitude_grid - float(parameters["hotspot_longitude_deg"])
    longitude_sigma = np.where(
        longitude_delta < 0.0,
        float(parameters["sigma_longitude_west_deg"]),
        float(parameters["sigma_longitude_east_deg"]),
    )
    longitude_offset = longitude_delta / longitude_sigma
    latitude_offset = (
        latitude_grid - float(parameters["hotspot_latitude_deg"])
    ) / float(parameters["sigma_latitude_deg"])
    in_region = (
        (longitude_grid >= float(parameters["longitude_min_deg"]))
        & (longitude_grid <= float(parameters["longitude_max_deg"]))
        & (latitude_grid >= float(parameters["latitude_min_deg"]))
        & (latitude_grid <= float(parameters["latitude_max_deg"]))
    )
    risk = np.zeros_like(longitude_grid)
    risk[in_region] = np.exp(
        -0.5
        * (
            longitude_offset[in_region] ** 2
            + latitude_offset[in_region] ** 2
        )
    )
    return longitude_grid, latitude_grid, risk


def empirical_fault_count_grid(
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
    shape = (len(latitude_edges) - 1, len(longitude_edges) - 1)
    counts = np.zeros(shape, dtype=float)
    valid = np.zeros(shape, dtype=bool)
    for row in bins:
        exposure = int(row["eligible_exposure_s"])
        if exposure < minimum_exposure:
            continue
        latitude = latitude_index[float(row["latitude_min_deg"])]
        longitude = longitude_index[float(row["longitude_min_deg"])]
        counts[latitude, longitude] = int(row["fault_count"])
        valid[latitude, longitude] = True
    return (
        np.asarray(longitude_edges),
        np.asarray(latitude_edges),
        np.ma.masked_where(~valid, counts),
    )


def add_geographic_frame(
    axis: plt.Axes,
    parameters: dict[str, object],
    *,
    label_hotspot: bool,
) -> None:
    longitude_min = float(parameters["longitude_min_deg"])
    longitude_max = float(parameters["longitude_max_deg"])
    latitude_min = float(parameters["latitude_min_deg"])
    latitude_max = float(parameters["latitude_max_deg"])
    hotspot_longitude = float(parameters["hotspot_longitude_deg"])
    hotspot_latitude = float(parameters["hotspot_latitude_deg"])
    view_longitude_min, view_longitude_max, view_latitude_min, view_latitude_max = (
        geographic_view(parameters)
    )
    axis.add_patch(
        Rectangle(
            (longitude_min, latitude_min),
            longitude_max - longitude_min,
            latitude_max - latitude_min,
            fill=False,
            edgecolor="#64748B",
            linewidth=0.7,
            linestyle=(0, (3, 2)),
            zorder=4,
        )
    )
    axis.scatter(
        [hotspot_longitude],
        [hotspot_latitude],
        marker="*",
        s=42,
        c="#F4F9FE",
        edgecolors="#08336E",
        linewidths=0.7,
        zorder=6,
    )
    if label_hotspot:
        axis.annotate(
            "reference hotspot",
            xy=(hotspot_longitude, hotspot_latitude),
            xytext=(hotspot_longitude + 8.0, hotspot_latitude + 12.0),
            fontsize=5.8,
            color="#08336E",
            arrowprops={
                "arrowstyle": "-",
                "color": "#08336E",
                "linewidth": 0.5,
            },
        )
    axis.set_xlim(view_longitude_min, view_longitude_max)
    axis.set_ylim(view_latitude_min, view_latitude_max)
    longitude_tick_start = np.ceil(view_longitude_min / 30.0) * 30.0
    latitude_tick_start = np.ceil(view_latitude_min / 15.0) * 15.0
    axis.set_xticks(np.arange(longitude_tick_start, view_longitude_max + 1.0, 30.0))
    axis.set_yticks(np.arange(latitude_tick_start, view_latitude_max + 1.0, 15.0))
    axis.set_xlabel("Longitude (degrees)")
    axis.set_ylabel("Latitude (degrees)")
    axis.set_aspect("equal", adjustable="box")
    axis.set_axisbelow(True)
    axis.grid(color="#D7E0EA", linewidth=0.35)
    for spine in axis.spines.values():
        spine.set_color("#5C6773")
        spine.set_linewidth(0.55)


def export_figure(figure: plt.Figure, output: Path) -> tuple[Path, Path, Path]:
    if output.suffix.lower() != ".png":
        raise ValueError("--output must end in .png")
    output.parent.mkdir(parents=True, exist_ok=True)
    svg_output = output.with_suffix(".svg")
    pdf_output = output.with_suffix(".pdf")
    common = {
        "bbox_inches": "tight",
        "pad_inches": 0.05,
        "facecolor": "white",
    }
    figure.savefig(output, dpi=600, **common)
    figure.savefig(svg_output, **common)
    svg_text = svg_output.read_text(encoding="utf-8")
    svg_output.write_text(
        "\n".join(line.rstrip() for line in svg_text.splitlines()) + "\n",
        encoding="utf-8",
    )
    figure.savefig(pdf_output, **common)
    return output, svg_output, pdf_output


def plot(
    summary: dict[str, object],
    bins: list[dict[str, str]],
    events: list[dict[str, str]],
    output: Path,
    minimum_exposure: int,
) -> tuple[Path, Path, Path]:
    parameters = summary["f2_parameters"]
    run = summary["run"]
    sampling = summary["sampling"]
    spatial = summary["spatial_validation"]
    if not all(isinstance(section, dict) for section in (parameters, run, sampling, spatial)):
        raise ValueError("summary sections are invalid")

    longitude_grid, latitude_grid, risk = risk_field(parameters)
    longitude_edges, latitude_edges, raw_counts = empirical_fault_count_grid(
        bins, minimum_exposure
    )
    finite_counts = raw_counts.compressed()
    if finite_counts.size == 0:
        raise ValueError("no empirical bin meets the exposure threshold")
    raw_peak = int(np.max(finite_counts))
    if raw_peak <= 0:
        raise ValueError("raw empirical fault grid has no positive count")
    upper_count = float(raw_peak)

    plt.rcParams.update(
        {
            "font.family": "sans-serif",
            "font.sans-serif": ["Arial", "Helvetica", "DejaVu Sans", "sans-serif"],
            "font.size": 6.5,
            "axes.titlesize": 7.2,
            "axes.labelsize": 6.5,
            "xtick.labelsize": 5.8,
            "ytick.labelsize": 5.8,
            "svg.fonttype": "none",
            "pdf.fonttype": 42,
        }
    )
    figure, axes = plt.subplots(1, 2, figsize=(7.2, 3.25))
    color_map = publication_colormap()

    risk_levels = np.linspace(0.0, 1.0, 41)
    risk_image = axes[0].contourf(
        longitude_grid,
        latitude_grid,
        risk,
        levels=risk_levels,
        cmap=color_map,
        norm=Normalize(0.0, 1.0),
    )
    threshold = float(parameters["spatial_risk_threshold"])
    threshold_contour = axes[0].contour(
        longitude_grid,
        latitude_grid,
        risk,
        levels=[threshold],
        colors=["#08336E"],
        linewidths=0.8,
        linestyles="--",
    )
    axes[0].clabel(
        threshold_contour,
        fmt={threshold: rf"NOTICE $w_{{F2}}={threshold:g}$"},
        inline=True,
        fontsize=5.5,
    )
    add_geographic_frame(axes[0], parameters, label_hotspot=True)
    axes[0].set_title(
        "a   Asymmetric F2 spatial risk",
        loc="left",
        fontweight="bold",
        pad=5.0,
    )
    risk_colorbar = figure.colorbar(
        risk_image,
        ax=axes[0],
        orientation="horizontal",
        fraction=0.07,
        pad=0.20,
        aspect=32,
    )
    risk_colorbar.set_ticks([0.0, 0.25, 0.5, 0.75, 1.0])
    risk_colorbar.set_label(r"Spatial risk $w_{F2}$", labelpad=2.0)
    risk_colorbar.outline.set_linewidth(0.45)

    count_image = axes[1].pcolormesh(
        longitude_edges,
        latitude_edges,
        raw_counts,
        cmap=color_map,
        shading="flat",
        norm=PowerNorm(
            gamma=FAULT_COUNT_DISPLAY_GAMMA,
            vmin=0.0,
            vmax=upper_count,
        ),
    )
    event_longitudes = np.asarray([float(event["longitude_deg"]) for event in events])
    event_latitudes = np.asarray([float(event["latitude_deg"]) for event in events])
    axes[1].scatter(
        event_longitudes,
        event_latitudes,
        s=3,
        facecolors="none",
        edgecolors="#08336E",
        linewidths=0.22,
        alpha=0.30,
        zorder=5,
    )
    add_geographic_frame(axes[1], parameters, label_hotspot=False)
    axes[1].set_title(
        "b   Long-horizon sampled F2 faults",
        loc="left",
        fontweight="bold",
        pad=5.0,
    )
    count_colorbar = figure.colorbar(
        count_image,
        ax=axes[1],
        orientation="horizontal",
        fraction=0.07,
        pad=0.20,
        aspect=32,
    )
    tick_count = min(5, raw_peak + 1)
    count_colorbar.set_ticks(
        np.unique(np.rint(np.linspace(0, raw_peak, tick_count)).astype(int))
    )
    count_colorbar.set_label(
        "Raw fault count per bin",
        labelpad=2.0,
    )
    count_colorbar.outline.set_linewidth(0.45)

    duration = int(run["duration_s"])
    fault_count = int(sampling["actual_fault_count"])
    expected = float(sampling["expected_fault_count_with_observed_recovery_suppression"])
    correlation = float(spatial["risk_rate_pearson_correlation"])
    figure.subplots_adjust(left=0.065, right=0.985, bottom=0.27, top=0.88, wspace=0.15)
    figure.text(
        0.5,
        0.035,
        f"{duration:,} s; {fault_count:,} sampled faults; conditional expectation "
        f"{expected:.1f}; bin risk-rate Pearson r={correlation:.3f}.\n"
        f"Panel b uses raw {float(run['longitude_bin_deg']):g} x "
        f"{float(run['latitude_bin_deg']):g} degree eligible-bin counts with the "
        f"color scale capped at {raw_peak} and display gamma "
        f"{FAULT_COUNT_DISPLAY_GAMMA:g}; circles retain the individual event locations.",
        ha="center",
        va="bottom",
        fontsize=5.6,
        color="#243342",
    )

    outputs = export_figure(figure, output)
    plt.close(figure)
    return outputs


def main() -> None:
    args = parse_args()
    if args.minimum_bin_exposure <= 0:
        raise ValueError("--minimum-bin-exposure must be positive")
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
    outputs = plot(summary, bins, events, args.output, args.minimum_bin_exposure)
    print("PASS outputs=" + ",".join(str(path) for path in outputs))


if __name__ == "__main__":
    main()
