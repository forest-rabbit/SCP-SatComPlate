#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-diagnostics-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT

constellation="contrib/satcompute/tests/fixtures/constellation/connected-16.csv"
task_inputs="contrib/satcompute/tests/fixtures/task"

set +e
result="$(./ns3 run --no-build \
"satcompute --faultMode=none --linkMetrics=0 --simulationDuration=1 \
--constellationConfig=$constellation --maxIslDistance=6171353 \
--delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 --islQueueBytes=1 \
--routingMode=global-first \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json --transferPayloadBytes=1024 \
--diagnosticMode=failure --taskCompletionPolicy=strict \
--outputDir=$smoke_output/run")"
status=$?
set -e
if [[ $status -ne 3 || "$result" != *'"status":"partial"'* ]]; then
  echo "diagnostics smoke did not produce the expected strict partial result" >&2
  exit 1
fi

python3 - "$smoke_output/run" <<'PY'
import csv
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
with (root / "run-summary.json").open(encoding="utf-8") as source:
    run = json.load(source)
if run["run_status"] != "PARTIAL" or not run["diagnostics_generated"]:
    raise SystemExit("diagnostic run summary differs")
failure = root / "diagnostics" / "failure"
for name in (
    "incomplete-tasks.csv",
    "incomplete-transfers.csv",
    "isl-queue-drops.csv",
    "diagnostic-summary.json",
    "flow-drop-reasons.csv",
):
    if not (failure / name).is_file():
        raise SystemExit(f"missing diagnostic file: {name}")
with (failure / "isl-queue-drops.csv").open(newline="", encoding="utf-8") as source:
    if not list(csv.DictReader(source)):
        raise SystemExit("diagnostic run recorded no ISL queue drop")
PY
python3 contrib/satcompute/tools/validation/check-flow-drop-reasons.py \
  --output-dir="$smoke_output/run" \
  --require-reason=QUEUE --require-zero-unattributed

echo "SatCompute diagnostics smoke passed."
