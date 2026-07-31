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
  printf '\n[CI:full-workload] %s\n' "$1"
}

for required_output in \
  /tmp/satcompute-ci-task-single \
  /tmp/satcompute-ci-task-single-hrw \
  /tmp/satcompute-ci-task-fcfs; do
  if [[ ! -f "${required_output}/run-summary.json" ]]; then
    echo "missing task-smoke prerequisite: ${required_output}" >&2
    echo "run contrib/satcompute/tools/ci/run-task-smoke.sh first" >&2
    exit 1
  fi
done

announce "heterogeneous compute"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/heterogeneous-compute-profile.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-heterogeneous.json \
  --simulationDuration=4 \
  --taskLogMode=silent \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-task-heterogeneous"

announce "TaskTrace ordering determinism"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/compute-profile-order-a.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-order-a.json \
  --simulationDuration=3 \
  --taskLogMode=silent \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-task-order-a"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/compute-profile-order-a.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-order-b.json \
  --simulationDuration=3 \
  --taskLogMode=silent \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-task-order-b"

announce "ComputeProfile ordering determinism"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/compute-profile-order-a.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-order-a.json \
  --simulationDuration=3 \
  --taskLogMode=silent \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-profile-order-a"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/compute-profile-order-b.json \
  --taskTrace=contrib/satcompute/input/traffic/task/test/task-order-a.json \
  --simulationDuration=3 \
  --taskLogMode=silent \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-profile-order-b"

announce "extended deterministic task checks"
python3 contrib/satcompute/tools/validation/check-task-output.py \
  --topology-dir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --single-output=/tmp/satcompute-ci-task-single \
  --hrw-single-output=/tmp/satcompute-ci-task-single-hrw \
  --single-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --single-trace=contrib/satcompute/input/traffic/task/test/task-single-ecmp.json \
  --fcfs-output=/tmp/satcompute-ci-task-fcfs \
  --fcfs-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --fcfs-trace=contrib/satcompute/input/traffic/task/test/task-fcfs.json \
  --heterogeneous-output=/tmp/satcompute-ci-task-heterogeneous \
  --heterogeneous-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/heterogeneous-compute-profile.json \
  --heterogeneous-trace=contrib/satcompute/input/traffic/task/test/task-heterogeneous.json \
  --task-order-first-output=/tmp/satcompute-ci-task-order-a \
  --task-order-second-output=/tmp/satcompute-ci-task-order-b \
  --task-order-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/compute-profile-order-a.json \
  --task-order-first-trace=contrib/satcompute/input/traffic/task/test/task-order-a.json \
  --task-order-second-trace=contrib/satcompute/input/traffic/task/test/task-order-b.json \
  --profile-order-first-output=/tmp/satcompute-ci-profile-order-a \
  --profile-order-second-output=/tmp/satcompute-ci-profile-order-b \
  --profile-order-first-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/compute-profile-order-a.json \
  --profile-order-second-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/compute-profile-order-b.json \
  --profile-order-trace=contrib/satcompute/input/traffic/task/test/task-order-a.json

announce "deterministic generator and seed variation"
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --nodes-file=contrib/satcompute/input/topology/examples/xw-66sat-static-2g/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-count=40 \
  --total-input-bytes=4000000 \
  --seed=n1-ci \
  --rules-version=n1.6-v1 \
  --arrival-start-ns=1000000000 \
  --arrival-end-ns=10000000000 \
  --arrival-mode=uniform \
  --large-1gb-count=20 \
  --large-500mb-count=40 \
  --scenario-scale-bp=0 \
  --non-tail-min-input-bytes=4096 \
  --non-tail-max-input-bytes=500000 \
  --output-task-trace=/tmp/satcompute-ci-generated-a.json \
  --output-workload-summary=/tmp/satcompute-ci-generated-a-summary.json
cmp contrib/satcompute/input/traffic/task/test/stress-generated-40.json \
  /tmp/satcompute-ci-generated-a.json
cmp contrib/satcompute/input/traffic/task/test/stress-generated-40-summary.json \
  /tmp/satcompute-ci-generated-a-summary.json
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --nodes-file=contrib/satcompute/input/topology/examples/xw-66sat-static-2g/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-count=40 \
  --total-input-bytes=4000000 \
  --seed=n1-ci-different \
  --rules-version=n1.6-v1 \
  --arrival-start-ns=1000000000 \
  --arrival-end-ns=10000000000 \
  --arrival-mode=uniform \
  --large-1gb-count=20 \
  --large-500mb-count=40 \
  --scenario-scale-bp=0 \
  --non-tail-min-input-bytes=4096 \
  --non-tail-max-input-bytes=500000 \
  --output-task-trace=/tmp/satcompute-ci-generated-different.json \
  --output-workload-summary=/tmp/satcompute-ci-generated-different-summary.json
if cmp -s /tmp/satcompute-ci-generated-a.json \
  /tmp/satcompute-ci-generated-different.json; then
  echo "different seed produced an identical TaskTrace"
  exit 1
fi
python3 contrib/satcompute/tools/generation/generate-task-workload.py \
  --nodes-file=contrib/satcompute/input/topology/examples/xw-66sat-static-2g/nodes_0s.json \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-count=40 \
  --total-input-bytes=2004000000 \
  --seed=n1-ci-tail \
  --rules-version=n1.6-v1 \
  --arrival-start-ns=1000000000 \
  --arrival-end-ns=10000000000 \
  --arrival-mode=uniform \
  --large-1gb-count=20 \
  --large-500mb-count=40 \
  --scenario-scale-bp=500 \
  --non-tail-min-input-bytes=4096 \
  --non-tail-max-input-bytes=500000 \
  --output-task-trace=/tmp/satcompute-ci-generated-tail.json \
  --output-workload-summary=/tmp/satcompute-ci-generated-tail-summary.json

announce "preflight success, failure, and warning boundaries"
python3 contrib/satcompute/tools/validation/preflight-task-workload.py \
  --topology-dir=contrib/satcompute/input/topology/examples/xw-66sat-static-2g \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-trace=/tmp/satcompute-ci-generated-a.json \
  --workload-summary=/tmp/satcompute-ci-generated-a-summary.json \
  --simulation-duration-s=20 \
  --chunk-mode=size-aware \
  --isl-mtu-bytes=65535 \
  --isl-queue-bytes=8000000 \
  --output-report=/tmp/satcompute-ci-generated-preflight.json
python3 contrib/satcompute/tools/validation/preflight-task-workload.py \
  --topology-dir=contrib/satcompute/input/topology/examples/xw-66sat-static-2g \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-trace=/tmp/satcompute-ci-generated-tail.json \
  --workload-summary=/tmp/satcompute-ci-generated-tail-summary.json \
  --simulation-duration-s=20 \
  --chunk-mode=size-aware \
  --isl-mtu-bytes=65535 \
  --isl-queue-bytes=8000000
if python3 contrib/satcompute/tools/validation/preflight-task-workload.py \
  --topology-dir=contrib/satcompute/input/topology/examples/xw-66sat-static-2g \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-trace=/tmp/satcompute-ci-generated-a.json \
  --workload-summary=/tmp/satcompute-ci-generated-a-summary.json \
  --simulation-duration-s=20 \
  --chunk-mode=size-aware \
  --isl-mtu-bytes=65535 \
  --isl-queue-bytes=8000000 \
  --max-total-input-bytes=3999999; then
  echo "preflight accepted an over-budget workload"
  exit 1
fi
python3 contrib/satcompute/tools/validation/preflight-task-workload.py \
  --topology-dir=contrib/satcompute/input/topology/examples/xw-66sat-static-2g \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-trace=/tmp/satcompute-ci-generated-a.json \
  --workload-summary=/tmp/satcompute-ci-generated-a-summary.json \
  --simulation-duration-s=20 \
  --chunk-mode=size-aware \
  --isl-mtu-bytes=65535 \
  --isl-queue-bytes=64000000 \
  | tee /tmp/satcompute-ci-generated-warning.log
grep -F "WARNING: aggregate theoretical queue capacity is large" \
  /tmp/satcompute-ci-generated-warning.log

announce "generated output end-to-end"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/examples/xw-66sat-static-2g \
  --computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --taskTrace=/tmp/satcompute-ci-generated-a.json \
  --simulationDuration=20 \
  --taskLogMode=silent \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=8000000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-generated-run"
python3 contrib/satcompute/tools/validation/check-task-output.py run \
  --topology-dir=contrib/satcompute/input/topology/examples/xw-66sat-static-2g \
  --compute-profile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-compute-profile.json \
  --task-trace=/tmp/satcompute-ci-generated-a.json \
  --workload-summary=/tmp/satcompute-ci-generated-a-summary.json \
  --output-dir=/tmp/satcompute-ci-generated-run

announce "completed"
