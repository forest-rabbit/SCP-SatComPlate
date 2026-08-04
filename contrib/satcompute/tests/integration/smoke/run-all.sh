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

echo "SatCompute module smoke passed."
