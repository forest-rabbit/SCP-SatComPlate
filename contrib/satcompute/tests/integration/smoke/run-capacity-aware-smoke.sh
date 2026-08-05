#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-capacity-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT

constellation="contrib/satcompute/tests/fixtures/constellation/diamond-4.json"
topology="contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic"
transfers="contrib/satcompute/tests/fixtures/traffic/transfers/engine-basic.json"

completed="$(./ns3 run --no-build \
  "satcompute --runName=smoke-capacity-aware --simulationDuration=5 \
--constellationConfig=$constellation --topologySource=replay \
--topologyDir=$topology --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-capacity-aware-hrw --transferTrace=$transfers \
--transferChunkMode=fixed --transferPayloadBytes=1024 \
--transferLogMode=silent --topologyExportEnabled=false \
--outputDir=$smoke_output/run")"
if [[ "$completed" != *'"status":"completed"'* ]]; then
  echo "capacity-aware smoke failed: $completed" >&2
  exit 1
fi

python3 - "$smoke_output/run" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
with (root / "run-summary.json").open(encoding="utf-8") as source:
    run = json.load(source)
with (root / "capacity-aware-summary.json").open(encoding="utf-8") as source:
    capacity = json.load(source)
if run["run_status"] != "COMPLETE":
    raise SystemExit("capacity-aware smoke is not COMPLETE")
if run["routing_mode"] != "global-capacity-aware-hrw":
    raise SystemExit("capacity-aware routing mode differs")
if run["pacing_mode"] != "path-bottleneck-serialization":
    raise SystemExit("capacity-aware pacing mode differs")
if run["transfer"]["completed_transfer_count"] != 2:
    raise SystemExit("capacity-aware smoke did not complete both transfers")
if any(capacity.values()):
    raise SystemExit(f"capacity-aware state leaked after completion: {capacity}")
PY

echo "SatCompute capacity-aware smoke passed."
