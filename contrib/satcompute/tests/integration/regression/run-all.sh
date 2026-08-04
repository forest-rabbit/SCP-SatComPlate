#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

regression_output="$(mktemp -d /tmp/satcompute-regression.XXXXXX)"
trap 'rm -rf "$regression_output"' EXIT

run_platform() {
  local scenario="$1"
  local output_directory="$2"
  ./ns3 run --no-build \
    "satcompute --scenarioConfig=$scenario --outputDir=$output_directory"
}

direct_scenario="contrib/satcompute/tests/fixtures/scenario/transfer-replay.json"
direct_result="$(run_platform "$direct_scenario" "$regression_output/direct")"
if [[ "$direct_result" != *'"status":"completed"'* ]]; then
  echo "direct replay regression failed: $direct_result" >&2
  exit 1
fi

task_scenario="contrib/satcompute/tests/fixtures/scenario/task-replay.json"
task_first="$(run_platform "$task_scenario" "$regression_output/task-first")"
task_second="$(run_platform "$task_scenario" "$regression_output/task-second")"
if [[ "$task_first" != *'"status":"completed"'* ||
      "$task_second" != *'"status":"completed"'* ]]; then
  echo "task replay repeat regression failed" >&2
  exit 1
fi

no_workload_scenario="contrib/satcompute/tests/fixtures/scenario/replay-dynamic.json"
no_workload_result="$(run_platform \
  "$no_workload_scenario" "$regression_output/no-workload")"
if [[ "$no_workload_result" != *'"status":"completed"'* ]]; then
  echo "workload-free replay regression failed: $no_workload_result" >&2
  exit 1
fi

capacity_scenario="contrib/satcompute/tests/fixtures/scenario/capacity-pending.json"
capacity_result="$(run_platform "$capacity_scenario" "$regression_output/capacity")"
if [[ "$capacity_result" != *'"status":"completed"'* ]]; then
  echo "workload-free capacity replay regression failed: $capacity_result" >&2
  exit 1
fi

strict_scenario="contrib/satcompute/tests/fixtures/scenario/transfer-partial-strict.json"
set +e
strict_result="$(run_platform "$strict_scenario" "$regression_output/strict")"
strict_status=$?
set -e
if [[ $strict_status -ne 3 || "$strict_result" != *'"status":"partial"'* ]]; then
  echo "strict partial-completion policy regression failed" >&2
  exit 1
fi

report_scenario="contrib/satcompute/tests/fixtures/scenario/transfer-partial-report.json"
report_result="$(run_platform "$report_scenario" "$regression_output/report")"
if [[ "$report_result" != *'"status":"partial"'* ]]; then
  echo "report partial-completion policy regression failed: $report_result" >&2
  exit 1
fi

python3 - "$regression_output" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])


def load_json(relative: str):
    with (root / relative).open(encoding="utf-8") as source:
        return json.load(source)


direct = load_json("direct/run-summary.json")
if direct["run_status"] != "COMPLETE":
    raise SystemExit("direct run is not complete")
if direct["transfer"]["completed_transfer_count"] != 2:
    raise SystemExit("direct run transfer count differs")
if direct["transfer"]["received_application_bytes"] != 3074:
    raise SystemExit("direct run received-byte total differs")

for filename in (
    "transfer-summary.csv",
    "udp-socket-drops.csv",
    "task-events.csv",
    "task-summary.csv",
    "compute-node-summary.csv",
    "routing-reservation-events.csv",
    "routing-summary.json",
):
    first = (root / "task-first" / filename).read_bytes()
    second = (root / "task-second" / filename).read_bytes()
    if first != second:
        raise SystemExit(f"repeated task output differs: {filename}")

no_workload = load_json("no-workload/run-summary.json")
if no_workload["workload_mode"] != "none" or no_workload["run_status"] != "COMPLETE":
    raise SystemExit("workload-free run summary differs")

capacity = load_json("capacity/routing-summary.json")
if capacity["capacity_aware"]["active_path_count_at_end"] != 0:
    raise SystemExit("workload-free capacity state is not empty")

strict = load_json("strict/run-summary.json")
report = load_json("report/run-summary.json")
if strict["run_status"] != "PARTIAL" or report["run_status"] != "PARTIAL":
    raise SystemExit("partial completion status differs")
if strict["task_completion_policy"] != "strict":
    raise SystemExit("strict completion policy was not recorded")
if report["task_completion_policy"] != "report":
    raise SystemExit("report completion policy was not recorded")
for directory in ("strict", "report"):
    diagnostic = load_json(f"{directory}/diagnostics/diagnostic-summary.json")
    if diagnostic["incomplete_transfer_count"] != 1:
        raise SystemExit(f"{directory} incomplete transfer count differs")
PY

echo "SatCompute replay platform regressions passed."
