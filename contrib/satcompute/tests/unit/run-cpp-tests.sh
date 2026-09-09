#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$repository_root"

test_output="$(mktemp -d /tmp/satcompute-cpp-tests.XXXXXX)"
trap 'rm -rf "$test_output"' EXIT

./ns3 run --no-build "satcompute-para-test"
./ns3 run --no-build "satcompute-compfrr-shadow-model-test"
./ns3 run --no-build "satcompute-link-window-test"

./ns3 run --no-build \
  "satcompute-constellation-definition-test \
--valid=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
--outputDir=$test_output/constellation"

./ns3 run --no-build "satcompute-routing-policy-factory-test"

./ns3 run --no-build \
  "satcompute-task-input-test --fixtureRoot=contrib/satcompute/tests/fixtures/task"
./ns3 run --no-build "satcompute-compute-service-test"
./ns3 run --no-build "satcompute-task-deadline-test --outputDir=$test_output/deadline"
./ns3 run --no-build "satcompute-fault-lifecycle-test"
./ns3 run --no-build \
  "satcompute-fault-trace-test --outputDir=$test_output/fault-trace"
./ns3 run --no-build "satcompute-fault-model-test"
./ns3 run --no-build "satcompute-fault-risk-query-test"
./ns3 run --no-build "satcompute-compute-fault-execution-test"
./ns3 run --no-build "satcompute-satellite-fault-execution-test"
./ns3 run --no-build "satcompute-online-orbit-foundation-test"
./ns3 run --no-build "satcompute-online-topology-controller-test"
