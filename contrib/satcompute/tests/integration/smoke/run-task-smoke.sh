#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-task-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT

constellation="contrib/satcompute/tests/fixtures/constellation/diamond-4.csv"
topology="contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic"
task_inputs="contrib/satcompute/tests/fixtures/task"

completed="$(./ns3 run --no-build \
  "satcompute --runName=smoke-task-replay --simulationDuration=5 \
--constellationConfig=$constellation --topologySource=replay \
--topologyDir=$topology --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-size-aware-hrw \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json --topologyExportEnabled=false \
--outputDir=$smoke_output/run")"
if [[ "$completed" != *'"status":"completed"'* ]]; then
  echo "task smoke failed: $completed" >&2
  exit 1
fi

python3 - "$smoke_output/run/run-summary.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    summary = json.load(source)
if summary["run_status"] != "COMPLETE":
    raise SystemExit("task smoke run is not COMPLETE")
if summary["task"]["completed_task_count"] != 1:
    raise SystemExit("task smoke did not complete its task")
if summary["transfer"]["completed_transfer_count"] != 2:
    raise SystemExit("task smoke did not complete both task transfers")
PY

echo "SatCompute task smoke passed."
