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
validation_scenario="contrib/satcompute/input/examples/synthetic-66-fixed.json"
validation_output="$smoke_output/validation"

validated="$(./ns3 run --no-build \
  "satcompute --scenarioConfig=$validation_scenario \
--outputDir=$validation_output --validateOnly=true")"
if [[ "$validated" != *'"status":"validated"'* ]]; then
  echo "scenario validation smoke failed: $validated" >&2
  exit 1
fi

python3 -m json.tool "$validation_output/effective-config.json" >/dev/null

execution_scenario="contrib/satcompute/tests/fixtures/scenario/task-replay.json"
execution_output="$smoke_output/execution"
completed="$(./ns3 run --no-build \
  "satcompute --scenarioConfig=$execution_scenario --outputDir=$execution_output")"
if [[ "$completed" != *'"status":"completed"'* ]]; then
  echo "replay execution smoke failed: $completed" >&2
  exit 1
fi

python3 -m json.tool "$execution_output/effective-config.json" >/dev/null
python3 -m json.tool "$execution_output/run-summary.json" >/dev/null
python3 - "$execution_output/run-summary.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    summary = json.load(source)
if summary["run_status"] != "COMPLETE":
    raise SystemExit("replay smoke run summary is not COMPLETE")
if summary["task"]["completed_task_count"] != 1:
    raise SystemExit("replay smoke did not complete its task")
if summary["transfer"]["completed_transfer_count"] != 2:
    raise SystemExit("replay smoke did not complete both task transfers")
PY

online_scenario="contrib/satcompute/tests/fixtures/scenario/online-fixed.json"
online_output="$smoke_output/online"
online_completed="$(./ns3 run --no-build \
  "satcompute --scenarioConfig=$online_scenario --outputDir=$online_output")"
if [[ "$online_completed" != *'"status":"completed"'* ]]; then
  echo "online execution smoke failed: $online_completed" >&2
  exit 1
fi

python3 - "$online_output/run-summary.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    summary = json.load(source)
if summary["run_status"] != "COMPLETE":
    raise SystemExit("online smoke run summary is not COMPLETE")
if summary["topology_source"] != "online":
    raise SystemExit("online smoke topology source differs")
if summary["applied_topology_slice_count"] != 3:
    raise SystemExit("online smoke update count differs")
PY

trace_scenario="contrib/satcompute/tests/fixtures/scenario/online-trace.json"
trace_output="$smoke_output/trace-export"
exported="$(./ns3 run --no-build \
  "satcompute --scenarioConfig=$trace_scenario --outputDir=$trace_output \
--exportOnly=true")"
if [[ "$exported" != *'"status":"exported"'* ]]; then
  echo "topology trace export smoke failed: $exported" >&2
  exit 1
fi

python3 - "$trace_output/topology-trace/manifest.json" <<'PY'
import hashlib
import json
import pathlib
import sys

manifest_path = pathlib.Path(sys.argv[1])
with manifest_path.open(encoding="utf-8") as source:
    manifest = json.load(source)
if manifest["state_semantics"] != "orbit-policy-evaluation":
    raise SystemExit("trace smoke state semantics differ")
if manifest["slice_count"] != 4:
    raise SystemExit("trace smoke slice count differs")
for slice_record in manifest["slices"]:
    for file_key, hash_key in (
        ("nodes_file", "nodes_sha256"),
        ("topology_file", "topology_sha256"),
    ):
        payload = (manifest_path.parent / slice_record[file_key]).read_bytes()
        if hashlib.sha256(payload).hexdigest() != slice_record[hash_key]:
            raise SystemExit(f"trace smoke hash differs: {slice_record[file_key]}")
PY

echo "SatCompute readiness, validation, replay, online, and trace-export smoke passed."
