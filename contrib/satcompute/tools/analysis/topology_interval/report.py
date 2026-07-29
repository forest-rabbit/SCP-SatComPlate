#!/usr/bin/env python3
"""Build deterministic interval-study CSV, JSON, Markdown, and decisions."""

from __future__ import annotations

import csv
import json
from collections import defaultdict
from pathlib import Path
from statistics import median
from typing import Any, Iterable


SCHEMA_VERSION = "0.1"
ROUTING_MODES = (
    "global-first",
    "global-hash-per-flow",
    "global-hrw-per-flow",
)
CSV_FIELDS = (
    "constellation",
    "node_count",
    "candidate_count",
    "window_index",
    "window_offset_s",
    "interval_s",
    "snapshot_count",
    "reference_unique_edge_set_count",
    "reference_edge_transition_count",
    "held_unique_edge_set_count",
    "absolute_edge_state_errors",
    "normalized_edge_state_disagreement",
    "endpoint_mismatch_seconds",
    "missed_active_edges",
    "spurious_active_edges",
    "missed_transition_count",
    "mean_event_timing_error_s",
    "p95_event_timing_error_s",
    "max_event_timing_error_s",
    "maximum_stale_duration_s",
    "component_count_mismatch_seconds",
    "unreachable_pair_error_sum",
    "max_unreachable_pair_error",
    "reference_unique_ecmp_candidate_fingerprint_count",
    "held_unique_ecmp_candidate_fingerprint_count",
    "exact_candidate_match_ratio",
    "candidate_mismatch_pair_seconds",
    "candidate_count_mismatch_pair_seconds",
    "shortest_hop_mismatch_pair_seconds",
    "reachability_mismatch_pair_seconds",
    "mean_candidate_jaccard",
    "p05_candidate_jaccard",
    "minimum_candidate_jaccard",
    "reference_ecmp_pair_fraction",
    "held_ecmp_pair_fraction",
    "routing_mode",
    "reference_selected_next_hop_change_count",
    "selected_next_hop_exact_match_ratio",
    "selected_next_hop_mismatch_count",
    "selected_candidate_survival_ratio",
    "selected_candidate_missing_count",
    "candidate_count_trace_mismatch_count",
    "event_candidate_count_trace_mismatch_count",
    "reachability_mismatch_count",
    "output_bytes",
    "checker_wall_time_s",
    "topology_wall_time_min_s",
    "topology_wall_time_median_s",
    "topology_wall_time_max_s",
    "topology_peak_rss_kib",
    "route_recomputation_count",
    "passes_fidelity_gate",
    "all_tested_intervals_fidelity_equivalent",
)
OUTPUT_FILES = (
    "REPORT.md",
    "interval-results.csv",
    "interval-results.json",
    "recommendations.json",
    "probe-pairs.json",
)


class IntervalReportError(ValueError):
    """Raised when interval records cannot form one strict report."""


def summarize_cost_runs(
    wall_times_s: Iterable[float],
    peak_rss_kib: Iterable[int | None],
) -> dict[str, float | int | None]:
    """Summarize repeated topology-only measurements."""
    wall_times = tuple(float(value) for value in wall_times_s)
    rss_values = tuple(
        int(value) for value in peak_rss_kib if value is not None
    )
    if not wall_times or any(value < 0.0 for value in wall_times):
        raise IntervalReportError(
            "topology cost requires non-negative wall times"
        )
    if any(value <= 0 for value in rss_values):
        raise IntervalReportError("peak RSS values must be positive")
    return {
        "topology_wall_time_min_s": min(wall_times),
        "topology_wall_time_median_s": median(wall_times),
        "topology_wall_time_max_s": max(wall_times),
        "topology_peak_rss_kib": max(rss_values) if rss_values else None,
    }


def _record_gate(record: dict[str, Any]) -> bool:
    return (
        record["missed_active_edges"] == 0
        and record["spurious_active_edges"] == 0
        and record["missed_transition_count"] == 0
        and record["component_count_mismatch_seconds"] == 0
        and record["unreachable_pair_error_sum"] == 0
        and record["exact_candidate_match_ratio"] == 1.0
        and record["selected_candidate_survival_ratio"] == 1.0
        and record["selected_next_hop_exact_match_ratio"] == 1.0
    )


def _record_key(record: dict[str, Any]) -> tuple[Any, ...]:
    return (
        record["constellation"],
        record["window_index"],
        record["interval_s"],
        ROUTING_MODES.index(record["routing_mode"]),
    )


def finalize_records_and_recommendations(
    records: Iterable[dict[str, Any]],
    *,
    expected_intervals: tuple[int, ...] = (1, 2, 5, 10, 20),
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """Apply the frozen all-mode fidelity gate and choose robust intervals."""
    normalized = [dict(record) for record in records]
    if not normalized:
        raise IntervalReportError("interval report has no records")
    if (
        not expected_intervals
        or tuple(sorted(set(expected_intervals))) != expected_intervals
        or expected_intervals[0] != 1
    ):
        raise IntervalReportError(
            "expected intervals must be sorted, unique, and begin at 1"
        )
    by_interval: dict[tuple[str, int, int], list[dict[str, Any]]] = (
        defaultdict(list)
    )
    for record in normalized:
        mode = record.get("routing_mode")
        if mode not in ROUTING_MODES:
            raise IntervalReportError(f"unknown routing mode: {mode}")
        by_interval[
            (
                record["constellation"],
                record["window_index"],
                record["interval_s"],
            )
        ].append(record)

    interval_passes = {}
    for key, group in by_interval.items():
        modes = tuple(
            sorted(
                (record["routing_mode"] for record in group),
                key=ROUTING_MODES.index,
            )
        )
        if modes != ROUTING_MODES:
            raise IntervalReportError(
                f"interval group {key} does not contain all routing modes"
            )
        interval_passes[key] = all(_record_gate(record) for record in group)

    by_window: dict[tuple[str, int], list[dict[str, Any]]] = defaultdict(list)
    for record in normalized:
        by_window[
            (record["constellation"], record["window_index"])
        ].append(record)

    window_decisions = {}
    for key, group in by_window.items():
        intervals = tuple(sorted({record["interval_s"] for record in group}))
        if intervals != expected_intervals:
            raise IntervalReportError(
                f"window {key} intervals differ from the study contract"
            )
        passing = [
            interval_s
            for interval_s in expected_intervals
            if interval_passes[(key[0], key[1], interval_s)]
        ]
        all_equivalent = len(passing) == len(expected_intervals)
        window_decisions[key] = {
            "constellation": key[0],
            "window_index": key[1],
            "window_offset_s": group[0]["window_offset_s"],
            "recommended_interval_s": max(passing) if passing else 1,
            "passing_intervals_s": passing,
            "all_tested_intervals_fidelity_equivalent": all_equivalent,
        }

    for record in normalized:
        interval_key = (
            record["constellation"],
            record["window_index"],
            record["interval_s"],
        )
        record["passes_fidelity_gate"] = interval_passes[interval_key]
        record["all_tested_intervals_fidelity_equivalent"] = (
            window_decisions[interval_key[:2]][
                "all_tested_intervals_fidelity_equivalent"
            ]
        )

    constellation_groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for decision in window_decisions.values():
        constellation_groups[decision["constellation"]].append(decision)
    constellation_decisions = []
    for constellation in sorted(constellation_groups):
        windows = sorted(
            constellation_groups[constellation],
            key=lambda item: item["window_index"],
        )
        if tuple(item["window_index"] for item in windows) != (0, 1, 2):
            raise IntervalReportError(
                f"{constellation} must contain windows 0, 1, and 2"
            )
        all_equivalent = all(
            item["all_tested_intervals_fidelity_equivalent"]
            for item in windows
        )
        constellation_decisions.append(
            {
                "constellation": constellation,
                "main_window_recommended_interval_s":
                    windows[0]["recommended_interval_s"],
                "robust_recommended_interval_s": min(
                    item["recommended_interval_s"] for item in windows
                ),
                "upper_bound_identified": not all_equivalent,
                "all_tested_intervals_fidelity_equivalent":
                    all_equivalent,
                "windows": windows,
            }
        )
    return (
        sorted(normalized, key=_record_key),
        {
            "schema_version": SCHEMA_VERSION,
            "decision_rule":
                "largest tested interval passing every frozen fidelity gate",
            "constellations": constellation_decisions,
        },
    )


def _json_bytes(payload: Any) -> bytes:
    return (
        json.dumps(
            payload,
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")


def _csv_text(records: list[dict[str, Any]]) -> str:
    from io import StringIO

    output = StringIO(newline="")
    writer = csv.DictWriter(
        output,
        fieldnames=CSV_FIELDS,
        extrasaction="ignore",
        lineterminator="\n",
    )
    writer.writeheader()
    for record in records:
        writer.writerow(
            {
                field: (
                    ""
                    if record.get(field) is None
                    else record.get(field)
                )
                for field in CSV_FIELDS
            }
        )
    return output.getvalue()


def _markdown(
    metadata: dict[str, Any],
    recommendations: dict[str, Any],
    record_count: int,
) -> str:
    lines = [
        "# N2 快照间隔研究",
        "",
        "本报告比较同一条 1 秒参考轨迹的 1/2/5/10/20 秒 "
        "zero-order-hold 快照。",
        "实验固定使用 8000 µs 单向 ISL 时延、2 Gbps 链路、"
        "plus-grid 候选图和无权 hop-count 路由；没有人为制造拓扑变化。",
        "",
        "## 结论",
        "",
        "| 星座 | 主窗口推荐 | 三窗口稳健推荐 | 已找到上界 | "
        "全部候选等价 |",
        "| --- | ---: | ---: | --- | --- |",
    ]
    for item in recommendations["constellations"]:
        lines.append(
            "| {constellation} | {main}s | {robust}s | {upper} | "
            "{equivalent} |".format(
                constellation=item["constellation"],
                main=item["main_window_recommended_interval_s"],
                robust=item["robust_recommended_interval_s"],
                upper="是" if item["upper_bound_identified"] else "否",
                equivalent=(
                    "是"
                    if item[
                        "all_tested_intervals_fidelity_equivalent"
                    ]
                    else "否"
                ),
            )
        )
    lines.extend(
        [
            "",
            "若“已找到上界”为否，推荐值只表示当前模型下最大已测试且成本最优的"
            "间隔，不表示真实星座的普适最优值，也不支持外推到 20 秒以上。",
            "",
            "## 证据范围",
            "",
            f"- 结果行数：{record_count}（每个组合分别记录三种路由模式）。",
            f"- 轨道窗口：{metadata['window_count']} 个。",
            f"- topology-only 重复次数：{metadata['topology_cost_repeats']}。",
            "- `global-size-aware-hrw` 依赖活动流预留状态，不属于本次纯拓扑"
            "间隔审计。",
            "- 完整快照、原始路由 JSONL 和运行日志位于实验工作目录，未提交。",
            "",
            "详细逐项数据见 `interval-results.csv` 与 "
            "`interval-results.json`；机器可读决策见 "
            "`recommendations.json`。",
            "",
        ]
    )
    return "\n".join(lines)


def write_report_bundle(
    output_dir: Path,
    metadata: dict[str, Any],
    records: Iterable[dict[str, Any]],
    probe_catalog: dict[str, Any],
) -> dict[str, Any]:
    """Write the five committed report artifacts in deterministic order."""
    root = Path(output_dir)
    root.mkdir(parents=True, exist_ok=True)
    unknown = {
        path.name for path in root.iterdir()
        if path.name not in OUTPUT_FILES
    }
    if unknown:
        raise IntervalReportError(
            f"report directory contains unknown entries: {sorted(unknown)}"
        )
    finalized, recommendations = finalize_records_and_recommendations(
        records
    )
    result = {
        "schema_version": SCHEMA_VERSION,
        "metadata": metadata,
        "records": finalized,
    }
    (root / "interval-results.json").write_bytes(_json_bytes(result))
    (root / "recommendations.json").write_bytes(
        _json_bytes(recommendations)
    )
    (root / "probe-pairs.json").write_bytes(_json_bytes(probe_catalog))
    (root / "interval-results.csv").write_text(
        _csv_text(finalized),
        encoding="utf-8",
        newline="",
    )
    (root / "REPORT.md").write_text(
        _markdown(metadata, recommendations, len(finalized)),
        encoding="utf-8",
        newline="\n",
    )
    return recommendations
