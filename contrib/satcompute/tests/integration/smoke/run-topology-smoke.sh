#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-topology-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT

constellation="contrib/satcompute/tests/fixtures/constellation/diamond-4.json"
common_arguments="--runName=smoke-trace --simulationDuration=2.5 \
--constellationConfig=$constellation --maxIslDistance=30000000 \
--delayMode=distance --fixedDelay=0 --networkUpdateInterval=2 \
--islBandwidthBps=100000000 --topologyExportInterval=1"

exported="$(./ns3 run --no-build \
  "satcompute $common_arguments --topologySource=online \
--routingMode=global-first --topologyExportEnabled=true \
--outputDir=$smoke_output/platform --exportOnly=true")"
if [[ "$exported" != *'"status":"exported"'* ]]; then
  echo "topology trace export smoke failed: $exported" >&2
  exit 1
fi

python3 - "$smoke_output/platform/topology-trace/manifest.json" <<'PY'
import hashlib
import json
import pathlib
import sys

manifest_path = pathlib.Path(sys.argv[1])
with manifest_path.open(encoding="utf-8") as source:
    manifest = json.load(source)
if manifest["state_semantics"] != "orbit-policy-evaluation":
    raise SystemExit("trace state semantics differ")
if manifest["slice_count"] != 4:
    raise SystemExit("trace slice count differs")
for slice_record in manifest["slices"]:
    for file_key, hash_key in (
        ("nodes_file", "nodes_sha256"),
        ("topology_file", "topology_sha256"),
    ):
        payload = (manifest_path.parent / slice_record[file_key]).read_bytes()
        if hashlib.sha256(payload).hexdigest() != slice_record[hash_key]:
            raise SystemExit(f"trace hash differs: {slice_record[file_key]}")
PY

generated="$(./ns3 run --no-build \
  "satcompute-topology-generator $common_arguments \
--outputDir=$smoke_output/generator")"
if [[ "$generated" != *'"status":"generated"'* ]]; then
  echo "shared topology generator smoke failed: $generated" >&2
  exit 1
fi
python3 contrib/satcompute/tools/generation/topology/check_export.py \
  --trace-dir="$smoke_output/generator"
diff -ru "$smoke_output/platform/topology-trace" "$smoke_output/generator"

echo "SatCompute online topology/export smoke passed."
