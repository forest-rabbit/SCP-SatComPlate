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
common="--simulationDuration=1 --constellationConfig=$constellation \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-capacity-aware-hrw --computeProfile=$profile \
--taskCompletionPolicy=report"
network_common="--simulationDuration=1 --constellationConfig=$constellation \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--routingMode=global-first"

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
generated_trace="$regression_output/generate-empty/fault-trace.json"
generate_empty_result="$(run_platform \
  "$regression_output/generate-empty" \
  "$network_common --faultMode=generate \
--faultModelConfig=$fault_inputs/model-all-disabled.json \
--faultTrace=$generated_trace")"
replay_empty_result="$(run_platform \
  "$regression_output/replay-empty" \
  "$network_common --faultMode=replay --faultTrace=$generated_trace")"

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
for result in "$generate_empty_result" "$replay_empty_result"; do
  if [[ "$result" != *'"status":"completed"'* ]]; then
    echo "empty generate/replay mode run failed: $result" >&2
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


generated_trace = load_json("generate-empty/fault-trace.json")
if generated_trace != {"schema_version": 2, "faults": []}:
    raise SystemExit(f"empty generated trace differs: {generated_trace}")
if load_csv("generate-empty/fault-events.csv") or load_csv("replay-empty/fault-events.csv"):
    raise SystemExit("empty generate/replay unexpectedly emitted runtime fault events")


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
