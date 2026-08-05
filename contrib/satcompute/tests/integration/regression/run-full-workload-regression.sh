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

constellation="contrib/satcompute/tests/fixtures/constellation/diamond-4.json"
topology="contrib/satcompute/tests/fixtures/topology/snapshots/diamond-4-dynamic"
transfer_inputs="contrib/satcompute/tests/fixtures/traffic/transfers"
task_inputs="contrib/satcompute/tests/fixtures/task"
common="--simulationDuration=5 --constellationConfig=$constellation \
--topologySource=replay --topologyDir=$topology --delayMode=fixed \
--fixedDelay=0.001 --networkUpdateInterval=2 --islBandwidthBps=100000000 \
--topologyExportEnabled=false"

direct_result="$(run_platform "$regression_output/direct" \
  "$common --runName=direct-replay --routingMode=global-first \
--transferTrace=$transfer_inputs/engine-basic.json")"
if [[ "$direct_result" != *'"status":"completed"'* ]]; then
  echo "direct replay regression failed: $direct_result" >&2
  exit 1
fi

task_arguments="$common --runName=task-replay \
--routingMode=global-size-aware-hrw \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json"
task_first="$(run_platform "$regression_output/task-first" "$task_arguments")"
task_second="$(run_platform "$regression_output/task-second" "$task_arguments")"
if [[ "$task_first" != *'"status":"completed"'* ||
      "$task_second" != *'"status":"completed"'* ]]; then
  echo "task replay repeat regression failed" >&2
  exit 1
fi

no_workload_result="$(run_platform \
  "$regression_output/no-workload" \
  "$common --runName=no-workload --routingMode=global-first")"
if [[ "$no_workload_result" != *'"status":"completed"'* ]]; then
  echo "workload-free replay regression failed: $no_workload_result" >&2
  exit 1
fi

partial_common="--simulationDuration=1 --constellationConfig=$constellation \
--topologySource=replay --topologyDir=$topology --delayMode=fixed \
--fixedDelay=0.001 --networkUpdateInterval=2 --islBandwidthBps=1000000 \
--routingMode=global-first --transferTrace=$transfer_inputs/platform-partial.json \
--transferPayloadBytes=1400 --diagnosticMode=failure --topologyExportEnabled=false"
set +e
strict_result="$(run_platform "$regression_output/strict" \
  "$partial_common --runName=partial-strict --taskCompletionPolicy=strict")"
strict_status=$?
set -e
if [[ $strict_status -ne 3 || "$strict_result" != *'"status":"partial"'* ]]; then
  echo "strict partial-completion policy regression failed" >&2
  exit 1
fi

report_result="$(run_platform "$regression_output/report" \
  "$partial_common --runName=partial-report --taskCompletionPolicy=report")"
if [[ "$report_result" != *'"status":"partial"'* ]]; then
  echo "report partial-completion policy regression failed: $report_result" >&2
  exit 1
fi
for directory in strict report; do
  python3 contrib/satcompute/tools/validation/check-flow-drop-reasons.py \
    --output-dir="$regression_output/$directory" \
    --minimum-explicit-drop-packets=0
done

task_failure_common="--simulationDuration=1 --constellationConfig=$constellation \
--topologySource=replay --topologyDir=$topology --delayMode=fixed \
--fixedDelay=0.001 --networkUpdateInterval=2 --islBandwidthBps=100000000 \
--islQueueBytes=1 --routingMode=global-first \
--computeProfile=$task_inputs/compute-profile-single.json \
--taskTrace=$task_inputs/task-single.json --transferPayloadBytes=1024 \
--diagnosticMode=failure --taskCompletionPolicy=strict \
--topologyExportEnabled=false"
set +e
task_failure_result="$(run_platform "$regression_output/task-failure" \
  "$task_failure_common --runName=task-failure")"
task_failure_status=$?
set -e
if [[ $task_failure_status -ne 3 ||
      "$task_failure_result" != *'"status":"partial"'* ]]; then
  echo "task failure diagnostic regression failed: $task_failure_result" >&2
  exit 1
fi
python3 contrib/satcompute/tools/validation/check-task-output.py failure \
  --topology-dir="$topology" \
  --compute-profile="$repository_root/$task_inputs/compute-profile-single.json" \
  --task-trace="$repository_root/$task_inputs/task-single.json" \
  --output-dir="$regression_output/task-failure" \
  --require-queue-drop
python3 contrib/satcompute/tools/validation/check-flow-drop-reasons.py \
  --output-dir="$regression_output/task-failure" \
  --require-reason=QUEUE --require-zero-unattributed

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

no_workload = load_json("no-workload/run-summary.json")
if no_workload["workload_mode"] != "none" or no_workload["run_status"] != "COMPLETE":
    raise SystemExit("workload-free run summary differs")

strict = load_json("strict/run-summary.json")
report = load_json("report/run-summary.json")
if strict["run_status"] != "PARTIAL" or report["run_status"] != "PARTIAL":
    raise SystemExit("partial completion status differs")
if strict["task_completion_policy"] != "strict":
    raise SystemExit("strict completion policy was not recorded")
if report["task_completion_policy"] != "report":
    raise SystemExit("report completion policy was not recorded")
for directory in ("strict", "report"):
    failure = root / directory / "diagnostics/failure"
    if not (failure / "flow-drop-reasons.csv").is_file():
        raise SystemExit(f"{directory} flow-drop reasons are missing")
    if (failure / "diagnostic-summary.json").exists():
        raise SystemExit(f"{directory} direct run wrote full task diagnostics")
    if not load_json(f"{directory}/run-summary.json")["diagnostics_generated"]:
        raise SystemExit(f"{directory} did not report diagnostic evidence")
PY

echo "SatCompute full workload regression passed."
