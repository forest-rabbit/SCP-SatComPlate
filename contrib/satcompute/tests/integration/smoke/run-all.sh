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

echo "SatCompute readiness, validation, replay, and online execution smoke passed."
