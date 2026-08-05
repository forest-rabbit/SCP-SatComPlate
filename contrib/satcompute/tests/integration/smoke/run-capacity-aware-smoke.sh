#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-capacity-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT

constellation="contrib/satcompute/tests/fixtures/constellation/connected-16.csv"
task_inputs="contrib/satcompute/tests/fixtures/task"

completed="$(./ns3 run --no-build \
  "satcompute --simulationDuration=5 \
--constellationConfig=$constellation --maxIslDistance=6171353 \
--delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-capacity-aware-hrw \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json \
--transferChunkMode=fixed --transferPayloadBytes=1024 \
--outputDir=$smoke_output/run")"
if [[ "$completed" != *'"status":"completed"'* ]]; then
  echo "capacity-aware smoke failed: $completed" >&2
  exit 1
fi

python3 - "$smoke_output/run" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
with (root / "run-summary.json").open(encoding="utf-8") as source:
    run = json.load(source)
with (root / "capacity-aware-summary.json").open(encoding="utf-8") as source:
    capacity = json.load(source)
if run["run_status"] != "COMPLETE":
    raise SystemExit("capacity-aware smoke is not COMPLETE")
if run["routing_mode"] != "global-capacity-aware-hrw":
    raise SystemExit("capacity-aware routing mode differs")
if run["pacing_mode"] != "path-bottleneck-serialization":
    raise SystemExit("capacity-aware pacing mode differs")
if run["transfer"]["completed_transfer_count"] != 2:
    raise SystemExit("capacity-aware smoke did not complete both transfers")
if run["task"]["completed_task_count"] != 1:
    raise SystemExit("capacity-aware smoke did not complete its task")
if any(capacity.values()):
    raise SystemExit(f"capacity-aware state leaked after completion: {capacity}")
PY

echo "SatCompute capacity-aware smoke passed."
