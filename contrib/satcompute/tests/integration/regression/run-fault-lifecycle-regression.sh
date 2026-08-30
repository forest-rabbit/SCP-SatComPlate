#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

regression_output="$(mktemp -d /tmp/satcompute-fault-regression.XXXXXX)"
trap 'rm -rf "$regression_output"' EXIT

run_platform() {
  local output_directory="$1"
  shift
  ./ns3 run --no-build "satcompute --outputDir=$output_directory $*"
}

constellation="contrib/satcompute/tests/fixtures/constellation/connected-16.csv"
task_inputs="contrib/satcompute/tests/fixtures/task"
fault_inputs="contrib/satcompute/tests/fixtures/fault"
profile="$task_inputs/compute-profile-single.json"
fault_task="$task_inputs/task-fault-running.json"
f1_example="contrib/satcompute/input/examples/leo-66-120s-f1"
common="--simulationDuration=1 --constellationConfig=$constellation \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-capacity-aware-hrw --computeProfile=$profile \
--taskCompletionPolicy=report"
network_common="--simulationDuration=1 --constellationConfig=$constellation \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-first"

./ns3 run --no-build \
  "satcompute-f1-calibration \
--outputDir=$regression_output/f1-calibration" >/dev/null

compute_result="$(run_platform \
  "$regression_output/compute" \
  "$common --taskTrace=$fault_task --faultMode=replay \
--faultTrace=$fault_inputs/compute-finite.json")"
satellite_first_result="$(run_platform \
  "$regression_output/satellite-first" \
  "$common --taskTrace=$fault_task --faultMode=replay \
--faultTrace=$fault_inputs/satellite-input-finite.json")"
satellite_second_result="$(run_platform \
  "$regression_output/satellite-second" \
  "$common --taskTrace=$fault_task --faultMode=replay \
--faultTrace=$fault_inputs/satellite-input-finite.json")"
satellite_only_result="$(run_platform \
  "$regression_output/satellite-only" \
  "$network_common --faultMode=replay \
--faultTrace=$fault_inputs/satellite-finite.json")"
f1_generated_trace="$regression_output/generate-f1/fault-trace.json"
f1_common="--simulationDuration=90 --constellationConfig=$constellation \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=20 --islBandwidthBps=100000000 \
--routingMode=global-capacity-aware-hrw --computeProfile=$profile \
--taskTrace=$task_inputs/task-f1-critical.json --taskCompletionPolicy=report"
generate_f1_result="$(run_platform \
  "$regression_output/generate-f1" \
  "$f1_common --faultMode=generate \
--faultTrace=$f1_generated_trace")"
f1_second_trace="$regression_output/generate-f1-second/fault-trace.json"
generate_f1_second_result="$(run_platform \
  "$regression_output/generate-f1-second" \
  "$f1_common --faultMode=generate \
--faultTrace=$f1_second_trace")"
replay_f1_result="$(run_platform \
  "$regression_output/replay-f1" \
  "$f1_common --faultMode=replay --faultTrace=$f1_generated_trace")"
f1_sampled_trace="$regression_output/generate-f1-sampled/fault-trace.json"
f1_sampled_common="--simulationDuration=80 --randomRun=64 \
--constellationConfig=$constellation --maxIslDistance=6171353 \
--delayMode=fixed --fixedDelay=0.001 --networkUpdateInterval=20 \
--islBandwidthBps=100000000 --routingMode=global-capacity-aware-hrw \
--computeProfile=$profile --taskTrace=$task_inputs/task-f1-risk-window.json \
--taskCompletionPolicy=report"
generate_f1_sampled_result="$(run_platform \
  "$regression_output/generate-f1-sampled" \
  "$f1_sampled_common --faultMode=generate \
--faultTrace=$f1_sampled_trace")"
replay_f1_sampled_result="$(run_platform \
  "$regression_output/replay-f1-sampled" \
  "$f1_sampled_common --faultMode=replay --faultTrace=$f1_sampled_trace")"
f1_66_trace="$regression_output/generate-f1-66/fault-trace.json"
f1_66_common="--simulationDuration=120 \
--constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.008 \
--networkUpdateInterval=20 --islBandwidthBps=2000000000 \
--routingMode=global-capacity-aware-hrw \
--computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
--taskTrace=$f1_example/task-trace.json --taskCompletionPolicy=report"
generate_f1_66_result="$(run_platform \
  "$regression_output/generate-f1-66" \
  "$f1_66_common --faultMode=generate \
--faultTrace=$f1_66_trace")"
replay_f1_66_result="$(run_platform \
  "$regression_output/replay-f1-66" \
  "$f1_66_common --faultMode=replay --faultTrace=$f1_66_trace")"

for result in "$compute_result" "$satellite_first_result" "$satellite_second_result"; do
  if [[ "$result" != *'"status":"partial"'* ]]; then
    echo "fault lifecycle run did not report its intentional failed task" >&2
    exit 1
  fi
done
if [[ "$satellite_only_result" != *'"status":"completed"'* ]]; then
  echo "workload-free satellite fault run failed: $satellite_only_result" >&2
  exit 1
fi
for result in "$generate_f1_result" "$generate_f1_second_result" "$replay_f1_result"; do
  if [[ "$result" != *'"status":"partial"'* ]]; then
    echo "F1 generate/replay did not report the intentionally failed old task: $result" >&2
    exit 1
  fi
done
for result in "$generate_f1_sampled_result" "$replay_f1_sampled_result"; do
  if [[ "$result" != *'"status":"partial"'* ]]; then
    echo "sampled F1 generate/replay missed its controlled branch: $result" >&2
    exit 1
  fi
done
for result in "$generate_f1_66_result" "$replay_f1_66_result"; do
  if [[ "$result" != *'"status":"partial"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "66-star F1 generate/replay result differs: $result" >&2
    exit 1
  fi
done

python3 - "$regression_output" <<'PY'
import csv
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])


def load_json(relative: str):
    with (root / relative).open(encoding="utf-8") as source:
        return json.load(source)


def load_csv(relative: str):
    with (root / relative).open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


calibration = load_json("f1-calibration/n4b-f1-calibration-summary.json")
selected = calibration["selected"]
if calibration["recoverable_compute_duration_s"] != 8.0 or not (
    27.6 <= selected["temperature_after_recovery_from_critical_c"] <= 27.7
):
    raise SystemExit(f"F1 recovery calibration differs: {selected}")
if (
    selected["heating_tau_s"],
    selected["cooling_tau_s"],
    selected["time_to_temperature_risk_s"],
    selected["time_to_risk_threshold_s"],
    selected["time_to_critical_s"],
    selected["representative_tasks_before_critical"],
) != (43.0, 40.0, 8, 44, 56, 6):
    raise SystemExit(f"F1 selected thermal calibration differs: {selected}")
calibration_rows = load_csv("f1-calibration/n4b-f1-calibration.csv")
monte_carlo = calibration["monte_carlo"]
candidates = monte_carlo["candidates"]
if (
    monte_carlo["run_count"],
    monte_carlo["satellite_count"],
    monte_carlo["duration_s"],
    monte_carlo["hotspot_count"],
) != (30, 66, 1000, 3):
    raise SystemExit("F1 Monte Carlo dimensions differ")
if [candidate["max_failure_intensity_per_s"] for candidate in candidates] != [
    0.005,
    0.01,
    0.02,
    0.05,
]:
    raise SystemExit("F1 failure-intensity candidate grid differs")
if selected["max_failure_intensity_per_s"] != 0.005 or (
    selected["configured_max_failure_intensity_per_s"] != 0.005
):
    raise SystemExit("F1 calibrated failure intensity differs")
if not (0.8 <= selected["monte_carlo_mean_fault_count"] <= 1.2) or (
    selected["monte_carlo_mean_risk_only_episode_count"] <= 0
):
    raise SystemExit("F1 Monte Carlo missed its functional selection target")
if any(
    candidates[index]["mean_fault_count"] >= candidates[index + 1]["mean_fault_count"]
    for index in range(len(candidates) - 1)
):
    raise SystemExit("F1 sampled fault count is not monotonic across candidates")
if len(calibration_rows) != 960:
    raise SystemExit("F1 calibration CSV candidate grid is incomplete")


f1_trace = load_json("generate-f1/fault-trace.json")
if f1_trace["schema_version"] != 2 or len(f1_trace["faults"]) != 1:
    raise SystemExit(f"F1 generated trace shape differs: {f1_trace}")
f1_fault = f1_trace["faults"][0]
if (
    f1_fault["node_id"],
    f1_fault["fault_type"],
    f1_fault["fault_occurred"],
    f1_fault["notice_time_ns"],
    f1_fault["start_time_ns"],
    f1_fault["warning_lead_time_ns"],
    f1_fault["duration_ns"],
) != (3, "compute", True, 44_000_000_000, 56_000_000_000, 12_000_000_000, 8_000_000_000):
    raise SystemExit(f"F1 generated fault differs: {f1_fault}")
if (root / "generate-f1/fault-trace.json").read_bytes() != (
    root / "generate-f1-second/fault-trace.json"
).read_bytes():
    raise SystemExit("same-seed F1 generated traces are not byte-identical")

for filename in (
    "fault-events.csv",
    "fault-summary.json",
    "task-events.csv",
    "task-summary.csv",
    "transfer-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
    "capacity-aware-summary.json",
):
    generated = (root / "generate-f1" / filename).read_bytes()
    replayed = (root / "replay-f1" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"F1 generate/replay output differs: {filename}")

f1_events = load_csv("generate-f1/fault-events.csv")
if [row["event_type"] for row in f1_events] != ["NOTICE", "START", "RECOVERY"]:
    raise SystemExit("F1 runtime event order differs")
if [int(row["simulation_time_ns"]) for row in f1_events] != [
    44_000_000_000,
    56_000_000_000,
    64_000_000_000,
]:
    raise SystemExit("F1 runtime event timestamps differ")
if any(row["route_recomputed"] != "false" for row in f1_events):
    raise SystemExit("F1 compute fault unexpectedly recomputed routes")

f1_tasks = {int(row["task_id"]): row for row in load_csv(
    "generate-f1/task-summary.csv"
)}
if (
    f1_tasks[1]["final_state"],
    f1_tasks[1]["failure_reason"],
    int(f1_tasks[1]["failure_time_ns"]),
) != ("FAILED", "COMPUTE_NODE_FAILURE", 56_000_000_000):
    raise SystemExit("F1 did not fail the running old task at START")
if f1_tasks[2]["final_state"] != "COMPLETED":
    raise SystemExit("F1 recovery did not allow a later task to complete")


sampled_trace = load_json("generate-f1-sampled/fault-trace.json")
if len(sampled_trace["faults"]) != 1:
    raise SystemExit("sampled F1 trace count differs")
sampled_fault = sampled_trace["faults"][0]
if (
    sampled_fault["node_id"],
    sampled_fault["fault_occurred"],
    sampled_fault["notice_time_ns"],
    sampled_fault["start_time_ns"],
    sampled_fault["warning_lead_time_ns"],
) != (3, True, None, 36_000_000_000, None):
    raise SystemExit(f"sampled unannounced F1 fault differs: {sampled_fault}")
if not (0.0 < sampled_fault["failure_probability"] < 1.0):
    raise SystemExit("sampled F1 branch did not use a fractional probability")
for filename in (
    "fault-events.csv",
    "fault-summary.json",
    "task-events.csv",
    "task-summary.csv",
    "transfer-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
    "capacity-aware-summary.json",
):
    generated = (root / "generate-f1-sampled" / filename).read_bytes()
    replayed = (root / "replay-f1-sampled" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"sampled F1 generate/replay output differs: {filename}")
sampled_events = load_csv("generate-f1-sampled/fault-events.csv")
if [row["event_type"] for row in sampled_events] != ["START", "RECOVERY"] or (
    any(row["route_recomputed"] != "false" for row in sampled_events)
):
    raise SystemExit("sampled F1 runtime lifecycle differs")


f1_66_trace = load_json("generate-f1-66/fault-trace.json")
faults_66 = f1_66_trace["faults"]
if len(faults_66) != 4:
    raise SystemExit(f"66-star F1 trace count differs: {faults_66}")
occurred_66 = [fault for fault in faults_66 if fault["fault_occurred"]]
risk_only_66 = [fault for fault in faults_66 if not fault["fault_occurred"]]
occurred_evidence = [
    (
        fault["node_id"],
        fault["notice_time_ns"],
        fault["start_time_ns"],
        fault["warning_lead_time_ns"],
        fault["duration_ns"],
    )
    for fault in occurred_66
]
if occurred_evidence != [
    (0, 44_000_000_000, 56_000_000_000, 12_000_000_000, 8_000_000_000),
    (11, 54_000_000_000, 66_000_000_000, 12_000_000_000, 8_000_000_000),
    (22, 64_000_000_000, 76_000_000_000, 12_000_000_000, 8_000_000_000),
]:
    raise SystemExit(f"66-star F1 occurred fault differs: {occurred_66}")
if len(risk_only_66) != 1 or (
    risk_only_66[0]["node_id"],
    risk_only_66[0]["notice_time_ns"],
    risk_only_66[0]["risk_duration_ns"],
) != (33, 44_000_000_000, 2_000_000_000):
    raise SystemExit(f"66-star F1 risk-only episode differs: {risk_only_66}")

for filename in (
    "fault-events.csv",
    "fault-summary.json",
    "task-events.csv",
    "task-summary.csv",
    "transfer-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
    "capacity-aware-summary.json",
):
    generated = (root / "generate-f1-66" / filename).read_bytes()
    replayed = (root / "replay-f1-66" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"66-star F1 generate/replay output differs: {filename}")

tasks_66 = {int(row["task_id"]): row for row in load_csv(
    "generate-f1-66/task-summary.csv"
)}
failed_task_ids = sorted(
    task_id
    for task_id, row in tasks_66.items()
    if row["final_state"] == "FAILED"
)
if len(tasks_66) != 20 or failed_task_ids != [4, 9, 14]:
    raise SystemExit("66-star F1 task terminal counts differ")
if any(
    tasks_66[task_id]["final_state"] != "COMPLETED"
    for task_id in (5, 10, 15, 16, 17, 18, 19, 20)
):
    raise SystemExit("66-star F1 recovery, risk-only, or control task did not complete")
run_66 = load_json("generate-f1-66/run-summary.json")
if (
    run_66["route_computation_count"],
    run_66["applied_topology_slice_count"],
    run_66["completed_task_count"],
) != (1, 6, 17):
    raise SystemExit("66-star F1 changed topology, routing, or task counts")


compute_events = load_csv("compute/fault-events.csv")
if [row["event_type"] for row in compute_events] != ["NOTICE", "START", "RECOVERY"]:
    raise SystemExit("compute fault event order differs")
if [int(row["simulation_time_ns"]) for row in compute_events] != [
    250_000_000,
    500_000_000,
    750_000_000,
]:
    raise SystemExit("compute fault event timestamps differ")
if any(row["failure_probability"] != "0.75" for row in compute_events):
    raise SystemExit("compute fault probability was sampled or rewritten")
if any(row["route_recomputed"] != "false" for row in compute_events):
    raise SystemExit("compute fault unexpectedly recomputed routes")

compute_summary = load_json("compute/fault-summary.json")
expected_compute = {
    "fault_count": 1,
    "compute_fault_count": 1,
    "satellite_fault_count": 0,
    "notice_event_count": 1,
    "start_event_count": 1,
    "recovery_event_count": 1,
    "active_fault_count_at_end": 0,
    "failed_task_count": 1,
    "failed_transfer_count": 0,
    "cancelled_transfer_count": 1,
    "route_recomputation_count_due_to_fault": 0,
}
if compute_summary != expected_compute:
    raise SystemExit(f"compute fault summary differs: {compute_summary}")
compute_run = load_json("compute/run-summary.json")
if (
    compute_run["run_status"],
    compute_run["route_computation_count"],
    compute_run["applied_topology_slice_count"],
) != ("PARTIAL", 1, 1):
    raise SystemExit("compute fault changed topology or route counters")

compute_tasks = load_csv("compute/task-summary.csv")
if len(compute_tasks) != 1 or (
    compute_tasks[0]["final_state"],
    compute_tasks[0]["failure_reason"],
    int(compute_tasks[0]["failure_time_ns"]),
) != ("FAILED", "COMPUTE_NODE_FAILURE", 500_000_000):
    raise SystemExit("compute task terminal evidence differs")
compute_transfers = {int(row["transfer_id"]): row for row in load_csv(
    "compute/transfer-summary.csv"
)}
if compute_transfers[1]["terminal_state"] != "COMPLETED":
    raise SystemExit("compute fault lost completed INPUT history")
if (
    compute_transfers[2]["terminal_state"],
    compute_transfers[2]["terminal_reason"],
    int(compute_transfers[2]["terminal_time_ns"]),
) != ("CANCELLED", "TASK_FAILED", 500_000_000):
    raise SystemExit("compute fault result cancellation differs")

satellite_events = load_csv("satellite-first/fault-events.csv")
if [row["event_type"] for row in satellite_events] != ["START", "RECOVERY"]:
    raise SystemExit("satellite fault event order differs")
if [int(row["simulation_time_ns"]) for row in satellite_events] != [
    100_500_000,
    350_500_000,
]:
    raise SystemExit("satellite fault event timestamps differ")
if any(row["route_recomputed"] != "true" for row in satellite_events):
    raise SystemExit("satellite fault did not record exact-time route recomputation")
if satellite_events[0]["communication_available_after"] != "false" or (
    satellite_events[1]["communication_available_after"] != "true"
):
    raise SystemExit("satellite communication availability evidence differs")

satellite_summary = load_json("satellite-first/fault-summary.json")
expected_satellite = {
    "fault_count": 1,
    "compute_fault_count": 0,
    "satellite_fault_count": 1,
    "notice_event_count": 0,
    "start_event_count": 1,
    "recovery_event_count": 1,
    "active_fault_count_at_end": 0,
    "failed_task_count": 1,
    "failed_transfer_count": 1,
    "cancelled_transfer_count": 1,
    "route_recomputation_count_due_to_fault": 2,
}
if satellite_summary != expected_satellite:
    raise SystemExit(f"satellite fault summary differs: {satellite_summary}")
satellite_run = load_json("satellite-first/run-summary.json")
if (
    satellite_run["run_status"],
    satellite_run["route_computation_count"],
    satellite_run["applied_topology_slice_count"],
) != ("PARTIAL", 3, 1):
    raise SystemExit("satellite fault route or topology counters differ")

satellite_only_summary = load_json("satellite-only/fault-summary.json")
expected_satellite_only = {
    "fault_count": 1,
    "compute_fault_count": 0,
    "satellite_fault_count": 1,
    "notice_event_count": 0,
    "start_event_count": 1,
    "recovery_event_count": 1,
    "active_fault_count_at_end": 0,
    "failed_task_count": 0,
    "failed_transfer_count": 0,
    "cancelled_transfer_count": 0,
    "route_recomputation_count_due_to_fault": 2,
}
if satellite_only_summary != expected_satellite_only:
    raise SystemExit(
        f"workload-free satellite fault summary differs: {satellite_only_summary}"
    )
for filename in ("task-summary.csv", "transfer-summary.csv"):
    if (root / "satellite-only" / filename).exists():
        raise SystemExit(f"workload-free satellite fault created {filename}")

satellite_tasks = load_csv("satellite-first/task-summary.csv")
if len(satellite_tasks) != 1 or (
    satellite_tasks[0]["final_state"],
    satellite_tasks[0]["failure_reason"],
    int(satellite_tasks[0]["failure_time_ns"]),
) != ("FAILED", "COMPUTE_SATELLITE_FAILURE", 100_500_000):
    raise SystemExit("satellite task terminal evidence differs")
satellite_transfers = {int(row["transfer_id"]): row for row in load_csv(
    "satellite-first/transfer-summary.csv"
)}
if (
    satellite_transfers[1]["terminal_state"],
    satellite_transfers[1]["terminal_reason"],
    int(satellite_transfers[1]["terminal_time_ns"]),
) != ("FAILED", "DESTINATION_SATELLITE_FAILED", 100_500_000):
    raise SystemExit("satellite INPUT failure evidence differs")
if (
    satellite_transfers[2]["terminal_state"],
    satellite_transfers[2]["terminal_reason"],
) != ("CANCELLED", "TASK_FAILED"):
    raise SystemExit("satellite RESULT cancellation evidence differs")

for directory in ("compute", "satellite-first", "satellite-second"):
    capacity = load_json(f"{directory}/capacity-aware-summary.json")
    if any(capacity.values()):
        raise SystemExit(f"{directory} leaked capacity state: {capacity}")
    size = load_json(f"{directory}/size-aware-summary.json")
    for key in (
        "active_flow_count_at_end",
        "assignment_count_at_end",
        "final_total_reserved_bytes",
    ):
        if size[key] != 0:
            raise SystemExit(f"{directory} leaked flow state: {key}={size[key]}")

for filename in (
    "fault-events.csv",
    "fault-summary.json",
    "task-events.csv",
    "task-summary.csv",
    "transfer-summary.csv",
    "ecmp-route-events.csv",
    "size-aware-reservation-events.csv",
    "size-aware-summary.json",
    "capacity-aware-summary.json",
):
    first = (root / "satellite-first" / filename).read_bytes()
    second = (root / "satellite-second" / filename).read_bytes()
    if first != second:
        raise SystemExit(f"repeated satellite fault output differs: {filename}")
PY

no_fault_result="$(run_platform \
  "$regression_output/satellite-first" \
  "$common --taskTrace=$task_inputs/task-single.json")"
if [[ "$no_fault_result" != *'"status":"completed"'* ]]; then
  echo "no-fault reuse regression failed: $no_fault_result" >&2
  exit 1
fi
if [[ -e "$regression_output/satellite-first/fault-events.csv" ||
      -e "$regression_output/satellite-first/fault-summary.json" ]]; then
  echo "no-fault run retained stale fault metrics" >&2
  exit 1
fi
python3 - "$regression_output/satellite-first" <<'PY'
import csv
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
with (root / "task-summary.csv").open(newline="", encoding="utf-8") as source:
    tasks = list(csv.DictReader(source))
if len(tasks) != 1 or (
    tasks[0]["final_state"],
    tasks[0]["failure_reason"],
    int(tasks[0]["failure_time_ns"]),
) != ("COMPLETED", "", -1):
    raise SystemExit("no-fault task terminal evidence changed")
with (root / "transfer-summary.csv").open(newline="", encoding="utf-8") as source:
    transfers = list(csv.DictReader(source))
if len(transfers) != 2 or any(
    row["terminal_state"] != "COMPLETED"
    or row["terminal_reason"] != "RECEIVER_COMPLETED"
    or int(row["terminal_time_ns"]) < 0
    for row in transfers
):
    raise SystemExit("no-fault transfer terminal evidence changed")
PY

echo "SatCompute deterministic fault lifecycle regression passed."
