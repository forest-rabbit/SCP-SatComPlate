#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-routing-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT

constellation_16="contrib/satcompute/tests/fixtures/constellation/connected-16.csv"

completed="$(./ns3 run --no-build \
"satcompute --faultMode=none --computeProfile=none --taskTrace=none --linkMetrics=0 --simulationDuration=3 \
--constellationConfig=$constellation_16 \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.008 \
--networkUpdateInterval=1 --islBandwidthBps=100000000 \
--routingMode=global-first \
--outputDir=$smoke_output/online")"
if [[ "$completed" != *'"status":"completed"'* ]]; then
  echo "online routing smoke failed: $completed" >&2
  exit 1
fi

python3 - "$smoke_output/online/run-summary.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    summary = json.load(source)
if summary["run_status"] != "COMPLETE":
    raise SystemExit("online routing smoke is not COMPLETE")
if summary["routing_mode"] != "global-first":
    raise SystemExit("online routing smoke used the wrong routing mode")
if summary["applied_topology_slice_count"] != 3:
    raise SystemExit("online routing smoke update count differs")
if summary["route_computation_count"] != 1:
    raise SystemExit("unchanged link membership rebuilt routes")
PY

echo "SatCompute routing smoke passed."
