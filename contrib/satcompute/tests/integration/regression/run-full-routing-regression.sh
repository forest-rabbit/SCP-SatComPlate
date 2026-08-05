#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

regression_output="$(mktemp -d /tmp/satcompute-routing-regression.XXXXXX)"
trap 'rm -rf "$regression_output"' EXIT

run_platform() {
  local output_directory="$1"
  shift
  ./ns3 run --no-build "satcompute --outputDir=$output_directory $*"
}

constellation_66="contrib/satcompute/input/topology/constellations/synthetic-66.csv"
constellation_4="contrib/satcompute/tests/fixtures/constellation/diamond-4.csv"
task_inputs="contrib/satcompute/tests/fixtures/task"

common="--simulationDuration=3 --constellationConfig=$constellation_4 \
--maxIslDistance=30000000 --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=1 --islBandwidthBps=100000000"

first_result="$(run_platform "$regression_output/first" \
  "$common --routingMode=global-first")"
hash_result="$(run_platform "$regression_output/hash" \
  "$common --routingMode=global-hash-per-flow \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json")"
hrw_result="$(run_platform "$regression_output/hrw" \
  "$common --routingMode=global-hrw-per-flow \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json")"
size_arguments="$common --routingMode=global-size-aware-hrw \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json"
size_first_result="$(run_platform "$regression_output/size-first" "$size_arguments")"
size_second_result="$(run_platform "$regression_output/size-second" "$size_arguments")"
capacity_result="$(run_platform "$regression_output/capacity" \
  "$common --routingMode=global-capacity-aware-hrw \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json")"
distance_result="$(run_platform "$regression_output/distance" \
  "--simulationDuration=3 --constellationConfig=$constellation_4 \
--maxIslDistance=30000000 --delayMode=distance --fixedDelay=0 \
--networkUpdateInterval=1 --islBandwidthBps=100000000 \
--routingMode=global-first")"
large_result="$(run_platform "$regression_output/online-66" \
  "--simulationDuration=1 --constellationConfig=$constellation_66 \
--routingMode=global-first")"

for result in \
  "$first_result" "$hash_result" "$hrw_result" \
  "$size_first_result" "$size_second_result" "$capacity_result" \
  "$distance_result" "$large_result"; do
  if [[ "$result" != *'"status":"completed"'* ]]; then
    echo "online routing regression failed: $result" >&2
    exit 1
  fi
done

python3 - "$regression_output" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])


def load_json(relative: str):
    with (root / relative).open(encoding="utf-8") as source:
        return json.load(source)


modes = {
    "first": "global-first",
    "hash": "global-hash-per-flow",
    "hrw": "global-hrw-per-flow",
    "size-first": "global-size-aware-hrw",
    "capacity": "global-capacity-aware-hrw",
    "distance": "global-first",
}
for directory, mode in modes.items():
    summary = load_json(f"{directory}/run-summary.json")
    if summary["run_status"] != "COMPLETE":
        raise SystemExit(f"{directory} is not complete")
    if summary["routing_mode"] != mode:
        raise SystemExit(f"{directory} routing mode differs")
    if summary["applied_topology_slice_count"] != 3:
        raise SystemExit(f"{directory} update count differs")

for directory in ("first", "distance"):
    if load_json(f"{directory}/run-summary.json")["route_computation_count"] != 1:
        raise SystemExit(f"{directory} rebuilt routes without an edge-set change")

for directory in ("hash", "hrw", "capacity"):
    if load_json(f"{directory}/run-summary.json")["transfer"][
        "completed_transfer_count"
    ] != 2:
        raise SystemExit(f"{directory} did not complete both transfers")
    if load_json(f"{directory}/run-summary.json")["task"][
        "completed_task_count"
    ] != 1:
        raise SystemExit(f"{directory} did not complete its task")

if load_json("size-first/run-summary.json")["task"]["completed_task_count"] != 1:
    raise SystemExit("size-aware mode did not complete its task")

for filename in (
    "transfer-summary.csv",
    "task-events.csv",
    "task-summary.csv",
    "compute-node-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
):
    first = (root / "size-first" / filename).read_bytes()
    second = (root / "size-second" / filename).read_bytes()
    if first != second:
        raise SystemExit(f"repeated online task output differs: {filename}")

if any(load_json("capacity/capacity-aware-summary.json").values()):
    raise SystemExit("capacity-aware state leaked after completion")

large = load_json("online-66/run-summary.json")
if large["run_status"] != "COMPLETE" or large["applied_topology_slice_count"] != 1:
    raise SystemExit("66-satellite online run differs")
PY

echo "SatCompute full online routing regression passed."
