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
repeat_audit="$acceptance_output/repeat-audit"
normal_trace="$generate_normal/fault-trace.json"
audit_trace="$generate_audit/fault-trace.json"

generate_normal_result="$(run_platform \
  "$generate_normal" \
  "$common --faultMode=generate --faultTrace=$normal_trace")"
generate_audit_result="$(run_platform \
  "$generate_audit" \
  "$common --faultMode=generate --faultProbabilityAudit=1 \
--faultTrace=$audit_trace")"
repeat_audit_result="$(run_platform \
  "$repeat_audit" \
  "$common --faultMode=generate --faultProbabilityAudit=1 \
--faultTrace=$repeat_audit/fault-trace.json")"

for result in "$generate_normal_result" "$generate_audit_result" \
  "$repeat_audit_result"; do
  if [[ "$result" != *'"status":"partial"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "N4B joint generate/repeat result differs: $result" >&2
    exit 1
  fi
done

probability_audit_tool="contrib/satcompute/tools/validation/compare-fault-probabilities.py"
python3 "$probability_audit_tool" \
  --model "$generate_audit/fault-model-probabilities.csv" \
  --prediction "$repeat_audit/fault-predictions.csv" \
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
    "fault-task-impact.csv",
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
    "fault-model-state.csv",
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
    "repeat-audit",
    core_outputs + audit_outputs,
    "generate/repeat output",
)

for filename in (
    "fault-model-probabilities.csv",
    "fault-model-state.csv",
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    if (root / "generate-normal" / filename).exists():
        raise SystemExit(f"normal generate unexpectedly emitted {filename}")
require_equal_files("generate-audit", "repeat-audit",
                    ("fault-trace.json", "fault-model-probabilities.csv"), "same-seed generation")

import runpy
audit = runpy.run_path("contrib/satcompute/tests/support/fault-run-audit.py")["audit"]
result = audit(root / "generate-audit", task_count=100)
satellites = [f for f in result["faults"] if f["fault_type"] == "satellite"]
assert len(satellites) == 1
assert (satellites[0]["node_id"], satellites[0]["start_time_ns"]) == (4, 829_256_867_404)
assert any(f["f1_occurred"] for f in result["faults"])
assert any(f["f2_occurred"] for f in result["faults"])
tasks = result["tasks"]
failed = {int(t["task_id"]) for t in tasks if t["final_state"] == "FAILED"}
assert failed
for task_id in (36, 37):
    task = next(t for t in tasks if int(t["task_id"]) == task_id)
    assert task["failure_reason"] == "COMPUTE_SATELLITE_FAILURE"
transfers = load_csv("generate-audit/transfer-summary.csv")
assert len(transfers) == 200
assert all(t["terminal_state"] in ("COMPLETED", "CANCELLED") for t in transfers)
assert all(t["terminal_reason"] == "TASK_FAILED" for t in transfers if t["terminal_state"] == "CANCELLED")
events = result["events"]
recomputed = [r for r in events if r["route_recomputed"] == "true"]
assert len(recomputed) == 1 and recomputed[0]["fault_source"] == "F3"
summary = load_json("generate-audit/run-summary.json")
assert summary["task_count"] == 100 and summary["completed_task_count"] == 100 - len(failed)
assert summary["route_computation_count"] == 2 and summary["applied_topology_slice_count"] == 50
for field in ("flow_monitor_lost_packets", "flow_monitor_reported_drop_packets", "flow_monitor_unattributed_lost_packets"):
    assert summary[field] == 0, field
assert not any(r["dropped_packets"] for r in summary["flow_monitor_drop_reasons"])
comparison = load_json("probability-audit/n4b-joint.json")
assert comparison["within_tolerance"] and comparison["matched_record_count"] > 0
size = load_json("generate-audit/size-aware-summary.json")
for field in ("active_flow_count_at_end", "assignment_count_at_end", "final_total_reserved_bytes"):
    assert size[field] == 0, field
assert not any(load_json("generate-audit/capacity-aware-summary.json").values())
print("Joint actual outcomes:", collections.Counter(t["final_state"] for t in tasks),
      "START:", len(result["faults"]), "probability records:", comparison["matched_record_count"])

PY

# Reuse the audited repeat directory with auditing disabled. The platform must remove
# stale audit artifacts without removing or changing its normal formal outputs.
repeat_normal_result="$(run_platform \
  "$repeat_audit" \
  "$common --faultMode=generate --faultTrace=$repeat_audit/fault-trace.json")"
if [[ "$repeat_normal_result" != *'"status":"partial"'* ||
      "$repeat_normal_result" != *'"satellite_count":66'* ]]; then
  echo "N4B joint normal repeat result differs: $repeat_normal_result" >&2
  exit 1
fi

python3 - "$acceptance_output" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
core_outputs = (
    "fault-events.csv",
    "fault-summary.json",
    "fault-task-impact.csv",
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
    "fault-model-state.csv",
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    if (root / "repeat-audit" / filename).exists():
        raise SystemExit(f"normal repeat retained stale audit file: {filename}")
for filename in core_outputs:
    generated = (root / "generate-normal" / filename).read_bytes()
    repeated = (root / "repeat-audit" / filename).read_bytes()
    if generated != repeated:
        raise SystemExit(f"normal generate/repeat output differs: {filename}")
PY

echo "N4B joint acceptance passed: 66 stars, 1000 s, 100 tasks, all live probability records matched."
