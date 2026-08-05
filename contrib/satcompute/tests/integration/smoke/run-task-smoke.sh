#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-task-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT

constellation="contrib/satcompute/tests/fixtures/constellation/connected-16.csv"
task_inputs="contrib/satcompute/tests/fixtures/task"

completed="$(./ns3 run --no-build \
  "satcompute --simulationDuration=5 \
--constellationConfig=$constellation --maxIslDistance=6171353 \
--delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-size-aware-hrw \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json \
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
