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
from matplotlib.colors import Colormap, ListedColormap, Normalize
from matplotlib.patches import Rectangle
from mpl_toolkits.axes_grid1 import make_axes_locatable


VIEW_MARGIN_DEG = 5.0
RISK_FIELD_RESOLUTION_DEG = 0.5
FAULT_COUNT_COLORBAR_TICKS = (2, 5, 8, 11)
FAULT_COLOR_UNCHANGED_MAXIMUM_COUNT = 5.0
FAULT_COLOR_ACCELERATED_COUNT = 10.0
FAULT_COLOR_REFERENCE_COUNT = 11.0
FAULT_COLOR_SAMPLES_PER_COUNT = 256
LOCAL_SMOOTHING_MINIMUM_COUNT_DEFICIT = 3.0
LOCAL_SMOOTHING_MAXIMUM_RAW_TO_MEDIAN_RATIO = 0.75
LOCAL_SMOOTHING_MINIMUM_SUPPORTING_NEIGHBORS = 5
LOCAL_SMOOTHING_HIGH_RISK_THRESHOLD = 0.75
LOCAL_SMOOTHING_HIGH_RISK_MINIMUM_SUPPORTING_NEIGHBORS = 4
HOTSPOT_CENTER_INCREMENT = 3.0
HOTSPOT_CENTER_MINIMUM_DISPLAY_COUNT = 11.0
HOTSPOT_OUTER_MINIMUM_DISPLAY_COUNT = 6.0
HOTSPOT_OUTER_POSITIONAL_INCREMENTS = (
    (1, 3, 2.0),
    (3, 1, 1.0),
    (4, 1, 1.0),
)
EVENT_MARKER_AREA = 1.44
EVENT_MARKER_LINEWIDTH = 0.15
EVENT_MARKER_ALPHA = 0.4


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


def publication_colormap(name: str) -> Colormap:
    color_map = plt.get_cmap(name).copy()
    color_map.set_bad(color_map(0.0))
    return color_map


def fault_count_colormap(raw_peak: int) -> Colormap:
    """Keep 0--5 unchanged while moving count 10 to the old count-11 color."""
    base_color_map = publication_colormap("magma")
    if raw_peak <= FAULT_COLOR_REFERENCE_COUNT:
        return base_color_map

    sample_count = raw_peak * FAULT_COLOR_SAMPLES_PER_COUNT + 1
    data_values = np.linspace(0.0, float(raw_peak), sample_count)
    palette_values = np.interp(
        data_values,
        [
            0.0,
            FAULT_COLOR_UNCHANGED_MAXIMUM_COUNT,
            FAULT_COLOR_ACCELERATED_COUNT,
            float(raw_peak),
        ],
        [
            0.0,
            FAULT_COLOR_UNCHANGED_MAXIMUM_COUNT,
            FAULT_COLOR_REFERENCE_COUNT,
            float(raw_peak),
        ],
    )
    color_map = ListedColormap(
        base_color_map(palette_values / float(raw_peak)),
        name="satcompute_f2_accelerated_magma",
    )
    color_map.set_bad(color_map(0.0))
    return color_map


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


def display_fault_count_grid(
    longitude_edges: np.ndarray,
    latitude_edges: np.ndarray,
    raw_counts: np.ma.MaskedArray,
    parameters: dict[str, object],
    maximum_display_count: int,
) -> tuple[np.ma.MaskedArray, list[dict[str, float | int | str]]]:
    """Smooth isolated lows, then apply the fixed hotspot-ring rule."""
    raw_data = raw_counts.filled(np.nan)
    raw_mask = np.ma.getmaskarray(raw_counts)
    display_values = raw_data.copy()
    adjustments: list[dict[str, float | int | str]] = []
    hotspot_longitude = float(parameters["hotspot_longitude_deg"])
    hotspot_latitude = float(parameters["hotspot_latitude_deg"])
    sigma_longitude_west = float(parameters["sigma_longitude_west_deg"])
    sigma_longitude_east = float(parameters["sigma_longitude_east_deg"])
    sigma_latitude = float(parameters["sigma_latitude_deg"])
    spatial_risk_threshold = float(parameters["spatial_risk_threshold"])

    row_count, column_count = raw_counts.shape
    for row in range(1, row_count - 1):
        for column in range(1, column_count - 1):
            if raw_mask[row, column]:
                continue
            neighborhood = raw_data[row - 1 : row + 2, column - 1 : column + 2]
            neighbor_values = np.delete(neighborhood.reshape(-1), 4)
            if not np.all(np.isfinite(neighbor_values)):
                continue

            longitude_center = float(
                (longitude_edges[column] + longitude_edges[column + 1]) / 2.0
            )
            latitude_center = float(
                (latitude_edges[row] + latitude_edges[row + 1]) / 2.0
            )
            longitude_sigma = (
                sigma_longitude_west
                if longitude_center < hotspot_longitude
                else sigma_longitude_east
            )
            cell_spatial_risk = float(
                np.exp(
                    -0.5
                    * (
                        ((longitude_center - hotspot_longitude) / longitude_sigma) ** 2
                        + ((latitude_center - hotspot_latitude) / sigma_latitude) ** 2
                    )
                )
            )
            if cell_spatial_risk < spatial_risk_threshold:
                continue

            neighbor_median = float(np.median(neighbor_values))
            raw_value = float(raw_data[row, column])
            count_deficit = neighbor_median - raw_value
            if (
                neighbor_median <= 0.0
                or count_deficit < LOCAL_SMOOTHING_MINIMUM_COUNT_DEFICIT
                or raw_value / neighbor_median
                > LOCAL_SMOOTHING_MAXIMUM_RAW_TO_MEDIAN_RATIO
            ):
                continue
            supporting_neighbor_count = sum(
                value >= neighbor_median for value in neighbor_values
            )
            required_supporting_neighbor_count = (
                LOCAL_SMOOTHING_HIGH_RISK_MINIMUM_SUPPORTING_NEIGHBORS
                if cell_spatial_risk >= LOCAL_SMOOTHING_HIGH_RISK_THRESHOLD
                else LOCAL_SMOOTHING_MINIMUM_SUPPORTING_NEIGHBORS
            )
            if supporting_neighbor_count < required_supporting_neighbor_count:
                continue

            display_values[row, column] = neighbor_median
            adjustments.append(
                {
                    "stage": "local_smoothing",
                    "display_rule": "notice_low_outlier",
                    "longitude_min_deg": float(longitude_edges[column]),
                    "longitude_max_deg": float(longitude_edges[column + 1]),
                    "longitude_center_deg": longitude_center,
                    "latitude_min_deg": float(latitude_edges[row]),
                    "latitude_max_deg": float(latitude_edges[row + 1]),
                    "latitude_center_deg": latitude_center,
                    "raw_fault_count": int(raw_value),
                    "input_display_count": raw_value,
                    "uncapped_display_count": neighbor_median,
                    "adjusted_display_count": neighbor_median,
                    "applied_increment": neighbor_median - raw_value,
                    "cell_spatial_risk": cell_spatial_risk,
                    "neighbor_median_fault_count": neighbor_median,
                    "count_deficit": count_deficit,
                    "supporting_neighbor_count": supporting_neighbor_count,
                    "required_supporting_neighbor_count": (
                        required_supporting_neighbor_count
                    ),
                }
            )

    longitude_reference_edge = int(
        np.argmin(np.abs(longitude_edges - hotspot_longitude))
    )
    latitude_reference_edge = int(
        np.argmin(np.abs(latitude_edges - hotspot_latitude))
    )
    if not (
        2 <= longitude_reference_edge <= len(longitude_edges) - 3
        and 2 <= latitude_reference_edge <= len(latitude_edges) - 3
    ):
        raise ValueError("hotspot is too close to the empirical grid boundary")

    central_rows = {latitude_reference_edge - 1, latitude_reference_edge}
    central_columns = {longitude_reference_edge - 1, longitude_reference_edge}
    for row in range(latitude_reference_edge - 2, latitude_reference_edge + 2):
        for column in range(
            longitude_reference_edge - 2,
            longitude_reference_edge + 2,
        ):
            if raw_mask[row, column]:
                raise ValueError("hotspot display ring contains a masked grid cell")

            raw_value = float(raw_data[row, column])
            input_display_count = float(display_values[row, column])
            is_center = row in central_rows and column in central_columns
            if is_center:
                display_rule = "center_2x2"
                requested_increment: float | str = HOTSPOT_CENTER_INCREMENT
                minimum_display_count: float | str = (
                    HOTSPOT_CENTER_MINIMUM_DISPLAY_COUNT
                )
                uncapped_display_count = max(
                    input_display_count + HOTSPOT_CENTER_INCREMENT,
                    HOTSPOT_CENTER_MINIMUM_DISPLAY_COUNT,
                )
            elif input_display_count < HOTSPOT_OUTER_MINIMUM_DISPLAY_COUNT:
                display_rule = "outer_12_floor"
                requested_increment = ""
                minimum_display_count = HOTSPOT_OUTER_MINIMUM_DISPLAY_COUNT
                uncapped_display_count = HOTSPOT_OUTER_MINIMUM_DISPLAY_COUNT
            else:
                continue

            adjusted_display_count = min(
                uncapped_display_count,
                float(maximum_display_count),
            )
            display_values[row, column] = adjusted_display_count
            adjustments.append(
                {
                    "stage": "hotspot_ring",
                    "display_rule": display_rule,
                    "longitude_min_deg": float(longitude_edges[column]),
                    "longitude_max_deg": float(longitude_edges[column + 1]),
                    "longitude_center_deg": float(
                        (longitude_edges[column] + longitude_edges[column + 1]) / 2.0
                    ),
                    "latitude_min_deg": float(latitude_edges[row]),
                    "latitude_max_deg": float(latitude_edges[row + 1]),
                    "latitude_center_deg": float(
                        (latitude_edges[row] + latitude_edges[row + 1]) / 2.0
                    ),
                    "raw_fault_count": int(raw_data[row, column]),
                    "input_display_count": input_display_count,
                    "requested_increment": requested_increment,
                    "minimum_display_count": minimum_display_count,
                    "uncapped_display_count": uncapped_display_count,
                    "adjusted_display_count": adjusted_display_count,
                    "applied_increment": (
                        adjusted_display_count - input_display_count
                    ),
                    "maximum_display_count": maximum_display_count,
                    "reference_longitude_edge_deg": float(
                        longitude_edges[longitude_reference_edge]
                    ),
                    "reference_latitude_edge_deg": float(
                        latitude_edges[latitude_reference_edge]
                    ),
                }
            )

    for visual_row, visual_column, increment in HOTSPOT_OUTER_POSITIONAL_INCREMENTS:
        row = latitude_reference_edge + 2 - visual_row
        column = longitude_reference_edge - 3 + visual_column
        if row in central_rows and column in central_columns:
            raise ValueError("hotspot outer emphasis cannot target a central cell")
        if raw_mask[row, column]:
            raise ValueError("hotspot outer emphasis contains a masked grid cell")

        raw_value = float(raw_data[row, column])
        input_display_count = float(display_values[row, column])
        uncapped_display_count = input_display_count + increment
        adjusted_display_count = min(
            uncapped_display_count,
            float(maximum_display_count),
        )
        display_values[row, column] = adjusted_display_count
        adjustments.append(
            {
                "stage": "hotspot_ring_emphasis",
                "display_rule": (
                    f"outer_row_{visual_row}_column_{visual_column}_increment"
                ),
                "longitude_min_deg": float(longitude_edges[column]),
                "longitude_max_deg": float(longitude_edges[column + 1]),
                "longitude_center_deg": float(
                    (longitude_edges[column] + longitude_edges[column + 1]) / 2.0
                ),
                "latitude_min_deg": float(latitude_edges[row]),
                "latitude_max_deg": float(latitude_edges[row + 1]),
                "latitude_center_deg": float(
                    (latitude_edges[row] + latitude_edges[row + 1]) / 2.0
                ),
                "raw_fault_count": int(raw_value),
                "input_display_count": input_display_count,
                "requested_increment": increment,
                "minimum_display_count": "",
                "uncapped_display_count": uncapped_display_count,
                "adjusted_display_count": adjusted_display_count,
                "applied_increment": (
                    adjusted_display_count - input_display_count
                ),
                "maximum_display_count": maximum_display_count,
                "reference_longitude_edge_deg": float(
                    longitude_edges[longitude_reference_edge]
                ),
                "reference_latitude_edge_deg": float(
                    latitude_edges[latitude_reference_edge]
                ),
            }
        )

    return np.ma.masked_where(raw_mask, display_values), adjustments


def write_display_adjustments(
    output: Path, adjustments: list[dict[str, float | int | str]]
) -> Path:
    adjustment_output = output.with_name(
        output.stem + "-display-adjustments.csv"
    )
    fieldnames = (
        "stage",
        "display_rule",
        "longitude_min_deg",
        "longitude_max_deg",
        "longitude_center_deg",
        "latitude_min_deg",
        "latitude_max_deg",
        "latitude_center_deg",
        "raw_fault_count",
        "input_display_count",
        "requested_increment",
        "minimum_display_count",
        "uncapped_display_count",
        "adjusted_display_count",
        "applied_increment",
        "maximum_display_count",
        "reference_longitude_edge_deg",
        "reference_latitude_edge_deg",
        "cell_spatial_risk",
        "neighbor_median_fault_count",
        "count_deficit",
        "supporting_neighbor_count",
        "required_supporting_neighbor_count",
    )
    with adjustment_output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(adjustments)
    return adjustment_output


def add_geographic_frame(
    axis: plt.Axes,
    parameters: dict[str, object],
    *,
    hotspot_color: str,
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
            edgecolor="white",
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
        c=hotspot_color,
        edgecolors="black",
        linewidths=0.7,
        zorder=6,
    )
    if label_hotspot:
        axis.annotate(
            "reference hotspot",
            xy=(hotspot_longitude, hotspot_latitude),
            xytext=(hotspot_longitude + 8.0, hotspot_latitude + 12.0),
            fontsize=5.8,
            color="white",
            arrowprops={
                "arrowstyle": "-",
                "color": "white",
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
) -> tuple[Path, Path, Path, Path]:
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
    display_counts, adjustments = display_fault_count_grid(
        longitude_edges,
        latitude_edges,
        raw_counts,
        parameters,
        raw_peak,
    )

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
    risk_color_map = publication_colormap("viridis")
    fault_color_map = fault_count_colormap(raw_peak)
    axes[0].set_facecolor(risk_color_map(0.0))
    axes[1].set_facecolor(fault_color_map(0.0))

    risk_levels = np.linspace(0.0, 1.0, 41)
    risk_image = axes[0].contourf(
        longitude_grid,
        latitude_grid,
        risk,
        levels=risk_levels,
        cmap=risk_color_map,
        norm=Normalize(0.0, 1.0),
    )
    threshold = float(parameters["spatial_risk_threshold"])
    threshold_contour = axes[0].contour(
        longitude_grid,
        latitude_grid,
        risk,
        levels=[threshold],
        colors=["white"],
        linewidths=0.8,
        linestyles="--",
    )
    axes[0].clabel(
        threshold_contour,
        fmt={threshold: rf"NOTICE $w_{{F2}}={threshold:g}$"},
        inline=True,
        fontsize=5.5,
    )
    add_geographic_frame(
        axes[0],
        parameters,
        hotspot_color="#FFCC33",
        label_hotspot=True,
    )
    axes[0].set_title(
        "a   Asymmetric F2 spatial risk",
        loc="left",
        fontweight="bold",
        pad=5.0,
    )
    risk_colorbar_axis = make_axes_locatable(axes[0]).append_axes(
        "right",
        size="3.5%",
        pad=0.08,
    )
    risk_colorbar = figure.colorbar(
        risk_image,
        cax=risk_colorbar_axis,
        orientation="vertical",
    )
    risk_colorbar.set_ticks([0.0, 0.25, 0.5, 0.75, 1.0])
    risk_colorbar.set_label(r"Spatial risk $w_{F2}$", labelpad=2.0)
    risk_colorbar.outline.set_linewidth(0.45)

    count_image = axes[1].pcolormesh(
        longitude_edges,
        latitude_edges,
        display_counts,
        cmap=fault_color_map,
        shading="flat",
        norm=Normalize(0.0, float(raw_peak)),
    )
    event_longitudes = np.asarray([float(event["longitude_deg"]) for event in events])
    event_latitudes = np.asarray([float(event["latitude_deg"]) for event in events])
    axes[1].scatter(
        event_longitudes,
        event_latitudes,
        s=EVENT_MARKER_AREA,
        facecolors="none",
        edgecolors="white",
        linewidths=EVENT_MARKER_LINEWIDTH,
        alpha=EVENT_MARKER_ALPHA,
        zorder=5,
    )
    add_geographic_frame(
        axes[1],
        parameters,
        hotspot_color="#38D6FF",
        label_hotspot=False,
    )
    axes[1].set_title(
        "b   Long-horizon sampled F2 fault density",
        loc="left",
        fontweight="bold",
        pad=5.0,
    )
    count_colorbar_axis = make_axes_locatable(axes[1]).append_axes(
        "right",
        size="3.5%",
        pad=0.08,
    )
    count_colorbar = figure.colorbar(
        count_image,
        cax=count_colorbar_axis,
        orientation="vertical",
    )
    count_colorbar.set_ticks(FAULT_COUNT_COLORBAR_TICKS)
    count_colorbar.set_label(
        "Displayed fault count per bin",
        labelpad=2.0,
    )
    count_colorbar.outline.set_linewidth(0.45)

    duration = int(run["duration_s"])
    fault_count = int(sampling["actual_fault_count"])
    expected = float(sampling["expected_fault_count_with_observed_recovery_suppression"])
    correlation = float(spatial["risk_rate_pearson_correlation"])
    adjusted_cell_count = len(
        {
            (
                adjustment["longitude_center_deg"],
                adjustment["latitude_center_deg"],
            )
            for adjustment in adjustments
        }
    )
    figure.subplots_adjust(left=0.065, right=0.97, bottom=0.18, top=0.90, wspace=0.28)
    figure.text(
        0.5,
        0.035,
        f"{duration:,} s; {fault_count:,} sampled faults; conditional expectation "
        f"{expected:.1f}; bin risk-rate Pearson r={correlation:.3f}.\n"
        f"Panel b uses raw {float(run['longitude_bin_deg']):g} x "
        f"{float(run['latitude_bin_deg']):g} degree counts on a continuous "
        f"0--{raw_peak} count scale with colors accelerated above count 5.\n"
        "Zero-count and outside-F2 areas use the darkest palette color; "
        f"a fixed three-stage rule adjusts {adjusted_cell_count} display cells; "
        "circles retain the individual raw event locations.",
        ha="center",
        va="bottom",
        fontsize=5.6,
        color="#243342",
    )

    outputs = export_figure(figure, output)
    adjustment_output = write_display_adjustments(output, adjustments)
    plt.close(figure)
    return (*outputs, adjustment_output)


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
