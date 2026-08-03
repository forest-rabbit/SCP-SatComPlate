#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly script_dir
repository_root="$(cd "${script_dir}/../../../../.." && pwd)"
readonly repository_root

cd "${repository_root}"
export PYTHONDONTWRITEBYTECODE=1

run_case()
{
  local routing_mode="$1"
  local output_dir="$2"
  ./waf --run-no-build "satcompute \
    --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/fqcodel-bottleneck \
    --simulationDuration=25 \
    --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/fqcodel-bottleneck-transfers.json \
    --diagnosticMode=failure \
    --transferChunkMode=fixed \
    --transferPayloadBytes=1400 \
    --islMtuBytes=1500 \
    --islQueueBytes=1500000 \
    --transferLogMode=silent \
    --routingMode=${routing_mode} \
    --ecmpHashSeed=1 \
    --outputDir=${output_dir}"
}

run_dynamic_route_case()
{
  local output_dir="$1"
  ./waf --run-no-build "satcompute \
    --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/capacity-aware-route-recovery \
    --simulationDuration=45 \
    --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/capacity-aware-route-recovery-transfers.json \
    --diagnosticMode=failure \
    --transferChunkMode=fixed \
    --transferPayloadBytes=64000 \
    --islMtuBytes=65535 \
    --islQueueBytes=4000000 \
    --receiverRcvBufBytes=4000000 \
    --transferLogMode=silent \
    --routingMode=global-capacity-aware-hrw \
    --ecmpHashSeed=1 \
    --outputDir=${output_dir}"
}

run_phase_zero_case()
{
  local transfer_trace="$1"
  local simulation_duration="$2"
  local output_dir="$3"
  ./waf --run-no-build "satcompute \
    --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/capacity-aware-same-epoch \
    --simulationDuration=${simulation_duration} \
    --transferTrace=${transfer_trace} \
    --diagnosticMode=failure \
    --transferChunkMode=fixed \
    --transferPayloadBytes=64000 \
    --islMtuBytes=65535 \
    --islQueueBytes=130000 \
    --receiverRcvBufBytes=4000000 \
    --transferLogMode=silent \
    --routingMode=global-capacity-aware-hrw \
    --ecmpHashSeed=1 \
    --outputDir=${output_dir}"
}

printf '\n[CI:capacity-aware] congested size-aware baseline\n'
run_case global-size-aware-hrw /tmp/satcompute-ci-capacity-baseline

printf '\n[CI:capacity-aware] deterministic capacity-aware repeats\n'
run_case global-capacity-aware-hrw /tmp/satcompute-ci-capacity-a
run_case global-capacity-aware-hrw /tmp/satcompute-ci-capacity-b

printf '\n[CI:capacity-aware] parallel ECMP capacity\n'
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --simulationDuration=2 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/size-aware-static-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=64000 \
  --islMtuBytes=65535 \
  --islQueueBytes=64000000 \
  --transferLogMode=silent \
  --routingMode=global-capacity-aware-hrw \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-capacity-parallel"

printf '\n[CI:capacity-aware] task input/result lifecycle\n'
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --computeProfile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --taskTrace=contrib/satcompute/tests/fixtures/traffic/tasks/task-single-ecmp.json \
  --simulationDuration=3 \
  --taskLogMode=silent \
  --taskCompletionPolicy=report \
  --diagnosticMode=failure \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=silent \
  --routingMode=global-capacity-aware-hrw \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-capacity-task"

printf '\n[CI:capacity-aware] same-edge route epoch\n'
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-hrw-dynamic \
  --simulationDuration=2 \
  --transferTrace=contrib/satcompute/tests/fixtures/traffic/transfers/diamond-4-hrw-dynamic-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=64000 \
  --islMtuBytes=65535 \
  --islQueueBytes=64000000 \
  --transferLogMode=silent \
  --routingMode=global-capacity-aware-hrw \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-capacity-epoch"

printf '\n[CI:capacity-aware] dynamic route invalidation and recovery\n'
run_dynamic_route_case /tmp/satcompute-ci-capacity-dynamic-a
run_dynamic_route_case /tmp/satcompute-ci-capacity-dynamic-b

printf '\n[CI:capacity-aware] sender-finished in-flight invalidation\n'
run_phase_zero_case \
  contrib/satcompute/tests/fixtures/traffic/transfers/capacity-aware-sender-finished-transfers.json \
  10 \
  /tmp/satcompute-ci-capacity-sender-finished

printf '\n[CI:capacity-aware] same-epoch capacity re-admission\n'
run_phase_zero_case \
  contrib/satcompute/tests/fixtures/traffic/transfers/capacity-aware-same-epoch-transfers.json \
  45 \
  /tmp/satcompute-ci-capacity-same-epoch

python3 contrib/satcompute/tools/validation/check-capacity-aware-output.py \
  --baseline=/tmp/satcompute-ci-capacity-baseline \
  --capacity-first=/tmp/satcompute-ci-capacity-a \
  --capacity-second=/tmp/satcompute-ci-capacity-b \
  --parallel-ecmp=/tmp/satcompute-ci-capacity-parallel \
  --task-mode=/tmp/satcompute-ci-capacity-task \
  --same-edge-epoch=/tmp/satcompute-ci-capacity-epoch \
  --dynamic-route-first=/tmp/satcompute-ci-capacity-dynamic-a \
  --dynamic-route-second=/tmp/satcompute-ci-capacity-dynamic-b \
  --sender-finished=/tmp/satcompute-ci-capacity-sender-finished \
  --same-epoch-readmission=/tmp/satcompute-ci-capacity-same-epoch

python3 contrib/satcompute/tools/validation/check-task-output.py stress \
  --topology-dir=contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-static \
  --compute-profile=contrib/satcompute/tests/fixtures/topology/compute-profiles/diamond-4-compute-profile.json \
  --task-trace=contrib/satcompute/tests/fixtures/traffic/tasks/task-single-ecmp.json \
  --output-dir=/tmp/satcompute-ci-capacity-task \
  --minimum-completion-rate-percent=100
