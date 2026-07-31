#!/usr/bin/env python3
"""Run the lightweight CI contract for snapshot-interval analysis."""

from __future__ import annotations

import tempfile
from pathlib import Path

from contrib.satcompute.tests.support.paths import (
    REPOSITORY_ROOT,
    TOPOLOGY_SNAPSHOT_FIXTURES,
)
from contrib.satcompute.tools.analysis.topology_interval.compare_intervals import (
    compare_scenarios,
)
from contrib.satcompute.tools.analysis.topology_interval.downsample_scenario import (
    downsample_scenario,
)
from contrib.satcompute.tools.analysis.topology_interval.edge_state import (
    load_topology_edge_trace,
)
from contrib.satcompute.tools.analysis.topology_interval.route_probe import (
    compare_selection_audits,
    verify_cpp_candidate_audit,
    write_probe_pairs,
)
from contrib.satcompute.tools.analysis.topology_interval.run_interval_study import (
    DEFAULT_INTERVALS,
    _run_waf,
    ensure_selection_audit,
    load_study_preset,
    write_window_config,
)
from contrib.satcompute.tools.generation.scenario.generate_scenario import (
    generate_scenario,
)
from contrib.satcompute.tools.generation.topology.common.hash_utils import (
    compact_json,
)


def _candidate_gate(
    waf: Path,
    topology_dir: Path,
    audit_times: str,
    simulation_duration_s: float,
    output_path: Path,
) -> dict:
    _run_waf(
        waf,
        "satcompute-route-candidate-audit",
        {
            "topologyDir": topology_dir,
            "simulationDuration": simulation_duration_s,
            "auditTimes": audit_times,
            "outputFile": output_path,
        },
        output_path.with_suffix(".log"),
    )
    return verify_cpp_candidate_audit(
        load_topology_edge_trace(topology_dir),
        output_path,
    )


def _require_fidelity(comparison: dict) -> None:
    edge = comparison["edge_state"]
    ecmp = comparison["ecmp_candidates"]
    if (
        edge["missed_active_edges"] != 0
        or edge["spurious_active_edges"] != 0
        or edge["missed_transition_count"] != 0
        or edge["component_count_mismatch_seconds"] != 0
        or edge["unreachable_pair_error_sum"] != 0
        or ecmp["exact_candidate_match_ratio"] != 1.0
    ):
        raise RuntimeError("120-second interval fixture failed fidelity")


def main() -> int:
    waf = REPOSITORY_ROOT / "waf"
    with tempfile.TemporaryDirectory(
        prefix="satcompute-interval-ci-"
    ) as temp:
        root = Path(temp)
        preset = load_study_preset("66")
        config_path = root / "scenario-config.json"
        write_window_config(preset, 120, 0, config_path)
        reference = root / "reference"
        generate_scenario(config_path, reference)

        snapshot_counts = {}
        held = {}
        for interval_s in DEFAULT_INTERVALS:
            output = root / f"interval-{interval_s}"
            manifest = downsample_scenario(
                reference,
                interval_s,
                output,
            )
            snapshot_counts[str(interval_s)] = manifest["snapshot_count"]
            held[interval_s] = output
            _require_fidelity(compare_scenarios(reference, output))
        if snapshot_counts != {
            "1": 121,
            "2": 61,
            "5": 25,
            "10": 13,
            "20": 7,
        }:
            raise RuntimeError("120-second downsample counts differ")

        fixture_root = TOPOLOGY_SNAPSHOT_FIXTURES
        candidate_gates = {
            "diamond_static": _candidate_gate(
                waf,
                fixture_root / "diamond-4-static",
                "0",
                1.0,
                root / "diamond-static.jsonl",
            ),
            "diamond_dynamic": _candidate_gate(
                waf,
                fixture_root / "diamond-4-dynamic",
                "0,2,4",
                4.1,
                root / "diamond-dynamic.jsonl",
            ),
            "synthetic_66": _candidate_gate(
                waf,
                reference / "topology",
                "0",
                1.0,
                root / "synthetic-66.jsonl",
            ),
        }

        probe_path = root / "probe-pairs.json"
        pairs, probe_sha256 = write_probe_pairs(
            reference,
            probe_path,
        )
        reference_rows = ensure_selection_audit(
            waf,
            reference,
            probe_path,
            "global-hrw-per-flow",
            120,
            1,
            root / "selection-reference.jsonl",
            66,
        )
        held_rows = ensure_selection_audit(
            waf,
            held[20],
            probe_path,
            "global-hrw-per-flow",
            120,
            20,
            root / "selection-20.jsonl",
            66,
        )
        selection = compare_selection_audits(
            reference_rows,
            held_rows,
        )
        if (
            selection["selected_next_hop_exact_match_ratio"] != 1.0
            or selection["selected_candidate_survival_ratio"] != 1.0
            or selection["selected_next_hop_mismatch_count"] != 0
        ):
            raise RuntimeError("C++ held route selection differs")

        print(
            compact_json(
                {
                    "schema_version": "0.1",
                    "snapshot_counts": snapshot_counts,
                    "candidate_gates": candidate_gates,
                    "probe_pair_count": len(pairs),
                    "probe_pairs_sha256": probe_sha256,
                    "selection": selection,
                    "conclusion": "success",
                }
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
