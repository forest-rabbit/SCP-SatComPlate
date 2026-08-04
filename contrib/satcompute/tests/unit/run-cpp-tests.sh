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
