#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$repository_root"

test_output="$(mktemp -d /tmp/satcompute-cpp-tests.XXXXXX)"
trap 'rm -rf "$test_output"' EXIT

./ns3 run --no-build "satcompute-para-test"

./ns3 run --no-build \
  "satcompute-constellation-definition-test \
--valid=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
--outputDir=$test_output/constellation"

./ns3 run --no-build "satcompute-link-state-test"
./ns3 run --no-build "satcompute-routing-policy-factory-test"
./ns3 run --no-build \
  "satcompute-routing-metrics-test --outputDir=$test_output/routing-metrics"

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
  "satcompute-task-input-test --fixtureRoot=contrib/satcompute/tests/fixtures/task"
./ns3 run --no-build "satcompute-compute-service-test"
./ns3 run --no-build "satcompute-online-orbit-foundation-test"
./ns3 run --no-build "satcompute-online-topology-controller-test"
./ns3 run --no-build \
  "satcompute-topology-slice-exporter-test \
--constellationConfig=contrib/satcompute/tests/fixtures/constellation/diamond-4.csv \
--outputDir=$test_output/topology-export"
