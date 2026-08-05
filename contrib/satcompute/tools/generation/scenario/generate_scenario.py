#!/usr/bin/env python3
"""Build a deterministic bundle of independent SatCompute workload inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile

from .configuration import (
    load_json_file,
    load_topology_context,
    validate_compute_profile,
    validate_task_trace,
    validate_transfer_trace,
)


BUNDLE_MANIFEST = "input-bundle-manifest.json"
SAFE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")


class InputBundleGenerationError(ValueError):
    """Raised when requested sources do not form one platform workload mode."""


def _sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _reject_overlap(trace_root, output_dir):
    trace = trace_root.resolve()
    output = output_dir.resolve()
    if trace == output or trace in output.parents or output in trace.parents:
        raise InputBundleGenerationError(
            "topology trace and output directories must not overlap"
        )


def _input_record(filename, source):
    return {"filename": filename, "sha256": _sha256(source)}


def generate_input_bundle(
    bundle_name: str,
    topology_trace: Path,
    output_dir: Path,
    *,
    compute_profile: Path | None = None,
    task_trace: Path | None = None,
    transfer_trace: Path | None = None,
):
    """Validate and byte-copy one task or direct-transfer workload bundle."""
    if not isinstance(bundle_name, str) or SAFE_NAME.fullmatch(bundle_name) is None:
        raise InputBundleGenerationError("bundle_name must be a safe non-empty token")
    output = Path(output_dir)
    context = load_topology_context(topology_trace)
    _reject_overlap(context.trace_root, output)
    if output.exists():
        raise InputBundleGenerationError(f"output directory already exists: {output}")

    has_compute = compute_profile is not None
    has_tasks = task_trace is not None
    has_transfers = transfer_trace is not None
    if has_compute != has_tasks:
        raise InputBundleGenerationError(
            "compute_profile and task_trace must be provided together"
        )
    if has_transfers == has_tasks:
        raise InputBundleGenerationError(
            "provide exactly one workload mode: task pair or transfer trace"
        )

    sources = {}
    compute_nodes = ()
    task_count = 0
    transfer_count = 0
    if has_tasks:
        compute_source = Path(compute_profile).resolve()
        task_source = Path(task_trace).resolve()
        compute_payload = load_json_file(compute_source, "ComputeProfile")
        task_payload = load_json_file(task_source, "TaskTrace")
        compute_nodes = validate_compute_profile(compute_payload, context)
        task_count = validate_task_trace(task_payload, context, compute_nodes)
        sources["compute_profile"] = ("compute-profile.json", compute_source)
        sources["task_trace"] = ("task-trace.json", task_source)
        workload_mode = "task"
    else:
        transfer_source = Path(transfer_trace).resolve()
        transfer_payload = load_json_file(transfer_source, "TransferTrace")
        transfer_count = validate_transfer_trace(transfer_payload, context)
        sources["transfer_trace"] = ("transfer-trace.json", transfer_source)
        workload_mode = "transfer"

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(
        tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent)
    )
    try:
        input_records = {
            "compute_profile": None,
            "task_trace": None,
            "transfer_trace": None,
            "fault_trace": None,
        }
        for kind, (filename, source) in sources.items():
            shutil.copyfile(source, temporary / filename)
            input_records[kind] = _input_record(filename, source)
        manifest = {
            "schema_version": "0.1",
            "bundle_name": bundle_name,
            "workload_mode": workload_mode,
            "topology_provenance": {
                "manifest_sha256": context.manifest_sha256,
                "constellation_config_sha256": (
                    context.constellation_config_sha256
                ),
                "simulation_duration_ns": context.simulation_duration_ns,
                "satellite_ids": list(context.satellite_ids),
            },
            "inputs": input_records,
            "counts": {
                "compute_node_count": len(compute_nodes),
                "task_count": task_count,
                "transfer_count": transfer_count,
            },
        }
        (temporary / BUNDLE_MANIFEST).write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

        from .check_scenario import check_input_bundle

        summary = check_input_bundle(temporary)
        temporary.replace(output)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    return {
        **summary,
        "manifest": str(output / BUNDLE_MANIFEST),
    }


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle-name", required=True)
    parser.add_argument("--topology-trace", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--compute-profile", type=Path)
    parser.add_argument("--task-trace", type=Path)
    parser.add_argument("--transfer-trace", type=Path)
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    try:
        result = generate_input_bundle(
            arguments.bundle_name,
            arguments.topology_trace,
            arguments.output_dir,
            compute_profile=arguments.compute_profile,
            task_trace=arguments.task_trace,
            transfer_trace=arguments.transfer_trace,
        )
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
