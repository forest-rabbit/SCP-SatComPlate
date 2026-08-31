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
f2_example="contrib/satcompute/input/examples/leo-66-1000s-f2"
all_compute_profile="contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json"
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
f2_trace="$regression_output/generate-f2-66/fault-trace.json"
f2_common="--simulationDuration=1000 --randomSeed=1 --randomRun=16 \
--constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
--orbitStartOffset=5695 --maxIslDistance=6171353 --delayMode=fixed \
--fixedDelay=0.008 --networkUpdateInterval=20 --islBandwidthBps=2000000000 \
--routingMode=global-capacity-aware-hrw --computeProfile=$all_compute_profile \
--taskTrace=$f2_example/task-trace.json --taskCompletionPolicy=report"
generate_f2_result="$(run_platform \
  "$regression_output/generate-f2-66" \
  "$f2_common --faultMode=generate --faultEnableF1=0 --faultEnableF2=1 \
--faultEnableF3=0 --faultTrace=$f2_trace")"
f2_second_trace="$regression_output/generate-f2-66-second/fault-trace.json"
generate_f2_second_result="$(run_platform \
  "$regression_output/generate-f2-66-second" \
  "$f2_common --faultMode=generate --faultEnableF1=0 --faultEnableF2=1 \
--faultEnableF3=0 --faultTrace=$f2_second_trace")"
replay_f2_result="$(run_platform \
  "$regression_output/replay-f2-66" \
  "$f2_common --faultMode=replay --faultEnableF1=0 --faultEnableF2=1 \
--faultEnableF3=0 --faultTrace=$f2_trace")"
combined_trace="$regression_output/generate-combined-66/fault-trace.json"
generate_combined_result="$(run_platform \
  "$regression_output/generate-combined-66" \
  "$f2_common --faultMode=generate --faultEnableF1=1 --faultEnableF2=1 \
--faultEnableF3=0 --faultTrace=$combined_trace")"
combined_second_trace="$regression_output/generate-combined-66-second/fault-trace.json"
generate_combined_second_result="$(run_platform \
  "$regression_output/generate-combined-66-second" \
  "$f2_common --faultMode=generate --faultEnableF1=1 --faultEnableF2=1 \
--faultEnableF3=0 --faultTrace=$combined_second_trace")"
replay_combined_result="$(run_platform \
  "$regression_output/replay-combined-66" \
  "$f2_common --faultMode=replay --faultEnableF1=1 --faultEnableF2=1 \
--faultEnableF3=0 --faultTrace=$combined_trace")"
f3_trace="$regression_output/generate-f3-66/fault-trace.json"
f3_common="--simulationDuration=1000 --randomSeed=1 --randomRun=1 \
--constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.008 \
--networkUpdateInterval=20 --islBandwidthBps=2000000000 \
--routingMode=global-first"
generate_f3_result="$(run_platform \
  "$regression_output/generate-f3-66" \
  "$f3_common --faultMode=generate --faultEnableF1=0 --faultEnableF2=0 \
--faultEnableF3=1 --faultTrace=$f3_trace")"
f3_second_trace="$regression_output/generate-f3-66-second/fault-trace.json"
generate_f3_second_result="$(run_platform \
  "$regression_output/generate-f3-66-second" \
  "$f3_common --faultMode=generate --faultEnableF1=0 --faultEnableF2=0 \
--faultEnableF3=1 --faultTrace=$f3_second_trace")"
replay_f3_result="$(run_platform \
  "$regression_output/replay-f3-66" \
  "$f3_common --faultMode=replay --faultTrace=$f3_trace")"
f3_priority_trace="$regression_output/generate-f3-priority/fault-trace.json"
f3_priority_common="$f1_66_common --randomSeed=1 --randomRun=106"
generate_f3_priority_result="$(run_platform \
  "$regression_output/generate-f3-priority" \
  "$f3_priority_common --faultMode=generate --faultEnableF1=1 \
--faultEnableF2=0 --faultEnableF3=1 --faultTrace=$f3_priority_trace")"
replay_f3_priority_result="$(run_platform \
  "$regression_output/replay-f3-priority" \
  "$f3_priority_common --faultMode=replay --faultTrace=$f3_priority_trace")"
all_faults_trace="$regression_output/generate-all-faults/fault-trace.json"
generate_all_faults_result="$(run_platform \
  "$regression_output/generate-all-faults" \
  "$f2_common --faultMode=generate --faultEnableF1=1 --faultEnableF2=1 \
--faultEnableF3=1 --faultTrace=$all_faults_trace")"
replay_all_faults_result="$(run_platform \
  "$regression_output/replay-all-faults" \
  "$f2_common --faultMode=replay --faultEnableF1=1 --faultEnableF2=1 \
--faultEnableF3=1 --faultTrace=$all_faults_trace")"

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
for result in "$generate_f2_result" "$generate_f2_second_result" "$replay_f2_result"; do
  if [[ "$result" != *'"status":"partial"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "66-star F2 generate/replay result differs: $result" >&2
    exit 1
  fi
done
for result in "$generate_combined_result" "$generate_combined_second_result" \
  "$replay_combined_result"; do
  if [[ "$result" != *'"status":"partial"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "66-star combined F1/F2 generate/replay result differs: $result" >&2
    exit 1
  fi
done
for result in "$generate_f3_result" "$generate_f3_second_result" "$replay_f3_result"; do
  if [[ "$result" != *'"status":"completed"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "66-star F3-only generate/replay result differs: $result" >&2
    exit 1
  fi
done
for result in "$generate_f3_priority_result" "$replay_f3_priority_result" \
  "$generate_all_faults_result" "$replay_all_faults_result"; do
  if [[ "$result" != *'"status":"partial"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "66-star combined F3 generate/replay result differs: $result" >&2
    exit 1
  fi
done

probability_audit_tool="contrib/satcompute/tools/validation/compare-fault-probabilities.py"
for scenario in f1-66 f2-66 combined-66; do
  case "$scenario" in
    f1-66)
      generate_directory="generate-f1-66"
      replay_directory="replay-f1-66"
      ;;
    f2-66)
      generate_directory="generate-f2-66"
      replay_directory="replay-f2-66"
      ;;
    combined-66)
      generate_directory="generate-combined-66"
      replay_directory="replay-combined-66"
      ;;
  esac
  python3 "$probability_audit_tool" \
    --model "$regression_output/$generate_directory/fault-model-probabilities.csv" \
    --prediction "$regression_output/$replay_directory/fault-predictions.csv" \
    --detail "$regression_output/probability-audit/$scenario.csv" \
    --summary "$regression_output/probability-audit/$scenario.json"
done

python3 - "$regression_output" <<'PY'
import csv
import json
import math
import pathlib
import sys

root = pathlib.Path(sys.argv[1])


def load_json(relative: str):
    with (root / relative).open(encoding="utf-8") as source:
        return json.load(source)


def load_csv(relative: str):
    with (root / relative).open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


PREDICTION_FIELDS = [
    "simulation_time_ns",
    "fault_id",
    "node_id",
    "task_id",
    "notice_time_ns",
    "risk_elapsed_time_ns",
    "task_compute_start_time_ns",
    "task_service_time_ns",
    "task_elapsed_time_ns",
    "remaining_compute_time_ns",
    "expected_compute_completion_time_ns",
    "completion_ratio",
    "f1_step_failure_probability",
    "f2_step_failure_probability",
    "combined_step_failure_probability",
    "horizon_step_count",
    "failure_before_finish_probability",
]
PREDICTION_SUMMARY_FIELDS = {
    "prediction_count",
    "risk_episode_count",
    "task_count",
}


def validate_prediction_outputs(directory: str, require_predictions: bool):
    prediction_path = root / directory / "fault-predictions.csv"
    with prediction_path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != PREDICTION_FIELDS:
            raise SystemExit(
                f"{directory} prediction schema differs: {reader.fieldnames}"
            )
        rows = list(reader)
    summary = load_json(f"{directory}/fault-prediction-summary.json")
    if set(summary) != PREDICTION_SUMMARY_FIELDS:
        raise SystemExit(f"{directory} prediction summary schema differs: {summary}")
    if require_predictions and not rows:
        raise SystemExit(f"{directory} produced no causal predictions")

    episode_ids = set()
    task_ids = set()
    for row in rows:
        simulation_time_ns = int(row["simulation_time_ns"])
        fault_id = int(row["fault_id"])
        task_id = int(row["task_id"])
        notice_time_ns = int(row["notice_time_ns"])
        risk_elapsed_time_ns = int(row["risk_elapsed_time_ns"])
        task_start_time_ns = int(row["task_compute_start_time_ns"])
        service_time_ns = int(row["task_service_time_ns"])
        elapsed_time_ns = int(row["task_elapsed_time_ns"])
        remaining_time_ns = int(row["remaining_compute_time_ns"])
        completion_time_ns = int(row["expected_compute_completion_time_ns"])
        completion_ratio = float(row["completion_ratio"])
        q_f1 = float(row["f1_step_failure_probability"])
        q_f2 = float(row["f2_step_failure_probability"])
        q_comp = float(row["combined_step_failure_probability"])
        horizon_step_count = int(row["horizon_step_count"])
        predicted_probability = float(row["failure_before_finish_probability"])

        if simulation_time_ns < notice_time_ns or (
            risk_elapsed_time_ns != simulation_time_ns - notice_time_ns
        ):
            raise SystemExit(f"{directory} used a non-causal NOTICE window: {row}")
        if task_start_time_ns > simulation_time_ns or service_time_ns <= 0 or (
            elapsed_time_ns < 0 or remaining_time_ns < 0
        ) or elapsed_time_ns + remaining_time_ns != service_time_ns or (
            completion_time_ns != simulation_time_ns + remaining_time_ns
        ):
            raise SystemExit(f"{directory} task snapshot differs: {row}")
        expected_ratio = elapsed_time_ns / service_time_ns
        if not (0.0 <= completion_ratio <= 1.0) or not math.isclose(
            completion_ratio, expected_ratio, rel_tol=1e-12, abs_tol=1e-12
        ):
            raise SystemExit(f"{directory} completion ratio differs: {row}")
        if not all(0.0 <= probability <= 1.0 for probability in (q_f1, q_f2, q_comp)) or (
            horizon_step_count < 1
        ):
            raise SystemExit(f"{directory} predictor input differs: {row}")
        expected_q_comp = 1.0 - (1.0 - q_f1) * (1.0 - q_f2)
        if not math.isclose(
            q_comp,
            expected_q_comp,
            rel_tol=1e-12,
            abs_tol=1e-12,
        ):
            raise SystemExit(f"{directory} combined probability differs: {row}")
        if horizon_step_count != remaining_time_ns // 1_000_000_000 + 1:
            raise SystemExit(f"{directory} prediction horizon differs: {row}")
        if not (q_comp <= predicted_probability <= 1.0):
            raise SystemExit(f"{directory} cumulative probability differs: {row}")
        episode_ids.add(fault_id)
        task_ids.add(task_id)

    if (
        summary["prediction_count"] != len(rows)
        or summary["risk_episode_count"] != len(episode_ids)
        or summary["task_count"] != len(task_ids)
    ):
        raise SystemExit(f"{directory} prediction summary counts differ: {summary}")
    return rows, summary


for scenario in ("f1-66", "f2-66", "combined-66"):
    audit = load_json(f"probability-audit/{scenario}.json")
    if set(audit) != {
        "absolute_tolerance",
        "model_record_count",
        "prediction_record_count",
        "matched_record_count",
        "missing_model_record_count",
        "missing_prediction_record_count",
        "context_mismatch_count",
        "probability_errors",
        "within_tolerance",
    }:
        raise SystemExit(f"{scenario} probability audit schema differs: {audit}")
    if (
        not audit["within_tolerance"]
        or audit["matched_record_count"] <= 0
        or audit["model_record_count"] != audit["matched_record_count"]
        or audit["prediction_record_count"] != audit["matched_record_count"]
        or audit["missing_model_record_count"] != 0
        or audit["missing_prediction_record_count"] != 0
        or audit["context_mismatch_count"] != 0
    ):
        raise SystemExit(f"{scenario} probability audit differs: {audit}")
    for field, errors in audit["probability_errors"].items():
        if set(errors) != {"mae", "rmse", "max_absolute_error"} or any(
            value is None or value > audit["absolute_tolerance"]
            for value in errors.values()
        ):
            raise SystemExit(
                f"{scenario} probability error differs for {field}: {errors}"
            )


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
if (root / "generate-f1/fault-model-probabilities.csv").read_bytes() != (
    root / "generate-f1-second/fault-model-probabilities.csv"
).read_bytes():
    raise SystemExit("same-seed F1 model probabilities are not byte-identical")

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
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    generated = (root / "generate-f1" / filename).read_bytes()
    replayed = (root / "replay-f1" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"F1 generate/replay output differs: {filename}")

validate_prediction_outputs("generate-f1", True)

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
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    generated = (root / "generate-f1-sampled" / filename).read_bytes()
    replayed = (root / "replay-f1-sampled" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"sampled F1 generate/replay output differs: {filename}")
validate_prediction_outputs("generate-f1-sampled", False)
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
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    generated = (root / "generate-f1-66" / filename).read_bytes()
    replayed = (root / "replay-f1-66" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"66-star F1 generate/replay output differs: {filename}")

predictions_66, prediction_summary_66 = validate_prediction_outputs(
    "generate-f1-66", True
)
if (
    len(predictions_66),
    prediction_summary_66["risk_episode_count"],
    prediction_summary_66["task_count"],
) != (41, 4, 7):
    raise SystemExit(
        f"66-star F1 prediction evidence differs: {prediction_summary_66}"
    )
if not any(int(row["risk_elapsed_time_ns"]) == 0 for row in predictions_66):
    raise SystemExit("66-star F1 predictions omitted the NOTICE boundary")
if not any(
    predictions_66[index]["combined_step_failure_probability"]
    != predictions_66[index - 1]["combined_step_failure_probability"]
    and predictions_66[index]["fault_id"] == predictions_66[index - 1]["fault_id"]
    for index in range(1, len(predictions_66))
):
    raise SystemExit("66-star F1 predictions froze q_comp within an episode")

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


f2_trace = load_json("generate-f2-66/fault-trace.json")
f2_faults = f2_trace["faults"]
if f2_trace["schema_version"] != 2 or len(f2_faults) != 8:
    raise SystemExit(f"66-star F2 trace shape differs: {f2_faults}")
f2_occurred = [fault for fault in f2_faults if fault["fault_occurred"]]
f2_risk_only = [fault for fault in f2_faults if not fault["fault_occurred"]]
if [
    (
        fault["node_id"],
        fault["notice_time_ns"],
        fault["start_time_ns"],
        fault["warning_lead_time_ns"],
        fault["duration_ns"],
    )
    for fault in f2_occurred
] != [
    (51, None, 386_000_000_000, None, 8_000_000_000),
]:
    raise SystemExit(f"66-star F2 occurred faults differ: {f2_occurred}")
if len(f2_risk_only) != 7 or any(
    fault["notice_time_ns"] is None or fault["risk_duration_ns"] <= 0
    for fault in f2_risk_only
):
    raise SystemExit(f"66-star F2 risk-only episodes differ: {f2_risk_only}")
if (root / "generate-f2-66/fault-trace.json").read_bytes() != (
    root / "generate-f2-66-second/fault-trace.json"
).read_bytes():
    raise SystemExit("same-seed F2 generated traces are not byte-identical")
if (root / "generate-f2-66/fault-model-probabilities.csv").read_bytes() != (
    root / "generate-f2-66-second/fault-model-probabilities.csv"
).read_bytes():
    raise SystemExit("same-seed F2 model probabilities are not byte-identical")

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
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    generated = (root / "generate-f2-66" / filename).read_bytes()
    replayed = (root / "replay-f2-66" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"F2 generate/replay output differs: {filename}")

validate_prediction_outputs("generate-f2-66", True)

f2_events = load_csv("generate-f2-66/fault-events.csv")
f2_start_events = [row for row in f2_events if row["event_type"] == "START"]
f2_recovery_events = [row for row in f2_events if row["event_type"] == "RECOVERY"]
if [
    (int(row["node_id"]), int(row["simulation_time_ns"]))
    for row in f2_start_events
] != [(51, 386_000_000_000)]:
    raise SystemExit("F2 START events differ")
if [int(row["simulation_time_ns"]) for row in f2_recovery_events] != [
    394_000_000_000,
]:
    raise SystemExit("F2 recovery duration differs")
if any(row["route_recomputed"] != "false" for row in f2_events):
    raise SystemExit("F2 compute fault unexpectedly recomputed routes")

f2_tasks = {
    int(row["task_id"]): row
    for row in load_csv("generate-f2-66/task-summary.csv")
}
if sorted(
    task_id
    for task_id, row in f2_tasks.items()
    if row["final_state"] == "FAILED"
) != [1]:
    raise SystemExit("F2 did not fail the active hotspot task")
if any(f2_tasks[task_id]["final_state"] != "COMPLETED" for task_id in (2, 3, 4, 5, 6, 7, 8)):
    raise SystemExit("F2 post-recovery, risk-only, or sparse task did not complete")
f2_run = load_json("generate-f2-66/run-summary.json")
if (
    f2_run["route_computation_count"],
    f2_run["applied_topology_slice_count"],
    f2_run["task_count"],
    f2_run["completed_task_count"],
) != (1, 50, 8, 7):
    raise SystemExit("66-star F2 changed topology, routing, or task counts")


combined_trace = load_json("generate-combined-66/fault-trace.json")
combined_faults = combined_trace["faults"]
combined_occurred = [
    fault for fault in combined_faults if fault["fault_occurred"]
]
if len(combined_faults) != 9 or [
    (fault["node_id"], fault["start_time_ns"])
    for fault in combined_occurred
] != [
    (51, 386_000_000_000),
    (29, 656_000_000_000),
]:
    raise SystemExit(f"combined F1/F2 fault trace differs: {combined_faults}")
if combined_occurred[0]["failure_probability"] <= f2_occurred[0]["failure_probability"]:
    raise SystemExit("combined trace did not expose q_comp")
if len({
    (fault["node_id"], fault["start_time_ns"])
    for fault in combined_occurred
}) != len(combined_occurred):
    raise SystemExit("combined F1/F2 source hits were not coalesced")
if (root / "generate-combined-66/fault-trace.json").read_bytes() != (
    root / "generate-combined-66-second/fault-trace.json"
).read_bytes():
    raise SystemExit("same-seed combined F1/F2 traces are not byte-identical")
if (root / "generate-combined-66/fault-model-probabilities.csv").read_bytes() != (
    root / "generate-combined-66-second/fault-model-probabilities.csv"
).read_bytes():
    raise SystemExit(
        "same-seed combined F1/F2 model probabilities are not byte-identical"
    )
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
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    generated = (root / "generate-combined-66" / filename).read_bytes()
    replayed = (root / "replay-combined-66" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"combined F1/F2 generate/replay output differs: {filename}")

validate_prediction_outputs("generate-combined-66", True)


f3_trace = load_json("generate-f3-66/fault-trace.json")
if f3_trace["schema_version"] != 2 or len(f3_trace["faults"]) != 1:
    raise SystemExit(f"F3-only trace shape differs: {f3_trace}")
f3_fault = f3_trace["faults"][0]
if (
    f3_fault["node_id"],
    f3_fault["fault_type"],
    f3_fault["fault_occurred"],
    f3_fault["start_time_ns"],
    f3_fault["notice_time_ns"],
    f3_fault["failure_probability"],
    f3_fault["duration_ns"],
) != (62, "satellite", True, 33_469_258_100, None, None, None):
    raise SystemExit(f"F3-only permanent fault differs: {f3_fault}")
if (root / "generate-f3-66/fault-trace.json").read_bytes() != (
    root / "generate-f3-66-second/fault-trace.json"
).read_bytes():
    raise SystemExit("same-seed F3 generated traces are not byte-identical")
for filename in ("fault-events.csv", "fault-summary.json", "ecmp-route-events.csv"):
    generated = (root / "generate-f3-66" / filename).read_bytes()
    replayed = (root / "replay-f3-66" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"F3-only generate/replay output differs: {filename}")
f3_events = load_csv("generate-f3-66/fault-events.csv")
if len(f3_events) != 1 or (
    f3_events[0]["event_type"],
    f3_events[0]["route_recomputed"],
    f3_events[0]["satellite_available_after"],
    f3_events[0]["communication_available_after"],
    f3_events[0]["compute_available_after"],
) != ("START", "true", "false", "false", "false"):
    raise SystemExit(f"F3-only runtime event differs: {f3_events}")
f3_summary = load_json("generate-f3-66/fault-summary.json")
if (
    f3_summary["satellite_fault_count"],
    f3_summary["compute_fault_count"],
    f3_summary["start_event_count"],
    f3_summary["recovery_event_count"],
    f3_summary["active_fault_count_at_end"],
) != (1, 0, 1, 0, 1):
    raise SystemExit(f"F3-only summary differs: {f3_summary}")


priority_trace = load_json("generate-f3-priority/fault-trace.json")
priority_compute = next(
    fault
    for fault in priority_trace["faults"]
    if fault["fault_id"] == 3
)
priority_satellite = next(
    fault
    for fault in priority_trace["faults"]
    if fault["fault_type"] == "satellite"
)
if (
    priority_compute["node_id"],
    priority_compute["start_time_ns"],
    priority_compute["duration_ns"],
) != (11, 66_000_000_000, 7_070_766_227):
    raise SystemExit(f"F3 did not shorten the active compute interval: {priority_compute}")
if (
    priority_satellite["node_id"],
    priority_satellite["start_time_ns"],
) != (11, 73_070_766_227):
    raise SystemExit(f"F3 priority target differs: {priority_satellite}")
priority_events = load_csv("generate-f3-priority/fault-events.csv")
priority_same_time = [
    row["event_type"]
    for row in priority_events
    if int(row["simulation_time_ns"]) == 73_070_766_227
]
if priority_same_time != ["RECOVERY", "START"]:
    raise SystemExit(f"F3 priority event order differs: {priority_same_time}")
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
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    generated = (root / "generate-f3-priority" / filename).read_bytes()
    replayed = (root / "replay-f3-priority" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"F3 priority generate/replay output differs: {filename}")

validate_prediction_outputs("generate-f3-priority", True)


all_faults_trace = load_json("generate-all-faults/fault-trace.json")
if not any(fault["fault_type"] == "satellite" for fault in all_faults_trace["faults"]):
    raise SystemExit("combined F1/F2/F3 trace has no F3 event")
if not any(
    fault["fault_type"] == "compute" and fault["fault_occurred"]
    for fault in all_faults_trace["faults"]
):
    raise SystemExit("combined F1/F2/F3 trace has no compute event")
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
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    generated = (root / "generate-all-faults" / filename).read_bytes()
    replayed = (root / "replay-all-faults" / filename).read_bytes()
    if generated != replayed:
        raise SystemExit(f"combined F1/F2/F3 generate/replay output differs: {filename}")

validate_prediction_outputs("generate-all-faults", True)


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
    "fault-predictions.csv",
    "fault-prediction-summary.json",
):
    first = (root / "satellite-first" / filename).read_bytes()
    second = (root / "satellite-second" / filename).read_bytes()
    if first != second:
        raise SystemExit(f"repeated satellite fault output differs: {filename}")

validate_prediction_outputs("compute", False)
validate_prediction_outputs("satellite-first", False)
PY

no_fault_result="$(run_platform \
  "$regression_output/satellite-first" \
  "$common --taskTrace=$task_inputs/task-single.json")"
if [[ "$no_fault_result" != *'"status":"completed"'* ]]; then
  echo "no-fault reuse regression failed: $no_fault_result" >&2
  exit 1
fi
if [[ -e "$regression_output/satellite-first/fault-events.csv" ||
      -e "$regression_output/satellite-first/fault-summary.json" ||
      -e "$regression_output/satellite-first/fault-predictions.csv" ||
      -e "$regression_output/satellite-first/fault-prediction-summary.json" ||
      -e "$regression_output/satellite-first/fault-model-probabilities.csv" ]]; then
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
