#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$repository_root"

test_output="$(mktemp -d /tmp/satcompute-cpp-tests.XXXXXX)"
trap 'rm -rf "$test_output"' EXIT

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
  "satcompute-constellation-definition-test \
--valid=contrib/satcompute/input/topology/constellations/synthetic-66.json \
--invalidRuntime=contrib/satcompute/tests/fixtures/constellation/invalid-runtime-field.json \
--outputDir=$test_output/constellation"

./ns3 run --no-build \
  "satcompute-resolved-config-test --outputDir=$test_output/resolved"

./ns3 run --no-build \
  "satcompute-effective-config-test --outputDir=$test_output/effective"

dynamic_topology="contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic"
delay_topology="contrib/satcompute/tests/fixtures/topology/snapshots/delay-only"
capacity_topology="contrib/satcompute/tests/fixtures/topology/snapshots/capacity-pending"
diamond_constellation="contrib/satcompute/tests/fixtures/constellation/diamond-4.json"
./ns3 run --no-build \
  "satcompute-snapshot-test --topologyDir=$dynamic_topology \
--outputDir=$test_output/snapshot"

./ns3 run --no-build "satcompute-link-state-test"

./ns3 run --no-build \
  "satcompute-replay-controller-test --delayTopologyDir=$delay_topology \
--dynamicTopologyDir=$dynamic_topology"

./ns3 run --no-build \
  "satcompute-satellite-topology-test --topologyDir=$dynamic_topology"

./ns3 run --no-build \
  "satcompute-routing-compatibility-test --topologyDir=$dynamic_topology"

./ns3 run --no-build \
  "satcompute-size-aware-routing-test --topologyDir=$dynamic_topology"

./ns3 run --no-build \
  "satcompute-capacity-aware-routing-test --topologyDir=$dynamic_topology"

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
  "satcompute-network-transfer-engine-test --topologyDir=$dynamic_topology \
--capacityTopologyDir=$capacity_topology \
--basicTransfers=$transfer_fixtures/engine-basic.json \
--capacityTransfers=$transfer_fixtures/capacity-pending.json"

task_fixtures="contrib/satcompute/tests/fixtures/task"
./ns3 run --no-build \
  "satcompute-task-input-test --fixtureRoot=$task_fixtures"

./ns3 run --no-build "satcompute-compute-service-test"

./ns3 run --no-build \
  "satcompute-task-coordinator-test --topologyDir=$dynamic_topology \
--fixtureRoot=$task_fixtures"

./ns3 run --no-build \
  "satcompute-run-output-writer-test \
--constellationConfig=$diamond_constellation --topologyDir=$dynamic_topology \
--fixtureRoot=contrib/satcompute/tests/fixtures \
--outputDir=$test_output/run-output"

./ns3 run --no-build "satcompute-online-orbit-foundation-test"

./ns3 run --no-build "satcompute-online-topology-controller-test"

./ns3 run --no-build \
  "satcompute-topology-trace-exporter-test \
--constellationConfig=$diamond_constellation \
--outputDir=$test_output/topology-trace"

./ns3 run --no-build \
  "satcompute-topology-replay-equivalence-test \
--constellationConfig=$diamond_constellation \
--outputDir=$test_output/topology-equivalence"
