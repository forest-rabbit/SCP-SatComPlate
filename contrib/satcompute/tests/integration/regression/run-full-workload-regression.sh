#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

regression_output="$(mktemp -d /tmp/satcompute-workload-regression.XXXXXX)"
trap 'rm -rf "$regression_output"' EXIT

run_platform() {
  local output_directory="$1"
  shift
  ./ns3 run --no-build "satcompute --outputDir=$output_directory $*"
}

constellation="contrib/satcompute/tests/fixtures/constellation/diamond-4.csv"
task_inputs="contrib/satcompute/tests/fixtures/task"
profile="$task_inputs/compute-profile-single.json"
trace="$task_inputs/task-single.json"
common="--simulationDuration=5 --constellationConfig=$constellation \
--maxIslDistance=30000000 --delayMode=fixed \
--fixedDelay=0.001 --networkUpdateInterval=2 --islBandwidthBps=100000000"

task_arguments="$common --routingMode=global-size-aware-hrw \
--computeProfile=$profile --taskTrace=$trace"
task_first="$(run_platform "$regression_output/task-first" "$task_arguments")"
task_second="$(run_platform "$regression_output/task-second" "$task_arguments")"
if [[ "$task_first" != *'"status":"completed"'* ||
      "$task_second" != *'"status":"completed"'* ]]; then
  echo "task repeat regression failed" >&2
  exit 1
fi

no_workload_result="$(run_platform \
  "$regression_output/no-workload" \
  "$common --routingMode=global-first")"
if [[ "$no_workload_result" != *'"status":"completed"'* ]]; then
  echo "workload-free regression failed: $no_workload_result" >&2
  exit 1
fi

partial_common="--simulationDuration=1 --constellationConfig=$constellation \
--maxIslDistance=30000000 --delayMode=fixed \
--fixedDelay=0.001 --networkUpdateInterval=2 --islBandwidthBps=100000000 \
--islQueueBytes=1 --routingMode=global-first \
--computeProfile=$profile --taskTrace=$trace --transferPayloadBytes=1024 \
--diagnosticMode=failure"
set +e
strict_result="$(run_platform "$regression_output/strict" \
  "$partial_common --taskCompletionPolicy=strict")"
strict_status=$?
set -e
if [[ $strict_status -ne 3 || "$strict_result" != *'"status":"partial"'* ]]; then
  echo "strict partial-completion policy regression failed" >&2
  exit 1
fi

report_result="$(run_platform "$regression_output/report" \
  "$partial_common --taskCompletionPolicy=report")"
if [[ "$report_result" != *'"status":"partial"'* ]]; then
  echo "report partial-completion policy regression failed: $report_result" >&2
  exit 1
fi

for directory in strict report; do
  python3 contrib/satcompute/tools/validation/check-flow-drop-reasons.py \
    --output-dir="$regression_output/$directory" \
    --require-reason=QUEUE --require-zero-unattributed
done

python3 - "$regression_output" "$trace" <<'PY'
import csv
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
trace_path = pathlib.Path(sys.argv[2])


def load_json(relative: str):
    with (root / relative).open(encoding="utf-8") as source:
        return json.load(source)


task = json.loads(trace_path.read_text(encoding="utf-8"))["tasks"][0]
for filename in (
    "transfer-summary.csv",
    "task-events.csv",
    "task-summary.csv",
    "compute-node-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
):
    first = (root / "task-first" / filename).read_bytes()
    second = (root / "task-second" / filename).read_bytes()
    if first != second:
        raise SystemExit(f"repeated task output differs: {filename}")

complete = load_json("task-first/run-summary.json")
if complete["task"]["completed_task_count"] != 1:
    raise SystemExit("task regression did not complete its task")
if complete["transfer"]["completed_transfer_count"] != 2:
    raise SystemExit("task regression did not complete both transfers")

with (root / "task-first/transfer-summary.csv").open(
    newline="", encoding="utf-8"
) as source:
    transfers = {int(row["transfer_id"]): row for row in csv.DictReader(source)}
if int(transfers[2]["declared_size_bytes"]) != task["output_bytes"]:
    raise SystemExit("result transfer does not use the explicit output_bytes")

no_workload = load_json("no-workload/run-summary.json")
if no_workload["workload_mode"] != "none" or no_workload["run_status"] != "COMPLETE":
    raise SystemExit("workload-free run summary differs")

for directory, policy in (("strict", "strict"), ("report", "report")):
    run = load_json(f"{directory}/run-summary.json")
    if run["run_status"] != "PARTIAL" or run["task_completion_policy"] != policy:
        raise SystemExit(f"{directory} completion policy differs")
    if not run["diagnostics_generated"]:
        raise SystemExit(f"{directory} did not report diagnostic evidence")
    failure = root / directory / "diagnostics/failure"
    for filename in (
        "incomplete-tasks.csv",
        "incomplete-transfers.csv",
        "isl-queue-drops.csv",
        "flow-drop-reasons.csv",
        "diagnostic-summary.json",
    ):
        if not (failure / filename).is_file():
            raise SystemExit(f"{directory} is missing {filename}")
PY

echo "SatCompute full task workload regression passed."
