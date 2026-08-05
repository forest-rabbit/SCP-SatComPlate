#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT
constellation_66="contrib/satcompute/input/topology/constellations/synthetic-66.json"
constellation_4="contrib/satcompute/tests/fixtures/constellation/diamond-4.json"
dynamic_topology="contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic"
task_inputs="contrib/satcompute/tests/fixtures/task"
validation_output="$smoke_output/validation"

validated="$(./ns3 run --no-build \
  "satcompute --constellationConfig=$constellation_66 \
--outputDir=$validation_output --validateOnly=true")"
if [[ "$validated" != *'"status":"validated"'* ]]; then
  echo "para/constellation validation smoke failed: $validated" >&2
  exit 1
fi

python3 -m json.tool "$validation_output/effective-config.json" >/dev/null

execution_output="$smoke_output/execution"
completed="$(./ns3 run --no-build \
  "satcompute --runName=smoke-task-replay --simulationDuration=5 \
--constellationConfig=$constellation_4 --topologySource=replay \
--topologyDir=$dynamic_topology --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-size-aware-hrw \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json --topologyExportEnabled=false \
--outputDir=$execution_output")"
if [[ "$completed" != *'"status":"completed"'* ]]; then
  echo "replay execution smoke failed: $completed" >&2
  exit 1
fi

python3 -m json.tool "$execution_output/effective-config.json" >/dev/null
python3 -m json.tool "$execution_output/run-summary.json" >/dev/null
python3 - "$execution_output/run-summary.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    summary = json.load(source)
if summary["run_status"] != "COMPLETE":
    raise SystemExit("replay smoke run summary is not COMPLETE")
if summary["task"]["completed_task_count"] != 1:
    raise SystemExit("replay smoke did not complete its task")
if summary["transfer"]["completed_transfer_count"] != 2:
    raise SystemExit("replay smoke did not complete both task transfers")
PY

online_output="$smoke_output/online"
online_completed="$(./ns3 run --no-build \
  "satcompute --runName=smoke-online-fixed --simulationDuration=3 \
--constellationConfig=$constellation_4 --topologySource=online \
--maxIslDistance=30000000 --delayMode=fixed --fixedDelay=0.008 \
--networkUpdateInterval=1 --islBandwidthBps=100000000 \
--routingMode=global-first --topologyExportEnabled=false \
--outputDir=$online_output")"
if [[ "$online_completed" != *'"status":"completed"'* ]]; then
  echo "online execution smoke failed: $online_completed" >&2
  exit 1
fi

python3 - "$online_output/run-summary.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    summary = json.load(source)
if summary["run_status"] != "COMPLETE":
    raise SystemExit("online smoke run summary is not COMPLETE")
if summary["topology_source"] != "online":
    raise SystemExit("online smoke topology source differs")
if summary["applied_topology_slice_count"] != 3:
    raise SystemExit("online smoke update count differs")
PY

trace_output="$smoke_output/trace-export"
exported="$(./ns3 run --no-build \
  "satcompute --runName=smoke-trace --simulationDuration=2.5 \
--constellationConfig=$constellation_4 --topologySource=online \
--maxIslDistance=30000000 --delayMode=distance --fixedDelay=0 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-first --topologyExportEnabled=true \
--topologyExportInterval=1 --outputDir=$trace_output --exportOnly=true")"
if [[ "$exported" != *'"status":"exported"'* ]]; then
  echo "topology trace export smoke failed: $exported" >&2
  exit 1
fi

python3 - "$trace_output/topology-trace/manifest.json" <<'PY'
import hashlib
import json
import pathlib
import sys

manifest_path = pathlib.Path(sys.argv[1])
with manifest_path.open(encoding="utf-8") as source:
    manifest = json.load(source)
if manifest["state_semantics"] != "orbit-policy-evaluation":
    raise SystemExit("trace smoke state semantics differ")
if manifest["slice_count"] != 4:
    raise SystemExit("trace smoke slice count differs")
for slice_record in manifest["slices"]:
    for file_key, hash_key in (
        ("nodes_file", "nodes_sha256"),
        ("topology_file", "topology_sha256"),
    ):
        payload = (manifest_path.parent / slice_record[file_key]).read_bytes()
        if hashlib.sha256(payload).hexdigest() != slice_record[hash_key]:
            raise SystemExit(f"trace smoke hash differs: {slice_record[file_key]}")
PY

echo "SatCompute para validation, replay, online, and trace-export smoke passed."
