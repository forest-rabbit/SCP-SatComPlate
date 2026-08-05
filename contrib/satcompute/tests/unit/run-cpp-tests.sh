#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$repository_root"

test_output="$(mktemp -d /tmp/satcompute-cpp-tests.XXXXXX)"
trap 'rm -rf "$test_output"' EXIT

fixed="contrib/satcompute/input/examples/synthetic-66-fixed.json"
distance="contrib/satcompute/input/examples/synthetic-66-distance.json"
task="contrib/satcompute/tests/fixtures/scenario/task-input.json"
invalid="contrib/satcompute/tests/fixtures/scenario/invalid-unknown-field.json"

./ns3 run --no-build "satcompute-para-test"
./ns3 run --no-build \
  "satcompute-para-test --verifyOverrides=transfer --runName=cli-run \
--simulationStart=5 --simulationDuration=15 --constellationConfig=constellation.json \
--topologySource=replay --topologyDir=slices --islCandidateStrategy=plus-grid \
--seamEnabled=1 --maxIslDistance=7000000 --delayMode=distance --fixedDelay=0 \
--networkUpdateInterval=2 --islBandwidthBps=1000 --islMtuBytes=65000 \
--islQueueBytes=2000 --receiverRcvBufBytes=3000 --routingMode=global-hrw-per-flow \
--routingRecomputePolicy=on-topology-change --ecmpHashSeed=8 \
--transferTrace=transfers.json --transferChunkMode=size-aware --transferPayloadBytes=2048 \
--taskCompletionPolicy=report --topologyExportEnabled=0 --topologyExportInterval=2 \
--includeFinalTopologyState=0 --outputDir=/tmp/cli-output --transferLogMode=verbose \
--taskLogMode=silent --diagnosticMode=failure --randomSeed=9 --randomRun=10 \
--randomStreamStart=11"
./ns3 run --no-build \
  "satcompute-para-test --verifyOverrides=task --computeProfile=compute.json \
--taskTrace=tasks.json"

./ns3 run --no-build \
  "satcompute-config-test --fixedScenario=$fixed \
--distanceScenario=$distance --taskScenario=$task \
--invalidScenario=$invalid --outputDir=$test_output"

replay_scenario="contrib/satcompute/tests/fixtures/scenario/replay-dynamic.json"
./ns3 run --no-build \
  "satcompute-snapshot-test --scenario=$replay_scenario --outputDir=$test_output/snapshot"

./ns3 run --no-build "satcompute-link-state-test"

replay_fixed="contrib/satcompute/tests/fixtures/scenario/replay-delay-fixed.json"
replay_distance="contrib/satcompute/tests/fixtures/scenario/replay-delay-distance.json"
replay_dynamic="contrib/satcompute/tests/fixtures/scenario/replay-dynamic.json"
./ns3 run --no-build \
  "satcompute-replay-controller-test --fixedScenario=$replay_fixed \
--distanceScenario=$replay_distance --dynamicScenario=$replay_dynamic"

./ns3 run --no-build \
  "satcompute-routing-compatibility-test --scenario=$replay_dynamic"

./ns3 run --no-build \
  "satcompute-size-aware-routing-test --scenario=$replay_dynamic"

./ns3 run --no-build \
  "satcompute-capacity-aware-routing-test --scenario=$replay_dynamic"

transfer_fixtures="contrib/satcompute/tests/fixtures/traffic/transfers"
./ns3 run --no-build \
  "satcompute-transfer-trace-test \
--canonicalA=$transfer_fixtures/canonical-order-a.json \
--canonicalB=$transfer_fixtures/canonical-order-b.json \
--invalidUnknownField=$transfer_fixtures/invalid-unknown-field.json \
--invalidDuplicateId=$transfer_fixtures/invalid-duplicate-id.json \
--invalidUnknownSatellite=$transfer_fixtures/invalid-unknown-satellite.json \
--invalidStopTime=$transfer_fixtures/invalid-stop-time.json"

./ns3 run --no-build \
  "satcompute-network-transfer-engine-test --scenario=$replay_dynamic \
--capacityScenario=contrib/satcompute/tests/fixtures/scenario/capacity-pending.json \
--basicTransfers=$transfer_fixtures/engine-basic.json \
--capacityTransfers=$transfer_fixtures/capacity-pending.json"

task_fixtures="contrib/satcompute/tests/fixtures/task"
./ns3 run --no-build \
  "satcompute-task-input-test --fixtureRoot=$task_fixtures"

./ns3 run --no-build "satcompute-compute-service-test"

./ns3 run --no-build \
  "satcompute-task-coordinator-test --scenario=$replay_dynamic \
--fixtureRoot=$task_fixtures"

./ns3 run --no-build \
  "satcompute-run-output-writer-test \
--taskScenario=contrib/satcompute/tests/fixtures/scenario/task-replay.json \
--transferScenario=contrib/satcompute/tests/fixtures/scenario/transfer-replay.json \
--outputDir=$test_output/run-output"

./ns3 run --no-build "satcompute-online-orbit-foundation-test"

./ns3 run --no-build "satcompute-online-topology-controller-test"

./ns3 run --no-build \
  "satcompute-topology-trace-exporter-test \
--scenario=contrib/satcompute/tests/fixtures/scenario/online-trace.json \
--outputDir=$test_output/topology-trace"

./ns3 run --no-build \
  "satcompute-topology-replay-equivalence-test \
--oneSecondScenario=contrib/satcompute/tests/fixtures/scenario/online-equivalence-1s.json \
--twoSecondScenario=contrib/satcompute/tests/fixtures/scenario/online-equivalence-2s.json \
--outputDir=$test_output/topology-equivalence"
