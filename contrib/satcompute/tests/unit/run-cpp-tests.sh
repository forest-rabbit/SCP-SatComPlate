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
