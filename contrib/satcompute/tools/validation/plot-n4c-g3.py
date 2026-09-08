#!/usr/bin/env python3
"""Plot G3 observed node load/thermal state and native F2 exposure; no smoothing.

Requires explicit audit-enabled generate output and its audited g3-summary.json.
Tables are the exact plotted points. Optional layout QA is an external developer
tool, not a runtime dependency. PDF/SVG retain text; PNG is a review preview.
"""
import argparse
import csv
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle


def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def write_points(path, records):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)


def save(fig, stem):
    for extension in (".pdf", ".svg", ".png"):
        fig.savefig(stem.with_suffix(extension), dpi=600, facecolor="white")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old-none", required=True, type=Path)
    parser.add_argument("--hotspot-none", required=True, type=Path)
    parser.add_argument("--generate", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--figure-dir", type=Path, help="Optional separate directory for the six figure exports")
    parser.add_argument("--layout-qa-scripts", type=Path)
    args = parser.parse_args()
    output = args.output_dir
    output.mkdir(parents=True, exist_ok=True)
    figure_dir = args.figure_dir or output
    figure_dir.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.family": "sans-serif", "font.sans-serif": ["DejaVu Sans"], "font.size": 8,
        "axes.titlesize": 9, "axes.labelsize": 8, "xtick.labelsize": 7,
        "ytick.labelsize": 7, "legend.fontsize": 7, "pdf.fonttype": 42,
        "ps.fonttype": 42, "svg.fonttype": "none", "axes.spines.top": False,
        "axes.spines.right": False})
    old = {r["node_id"]: r for r in rows(args.old_none / "compute-node-summary.csv")}
    hot = {r["node_id"]: r for r in rows(args.hotspot_none / "compute-node-summary.csv")}
    generated = rows(args.generate / "compute-node-summary.csv")
    audit = json.loads((args.generate / "g3-summary.json").read_text())["node_state_audit"]
    events = rows(args.generate / "fault-events.csv")
    f1_nodes = {e["node_id"] for e in events if e["event_type"] == "START" and "F1" in e["fault_source"]}
    points = [{"node_id": r["node_id"], "old_none_busy_s": int(old[r["node_id"]]["busy_time_ns"])/1e9,
        "hotspot_none_busy_s": int(hot[r["node_id"]]["busy_time_ns"])/1e9,
        "generate_utilization_percent": float(r["utilization_percent"]),
        "peak_temperature_c": audit[r["node_id"]]["temperature_c"]["max"],
        "peak_p_f1": audit[r["node_id"]]["p_f1"]["max"],
        "f1_start_observed": int(r["node_id"] in f1_nodes)} for r in generated]
    write_points(output / "node-points.csv", points)
    fig, axes = plt.subplots(1, 2, figsize=(7.1, 2.9))
    fig.subplots_adjust(left=.09, right=.98, bottom=.20, top=.85, wspace=.34)
    axes[0].scatter([p["old_none_busy_s"] for p in points],
                    [p["hotspot_none_busy_s"] for p in points], s=15,
                    color="#3888A3", alpha=.75, linewidths=0)
    limit = max(max(p["old_none_busy_s"], p["hotspot_none_busy_s"]) for p in points)*1.08
    axes[0].plot([0, limit], [0, limit], color="#888888", lw=.7, linestyle="--")
    axes[0].set(xlim=(0, limit), ylim=(0, limit), xlabel="G2 none busy time (s)",
                ylabel="G3 hotspot none busy time (s)", title=f"a   Matched nodes (n = {len(points)})")
    for observed, color, marker, label in ((0, "#8A949B", "o", "No F1 START"),
                                           (1, "#B95D42", "^", "F1 START observed")):
        selected = [p for p in points if p["f1_start_observed"] == observed]
        axes[1].scatter([p["generate_utilization_percent"] for p in selected],
                        [p["peak_temperature_c"] for p in selected], s=18,
                        color=color, marker=marker, alpha=.8, linewidths=0, label=label)
    axes[1].set(xlabel="Generate utilization (%)", ylabel="Peak model temperature (C)",
                title="b   Load and thermal exposure")
    axes[1].legend(loc="lower right", frameon=False)
    if args.layout_qa_scripts:
        sys.path.insert(0, str(args.layout_qa_scripts))
        from audit_panel_alignment import require_matplotlib_panel_alignment
        require_matplotlib_panel_alignment(fig, json_out=output / "load-thermal-alignment.json",
            overlay_svg=output / "load-thermal-alignment.svg", tolerance_pt=1.5,
            gutter_tolerance_pt=1.5, strict=True)
    save(fig, figure_dir / "load-thermal")

    states = rows(args.generate / "fault-model-state.csv")
    lookup = {(s["simulation_time_ns"], s["node_id"]): s for s in states}
    exposure = [{k: s[k] for k in ("simulation_time_ns", "node_id", "latitude_deg",
                                 "longitude_deg", "f2_spatial_risk", "sampling_eligible")}
                for s in states if s["in_saa"] == "1"]
    hits = []
    for event in events:
        if event["event_type"] == "START" and "F2" in event["fault_source"]:
            s = lookup[(event["simulation_time_ns"], event["node_id"])]
            hits.append({"fault_id": event["fault_id"], "time_ns": event["simulation_time_ns"],
                         "node_id": event["node_id"], "latitude_deg": s["latitude_deg"],
                         "longitude_deg": s["longitude_deg"], "p_f2": s["p_f2"]})
    if not exposure:
        raise ValueError("no native SAA exposure in the supplied audit")
    write_points(output / "f2-exposure-points.csv", exposure)
    if hits:
        write_points(output / "f2-event-points.csv", hits)
    fig, ax = plt.subplots(figsize=(5.5, 3.25))
    fig.subplots_adjust(left=.13, right=.84, bottom=.18, top=.75)
    cloud = ax.scatter([float(p["longitude_deg"]) for p in exposure],
        [float(p["latitude_deg"]) for p in exposure],
        c=[float(p["f2_spatial_risk"]) for p in exposure], cmap="viridis", vmin=0, vmax=1,
        s=4, linewidths=0, rasterized=True)
    ax.add_patch(Rectangle((-90, -50), 95, 55, fill=False, edgecolor="#555555", lw=.8, linestyle="--"))
    if hits:
        ax.scatter([float(p["longitude_deg"]) for p in hits],
            [float(p["latitude_deg"]) for p in hits], marker="*", s=65,
            color="#C94C35", edgecolor="white", linewidths=.5, label=f"Actual F2 START (n = {len(hits)})")
        ax.legend(loc="lower left", bbox_to_anchor=(0, 1.01), frameon=False, borderaxespad=0)
    ax.set(xlim=(-95, 10), ylim=(-55, 10), xlabel="Longitude (deg)", ylabel="Latitude (deg)")
    fig.suptitle(f"Native orbit exposure: {len(exposure):,} node-second samples", fontsize=9, y=.96)
    color_ax = fig.add_axes([.88, .18, .025, .57])
    fig.colorbar(cloud, cax=color_ax, label="Model spatial risk (not event density)")
    save(fig, figure_dir / "f2-native-exposure")
    provenance = {"inputs": {k: str(getattr(args, k)) for k in ("old_none", "hotspot_none", "generate")},
        "node_points": len(points), "saa_node_second_samples": len(exposure), "actual_f2_events": len(hits),
        "processing": "No smoothing, interpolation, invented samples, or manual count adjustment.",
        "interpretation": "Load/temperature are simulator observations; F2 colors are model risk, not fault density.",
        "crop": {"longitude": [-95, 10], "latitude": [-55, 10]},
        "alignment_qa_requested": bool(args.layout_qa_scripts)}
    (output / "figure-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    main()
