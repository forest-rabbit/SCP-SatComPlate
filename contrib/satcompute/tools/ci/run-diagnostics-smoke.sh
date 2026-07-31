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
  printf '\n[CI:diagnostics-smoke] %s\n' "$1"
}

announce "transfer-only FqCoDel DropReason"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/fqcodel-bottleneck \
  --simulationDuration=3 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/fqcodel-bottleneck-transfers.json \
  --diagnosticMode=failure \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1400 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-fqcodel"
test -f /tmp/satcompute-ci-fqcodel/diagnostics/failure/flow-drop-reasons.csv
test ! -e /tmp/satcompute-ci-fqcodel/flow-drop-reasons.csv
python3 contrib/satcompute/tools/validation/check-flow-drop-reasons.py \
  --output-dir=/tmp/satcompute-ci-fqcodel \
  --require-reason=QUEUE_DISC \
  --forbid-reason=QUEUE \
  --require-zero-unattributed \
  --expected-explicit-drop-packets=6262

announce "strict device-queue failure"
set +e
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-single-ecmp.json \
  --simulationDuration=2 \
  --taskLogMode=silent \
  --taskCompletionPolicy=strict \
  --diagnosticMode=failure \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=1000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-task-strict"
strict_status=$?
set -e
test "${strict_status}" -eq 1
python3 contrib/satcompute/tools/validation/check-task-output.py failure \
  --topology-dir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --compute-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --task-trace=contrib/satcompute/input/traffic/task/test/task-single-ecmp.json \
  --output-dir=/tmp/satcompute-ci-task-strict \
  --require-queue-drop
python3 contrib/satcompute/tools/validation/check-flow-drop-reasons.py \
  --output-dir=/tmp/satcompute-ci-task-strict \
  --require-reason=QUEUE \
  --forbid-reason=QUEUE_DISC \
  --require-zero-unattributed \
  --expected-explicit-drop-packets=1

announce "strict UDP socket-buffer failure"
set +e
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-single-ecmp.json \
  --simulationDuration=2 \
  --taskLogMode=silent \
  --taskCompletionPolicy=strict \
  --diagnosticMode=failure \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=1500000 \
  --receiverRcvBufBytes=1000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-task-udp-drop"
udp_status=$?
set -e
test "${udp_status}" -eq 1
python3 contrib/satcompute/tools/validation/check-task-output.py failure \
  --topology-dir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --compute-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --task-trace=contrib/satcompute/input/traffic/task/test/task-single-ecmp.json \
  --output-dir=/tmp/satcompute-ci-task-udp-drop \
  --require-udp-socket-drop

announce "diagnostic cleanup contract"
test -f /tmp/satcompute-ci-task-strict/diagnostics/failure/diagnostic-summary.json
test ! -e /tmp/satcompute-ci-task-strict/diagnostic-summary.json
touch /tmp/satcompute-ci-task-strict/diagnostics/failure/user-note.txt
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --simulationDuration=0.1 \
  --diagnosticMode=off \
  --outputDir=/tmp/satcompute-ci-task-strict"
test -f /tmp/satcompute-ci-task-strict/diagnostics/failure/user-note.txt
for filename in incomplete-tasks.csv incomplete-transfers.csv \
  isl-queue-drops.csv isl-queue-drop-summary.csv \
  udp-socket-drops.csv udp-socket-drop-summary.csv \
  flow-link-concentration.csv flow-drop-reasons.csv \
  diagnostic-summary.json; do
  test ! -e "/tmp/satcompute-ci-task-strict/${filename}"
  test ! -e \
    "/tmp/satcompute-ci-task-strict/diagnostics/failure/${filename}"
done
rm /tmp/satcompute-ci-task-strict/diagnostics/failure/user-note.txt
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --simulationDuration=0.1 \
  --diagnosticMode=off \
  --outputDir=/tmp/satcompute-ci-task-strict"
test ! -e /tmp/satcompute-ci-task-strict/diagnostics/failure
test ! -e /tmp/satcompute-ci-task-strict/diagnostics

announce "report-mode partial task contract"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-fcfs.json \
  --simulationDuration=1 \
  --taskLogMode=silent \
  --taskCompletionPolicy=report \
  --diagnosticMode=failure \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-task-report"
python3 contrib/satcompute/tools/validation/check-task-output.py stress \
  --topology-dir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --compute-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --task-trace=contrib/satcompute/input/traffic/task/test/task-fcfs.json \
  --output-dir=/tmp/satcompute-ci-task-report \
  --minimum-completion-rate-percent=0

announce "completed"
