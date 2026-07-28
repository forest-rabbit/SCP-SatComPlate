#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly script_dir
repository_root="$(cd "${script_dir}/../../../.." && pwd)"
readonly repository_root

cd "${repository_root}"
export PYTHONDONTWRITEBYTECODE=1

announce()
{
  printf '\n[CI:task-smoke] %s\n' "$1"
}

announce "single task with Hash and HRW"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --computeProfile=contrib/satcompute/input/topology/resources/test/diamond-4-compute-profile.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-single-ecmp.json \
  --simulationDuration=3 \
  --taskLogMode=silent \
  --diagnosticMode=failure \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-task-single"
test ! -e /tmp/satcompute-ci-task-single/diagnostics/failure
test ! -e /tmp/satcompute-ci-task-single/diagnostics
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --computeProfile=contrib/satcompute/input/topology/resources/test/diamond-4-compute-profile.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-single-ecmp.json \
  --simulationDuration=3 \
  --taskLogMode=silent \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hrw-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-task-single-hrw"

announce "FCFS task scheduling"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --computeProfile=contrib/satcompute/input/topology/resources/test/diamond-4-compute-profile.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-fcfs.json \
  --simulationDuration=4 \
  --taskLogMode=silent \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-task-fcfs"

python3 contrib/satcompute/tools/validation/check-task-output.py smoke \
  --topology-dir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --single-output=/tmp/satcompute-ci-task-single \
  --hrw-single-output=/tmp/satcompute-ci-task-single-hrw \
  --single-profile=contrib/satcompute/input/topology/resources/test/diamond-4-compute-profile.json \
  --single-trace=contrib/satcompute/input/traffic/task/test/task-single-ecmp.json \
  --fcfs-output=/tmp/satcompute-ci-task-fcfs \
  --fcfs-profile=contrib/satcompute/input/topology/resources/test/diamond-4-compute-profile.json \
  --fcfs-trace=contrib/satcompute/input/traffic/task/test/task-fcfs.json

announce "generated 40-task end-to-end fixture"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/examples/xw-66sat-static-2g \
  --computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/stress-generated-40.json \
  --simulationDuration=20 \
  --taskLogMode=silent \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=8000000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-generated-fast-run"
python3 contrib/satcompute/tools/validation/check-task-output.py run \
  --topology-dir=contrib/satcompute/input/topology/examples/xw-66sat-static-2g \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-trace=contrib/satcompute/input/traffic/task/test/stress-generated-40.json \
  --workload-summary=contrib/satcompute/input/traffic/task/test/stress-generated-40-summary.json \
  --output-dir=/tmp/satcompute-ci-generated-fast-run

announce "completed"
