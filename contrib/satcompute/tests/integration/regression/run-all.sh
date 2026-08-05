#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

regression_output="$(mktemp -d /tmp/satcompute-regression.XXXXXX)"
trap 'rm -rf "$regression_output"' EXIT

run_platform() {
  local output_directory="$1"
  shift
  ./ns3 run --no-build \
    "satcompute --outputDir=$output_directory $*"
}

constellation_66="contrib/satcompute/input/topology/constellations/synthetic-66.json"
constellation_4="contrib/satcompute/tests/fixtures/constellation/diamond-4.json"
constellation_2="contrib/satcompute/tests/fixtures/constellation/delay-only-2.json"
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
--transferLogMode=silent --topologyExportEnabled=false"
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
--transferLogMode=silent --topologyExportEnabled=false")"
for result in "$hash_static_first" "$hash_static_second" "$hash_dynamic"; do
  if [[ "$result" != *'"status":"completed"'* ]]; then
    echo "legacy routing metric regression failed: $result" >&2
    exit 1
  fi
done
python3 contrib/satcompute/tools/validation/check-ecmp-output.py \
  --first="$regression_output/hash-static-first" \
  --second="$regression_output/hash-static-second" \
  --dynamic="$regression_output/hash-dynamic"

replay_common="--simulationDuration=5 --constellationConfig=$constellation_4 \
--topologySource=replay --topologyDir=$dynamic_topology --delayMode=fixed \
--fixedDelay=0.001 --networkUpdateInterval=2 --islBandwidthBps=100000000 \
--topologyExportEnabled=false"

direct_result="$(run_platform "$regression_output/direct" \
  "$replay_common --runName=direct-replay --routingMode=global-first \
--transferTrace=$transfer_inputs/engine-basic.json")"
if [[ "$direct_result" != *'"status":"completed"'* ]]; then
  echo "direct replay regression failed: $direct_result" >&2
  exit 1
fi

task_arguments="$replay_common --runName=task-replay \
--routingMode=global-size-aware-hrw \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json"
task_first="$(run_platform "$regression_output/task-first" "$task_arguments")"
task_second="$(run_platform "$regression_output/task-second" "$task_arguments")"
if [[ "$task_first" != *'"status":"completed"'* ||
      "$task_second" != *'"status":"completed"'* ]]; then
  echo "task replay repeat regression failed" >&2
  exit 1
fi

no_workload_result="$(run_platform \
  "$regression_output/no-workload" \
  "$replay_common --runName=no-workload --routingMode=global-first")"
if [[ "$no_workload_result" != *'"status":"completed"'* ]]; then
  echo "workload-free replay regression failed: $no_workload_result" >&2
  exit 1
fi

capacity_result="$(run_platform "$regression_output/capacity" \
  "--runName=capacity-pending --simulationDuration=3 \
--constellationConfig=$constellation_2 --topologySource=replay \
--topologyDir=$capacity_topology --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=1 --islBandwidthBps=1000000 \
--routingMode=global-capacity-aware-hrw --transferPayloadBytes=1400 \
--topologyExportEnabled=false")"
if [[ "$capacity_result" != *'"status":"completed"'* ]]; then
  echo "workload-free capacity replay regression failed: $capacity_result" >&2
  exit 1
fi

partial_common="--simulationDuration=1 --constellationConfig=$constellation_4 \
--topologySource=replay --topologyDir=$dynamic_topology --delayMode=fixed \
--fixedDelay=0.001 --networkUpdateInterval=2 --islBandwidthBps=1000000 \
--routingMode=global-first --transferTrace=$transfer_inputs/platform-partial.json \
--transferPayloadBytes=1400 --topologyExportEnabled=false"
set +e
strict_result="$(run_platform "$regression_output/strict" \
  "$partial_common --runName=partial-strict --taskCompletionPolicy=strict")"
strict_status=$?
set -e
if [[ $strict_status -ne 3 || "$strict_result" != *'"status":"partial"'* ]]; then
  echo "strict partial-completion policy regression failed" >&2
  exit 1
fi

report_result="$(run_platform "$regression_output/report" \
  "$partial_common --runName=partial-report --taskCompletionPolicy=report")"
if [[ "$report_result" != *'"status":"partial"'* ]]; then
  echo "report partial-completion policy regression failed: $report_result" >&2
  exit 1
fi

online_common="--simulationDuration=3 --constellationConfig=$constellation_4 \
--topologySource=online --maxIslDistance=30000000 --delayMode=fixed \
--fixedDelay=0.008 --networkUpdateInterval=1 --islBandwidthBps=100000000 \
--topologyExportEnabled=false"
online_fixed_result="$(run_platform "$regression_output/online-fixed" \
  "$online_common --runName=online-fixed --routingMode=global-first")"
online_distance_result="$(run_platform "$regression_output/online-distance" \
  "--simulationDuration=3 --constellationConfig=$constellation_4 \
--topologySource=online --maxIslDistance=30000000 --delayMode=distance \
--fixedDelay=0 --networkUpdateInterval=1 --islBandwidthBps=100000000 \
--routingMode=global-hrw-per-flow --topologyExportEnabled=false \
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
trace_arguments="--runName=online-trace --simulationDuration=2.5 \
--constellationConfig=$constellation_4 --topologySource=online \
--maxIslDistance=30000000 --delayMode=distance --fixedDelay=0 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-first --topologyExportEnabled=true \
--topologyExportInterval=1"
online_trace_result="$(run_platform \
  "$regression_output/online-trace" "$trace_arguments")"
export_trace_result="$(run_platform \
  "$regression_output/export-trace" "$trace_arguments --exportOnly=true")"
for result in \
  "$online_fixed_result" \
  "$online_distance_result" \
  "$online_transfer_result" \
  "$online_task_first" \
  "$online_task_second" \
  "$online_trace_result"; do
  if [[ "$result" != *'"status":"completed"'* ]]; then
    echo "online platform regression failed: $result" >&2
    exit 1
  fi
done
if [[ "$export_trace_result" != *'"status":"exported"'* ]]; then
  echo "export-only topology trace regression failed: $export_trace_result" >&2
  exit 1
fi

generated_replay_result="$(run_platform \
  "$regression_output/generated-replay" \
  "--runName=generated-replay --simulationDuration=2.5 \
--constellationConfig=$constellation_4 --topologySource=replay \
--topologyDir=$regression_output/export-trace/topology-trace \
--delayMode=distance --fixedDelay=0 --networkUpdateInterval=2 \
--islBandwidthBps=100000000 --routingMode=global-first \
--topologyExportEnabled=false")"
if [[ "$generated_replay_result" != *'"status":"completed"'* ]]; then
  echo "generated topology replay regression failed: $generated_replay_result" >&2
  exit 1
fi

online_capacity_result="$(run_platform \
  "$regression_output/online-capacity" \
  "$online_common --runName=online-capacity \
--routingMode=global-capacity-aware-hrw")"
online_66_result="$(run_platform \
  "$regression_output/online-66" \
  "--runName=online-66 --simulationDuration=1 \
--constellationConfig=$constellation_66 --topologySource=online \
--routingMode=global-first --topologyExportEnabled=false")"
if [[ "$online_capacity_result" != *'"status":"completed"'* ||
      "$online_66_result" != *'"status":"completed"'* ]]; then
  echo "capacity-aware or 66-satellite online regression failed" >&2
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


direct = load_json("direct/run-summary.json")
if direct["run_status"] != "COMPLETE":
    raise SystemExit("direct run is not complete")
if direct["transfer"]["completed_transfer_count"] != 2:
    raise SystemExit("direct run transfer count differs")
if direct["transfer"]["received_application_bytes"] != 3074:
    raise SystemExit("direct run received-byte total differs")

for filename in (
    "transfer-summary.csv",
    "udp-socket-drops.csv",
    "task-events.csv",
    "task-summary.csv",
    "compute-node-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
    "routing-reservation-events.csv",
    "routing-summary.json",
):
    first = (root / "task-first" / filename).read_bytes()
    second = (root / "task-second" / filename).read_bytes()
    if first != second:
        raise SystemExit(f"repeated task output differs: {filename}")

no_workload = load_json("no-workload/run-summary.json")
if no_workload["workload_mode"] != "none" or no_workload["run_status"] != "COMPLETE":
    raise SystemExit("workload-free run summary differs")

capacity = load_json("capacity/capacity-aware-summary.json")
if capacity["active_path_count_at_end"] != 0:
    raise SystemExit("workload-free capacity state is not empty")
capacity_size = load_json("capacity/size-aware-summary.json")
if capacity_size["active_flow_count_at_end"] != 0:
    raise SystemExit("workload-free capacity flow registry is not empty")

strict = load_json("strict/run-summary.json")
report = load_json("report/run-summary.json")
if strict["run_status"] != "PARTIAL" or report["run_status"] != "PARTIAL":
    raise SystemExit("partial completion status differs")
if strict["task_completion_policy"] != "strict":
    raise SystemExit("strict completion policy was not recorded")
if report["task_completion_policy"] != "report":
    raise SystemExit("report completion policy was not recorded")
for directory in ("strict", "report"):
    diagnostic = load_json(f"{directory}/diagnostics/diagnostic-summary.json")
    if diagnostic["incomplete_transfer_count"] != 1:
        raise SystemExit(f"{directory} incomplete transfer count differs")

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
    "udp-socket-drops.csv",
    "task-events.csv",
    "task-summary.csv",
    "compute-node-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
    "routing-reservation-events.csv",
    "routing-summary.json",
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

online_trace = load_json("online-trace/run-summary.json")
trace_manifest = load_json("online-trace/topology-trace/manifest.json")
export_manifest = load_json("export-trace/topology-trace/manifest.json")
if online_trace["applied_topology_slice_count"] != 2:
    raise SystemExit("trace scenario network update count differs")
if trace_manifest["slice_count"] != 4:
    raise SystemExit("independent one-second trace slice count differs")
if trace_manifest["trace_interval_ns"] != 1_000_000_000:
    raise SystemExit("trace output interval differs")
if trace_manifest["network_update_interval_ns"] != 2_000_000_000:
    raise SystemExit("trace network interval differs")
if trace_manifest != export_manifest:
    raise SystemExit("online and export-only manifests differ")

generated_replay = load_json("generated-replay/run-summary.json")
if generated_replay["topology_source"] != "replay":
    raise SystemExit("generated trace was not consumed through replay")
if generated_replay["applied_topology_slice_count"] != 2:
    raise SystemExit("generated trace replay update count differs")
if generated_replay["route_computation_count"] != 1:
    raise SystemExit("generated delay-only trace replay rebuilt routes")

online_trace_root = root / "online-trace/topology-trace"
export_trace_root = root / "export-trace/topology-trace"
online_files = sorted(path.name for path in online_trace_root.iterdir())
export_files = sorted(path.name for path in export_trace_root.iterdir())
if online_files != export_files:
    raise SystemExit("online and export-only trace inventories differ")
for filename in online_files:
    if (online_trace_root / filename).read_bytes() != (
        export_trace_root / filename
    ).read_bytes():
        raise SystemExit(f"online and export-only trace differs: {filename}")
PY

echo "SatCompute replay, online, and topology-trace regressions passed."
