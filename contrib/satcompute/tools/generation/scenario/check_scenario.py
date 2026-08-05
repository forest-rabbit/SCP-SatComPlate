#!/usr/bin/env python3
"""Check one v0.3 independent-input bundle; it is not a runtime scenario config."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

from .configuration import (
    TopologyContext,
    load_json_file,
    validate_compute_profile,
    validate_task_trace,
    validate_transfer_trace,
)
from .generate_scenario import BUNDLE_MANIFEST


class InputBundleCheckError(ValueError):
    """Raised when a bundle inventory, hash, or input relationship differs."""


ROOT_FIELDS = {
    "schema_version",
    "bundle_name",
    "workload_mode",
    "topology_provenance",
    "inputs",
    "counts",
}
INPUT_KINDS = {
    "compute_profile": "compute-profile.json",
    "task_trace": "task-trace.json",
    "transfer_trace": "transfer-trace.json",
    "fault_trace": "fault-trace.json",
}


def _require(condition, message):
    if not condition:
        raise InputBundleCheckError(message)


def _sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _require_sha256(value, name):
    _require(
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value),
        f"{name} must be a lowercase SHA-256",
    )


def check_input_bundle(bundle_dir: Path):
    """Validate a closed-world bundle and all currently supported inputs."""
    root = Path(bundle_dir)
    manifest = load_json_file(root / BUNDLE_MANIFEST, "input bundle manifest")
    _require(set(manifest) == ROOT_FIELDS, "bundle manifest fields differ")
    _require(manifest["schema_version"] == "0.1", "bundle schema must be 0.1")
    _require(
        isinstance(manifest["bundle_name"], str) and manifest["bundle_name"],
        "bundle_name must be non-empty",
    )
    mode = manifest["workload_mode"]
    _require(mode in {"task", "transfer"}, "workload_mode differs")

    provenance = manifest["topology_provenance"]
    _require(
        isinstance(provenance, dict)
        and set(provenance)
        == {
            "manifest_sha256",
            "constellation_config_sha256",
            "simulation_duration_ns",
            "satellite_ids",
        },
        "topology_provenance fields differ",
    )
    _require_sha256(provenance["manifest_sha256"], "manifest_sha256")
    _require_sha256(
        provenance["constellation_config_sha256"],
        "constellation_config_sha256",
    )
    duration_ns = provenance["simulation_duration_ns"]
    satellite_ids = provenance["satellite_ids"]
    _require(
        isinstance(duration_ns, int)
        and not isinstance(duration_ns, bool)
        and duration_ns > 0,
        "simulation_duration_ns must be positive",
    )
    _require(
        isinstance(satellite_ids, list)
        and satellite_ids
        and satellite_ids == sorted(set(satellite_ids))
        and all(
            isinstance(node_id, int)
            and not isinstance(node_id, bool)
            and node_id >= 0
            for node_id in satellite_ids
        ),
        "satellite_ids must be canonical non-negative integers",
    )

    inputs = manifest["inputs"]
    _require(
        isinstance(inputs, dict) and set(inputs) == set(INPUT_KINDS),
        "input inventory fields differ",
    )
    _require(inputs["fault_trace"] is None, "fault_trace is reserved and unsupported")
    expected_presence = (
        {
            "compute_profile": True,
            "task_trace": True,
            "transfer_trace": False,
        }
        if mode == "task"
        else {
            "compute_profile": False,
            "task_trace": False,
            "transfer_trace": True,
        }
    )
    expected_files = {BUNDLE_MANIFEST}
    paths = {}
    for kind, expected_filename in INPUT_KINDS.items():
        record = inputs[kind]
        if kind == "fault_trace":
            continue
        _require(
            (record is not None) == expected_presence[kind],
            f"{kind} presence differs from workload_mode",
        )
        if record is None:
            continue
        _require(
            isinstance(record, dict) and set(record) == {"filename", "sha256"},
            f"{kind} record fields differ",
        )
        _require(record["filename"] == expected_filename, f"{kind} filename differs")
        _require_sha256(record["sha256"], f"{kind}.sha256")
        path = root / expected_filename
        _require(path.is_file(), f"missing {expected_filename}")
        _require(_sha256(path) == record["sha256"], f"hash differs for {expected_filename}")
        expected_files.add(expected_filename)
        paths[kind] = path

    actual_files = {path.name for path in root.iterdir() if path.is_file()}
    actual_directories = [path.name for path in root.iterdir() if path.is_dir()]
    _require(not actual_directories, "bundle must not contain subdirectories")
    _require(actual_files == expected_files, "bundle closed-world file inventory differs")

    context = TopologyContext(
        trace_root=root,
        manifest_path=root / BUNDLE_MANIFEST,
        manifest_sha256=provenance["manifest_sha256"],
        constellation_config_sha256=provenance["constellation_config_sha256"],
        simulation_duration_ns=duration_ns,
        satellite_ids=tuple(satellite_ids),
    )
    compute_nodes = ()
    task_count = 0
    transfer_count = 0
    if mode == "task":
        compute_nodes = validate_compute_profile(
            load_json_file(paths["compute_profile"], "ComputeProfile"),
            context,
        )
        task_count = validate_task_trace(
            load_json_file(paths["task_trace"], "TaskTrace"),
            context,
            compute_nodes,
        )
    else:
        transfer_count = validate_transfer_trace(
            load_json_file(paths["transfer_trace"], "TransferTrace"),
            context,
        )

    counts = manifest["counts"]
    expected_counts = {
        "compute_node_count": len(compute_nodes),
        "task_count": task_count,
        "transfer_count": transfer_count,
    }
    _require(counts == expected_counts, "bundle counts differ")
    return {
        "bundle_name": manifest["bundle_name"],
        "workload_mode": mode,
        "satellite_count": len(satellite_ids),
        **expected_counts,
    }


# Keep the ns-3.33 checker import name while explicitly checking only a bundle.
check_scenario = check_input_bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle-dir", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        summary = check_input_bundle(arguments.bundle_dir)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(summary, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
