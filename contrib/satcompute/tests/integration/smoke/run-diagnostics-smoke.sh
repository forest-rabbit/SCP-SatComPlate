#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-diagnostics-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT

constellation="contrib/satcompute/tests/fixtures/constellation/diamond-4.json"
topology="contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic"
task_inputs="contrib/satcompute/tests/fixtures/task"

set +e
result="$(./ns3 run --no-build \
  "satcompute --runName=smoke-task-failure --simulationDuration=1 \
--constellationConfig=$constellation --topologySource=replay \
--topologyDir=$topology --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 --islQueueBytes=1 \
--routingMode=global-first \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json --transferPayloadBytes=1024 \
--diagnosticMode=failure --taskCompletionPolicy=strict \
--topologyExportEnabled=false --outputDir=$smoke_output/run")"
status=$?
set -e
if [[ $status -ne 3 || "$result" != *'"status":"partial"'* ]]; then
  echo "diagnostics smoke did not produce the expected strict partial result" >&2
  exit 1
fi

python3 contrib/satcompute/tools/validation/check-task-output.py failure \
  --topology-dir="$topology" \
  --compute-profile="$repository_root/$task_inputs/compute-profile-single.json" \
  --task-trace="$repository_root/$task_inputs/task-single.json" \
  --output-dir="$smoke_output/run" \
  --require-queue-drop
python3 contrib/satcompute/tools/validation/check-flow-drop-reasons.py \
  --output-dir="$smoke_output/run" \
  --require-reason=QUEUE --require-zero-unattributed

echo "SatCompute diagnostics smoke passed."
