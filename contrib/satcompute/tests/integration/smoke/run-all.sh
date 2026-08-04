#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

expected='{"application":"satcompute","scenario_schema_version":"0.2","status":"ready"}'
actual="$(./ns3 run --no-build satcompute)"

if [[ "$actual" != "$expected" ]]; then
  echo "unexpected satcompute smoke output: $actual" >&2
  exit 1
fi

smoke_output="$(mktemp -d /tmp/satcompute-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT
scenario="contrib/satcompute/input/examples/synthetic-66-fixed.json"

validated="$(./ns3 run --no-build \
  "satcompute --scenarioConfig=$scenario --outputDir=$smoke_output")"
if [[ "$validated" != *'"status":"validated"'* ]]; then
  echo "scenario validation smoke failed: $validated" >&2
  exit 1
fi

python3 -m json.tool "$smoke_output/effective-config.json" >/dev/null
echo "SatCompute module and configuration smoke passed."
