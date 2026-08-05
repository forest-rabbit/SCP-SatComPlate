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
constellation_2="contrib/satcompute/tests/fixtures/constellation/delay-only-2.csv"
dynamic_topology="contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic"
static_topology="contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static"
capacity_topology="contrib/satcompute/tests/fixtures/topology/snapshots/capacity-pending"
transfer_inputs="contrib/satcompute/tests/fixtures/traffic/transfers"
task_inputs="contrib/satcompute/tests/fixtures/task"

hash_static_common="--simulationDuration=3 --constellationConfig=$constellation_4 \
--topologySource=replay --topologyDir=$static_topology --delayMode=fixed \
--fixedDelay=0.001 --networkUpdateInterval=20 --islBandwidthBps=100000000 \
--routingMode=global-hash-per-flow --ecmpHashSeed=1 \
--transferTrace=$transfer_inputs/diamond-4-static-transfers.json \
--transferChunkMode=fixed --transferPayloadBytes=1024 \
--transferLogMode=silent"
hash_static_first="$(run_platform "$regression_output/hash-static-first" \
  "$hash_static_common --runName=hash-static-first")"
hash_static_second="$(run_platform "$regression_output/hash-static-second" \
  "$hash_static_common --runName=hash-static-second")"
hash_dynamic="$(run_platform "$regression_output/hash-dynamic" \
  "--runName=hash-dynamic --simulationDuration=6 \
--constellationConfig=$constellation_4 --topologySource=replay \
--topologyDir=$dynamic_topology --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-hash-per-flow --ecmpHashSeed=1 \
--transferTrace=$transfer_inputs/diamond-4-dynamic-transfers.json \
--transferChunkMode=fixed --transferPayloadBytes=1024 \
--transferLogMode=silent")"
for result in "$hash_static_first" "$hash_static_second" "$hash_dynamic"; do
  if [[ "$result" != *'"status":"completed"'* ]]; then
    echo "hash routing regression failed: $result" >&2
    exit 1
  fi
done
python3 contrib/satcompute/tools/validation/check-ecmp-output.py \
  --first="$regression_output/hash-static-first" \
  --second="$regression_output/hash-static-second" \
  --dynamic="$regression_output/hash-dynamic"

online_common="--simulationDuration=3 --constellationConfig=$constellation_4 \
--topologySource=online --maxIslDistance=30000000 --delayMode=fixed \
--fixedDelay=0.008 --networkUpdateInterval=1 --islBandwidthBps=100000000"
online_fixed_result="$(run_platform "$regression_output/online-fixed" \
  "$online_common --runName=online-fixed --routingMode=global-first")"
online_distance_result="$(run_platform "$regression_output/online-distance" \
  "--simulationDuration=3 --constellationConfig=$constellation_4 \
--topologySource=online --maxIslDistance=30000000 --delayMode=distance \
--fixedDelay=0 --networkUpdateInterval=1 --islBandwidthBps=100000000 \
--routingMode=global-hrw-per-flow \
--runName=online-distance")"
online_transfer_result="$(run_platform "$regression_output/online-transfer" \
  "$online_common --runName=online-transfer --routingMode=global-hash-per-flow \
--transferTrace=$transfer_inputs/engine-basic.json")"
online_task_arguments="$online_common --runName=online-task \
--routingMode=global-size-aware-hrw \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json"
online_task_first="$(run_platform \
  "$regression_output/online-task-first" "$online_task_arguments")"
online_task_second="$(run_platform \
  "$regression_output/online-task-second" "$online_task_arguments")"
online_capacity_result="$(run_platform \
  "$regression_output/online-capacity" \
  "$online_common --runName=online-capacity \
--routingMode=global-capacity-aware-hrw")"
online_66_result="$(run_platform \
  "$regression_output/online-66" \
  "--runName=online-66 --simulationDuration=1 \
--constellationConfig=$constellation_66 --topologySource=online \
--routingMode=global-first")"

for result in \
  "$online_fixed_result" \
  "$online_distance_result" \
  "$online_transfer_result" \
  "$online_task_first" \
  "$online_task_second" \
  "$online_capacity_result" \
  "$online_66_result"; do
  if [[ "$result" != *'"status":"completed"'* ]]; then
    echo "online routing regression failed: $result" >&2
    exit 1
  fi
done

replay_capacity_result="$(run_platform "$regression_output/replay-capacity" \
  "--runName=replay-capacity --simulationDuration=3 \
--constellationConfig=$constellation_2 --topologySource=replay \
--topologyDir=$capacity_topology --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=1 --islBandwidthBps=1000000 \
--routingMode=global-capacity-aware-hrw --transferPayloadBytes=1400")"
if [[ "$replay_capacity_result" != *'"status":"completed"'* ]]; then
  echo "workload-free replay capacity regression failed: $replay_capacity_result" >&2
  exit 1
fi

python3 - "$regression_output" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])


def load_json(relative: str):
    with (root / relative).open(encoding="utf-8") as source:
        return json.load(source)


online_modes = {
    "online-fixed": "global-first",
    "online-distance": "global-hrw-per-flow",
    "online-transfer": "global-hash-per-flow",
    "online-task-first": "global-size-aware-hrw",
    "online-capacity": "global-capacity-aware-hrw",
}
for directory, mode in online_modes.items():
    summary = load_json(f"{directory}/run-summary.json")
    if summary["run_status"] != "COMPLETE":
        raise SystemExit(f"{directory} is not complete")
    if summary["topology_source"] != "online":
        raise SystemExit(f"{directory} did not use online topology")
    if summary["routing_mode"] != mode:
        raise SystemExit(f"{directory} routing mode differs")
    if summary["applied_topology_slice_count"] != 3:
        raise SystemExit(f"{directory} update count differs")

if load_json("online-fixed/run-summary.json")["route_computation_count"] != 1:
    raise SystemExit("unchanged fixed online topology rebuilt routes")
if load_json("online-distance/run-summary.json")["route_computation_count"] != 1:
    raise SystemExit("distance-only online updates rebuilt routes")
if load_json("online-transfer/run-summary.json")["transfer"][
    "completed_transfer_count"
] != 2:
    raise SystemExit("online transfer workload did not complete")
if load_json("online-task-first/run-summary.json")["task"][
    "completed_task_count"
] != 1:
    raise SystemExit("online task workload did not complete")

for filename in (
    "transfer-summary.csv",
    "task-events.csv",
    "task-summary.csv",
    "compute-node-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
):
    first = (root / "online-task-first" / filename).read_bytes()
    second = (root / "online-task-second" / filename).read_bytes()
    if first != second:
        raise SystemExit(f"repeated online task output differs: {filename}")

large = load_json("online-66/run-summary.json")
if large["run_status"] != "COMPLETE" or large["topology_source"] != "online":
    raise SystemExit("66-satellite online run differs")
if large["applied_topology_slice_count"] != 1:
    raise SystemExit("66-satellite online initial update count differs")

for directory in ("online-capacity", "replay-capacity"):
    capacity = load_json(f"{directory}/capacity-aware-summary.json")
    if any(capacity.values()):
        raise SystemExit(f"{directory} retained capacity state: {capacity}")
PY

echo "SatCompute full routing regression passed."
