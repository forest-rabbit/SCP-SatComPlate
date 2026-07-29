#!/usr/bin/env python3
"""Run the fixed-delay three-scale, three-window interval study."""

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ...generation.scenario.check_scenario import check_scenario
from ...generation.scenario.configuration import load_config
from ...generation.scenario.generate_scenario import generate_scenario
from ...generation.topology.common.hash_utils import (
    REPOSITORY_ROOT,
    compact_json_bytes,
)
from ...generation.topology.common.satcompute_schema import read_json
from ...generation.topology.orbit.hypatia.mean_motion import (
    mean_motion_rev_per_day,
)
from .compare_intervals import compare_scenarios
from .downsample_scenario import downsample_scenario
from .edge_state import edge_set_sha256, load_edge_trace
from .report import (
    ROUTING_MODES,
    summarize_cost_runs,
    write_report_bundle,
)
from .route_probe import (
    compare_selection_audits,
    count_selected_next_hop_changes,
    load_selection_audit,
    write_probe_pairs,
)


SCHEMA_VERSION = "0.1"
ANALYSIS_EVIDENCE_VERSION = 1
DEFAULT_INTERVALS = (1, 2, 5, 10, 20)
HASH_SEED = 1
CONFIG_DIRECTORY = (
    REPOSITORY_ROOT
    / "contrib"
    / "satcompute"
    / "tools"
    / "generation"
    / "scenario"
    / "config"
)
PRESET_FILES = {
    "66": "synthetic-66-compute-22.json",
    "351": "synthetic-351-telesat-t1-compute-117.json",
    "720": "synthetic-720-oneweb-compute-240.json",
}


class IntervalStudyError(RuntimeError):
    """Raised when the study cannot produce complete auditable evidence."""


@dataclass(frozen=True)
class StudyPreset:
    key: str
    path: Path
    scenario_name: str
    node_count: int
    candidate_count: int
    altitude_km: float


def orbital_period_seconds(altitude_km: float) -> float:
    """Return the WGS72 near-circular period used by TLE generation."""
    return 86400.0 / mean_motion_rev_per_day(altitude_km)


def orbital_window_offsets(altitude_km: float) -> tuple[int, int, int]:
    """Return deterministic nearest-second samples at 0, P/3, and 2P/3."""
    period_s = orbital_period_seconds(altitude_km)
    return (
        0,
        int(period_s / 3.0 + 0.5),
        int(2.0 * period_s / 3.0 + 0.5),
    )


def load_study_preset(key: str) -> StudyPreset:
    if key not in PRESET_FILES:
        raise IntervalStudyError(f"unknown study preset: {key}")
    path = CONFIG_DIRECTORY / PRESET_FILES[key]
    config = load_config(path)
    constellation = config.constellation
    candidate_count = (
        config.total_satellite_count
        + (constellation.num_orbits - 1)
        * constellation.satellites_per_orbit
    )
    return StudyPreset(
        key,
        path,
        config.scenario_name,
        config.total_satellite_count,
        candidate_count,
        constellation.altitude_km,
    )


def _pretty_json_bytes(payload: Any) -> bytes:
    return (
        json.dumps(
            payload,
            ensure_ascii=False,
            indent=2,
            sort_keys=False,
        )
        + "\n"
    ).encode("utf-8")


def write_window_config(
    preset: StudyPreset,
    duration_s: int,
    offset_s: int,
    output_path: Path,
) -> None:
    payload = json.loads(preset.path.read_text(encoding="utf-8"))
    schedule = payload["topology"]["schedule"]
    schedule["start_time_s"] = 0
    schedule["orbit_sample_offset_s"] = offset_s
    schedule["duration_s"] = duration_s
    schedule["step_s"] = 1
    expected = _pretty_json_bytes(payload)
    path = Path(output_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if path.read_bytes() != expected:
            raise IntervalStudyError(
                f"existing generated config differs: {path}"
            )
    else:
        path.write_bytes(expected)
    load_config(path)


def _scenario_manifest(root: Path) -> dict[str, Any]:
    manifest = read_json(root / "scenario-manifest.json")
    if not isinstance(manifest, dict):
        raise IntervalStudyError(f"scenario manifest is invalid: {root}")
    return manifest


def ensure_reference_scenario(
    config_path: Path,
    output_dir: Path,
) -> dict[str, Any]:
    if output_dir.exists():
        check_scenario(output_dir)
        expected = load_config(config_path).input_dict()
        manifest = _scenario_manifest(output_dir)
        if manifest["scenario_config"] != expected:
            raise IntervalStudyError(
                f"existing reference uses another config: {output_dir}"
            )
        return manifest
    print(f"[study] generate reference: {output_dir}", flush=True)
    return generate_scenario(config_path, output_dir)


def ensure_held_scenario(
    reference_dir: Path,
    interval_s: int,
    output_dir: Path,
) -> dict[str, Any]:
    if output_dir.exists():
        check_scenario(output_dir)
        manifest = _scenario_manifest(output_dir)
        reference = _scenario_manifest(reference_dir)
        if (
            manifest["reference_scenario_sha256"]
            != reference["aggregate_scenario_sha256"]
            or manifest["downsample_interval_s"] != interval_s
        ):
            raise IntervalStudyError(
                f"existing held scenario has wrong provenance: {output_dir}"
            )
        return manifest
    print(
        f"[study] downsample interval={interval_s}s: {output_dir}",
        flush=True,
    )
    return downsample_scenario(reference_dir, interval_s, output_dir)


def directory_size_bytes(root: Path) -> int:
    return sum(
        path.stat().st_size
        for path in Path(root).rglob("*")
        if path.is_file() and not path.is_symlink()
    )


def _waf_command(program: str, options: dict[str, Any]) -> str:
    arguments = [program]
    arguments.extend(
        f"--{name}={value}" for name, value in options.items()
    )
    return shlex.join(arguments)


def _run_waf(
    waf: Path,
    program: str,
    options: dict[str, Any],
    log_path: Path,
    *,
    rss_path: Path | None = None,
) -> int | None:
    invocation = [
        str(waf),
        "--run-no-build",
        _waf_command(program, options),
    ]
    if rss_path is not None:
        command = [
            "/usr/bin/time",
            "--format=%M",
            "--output",
            str(rss_path),
            *invocation,
        ]
    else:
        command = invocation
    completed = subprocess.run(
        command,
        cwd=REPOSITORY_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    log_path.write_text(completed.stdout, encoding="utf-8")
    if completed.returncode != 0:
        tail = "\n".join(completed.stdout.splitlines()[-30:])
        raise IntervalStudyError(
            f"{program} failed with {completed.returncode}:\n{tail}"
        )
    if rss_path is None:
        return None
    try:
        peak_rss_kib = int(rss_path.read_text(encoding="utf-8").strip())
    except (OSError, ValueError) as error:
        raise IntervalStudyError(
            f"cannot parse GNU time RSS output: {rss_path}"
        ) from error
    if peak_rss_kib <= 0:
        raise IntervalStudyError("GNU time reported non-positive peak RSS")
    return peak_rss_kib


def _selection_times(rows: tuple[Any, ...]) -> tuple[int, ...]:
    return tuple(sorted({row.time_s for row in rows}))


def ensure_selection_audit(
    waf: Path,
    scenario_dir: Path,
    probe_pairs_path: Path,
    mode: str,
    duration_s: int,
    step_s: int,
    output_path: Path,
    node_count: int,
) -> tuple[Any, ...]:
    expected_times = tuple(range(0, duration_s + 1, step_s))
    if output_path.exists():
        try:
            rows = load_selection_audit(output_path, node_count)
            if (
                {row.routing_mode for row in rows} == {mode}
                and _selection_times(rows) == expected_times
            ):
                return rows
        except (OSError, ValueError):
            pass
        output_path.unlink()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    audit_times = ",".join(str(value) for value in expected_times)
    print(
        f"[study] route audit mode={mode} step={step_s}s: "
        f"{scenario_dir.name}",
        flush=True,
    )
    _run_waf(
        waf,
        "satcompute-route-selection-audit",
        {
            "topologyDir": scenario_dir / "topology",
            "simulationDuration": f"{duration_s + 0.000001:.6f}",
            "auditTimes": audit_times,
            "routingMode": mode,
            "ecmpHashSeed": HASH_SEED,
            "probePairs": probe_pairs_path,
            "outputFile": output_path,
        },
        output_path.with_suffix(".log"),
    )
    rows = load_selection_audit(output_path, node_count)
    if (
        {row.routing_mode for row in rows} != {mode}
        or _selection_times(rows) != expected_times
    ):
        raise IntervalStudyError(
            f"generated route audit has the wrong schedule: {output_path}"
        )
    return rows


def _read_cost_result(
    path: Path,
    node_count: int,
    expected_epoch: int,
) -> dict[str, Any]:
    payload = read_json(path)
    if (
        not isinstance(payload, dict)
        or frozenset(payload)
        != frozenset(
            (
                "schema_version",
                "node_count",
                "simulation_duration_s",
                "wall_clock_s",
                "final_route_epoch",
            )
        )
        or payload["schema_version"] != SCHEMA_VERSION
        or payload["node_count"] != node_count
        or payload["final_route_epoch"] != expected_epoch
        or not isinstance(payload["wall_clock_s"], (int, float))
        or isinstance(payload["wall_clock_s"], bool)
        or payload["wall_clock_s"] < 0.0
    ):
        raise IntervalStudyError(f"topology cost result is invalid: {path}")
    return payload


def ensure_topology_cost_runs(
    waf: Path,
    scenario_dir: Path,
    duration_s: int,
    node_count: int,
    snapshot_count: int,
    repeats: int,
    output_dir: Path,
) -> dict[str, float | int | None]:
    output_dir.mkdir(parents=True, exist_ok=True)
    expected_epoch = snapshot_count - 1
    wall_times = []
    peak_rss_values = []
    for repeat in range(1, repeats + 1):
        result_path = output_dir / f"run-{repeat}.json"
        rss_path = output_dir / f"run-{repeat}-rss-kib.txt"
        try:
            result = _read_cost_result(
                result_path,
                node_count,
                expected_epoch,
            )
            peak_rss = int(rss_path.read_text(encoding="utf-8").strip())
            if peak_rss <= 0:
                raise ValueError("non-positive RSS")
        except (OSError, ValueError, IntervalStudyError):
            result_path.unlink(missing_ok=True)
            rss_path.unlink(missing_ok=True)
            print(
                f"[study] topology cost repeat={repeat}: "
                f"{scenario_dir.name}",
                flush=True,
            )
            peak_rss = _run_waf(
                waf,
                "satcompute-topology-cost-audit",
                {
                    "topologyDir": scenario_dir / "topology",
                    "simulationDuration":
                        f"{duration_s + 0.000001:.6f}",
                    "outputFile": result_path,
                },
                output_dir / f"run-{repeat}.log",
                rss_path=rss_path,
            )
            result = _read_cost_result(
                result_path,
                node_count,
                expected_epoch,
            )
        wall_times.append(float(result["wall_clock_s"]))
        peak_rss_values.append(peak_rss)
    return summarize_cost_runs(wall_times, peak_rss_values)


def _edge_transition_count(reference_dir: Path) -> tuple[int, int]:
    trace = load_edge_trace(reference_dir)
    edges = tuple(snapshot for _, snapshot in trace.snapshots)
    unique = len({edge_set_sha256(snapshot) for snapshot in edges})
    transitions = sum(
        len(previous ^ current)
        for previous, current in zip(edges, edges[1:])
    )
    return unique, transitions


def _checker_wall_time_s(scenario_dir: Path) -> float:
    start = time.perf_counter()
    check_scenario(scenario_dir)
    return time.perf_counter() - start


def ensure_comparison_evidence(
    output_path: Path,
    reference_dir: Path,
    held_dir: Path,
) -> tuple[dict[str, Any], float, int]:
    reference_manifest = _scenario_manifest(reference_dir)
    held_manifest = _scenario_manifest(held_dir)
    output_bytes = directory_size_bytes(held_dir)
    if output_path.exists():
        try:
            payload = read_json(output_path)
            comparison = payload["comparison"]
            if (
                payload["schema_version"] == SCHEMA_VERSION
                and payload["analysis_evidence_version"]
                == ANALYSIS_EVIDENCE_VERSION
                and payload["output_bytes"] == output_bytes
                and comparison["reference_scenario_sha256"]
                == reference_manifest["aggregate_scenario_sha256"]
                and comparison["held_scenario_sha256"]
                == held_manifest["aggregate_scenario_sha256"]
            ):
                return (
                    comparison,
                    float(payload["checker_wall_time_s"]),
                    output_bytes,
                )
        except (KeyError, TypeError, ValueError):
            pass
    checker_wall_time_s = _checker_wall_time_s(held_dir)
    comparison = compare_scenarios(reference_dir, held_dir)
    payload = {
        "schema_version": SCHEMA_VERSION,
        "analysis_evidence_version": ANALYSIS_EVIDENCE_VERSION,
        "checker_wall_time_s": checker_wall_time_s,
        "output_bytes": output_bytes,
        "comparison": comparison,
    }
    output_path.write_bytes(compact_json_bytes(payload))
    return comparison, checker_wall_time_s, output_bytes


def _build_record(
    preset: StudyPreset,
    window_index: int,
    window_offset_s: int,
    interval_s: int,
    held_manifest: dict[str, Any],
    comparison: dict[str, Any],
    mode: str,
    selection: dict[str, Any],
    reference_edge_sets: int,
    reference_edge_transitions: int,
    reference_selection_changes: int,
    output_bytes: int,
    checker_wall_time_s: float,
    cost: dict[str, Any] | None,
) -> dict[str, Any]:
    edge = comparison["edge_state"]
    ecmp = comparison["ecmp_candidates"]
    cost_fields = cost or {
        "topology_wall_time_min_s": None,
        "topology_wall_time_median_s": None,
        "topology_wall_time_max_s": None,
        "topology_peak_rss_kib": None,
    }
    return {
        "constellation": preset.scenario_name,
        "node_count": preset.node_count,
        "candidate_count": preset.candidate_count,
        "window_index": window_index,
        "window_offset_s": window_offset_s,
        "interval_s": interval_s,
        "snapshot_count": held_manifest["snapshot_count"],
        "reference_unique_edge_set_count": reference_edge_sets,
        "reference_edge_transition_count": reference_edge_transitions,
        "held_unique_edge_set_count":
            edge["held_unique_edge_set_count"],
        "absolute_edge_state_errors":
            edge["absolute_edge_state_errors"],
        "normalized_edge_state_disagreement":
            edge["normalized_edge_state_disagreement"],
        "endpoint_mismatch_seconds":
            edge["endpoint_mismatch_seconds"],
        "missed_active_edges": edge["missed_active_edges"],
        "spurious_active_edges": edge["spurious_active_edges"],
        "missed_transition_count": edge["missed_transition_count"],
        "mean_event_timing_error_s":
            edge["mean_event_timing_error_s"],
        "p95_event_timing_error_s":
            edge["p95_event_timing_error_s"],
        "max_event_timing_error_s":
            edge["max_event_timing_error_s"],
        "maximum_stale_duration_s":
            edge["maximum_stale_duration_s"],
        "component_count_mismatch_seconds":
            edge["component_count_mismatch_seconds"],
        "unreachable_pair_error_sum":
            edge["unreachable_pair_error_sum"],
        "max_unreachable_pair_error":
            edge["max_unreachable_pair_error"],
        "reference_unique_ecmp_candidate_fingerprint_count":
            ecmp[
                "reference_unique_ecmp_candidate_fingerprint_count"
            ],
        "held_unique_ecmp_candidate_fingerprint_count":
            ecmp["held_unique_ecmp_candidate_fingerprint_count"],
        "exact_candidate_match_ratio":
            ecmp["exact_candidate_match_ratio"],
        "candidate_mismatch_pair_seconds":
            ecmp["candidate_mismatch_pair_seconds"],
        "candidate_count_mismatch_pair_seconds":
            ecmp["candidate_count_mismatch_pair_seconds"],
        "shortest_hop_mismatch_pair_seconds":
            ecmp["shortest_hop_mismatch_pair_seconds"],
        "reachability_mismatch_pair_seconds":
            ecmp["reachability_mismatch_pair_seconds"],
        "mean_candidate_jaccard": ecmp["mean_candidate_jaccard"],
        "p05_candidate_jaccard": ecmp["p05_candidate_jaccard"],
        "minimum_candidate_jaccard":
            ecmp["minimum_candidate_jaccard"],
        "reference_ecmp_pair_fraction":
            ecmp["reference_ecmp_pair_fraction"],
        "held_ecmp_pair_fraction":
            ecmp["held_ecmp_pair_fraction"],
        "routing_mode": mode,
        "reference_selected_next_hop_change_count":
            reference_selection_changes,
        "selected_next_hop_exact_match_ratio":
            selection["selected_next_hop_exact_match_ratio"],
        "selected_next_hop_mismatch_count":
            selection["selected_next_hop_mismatch_count"],
        "selected_candidate_survival_ratio":
            selection["selected_candidate_survival_ratio"],
        "selected_candidate_missing_count":
            selection["selected_candidate_missing_count"],
        "candidate_count_trace_mismatch_count":
            selection["candidate_count_trace_mismatch_count"],
        "event_candidate_count_trace_mismatch_count":
            selection["event_candidate_count_trace_mismatch_count"],
        "reachability_mismatch_count":
            selection["reachability_mismatch_count"],
        "output_bytes": output_bytes,
        "checker_wall_time_s": checker_wall_time_s,
        **cost_fields,
        "route_recomputation_count":
            held_manifest["snapshot_count"] - 1,
    }


def run_study(
    preset_keys: tuple[str, ...],
    work_dir: Path,
    report_dir: Path,
    duration_s: int,
    intervals: tuple[int, ...],
    topology_cost_repeats: int,
    waf: Path,
) -> dict[str, Any]:
    if (
        duration_s <= 0
        or any(duration_s % interval_s != 0 for interval_s in intervals)
    ):
        raise IntervalStudyError(
            "duration must be positive and divisible by every interval"
        )
    if intervals != DEFAULT_INTERVALS:
        raise IntervalStudyError(
            f"full report intervals must be {DEFAULT_INTERVALS}"
        )
    if topology_cost_repeats <= 0:
        raise IntervalStudyError("topology cost repeats must be positive")
    if not waf.is_file():
        raise IntervalStudyError(f"waf does not exist: {waf}")

    root = Path(work_dir)
    root.mkdir(parents=True, exist_ok=True)
    records = []
    probe_catalog = {
        "schema_version": SCHEMA_VERSION,
        "constellations": [],
    }
    for preset_key in preset_keys:
        preset = load_study_preset(preset_key)
        offsets = orbital_window_offsets(preset.altitude_km)
        preset_root = root / preset.scenario_name
        probe_pairs_path = preset_root / "probe-pairs.json"
        probe_catalog_entry = None
        for window_index, offset_s in enumerate(offsets):
            window_root = (
                preset_root
                / f"window-{window_index}-offset-{offset_s}"
            )
            config_path = window_root / "scenario-config.json"
            write_window_config(
                preset,
                duration_s,
                offset_s,
                config_path,
            )
            reference_dir = window_root / "reference"
            ensure_reference_scenario(
                config_path,
                reference_dir,
            )
            topology_manifest = read_json(
                reference_dir / "topology" / "manifest.json"
            )
            if (
                not isinstance(topology_manifest, dict)
                or topology_manifest["candidate_count"]
                != preset.candidate_count
                or topology_manifest["node_count"] != preset.node_count
            ):
                raise IntervalStudyError(
                    f"generated candidate contract differs: {reference_dir}"
                )
            if probe_catalog_entry is None:
                pairs, probe_sha256 = write_probe_pairs(
                    reference_dir,
                    probe_pairs_path,
                )
                probe_catalog_entry = {
                    "constellation": preset.scenario_name,
                    "node_count": preset.node_count,
                    "probe_pairs_sha256": probe_sha256,
                    "probe_pairs": [
                        pair.input_dict() for pair in pairs
                    ],
                }

            reference_edge_sets, reference_edge_transitions = (
                _edge_transition_count(reference_dir)
            )
            held_data = {}
            for interval_s in intervals:
                held_dir = window_root / f"interval-{interval_s}"
                held_manifest = ensure_held_scenario(
                    reference_dir,
                    interval_s,
                    held_dir,
                )
                evidence = (
                    window_root
                    / "analysis"
                    / f"interval-{interval_s}.json"
                )
                evidence.parent.mkdir(parents=True, exist_ok=True)
                comparison, checker_wall, output_bytes = (
                    ensure_comparison_evidence(
                        evidence,
                        reference_dir,
                        held_dir,
                    )
                )
                held_data[interval_s] = {
                    "directory": held_dir,
                    "manifest": held_manifest,
                    "checker_wall_time_s": checker_wall,
                    "comparison": comparison,
                    "output_bytes": output_bytes,
                }

            costs = {}
            if window_index == 0:
                for interval_s in intervals:
                    item = held_data[interval_s]
                    costs[interval_s] = ensure_topology_cost_runs(
                        waf,
                        item["directory"],
                        duration_s,
                        preset.node_count,
                        item["manifest"]["snapshot_count"],
                        topology_cost_repeats,
                        (
                            window_root
                            / "cost"
                            / f"interval-{interval_s}"
                        ),
                    )

            for mode in ROUTING_MODES:
                audit_root = window_root / "route-audit"
                reference_rows = ensure_selection_audit(
                    waf,
                    reference_dir,
                    probe_pairs_path,
                    mode,
                    duration_s,
                    1,
                    audit_root / f"reference-{mode}.jsonl",
                    preset.node_count,
                )
                reference_changes = count_selected_next_hop_changes(
                    reference_rows
                )
                for interval_s in intervals:
                    item = held_data[interval_s]
                    if interval_s == 1:
                        held_rows = reference_rows
                    else:
                        held_rows = ensure_selection_audit(
                            waf,
                            item["directory"],
                            probe_pairs_path,
                            mode,
                            duration_s,
                            interval_s,
                            (
                                audit_root
                                / f"interval-{interval_s}-{mode}.jsonl"
                            ),
                            preset.node_count,
                        )
                    selection = compare_selection_audits(
                        reference_rows,
                        held_rows,
                    )
                    records.append(
                        _build_record(
                            preset,
                            window_index,
                            offset_s,
                            interval_s,
                            item["manifest"],
                            item["comparison"],
                            mode,
                            selection,
                            reference_edge_sets,
                            reference_edge_transitions,
                            reference_changes,
                            item["output_bytes"],
                            item["checker_wall_time_s"],
                            costs.get(interval_s),
                        )
                    )
        if probe_catalog_entry is None:
            raise AssertionError("study preset produced no windows")
        probe_catalog["constellations"].append(probe_catalog_entry)
        print(
            f"[study] completed constellation: {preset.scenario_name}",
            flush=True,
        )

    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=REPOSITORY_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    ).stdout.strip()
    metadata = {
        "schema_version": SCHEMA_VERSION,
        "satcompute_commit": commit,
        "duration_s": duration_s,
        "reference_step_s": 1,
        "intervals_s": list(intervals),
        "window_count": 3,
        "topology_cost_repeats": topology_cost_repeats,
        "fixed_delay_us": 8000,
        "link_bandwidth_kbps": 2_000_000,
        "isl_candidate_strategy": "plus-grid",
        "seam_enabled": False,
        "routing_metric": "unweighted-hop-count",
        "routing_modes": list(ROUTING_MODES),
        "ecmp_hash_seed": HASH_SEED,
        "constellations": [
            {
                "scenario_name": load_study_preset(key).scenario_name,
                "orbital_period_s":
                    orbital_period_seconds(
                        load_study_preset(key).altitude_km
                    ),
                "window_offsets_s": list(
                    orbital_window_offsets(
                        load_study_preset(key).altitude_km
                    )
                ),
            }
            for key in preset_keys
        ],
    }
    recommendations = write_report_bundle(
        report_dir,
        metadata,
        records,
        probe_catalog,
    )
    print(f"[study] report: {report_dir}", flush=True)
    return recommendations


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument(
        "--preset",
        action="append",
        choices=tuple(PRESET_FILES),
        dest="presets",
        help="repeat to select a subset; default runs all scales",
    )
    parser.add_argument("--duration-s", type=int, default=1000)
    parser.add_argument(
        "--topology-cost-repeats",
        type=int,
        default=3,
    )
    parser.add_argument(
        "--waf",
        type=Path,
        default=REPOSITORY_ROOT / "waf",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    preset_keys = tuple(arguments.presets or PRESET_FILES)
    try:
        recommendations = run_study(
            preset_keys,
            arguments.work_dir.absolute(),
            arguments.report_dir.absolute(),
            1000 if arguments.duration_s is None else arguments.duration_s,
            DEFAULT_INTERVALS,
            arguments.topology_cost_repeats,
            arguments.waf.absolute(),
        )
    except (OSError, ValueError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(recommendations, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
