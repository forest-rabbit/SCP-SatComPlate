"""Validate a generated SatCompute 0.3 topology trace without orbit math."""

import hashlib
import json
from pathlib import Path


class TraceValidationError(ValueError):
    """Raised when a topology trace violates its closed-world inventory."""


ROOT_FIELDS = {
    "schema_version",
    "run_name",
    "constellation_config_sha256",
    "ns3_version",
    "state_semantics",
    "coordinate_frame",
    "coordinate_units",
    "speed_of_light_m_per_s",
    "simulation_duration_ns",
    "trace_interval_ns",
    "network_update_interval_ns",
    "include_final_state",
    "constellation",
    "topology",
    "randomness",
    "slice_count",
    "slices",
}
SLICE_FIELDS = {
    "simulation_time_ns",
    "nodes_file",
    "nodes_sha256",
    "topology_file",
    "topology_sha256",
    "active_link_count",
}


def require(condition, message):
    if not condition:
        raise TraceValidationError(message)


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise TraceValidationError(f"cannot read JSON {path}: {error}") from error


def time_token(time_ns):
    seconds, fraction = divmod(time_ns, 1_000_000_000)
    if fraction == 0:
        return str(seconds)
    return f"{seconds}.{fraction:09d}".rstrip("0")


def verify_file(directory, filename, expected_hash):
    relative = Path(filename)
    require(relative.name == filename and not relative.is_absolute(), "slice path must be a basename")
    path = directory / relative
    require(path.is_file(), f"missing listed slice {filename}")
    require(
        len(expected_hash) == 64
        and all(character in "0123456789abcdef" for character in expected_hash),
        f"invalid SHA-256 for {filename}",
    )
    actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
    require(actual_hash == expected_hash, f"hash differs for {filename}")
    return path


def validate_trace(directory):
    directory = Path(directory)
    manifest_path = directory / "manifest.json"
    manifest = read_json(manifest_path)
    require(isinstance(manifest, dict) and set(manifest) == ROOT_FIELDS, "manifest fields differ")
    require(manifest["schema_version"] == "0.3", "manifest schema must be 0.3")
    require(manifest["ns3_version"] == "3.48", "manifest ns-3 version differs")
    require(manifest["state_semantics"] == "orbit-policy-evaluation", "state semantics differ")
    require(
        manifest["coordinate_frame"] == "ECEF" and manifest["coordinate_units"] == "m",
        "coordinate contract differs",
    )
    require(manifest["speed_of_light_m_per_s"] == 299792458, "speed of light differs")
    require(isinstance(manifest["slices"], list) and manifest["slices"], "slice inventory is empty")
    require(manifest["slice_count"] == len(manifest["slices"]), "slice_count differs")

    previous_time = None
    canonical_ids = None
    for record in manifest["slices"]:
        require(isinstance(record, dict) and set(record) == SLICE_FIELDS, "slice record fields differ")
        time_ns = record["simulation_time_ns"]
        require(isinstance(time_ns, int) and not isinstance(time_ns, bool) and time_ns >= 0, "invalid slice time")
        require(previous_time is None or time_ns > previous_time, "slice times are not strictly increasing")
        previous_time = time_ns
        token = time_token(time_ns)
        require(record["nodes_file"] == f"nodes_{token}s.json", "nodes filename differs")
        require(record["topology_file"] == f"topology_{token}s.json", "topology filename differs")

        nodes_path = verify_file(directory, record["nodes_file"], record["nodes_sha256"])
        topology_path = verify_file(
            directory,
            record["topology_file"],
            record["topology_sha256"],
        )
        nodes = read_json(nodes_path)
        topology = read_json(topology_path)
        require(
            nodes.get("schema_version") == "0.2"
            and topology.get("schema_version") == "0.2",
            "slice schema differs",
        )
        require(
            nodes.get("simulation_time_ns") == time_ns
            and topology.get("simulation_time_ns") == time_ns,
            "slice embedded time differs",
        )
        require(
            nodes.get("state_semantics") == "orbit-policy-evaluation"
            and topology.get("state_semantics") == "orbit-policy-evaluation",
            "slice semantics differ",
        )
        require(nodes.get("node_count") == len(nodes.get("nodes", [])), "node_count differs")
        ids = [node.get("node_id") for node in nodes.get("nodes", [])]
        require(ids == sorted(set(ids)), "satellite IDs are not canonical")
        if canonical_ids is None:
            canonical_ids = ids
        require(ids == canonical_ids, "satellite ID set changes across slices")
        require(
            topology.get("active_link_count") == len(topology.get("links", [])),
            "active_link_count differs",
        )
        require(
            record["active_link_count"] == topology["active_link_count"],
            "manifest active-link count differs",
        )

    return {
        "manifest": manifest_path,
        "run_name": manifest["run_name"],
        "satellite_count": len(canonical_ids),
        "slice_count": manifest["slice_count"],
    }
