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
  ./ns3 run --no-build "satcompute --faultMode=none --computeProfile=none --taskTrace=none --faultEnableF2=0 --faultEnableF3=0 --faultF3Mode=fixed_k --randomRun=1 --linkMetrics=0 --outputDir=$output_directory $*"
}

constellation="contrib/satcompute/tests/fixtures/constellation/connected-16.csv"
task_inputs="contrib/satcompute/tests/fixtures/task"
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
probability_audit="--faultProbabilityAudit=1"

./ns3 run --no-build \
  "satcompute-f1-calibration \
--outputDir=$regression_output/f1-calibration" >/dev/null

default_generate_trace="$regression_output/generate-default/fault-trace.json"
run_platform \
  "$regression_output/generate-default" \
  "$common --taskTrace=$fault_task --faultMode=generate \
--faultProbabilityAudit=1 --faultTrace=$default_generate_trace" >/dev/null
default_generate_result="$(run_platform \
  "$regression_output/generate-default" \
  "$common --taskTrace=$fault_task --faultMode=generate \
--faultTrace=$default_generate_trace")"
# Executor edge cases use test-only ns event injection, never file input.
f1_generated_trace="$regression_output/generate-f1/fault-trace.json"
f1_common="--simulationDuration=90 --constellationConfig=$constellation \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.001 \
--networkUpdateInterval=20 --islBandwidthBps=100000000 \
--routingMode=global-capacity-aware-hrw --computeProfile=$profile \
--taskTrace=$task_inputs/task-f1-critical.json --taskCompletionPolicy=report \
$probability_audit"
generate_f1_result="$(run_platform \
  "$regression_output/generate-f1" \
  "$f1_common --faultMode=generate \
--faultTrace=$f1_generated_trace")"
f1_second_trace="$regression_output/generate-f1-second/fault-trace.json"
generate_f1_second_result="$(run_platform \
  "$regression_output/generate-f1-second" \
  "$f1_common --faultMode=generate \
--faultTrace=$f1_second_trace")"
repeat_f1_result="$(run_platform \
  "$regression_output/repeat-f1" \
  "$f1_common --faultMode=generate \
--faultTrace=$regression_output/repeat-f1/fault-trace.json")"
f1_sampled_trace="$regression_output/generate-f1-sampled/fault-trace.json"
f1_sampled_common="--simulationDuration=80 --randomRun=64 \
--constellationConfig=$constellation --maxIslDistance=6171353 \
--delayMode=fixed --fixedDelay=0.001 --networkUpdateInterval=20 \
--islBandwidthBps=100000000 --routingMode=global-capacity-aware-hrw \
--computeProfile=$profile --taskTrace=$task_inputs/task-f1-risk-window.json \
--taskCompletionPolicy=report $probability_audit"
generate_f1_sampled_result="$(run_platform \
  "$regression_output/generate-f1-sampled" \
  "$f1_sampled_common --faultMode=generate \
--faultTrace=$f1_sampled_trace")"
repeat_f1_sampled_result="$(run_platform \
  "$regression_output/repeat-f1-sampled" \
  "$f1_sampled_common --faultMode=generate \
--faultTrace=$regression_output/repeat-f1-sampled/fault-trace.json")"
f1_66_trace="$regression_output/generate-f1-66/fault-trace.json"
f1_66_common="--simulationDuration=120 \
--constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
--maxIslDistance=6171353 --delayMode=fixed --fixedDelay=0.008 \
--networkUpdateInterval=20 --islBandwidthBps=2000000000 \
--routingMode=global-capacity-aware-hrw \
--computeProfile=contrib/satcompute/input/topology/resources/workload/xw-66sat-static-2g-all-compute-profile.json \
--taskTrace=$f1_example/task-trace.json --taskCompletionPolicy=report \
$probability_audit"
generate_f1_66_result="$(run_platform \
  "$regression_output/generate-f1-66" \
  "$f1_66_common --faultMode=generate \
--faultTrace=$f1_66_trace")"
repeat_f1_66_result="$(run_platform \
  "$regression_output/repeat-f1-66" \
  "$f1_66_common --faultMode=generate \
--faultTrace=$regression_output/repeat-f1-66/fault-trace.json")"
f2_trace="$regression_output/generate-f2-66/fault-trace.json"
f2_common="--simulationDuration=1000 --randomSeed=1 --randomRun=16 \
--constellationConfig=contrib/satcompute/input/topology/constellations/synthetic-66.csv \
--orbitStartOffset=302 --maxIslDistance=6171353 --delayMode=fixed \
--fixedDelay=0.008 --networkUpdateInterval=20 --islBandwidthBps=2000000000 \
--routingMode=global-capacity-aware-hrw --computeProfile=$all_compute_profile \
--taskTrace=$f2_example/task-trace.json --taskCompletionPolicy=report \
$probability_audit"
generate_f2_result="$(run_platform \
  "$regression_output/generate-f2-66" \
  "$f2_common --faultMode=generate --faultEnableF1=0 --faultEnableF2=1 \
--faultEnableF3=0 --faultTrace=$f2_trace")"
f2_second_trace="$regression_output/generate-f2-66-second/fault-trace.json"
generate_f2_second_result="$(run_platform \
  "$regression_output/generate-f2-66-second" \
  "$f2_common --faultMode=generate --faultEnableF1=0 --faultEnableF2=1 \
--faultEnableF3=0 --faultTrace=$f2_second_trace")"
repeat_f2_result="$(run_platform \
  "$regression_output/repeat-f2-66" \
  "$f2_common --faultMode=generate --faultEnableF1=0 --faultEnableF2=1 \
--faultEnableF3=0 --faultTrace=$regression_output/repeat-f2-66/fault-trace.json")"
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
repeat_combined_result="$(run_platform \
  "$regression_output/repeat-combined-66" \
  "$f2_common --faultMode=generate --faultEnableF1=1 --faultEnableF2=1 \
--faultEnableF3=0 --faultTrace=$regression_output/repeat-combined-66/fault-trace.json")"
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
repeat_f3_result="$(run_platform \
  "$regression_output/repeat-f3-66" \
  "$f3_common --faultMode=generate --faultEnableF1=0 --faultEnableF2=0 \
--faultEnableF3=1 --faultTrace=$regression_output/repeat-f3-66/fault-trace.json")"
f3_priority_trace="$regression_output/generate-f3-priority/fault-trace.json"
f3_priority_common="$f1_66_common --randomSeed=1 --randomRun=106"
generate_f3_priority_result="$(run_platform \
  "$regression_output/generate-f3-priority" \
  "$f3_priority_common --faultMode=generate --faultEnableF1=1 \
--faultEnableF2=0 --faultEnableF3=1 --faultTrace=$f3_priority_trace")"
repeat_f3_priority_result="$(run_platform \
  "$regression_output/repeat-f3-priority" \
  "$f3_priority_common --faultMode=generate --faultEnableF1=1 \
--faultEnableF2=0 --faultEnableF3=1 --faultTrace=$regression_output/repeat-f3-priority/fault-trace.json")"
all_faults_trace="$regression_output/generate-all-faults/fault-trace.json"
generate_all_faults_result="$(run_platform \
  "$regression_output/generate-all-faults" \
  "$f2_common --faultMode=generate --faultEnableF1=1 --faultEnableF2=1 \
--faultEnableF3=1 --faultTrace=$all_faults_trace")"
repeat_all_faults_result="$(run_platform \
  "$regression_output/repeat-all-faults" \
  "$f2_common --faultMode=generate --faultEnableF1=1 --faultEnableF2=1 \
--faultEnableF3=1 --faultTrace=$regression_output/repeat-all-faults/fault-trace.json")"

if [[ "$default_generate_result" != *'"status":"partial"'* ]]; then
  echo "default generated run did not report its truncated task" >&2
  exit 1
fi
for result in "$generate_f1_result" "$generate_f1_second_result" "$repeat_f1_result"; do
  if [[ "$result" != *'"status":"partial"'* ]]; then
    echo "F1 generate/repeat did not report the intentionally failed old task: $result" >&2
    exit 1
  fi
done
for result in "$generate_f1_sampled_result" "$repeat_f1_sampled_result"; do
  if [[ "$result" != *'"status":"partial"'* ]]; then
    echo "sampled F1 generate/repeat missed its controlled branch: $result" >&2
    exit 1
  fi
done
for result in "$generate_f1_66_result" "$repeat_f1_66_result"; do
  if [[ "$result" != *'"status":"partial"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "66-star F1 generate/repeat result differs: $result" >&2
    exit 1
  fi
done
for result in "$generate_f2_result" "$generate_f2_second_result" "$repeat_f2_result"; do
  if [[ "$result" != *'"status":"partial"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "66-star F2 generate/repeat result differs: $result" >&2
    exit 1
  fi
done
for result in "$generate_combined_result" "$generate_combined_second_result" \
  "$repeat_combined_result"; do
  if [[ "$result" != *'"status":"partial"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "66-star combined F1/F2 generate/repeat result differs: $result" >&2
    exit 1
  fi
done
for result in "$generate_f3_result" "$generate_f3_second_result" "$repeat_f3_result"; do
  if [[ "$result" != *'"status":"completed"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "66-star F3-only generate/repeat result differs: $result" >&2
    exit 1
  fi
done
for result in "$generate_f3_priority_result" "$repeat_f3_priority_result" \
  "$generate_all_faults_result" "$repeat_all_faults_result"; do
  if [[ "$result" != *'"status":"partial"'* ||
        "$result" != *'"satellite_count":66'* ]]; then
    echo "66-star combined F3 generate/repeat result differs: $result" >&2
    exit 1
  fi
done

probability_audit_tool="contrib/satcompute/tools/validation/compare-fault-probabilities.py"
for scenario in f1-66 f2-66 combined-66; do
  case "$scenario" in
    f1-66)
      generate_directory="generate-f1-66"
      repeat_directory="repeat-f1-66"
      ;;
    f2-66)
      generate_directory="generate-f2-66"
      repeat_directory="repeat-f2-66"
      ;;
    combined-66)
      generate_directory="generate-combined-66"
      repeat_directory="repeat-combined-66"
      ;;
  esac
  python3 "$probability_audit_tool" \
    --model "$regression_output/$generate_directory/fault-model-probabilities.csv" \
    --prediction "$regression_output/$repeat_directory/fault-predictions.csv" \
    --detail "$regression_output/probability-audit/$scenario.csv" \
    --summary "$regression_output/probability-audit/$scenario.json"
done

python3 - "$regression_output" <<'PY'
import csv
import json
from pathlib import Path
import runpy
import sys
root = Path(sys.argv[1])
check = runpy.run_path("contrib/satcompute/tests/support/fault-run-audit.py")["audit"]
cal = json.loads((root / "f1-calibration/n4b-f1-calibration-summary.json").read_text())
assert cal["heating_to_critical_s"] == 30 and cal["cooling_from_critical_to_base_s"] == 4
for gamma in (1.5, 2.):
    expected = (5**(1-gamma) - 18**(1-gamma)) / ((gamma-1)*30)
    assert abs(cal["derived_heating_coefficients_by_gamma"][f"{gamma:.6f}"] - expected) < 1e-14
assert cal["derived_cooling_rate_c_per_s"] == 3.25
with (root / "f1-calibration/n4b-f1-calibration.csv").open() as stream:
    references = list(csv.DictReader(stream))
assert len(references) == 548
assert {(r["beta"], r["gamma"]) for r in references} == {
    ("8", "1.5"), ("8", "2"), ("10", "1.5"), ("10", "2")}
assert all(abs(float(r["temperature_c"])-30) < 1e-10 for r in references
           if r["phase"] == "heating" and float(r["elapsed_s"]) == 30)
core = ("fault-trace.json", "fault-events.csv", "fault-summary.json", "fault-task-impact.csv",
        "task-events.csv", "task-summary.csv", "transfer-summary.csv", "ecmp-route-events.csv",
        "size-aware-reservation-events.csv", "size-aware-summary.json", "capacity-aware-summary.json",
        "fault-predictions.csv", "fault-model-probabilities.csv", "fault-model-state.csv",
        "fault-prediction-summary.json")
for directory in sorted(root.glob("generate-*")):
    if directory.is_dir():
        result = check(directory, allow_truncated=directory.name == "generate-default")
        suffix = directory.name.removeprefix("generate-")
        repeat = root / ("repeat-" + suffix)
        if repeat.is_dir():
            check(repeat)
            for filename in core:
                a, b = directory / filename, repeat / filename
                assert a.exists() == b.exists(), (suffix, filename, "presence differs")
                if a.exists():
                    assert a.read_bytes() == b.read_bytes(), (suffix, filename, "repeat differs")
        second = root / (directory.name + "-second")
        if second.is_dir():
            assert (directory / "fault-trace.json").read_bytes() == (second / "fault-trace.json").read_bytes()
        if suffix in ("f1", "f1-sampled", "f1-66"):
            assert any(f["f1_occurred"] for f in result["faults"]), suffix
        if suffix in ("f3-66", "f3-priority", "all-faults"):
            assert sum(f["fault_type"] == "satellite" for f in result["faults"]) == 1, suffix
f2 = check(root / "generate-f2-66")
assert [(f["node_id"], f["start_time_ns"], f["duration_ns"]) for f in f2["faults"]] == [
    (17, 236_000_000_000, 8_000_000_000), (16, 850_000_000_000, 8_000_000_000)]
for scenario in ("f1-66", "f2-66", "combined-66"):
    comparison = json.loads((root / f"probability-audit/{scenario}.json").read_text())
    assert comparison["within_tolerance"] and comparison["matched_record_count"] > 0
for filename in ("fault-model-probabilities.csv", "fault-model-state.csv",
                 "fault-predictions.csv", "fault-prediction-summary.json"):
    assert not (root / "generate-default" / filename).exists(), filename

PY

no_fault_result="$(run_platform \
  "$regression_output/generate-default" \
  "$common --taskTrace=$task_inputs/task-single.json")"
if [[ "$no_fault_result" != *'"status":"completed"'* ]]; then
  echo "no-fault reuse regression failed: $no_fault_result" >&2
  exit 1
fi
if [[ -e "$regression_output/generate-default/fault-events.csv" ||
      -e "$regression_output/generate-default/fault-summary.json" ||
      -e "$regression_output/generate-default/fault-task-impact.csv" ||
      -e "$regression_output/generate-default/fault-model-state.csv" ||
      -e "$regression_output/generate-default/fault-predictions.csv" ||
      -e "$regression_output/generate-default/fault-prediction-summary.json" ||
      -e "$regression_output/generate-default/fault-model-probabilities.csv" ]]; then
  echo "no-fault run retained stale fault metrics" >&2
  exit 1
fi
python3 - "$regression_output/generate-default" <<'PY'
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

if ./ns3 run --no-build "satcompute --faultMode=replay --outputDir=$regression_output/rejected" >"$regression_output/rejected.log" 2>&1; then
  echo "removed replay mode was accepted" >&2
  exit 1
fi
if ! rg -q 'faultMode has an unsupported value: replay' "$regression_output/rejected.log"; then
  echo "removed mode was not rejected by configuration validation" >&2
  exit 1
fi

echo "SatCompute generated fault lifecycle regression passed."
