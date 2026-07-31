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
  printf '\n[CI:full-routing] %s\n' "$1"
}

for required_output in \
  /tmp/satcompute-ci-static-a \
  /tmp/satcompute-ci-static-b \
  /tmp/satcompute-ci-dynamic; do
  if [[ ! -f "${required_output}/run-summary.json" ]]; then
    echo "missing routing-smoke prerequisite: ${required_output}" >&2
    echo "run contrib/satcompute/tests/integration/smoke/run-routing-smoke.sh first" >&2
    exit 1
  fi
done

announce "canonical endpoint ordering and remainder"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/canonical-order-a \
  --simulationDuration=1 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/canonical-order-a.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-canonical-a"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/canonical-order-b \
  --simulationDuration=1 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/canonical-order-b.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-canonical-order-b"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/canonical-endpoint-reversed \
  --simulationDuration=1 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/canonical-order-a.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-canonical-reversed"

announce "varied 5000-transfer workload"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/examples/xw-66sat \
  --simulationDuration=8 \
  --transferTrace=contrib/satcompute/input/traffic/workload/workload-5000-varied.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=4096 \
  --islMtuBytes=9000 \
  --islQueueBytes=1500000 \
  --transferLogMode=summary \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-scale" \
  | tee /tmp/satcompute-ci-scale.log

announce "mixed large workload"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --simulationDuration=45 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/mixed-large-ci.json \
  --transferChunkMode=size-aware \
  --islMtuBytes=65535 \
  --islQueueBytes=1500000 \
  --transferLogMode=summary \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-mixed-large"

announce "extended deterministic routing checks"
python3 contrib/satcompute/tools/validation/check-ecmp-output.py \
  --first=/tmp/satcompute-ci-static-a \
  --second=/tmp/satcompute-ci-static-b \
  --dynamic=/tmp/satcompute-ci-dynamic \
  --canonical-first=/tmp/satcompute-ci-canonical-a \
  --canonical-second=/tmp/satcompute-ci-canonical-reversed \
  --remainder=/tmp/satcompute-ci-canonical-a \
  --scale=/tmp/satcompute-ci-scale \
  --scale-input=contrib/satcompute/input/traffic/workload/workload-5000-varied.json \
  --scale-log=/tmp/satcompute-ci-scale.log \
  --large=/tmp/satcompute-ci-mixed-large \
  --large-input=contrib/satcompute/tests/fixtures/traffic/transfers/mixed-large-ci.json
python3 contrib/satcompute/tools/validation/check-ecmp-output.py \
  --canonical-first=/tmp/satcompute-ci-canonical-a \
  --canonical-second=/tmp/satcompute-ci-canonical-order-b

announce "completed"
