#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

acceptance_output="$(mktemp -d /tmp/satcompute-n4b-joint-acceptance.XXXXXX)"
trap 'rm -rf "$acceptance_output"' EXIT

run_platform() {
  local output_directory="$1"
  shift
  ./ns3 run --no-build "satcompute --outputDir=$output_directory $*"
}

example="contrib/satcompute/input/examples/leo-66-1000s-n4b-joint"
constellation="contrib/satcompute/input/topology/constellations/synthetic-66.csv"
compute_profile="contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json"
task_trace="$example/task-trace.json"
common="--simulationDuration=1000 --randomSeed=1 --randomRun=16 \
--constellationConfig=$constellation --orbitStartOffset=302 \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.008 \
--networkUpdateInterval=20 --islBandwidthBps=2000000000 \
--routingMode=global-capacity-aware-hrw --computeProfile=$compute_profile \
--taskTrace=$task_trace --taskCompletionPolicy=report \
--faultEnableF1=1 --faultEnableF2=1 --faultEnableF3=1"

generate_normal="$acceptance_output/generate-normal"
generate_audit="$acceptance_output/generate-audit"
replay_audit="$acceptance_output/replay-audit"
normal_trace="$generate_normal/fault-trace.json"
audit_trace="$generate_audit/fault-trace.json"

generate_normal_result="$(run_platform \
  "$generate_normal" \
  "$common --faultMode=generate --faultTrace=$normal_trace")"
generate_audit_result="$(run_platform \
  "$generate_audit" \
  "$common --faultMode=generate --faultProbabilityAudit=1 \
--faultTrace=$audit_trace")"
replay_audit_result="$(run_platform \
  "$replay_audit" \
  "$common --faultMode=replay --faultProbabilityAudit=1 \
--faultTrace=$audit_trace")"

for result in "$generate_normal_result" "$generate_audit_result" \
  "$replay_audit_result"; do
  if [[ "$result" != *'"status":"partial"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "N4B joint generate/replay result differs: $result" >&2
    exit 1
  fi
done

probability_audit_tool="contrib/satcompute/tools/validation/compare-fault-probabilities.py"
python3 "$probability_audit_tool" \
  --model "$generate_audit/fault-model-probabilities.csv" \
  --prediction "$replay_audit/fault-predictions.csv" \
  --detail "$acceptance_output/probability-audit/n4b-joint.csv" \
  --summary "$acceptance_output/probability-audit/n4b-joint.json"

python3 - "$acceptance_output" <<'PY'
import collections
import csv
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])


def load_json(relative):
    with (root / relative).open(encoding="utf-8") as source:
        return json.load(source)


def load_csv(relative):
    with (root / relative).open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def require_equal_files(left_directory, right_directory, filenames, label):
    for filename in filenames:
        left = (root / left_directory / filename).read_bytes()
        right = (root / right_directory / filename).read_bytes()
        if left != right:
            raise SystemExit(f"{label} differs: {filename}")


core_outputs = (
    "fault-events.csv",
    "fault-summary.json",
    "task-events.csv",
    "task-summary.csv",
    "transfer-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
    "capacity-aware-summary.json",
)
audit_outputs = (
    "fault-predictions.csv",
    "fault-prediction-summary.json",
)

if (root / "generate-normal/fault-trace.json").read_bytes() != (
    root / "generate-audit/fault-trace.json"
).read_bytes():
    raise SystemExit("probability auditing changed the generated Fault Trace")

require_equal_files(
    "generate-normal",
    "generate-audit",
    core_outputs,
    "normal/audited generate output",
)
require_equal_files(
    "generate-audit",
    "replay-audit",
    core_outputs + audit_outputs,
    "generate/replay output",
)

for filename in (
    "fault-model-probabilities.csv",
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    if (root / "generate-normal" / filename).exists():
        raise SystemExit(f"normal generate unexpectedly emitted {filename}")
if (root / "replay-audit/fault-model-probabilities.csv").exists():
    raise SystemExit("replay unexpectedly emitted live model probabilities")

trace = load_json("generate-audit/fault-trace.json")
if trace.get("schema_version") != 2 or len(trace.get("faults", [])) != 13:
    raise SystemExit(f"joint Fault Trace shape differs: {trace}")
compute_faults = [
    fault
    for fault in trace["faults"]
    if fault["fault_type"] == "compute" and fault["fault_occurred"]
]
actual_compute_evidence = [
    (fault["node_id"], fault["start_time_ns"], fault["duration_ns"])
    for fault in compute_faults
]
if actual_compute_evidence != [
    (0, 56_000_000_000, 8_000_000_000),
    (11, 66_000_000_000, 8_000_000_000),
    (22, 76_000_000_000, 8_000_000_000),
    (22, 100_000_000_000, 8_000_000_000),
    (17, 236_000_000_000, 8_000_000_000),
    (16, 850_000_000_000, 8_000_000_000),
]:
    raise SystemExit(f"joint compute fault evidence differs: {actual_compute_evidence}")
risk_only = [
    fault
    for fault in trace["faults"]
    if fault["fault_type"] == "compute" and not fault["fault_occurred"]
]
if len(risk_only) != 6:
    raise SystemExit(f"joint risk-only episode count differs: {len(risk_only)}")
satellite_faults = [
    fault for fault in trace["faults"] if fault["fault_type"] == "satellite"
]
if len(satellite_faults) != 1 or (
    satellite_faults[0]["node_id"],
    satellite_faults[0]["start_time_ns"],
    satellite_faults[0]["duration_ns"],
) != (4, 829_256_867_404, None):
    raise SystemExit(f"joint satellite fault differs: {satellite_faults}")

expected_fault_summary = {
    "active_fault_count_at_end": 1,
    "cancelled_transfer_count": 8,
    "compute_fault_count": 12,
    "failed_task_count": 7,
    "failed_transfer_count": 0,
    "fault_count": 13,
    "notice_event_count": 11,
    "recovery_event_count": 6,
    "route_recomputation_count_due_to_fault": 1,
    "satellite_fault_count": 1,
    "start_event_count": 7,
}
fault_summary = load_json("generate-audit/fault-summary.json")
if fault_summary != expected_fault_summary:
    raise SystemExit(f"joint fault summary differs: {fault_summary}")

task_rows = load_csv("generate-audit/task-summary.csv")
if len(task_rows) != 100:
    raise SystemExit(f"joint task count differs: {len(task_rows)}")
task_by_id = {int(row["task_id"]): row for row in task_rows}
failed_tasks = {
    task_id: (
        row["failure_reason"],
        int(row["failure_time_ns"]),
    )
    for task_id, row in task_by_id.items()
    if row["final_state"] == "FAILED"
}
expected_failed_tasks = {
    6: ("COMPUTE_NODE_FAILURE", 56_000_000_000),
    13: ("COMPUTE_NODE_FAILURE", 66_000_000_000),
    20: ("COMPUTE_NODE_FAILURE", 76_000_000_000),
    31: ("COMPUTE_NODE_FAILURE", 236_000_000_000),
    34: ("COMPUTE_NODE_FAILURE", 850_000_000_000),
    36: ("COMPUTE_SATELLITE_FAILURE", 829_256_867_404),
    37: ("COMPUTE_SATELLITE_FAILURE", 840_100_000_000),
}
if failed_tasks != expected_failed_tasks:
    raise SystemExit(f"joint failed-task evidence differs: {failed_tasks}")
if collections.Counter(row["final_state"] for row in task_rows) != {
    "COMPLETED": 93,
    "FAILED": 7,
}:
    raise SystemExit("joint task terminal-state counts differ")
for task_id in (7, 14, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 32, 33, 35, 38):
    if task_by_id[task_id]["final_state"] != "COMPLETED":
        raise SystemExit(f"joint recovery/control task {task_id} did not complete")

transfer_rows = load_csv("generate-audit/transfer-summary.csv")
if len(transfer_rows) != 200 or collections.Counter(
    row["terminal_state"] for row in transfer_rows
) != {"COMPLETED": 192, "CANCELLED": 8}:
    raise SystemExit("joint transfer terminal-state counts differ")
cancelled_transfers = [
    row for row in transfer_rows if row["terminal_state"] == "CANCELLED"
]
if any(row["terminal_reason"] != "TASK_FAILED" for row in cancelled_transfers):
    raise SystemExit("joint cancelled-transfer reason differs")

events = load_csv("generate-audit/fault-events.csv")
recomputed_events = [row for row in events if row["route_recomputed"] == "true"]
if len(recomputed_events) != 1 or (
    recomputed_events[0]["fault_type"],
    recomputed_events[0]["event_type"],
    int(recomputed_events[0]["node_id"]),
) != ("satellite", "START", 4):
    raise SystemExit(f"joint route-recomputation evidence differs: {recomputed_events}")

run_summary = load_json("generate-audit/run-summary.json")
if (
    run_summary["run_status"],
    run_summary["task_count"],
    run_summary["completed_task_count"],
    run_summary["task_completion_rate_percent"],
    run_summary["route_computation_count"],
    run_summary["applied_topology_slice_count"],
) != ("PARTIAL", 100, 93, 93, 2, 50):
    raise SystemExit(f"joint run summary differs: {run_summary}")
for field in (
    "flow_monitor_lost_packets",
    "flow_monitor_reported_drop_packets",
    "flow_monitor_unattributed_lost_packets",
):
    if run_summary[field] != 0:
        raise SystemExit(f"joint network loss differs for {field}: {run_summary[field]}")
if any(reason["dropped_packets"] != 0 for reason in run_summary["flow_monitor_drop_reasons"]):
    raise SystemExit("joint run contains a FlowMonitor drop reason")
if (
    run_summary["transfer"]["transfer_count"],
    run_summary["transfer"]["completed_transfer_count"],
) != (200, 192):
    raise SystemExit("joint run transfer summary differs")

prediction_summary = load_json("generate-audit/fault-prediction-summary.json")
if prediction_summary != {
    "prediction_count": 82,
    "risk_episode_count": 9,
    "task_count": 12,
}:
    raise SystemExit(f"joint prediction summary differs: {prediction_summary}")
model_rows = load_csv("generate-audit/fault-model-probabilities.csv")
prediction_rows = load_csv("replay-audit/fault-predictions.csv")
if len(model_rows) != 82 or len(prediction_rows) != 82:
    raise SystemExit("joint probability record count differs")

probability_audit = load_json("probability-audit/n4b-joint.json")
if (
    not probability_audit["within_tolerance"]
    or probability_audit["model_record_count"] != 82
    or probability_audit["prediction_record_count"] != 82
    or probability_audit["matched_record_count"] != 82
    or probability_audit["missing_model_record_count"] != 0
    or probability_audit["missing_prediction_record_count"] != 0
    or probability_audit["context_mismatch_count"] != 0
):
    raise SystemExit(f"joint probability audit differs: {probability_audit}")
for field, errors in probability_audit["probability_errors"].items():
    if any(
        errors[metric] is None
        or errors[metric] > probability_audit["absolute_tolerance"]
        for metric in ("mae", "rmse", "max_absolute_error")
    ):
        raise SystemExit(f"joint probability error differs for {field}: {errors}")

size_summary = load_json("generate-audit/size-aware-summary.json")
for field in (
    "active_flow_count_at_end",
    "assignment_count_at_end",
    "final_total_reserved_bytes",
):
    if size_summary[field] != 0:
        raise SystemExit(f"joint Size-aware account leaked for {field}")
capacity_summary = load_json("generate-audit/capacity-aware-summary.json")
if any(value != 0 for value in capacity_summary.values()):
    raise SystemExit(f"joint Capacity-aware account leaked: {capacity_summary}")
PY

# Reuse the audited replay directory with auditing disabled. The platform must remove
# stale audit artifacts without removing or changing its normal formal outputs.
replay_normal_result="$(run_platform \
  "$replay_audit" \
  "$common --faultMode=replay --faultTrace=$audit_trace")"
if [[ "$replay_normal_result" != *'"status":"partial"'* ||
      "$replay_normal_result" != *'"satellite_count":66'* ]]; then
  echo "N4B joint normal replay result differs: $replay_normal_result" >&2
  exit 1
fi

python3 - "$acceptance_output" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
core_outputs = (
    "fault-events.csv",
    "fault-summary.json",
    "task-events.csv",
    "task-summary.csv",
    "transfer-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
    "capacity-aware-summary.json",
)
for filename in (
    "fault-model-probabilities.csv",
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    if (root / "replay-audit" / filename).exists():
        raise SystemExit(f"normal replay retained stale audit file: {filename}")
for filename in core_outputs:
    generated = (root / "generate-normal" / filename).read_bytes()
    replayed = (root / "replay-audit" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"normal generate/replay output differs: {filename}")
PY

echo "N4B joint acceptance passed: 66 stars, 1000 s, 100 tasks, 82 probability records."
