#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-routing-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT

constellation_66="contrib/satcompute/input/topology/constellations/synthetic-66.json"
constellation_4="contrib/satcompute/tests/fixtures/constellation/diamond-4.json"

validated="$(./ns3 run --no-build \
  "satcompute --constellationConfig=$constellation_66 \
--outputDir=$smoke_output/validation --validateOnly=true")"
if [[ "$validated" != *'"status":"validated"'* ]]; then
  echo "para/constellation validation smoke failed: $validated" >&2
  exit 1
fi
python3 -m json.tool "$smoke_output/validation/effective-config.json" >/dev/null

completed="$(./ns3 run --no-build \
  "satcompute --runName=smoke-online-fixed --simulationDuration=3 \
--constellationConfig=$constellation_4 --topologySource=online \
--maxIslDistance=30000000 --delayMode=fixed --fixedDelay=0.008 \
--networkUpdateInterval=1 --islBandwidthBps=100000000 \
--routingMode=global-first --topologyExportEnabled=false \
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
if summary["topology_source"] != "online":
    raise SystemExit("online routing smoke used the wrong topology source")
if summary["routing_mode"] != "global-first":
    raise SystemExit("online routing smoke used the wrong routing mode")
if summary["applied_topology_slice_count"] != 3:
    raise SystemExit("online routing smoke update count differs")
if summary["route_computation_count"] != 1:
    raise SystemExit("unchanged link membership rebuilt routes")
PY

echo "SatCompute routing smoke passed."
