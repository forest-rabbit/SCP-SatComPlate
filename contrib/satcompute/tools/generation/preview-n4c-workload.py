#!/usr/bin/env python3
"""Write an offline G1 budget preview, never a runnable TaskTrace or fault input."""

import argparse
from dataclasses import asdict
from fractions import Fraction
import csv
import json
from pathlib import Path
import runpy
import subprocess
import sys

from task_workload_model import (
    IMAGE_REFERENCES, QWEN_CONFIG_URL, TASK_MODELING_COMMIT, TASK_PROFILES,
    LlmParameters, TaskBudget, ceil_div, checkpoint_budgets, image_budget, image_reference,
    llm_budget, require_uint, service_time_ns, uniform_unit_ends,
)


GENERATOR = runpy.run_path(str(Path(__file__).with_name("generate-task-workload.py")))
STABLE_VALUE = GENERATOR["deterministic_value"]
ALLOCATE = GENERATOR["bounded_weighted_allocation"]
LARGEST_REMAINDER = GENERATOR["largest_remainder"]
TASK_COUNT = 1500
TOTAL_INPUT_BYTES = 81_750_000_000
COUNTS = dict(zip(TASK_PROFILES, (450, 450, 450, 150)))
REFERENCE_RATE = 20_000
REFERENCE_LLM_WU_PER_TOKEN = LlmParameters().work_units_per_token
COMPUTE_NODE_COUNT = 66
REFERENCE_TASK_LABELS = {
    "dense-image": "dense-image-small-001",
    "sparse-inference": "sparse-inference-extended-granularity",
    "compression": "compression-small-001",
}


def json_text(value: object) -> str:
    """Serialize evidence deterministically without timestamps or absolute paths."""
    return json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n"


def preview_attributes(seed: str) -> list[dict]:
    """Allocate only task attributes; endpoints/deadline/arrival remain G2 work."""
    if not isinstance(seed, str) or not seed:
        raise ValueError("seed must be a non-empty string")
    ids = sorted(range(1, TASK_COUNT + 1), key=lambda tid: (STABLE_VALUE(seed, tid, "n4c-class"), tid))
    attributes = {}
    offset = 0
    for profile, count in COUNTS.items():
        for task_id in ids[offset:offset + count]:
            attributes[task_id] = {"task_id": task_id, "task_profile": profile}
        offset += count

    for task_id, task in attributes.items():
        if task["task_profile"] != "llm":
            continue
        prompt = 128 + STABLE_VALUE(seed, task_id, "n4c-prompt-tokens") % 129
        total = 1000 + STABLE_VALUE(seed, task_id, "n4c-total-tokens") % 1001
        repeats = 10 + STABLE_VALUE(seed, task_id, "n4c-request-length") % 21
        request = {
            "prompt": "Summarize these observations: " + "cloud, coast, vegetation; " * repeats,
            "max_new_tokens": total - prompt,
        }
        # This is a real serialized synthetic request, not tokenizer evidence.
        request_text = json.dumps(request, sort_keys=True, separators=(",", ":"))
        task.update(input_bytes=len(request_text.encode("utf-8")), prompt_tokens=prompt,
                    generation_tokens=total - prompt, request_json=request_text)

    tail_ids = set()
    tail_offset = {"compression": 0, "dense-image": 0}
    for size, count in ((1_000_000_000, 15), (500_000_000, 30)):
        shares = LARGEST_REMAINDER(count, {"compression": 7500, "dense-image": 2500},
                                  ("compression", "dense-image"))
        for profile, assigned_count in shares.items():
            candidates = sorted((tid for tid, task in attributes.items()
                                 if task["task_profile"] == profile),
                                key=lambda tid: (STABLE_VALUE(seed, tid, "n4c-tail"), tid))
            start = tail_offset[profile]
            for tid in candidates[start:start + assigned_count]:
                attributes[tid]["input_bytes"] = size
                tail_ids.add(tid)
            tail_offset[profile] += assigned_count

    ordinary = sorted(tid for tid, task in attributes.items()
                      if task["task_profile"] != "llm" and tid not in tail_ids)
    remaining = TOTAL_INPUT_BYTES - sum(task.get("input_bytes", 0) for task in attributes.values())
    weights = [10 + STABLE_VALUE(seed, tid, "n4c-input-weight") % 91 for tid in ordinary]
    sizes = ALLOCATE(remaining, weights, 1 << 20, 300_000_000, ordinary)
    for tid, size in zip(ordinary, sizes):
        attributes[tid]["input_bytes"] = size

    # Four-band uint16 raw arrays have eight bytes per logical pixel. Preserve
    # the exact global budget by assigning alignment remainders to encoded files.
    remainder = 0
    for tid in ordinary:
        task = attributes[tid]
        if task["task_profile"] in ("dense-image", "compression"):
            extra = task["input_bytes"] % 8
            task["input_bytes"] -= extra
            remainder += extra
    for tid in ordinary:
        task = attributes[tid]
        if task["task_profile"] == "sparse-inference":
            extra = min(remainder, 300_000_000 - task["input_bytes"])
            task["input_bytes"] += extra
            remainder -= extra
    if remainder:
        raise ValueError("raw-array alignment cannot preserve the input budget within file bounds")
    result = [attributes[tid] for tid in sorted(attributes)]
    if sum(task["input_bytes"] for task in result) != TOTAL_INPUT_BYTES:
        raise AssertionError("preview input allocation lost bytes")
    return result


def task_budget(task: dict, llm_parameters: LlmParameters) -> TaskBudget:
    """Map one attribute record without consulting any other task."""
    if task["task_profile"] == "llm":
        return llm_budget(task["input_bytes"], task["prompt_tokens"],
                          task["generation_tokens"], llm_parameters)
    return image_budget(task["task_profile"], task["input_bytes"], str(task["task_id"]))


def preview_unit_ends(budget: TaskBudget) -> tuple[tuple[int, ...], str]:
    """Freeze disclosed layout assumptions, not measurements of new images."""
    if budget.task_profile == "llm":
        return uniform_unit_ends(budget.extent, 1), "synthetic-completed-token"
    if budget.task_profile == "sparse-inference":
        # The reference has 100 files / 26,246,291 B. A preview uses equal-sized
        # synthetic files; an actual file list must supply its own legal ends.
        reference = image_reference("sparse-inference")
        count = min(budget.extent, ceil_div(budget.extent * 100, reference.input_bytes))
        base, remainder = divmod(budget.extent, count)
        ends = tuple(index * base + min(index, remainder) for index in range(1, count + 1))
        return ends, "synthetic-equal-files-reference-mean-size"
    return uniform_unit_ends(budget.extent, 524_288), "raw-524288-byte-tiles-with-final-edge"


def distribution(values: list[int | float]) -> dict:
    """Linearly interpolated descriptive quantiles, not confidence intervals."""
    if not values:
        raise ValueError("distribution requires samples")
    ordered = sorted(values)

    def quantile(numerator: int, denominator: int) -> float:
        position = Fraction((len(ordered) - 1) * numerator, denominator)
        lower = position.numerator // position.denominator
        upper = min(lower + 1, len(ordered) - 1)
        return float(ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower))

    return {"min": ordered[0], "median": quantile(1, 2),
            "p95": quantile(95, 100), "max": ordered[-1]}


def budget_row(task_id: int | str, budget: TaskBudget) -> dict:
    """Keep exact rational coefficients alongside their display approximations."""
    sigma = budget.sigma_variable_bytes_per_work_unit
    rho = budget.rho_variable
    return {
        "task_id": task_id, "task_profile": budget.task_profile,
        "input_bytes": budget.input_bytes, "output_bytes": budget.output_bytes,
        "compute_work_units": budget.compute_work_units,
        "k_payload_bytes": budget.payload_bytes, "k_index_bytes": budget.index_bytes,
        "k_variable_bytes": budget.k_variable_bytes, "h_bytes_per_checkpoint": budget.header_bytes,
        "rho_variable": None if rho is None else float(rho),
        "sigma_variable_bytes_per_work_unit": float(sigma),
        "sigma_variable_numerator": sigma.numerator, "sigma_variable_denominator": sigma.denominator,
    }


def summarize_attributes(attributes: list[dict], reference_rate: int = REFERENCE_RATE,
                         llm_wu_per_token: int = REFERENCE_LLM_WU_PER_TOKEN) -> tuple[dict, list[dict]]:
    """Produce service demand estimates; do not simulate queues or thermal state."""
    require_uint(reference_rate, "reference_rate", 1)
    parameters = LlmParameters(work_units_per_token=llm_wu_per_token)
    rows = []
    ids = set()
    for task in sorted(attributes, key=lambda item: item["task_id"]):
        tid = require_uint(task["task_id"], "task_id", 1)
        if tid in ids:
            raise ValueError("duplicate preview task ID")
        ids.add(tid)
        budget = task_budget(task, parameters)
        ends, layout = preview_unit_ends(budget)
        rows.append({**budget_row(tid, budget), "prompt_tokens": task.get("prompt_tokens"),
                     "generation_tokens": task.get("generation_tokens"),
                     "cached_tokens": budget.extent if budget.task_profile == "llm" else None,
                     "reference_service_time_ns": service_time_ns(budget.compute_work_units, reference_rate),
                     "preview_compute_node_id": (tid - 1) % COMPUTE_NODE_COUNT,
                     "input_representation": next((ref.input_representation for ref in IMAGE_REFERENCES
                                                    if ref.profile == budget.task_profile),
                                                   "utf8-serialized-synthetic-request"),
                     "legal_unit_count": len(ends), "legal_unit_assumption": layout})
    if not rows:
        raise ValueError("attributes must be non-empty")

    class_summaries = {}
    for profile in TASK_PROFILES:
        group = [row for row in rows if row["task_profile"] == profile]
        if not group:
            continue
        seconds = [row["reference_service_time_ns"] / 1e9 for row in group]
        class_summaries[profile] = {
            "count": len(group),
            "input_bytes": sum(row["input_bytes"] for row in group),
            "output_bytes": sum(row["output_bytes"] for row in group),
            "work_units": distribution([row["compute_work_units"] for row in group]),
            "total_work_units": sum(row["compute_work_units"] for row in group),
            "service_time_seconds": distribution(seconds),
            "service_below_one_second_count": sum(value < 1 for value in seconds),
            "service_below_two_seconds_count": sum(value < 2 for value in seconds),
        }
    nodes = []
    for node_id in range(COMPUTE_NODE_COUNT):
        group = [row for row in rows if row["preview_compute_node_id"] == node_id]
        demand = sum(row["reference_service_time_ns"] for row in group) / 1e9
        nodes.append({"node_id": node_id, "task_count": len(group),
                      "service_demand_seconds": demand,
                      "demand_over_1000_second_capacity": demand / 1000})
    llm_group = [row for row in rows if row["task_profile"] == "llm"]
    summary = {
        "purpose": "offline-g1-candidate-not-tasktrace-not-runtime-validation",
        "reference_rate_work_units_per_second": reference_rate,
        "llm_parameters": asdict(parameters), "llm_config_source": QWEN_CONFIG_URL,
        "image_reference_commit": TASK_MODELING_COMMIT,
        "image_reference_bytes": [asdict(reference) for reference in IMAGE_REFERENCES],
        "task_count": len(rows), "total_input_bytes": sum(row["input_bytes"] for row in rows),
        "total_output_bytes": sum(row["output_bytes"] for row in rows),
        "total_compute_work_units": sum(row["compute_work_units"] for row in rows),
        "total_variable_state_bytes": sum(row["k_variable_bytes"] for row in rows),
        "llm_5_to_10_seconds_target_met": bool(llm_group) and all(
            5_000_000_000 <= row["reference_service_time_ns"] <= 10_000_000_000 for row in llm_group),
        "tail_counts": {str(size): sum(row["input_bytes"] == size for row in rows)
                        for size in (1_000_000_000, 500_000_000)},
        "tail_counts_by_profile": {
            profile: {str(size): sum(row["input_bytes"] == size and row["task_profile"] == profile
                                    for row in rows) for size in (1_000_000_000, 500_000_000)}
            for profile in TASK_PROFILES},
        "classes": class_summaries, "node_demand_preview": nodes,
        "node_service_demand_seconds": distribution([node["service_demand_seconds"] for node in nodes]),
        "assumptions": [
            "Image a_z=1; all scaled state/output bytes are reference-ratio budgets, not new measurements.",
            "Ordinary images: FNV weights 10..100, bounds 1048576..300000000 B; raw arrays aligned to 8 B.",
            "LLM: synthetic P=128..256 and P+G=1000..2000, equal WU/token, no tokenizer or inference.",
            "LLM RESULT: generated uint32 token IDs; raw KV state only, H=0 is a scenario assumption.",
            "Sparse safe points: equal-sized synthetic files at the measured mean file size, not actual DOTA files.",
            "Node IDs 0..65 round-robin by task ID only estimate demand; no geography, arrival, queue or deadline.",
            "No faults, network transfers, checkpoint execution, or N5 algorithms were run.",
        ],
    }
    return summary, rows


def representative_budgets(parameters: LlmParameters) -> list[tuple[str, TaskBudget]]:
    """Reference measurements and representative decimal sizes for G1 review."""
    result = []
    for reference in IMAGE_REFERENCES:
        label = REFERENCE_TASK_LABELS[reference.profile]
        for size in (reference.input_bytes, 10_000_000, 100_000_000, 500_000_000, 1_000_000_000):
            result.append((f"{reference.profile}-{size}", image_budget(reference.profile, size, label)))
    for tokens in (1000, 1500, 2000):
        result.append((f"llm-{tokens}", llm_budget(768, 200, tokens - 200, parameters)))
    return result


def checkpoint_grid_rows(parameters: LlmParameters, rate: int) -> list[dict]:
    """Audit every requested search granularity, not a frequency optimization."""
    representatives = [(ref.profile, image_budget(ref.profile, ref.input_bytes,
                                                  REFERENCE_TASK_LABELS[ref.profile]))
                       for ref in IMAGE_REFERENCES]
    representatives.append(("llm", llm_budget(768, 200, 1300, parameters)))
    rows = []
    for label, budget in representatives:
        ends, layout = preview_unit_ends(budget)
        for interval in (*range(10, 101), 200):
            records = checkpoint_budgets(budget, ends, interval)
            previous_time = 0
            seconds = []
            for record in records:
                now = service_time_ns(record.completed_work_units, rate)
                seconds.append((now - previous_time) / 1e9)
                previous_time = now
            if (sum(row.delta_work_units for row in records) != budget.compute_work_units or
                sum(row.delta_variable_bytes for row in records) != budget.k_variable_bytes):
                raise AssertionError("checkpoint grid lost WU or variable bytes")
            stats = distribution(seconds)
            rows.append({"task_profile": label, "legal_unit_assumption": layout,
                         "nominal_interval_percent": interval / 10,
                         "checkpoint_count": len(records), "k_variable_bytes": budget.k_variable_bytes,
                         "h_bytes_per_checkpoint": budget.header_bytes,
                         "k_total_bytes": sum(row.delta_total_bytes for row in records),
                         "max_alignment_overshoot_percentage_points": max(
                             float(Fraction(row.completed_extent * 100, budget.extent)) -
                             row.nominal_progress_per_mille / 10 for row in records),
                         "max_work_rounding_error_wu": max(
                             float(row.completed_work_units -
                                   Fraction(budget.compute_work_units * row.completed_extent, budget.extent))
                             for row in records),
                         "intervals_below_one_second": sum(value < 1 for value in seconds),
                         **{f"actual_interval_seconds_{key}": value for key, value in stats.items()}})
    return rows


def write_csv(path: Path, rows: list[dict]) -> None:
    """Write explicit audit output; callers own a newly created output directory."""
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True, help="New directory; existing paths are refused")
    parser.add_argument("--seed", default="n4c-g1-66")
    parser.add_argument("--reference-rate", type=int, default=REFERENCE_RATE, help="Candidate WU/s per node")
    parser.add_argument("--llm-work-units-per-token", type=int, default=REFERENCE_LLM_WU_PER_TOKEN)
    args = parser.parse_args()
    try:
        attributes = preview_attributes(args.seed)
        summary, rows = summarize_attributes(attributes, args.reference_rate, args.llm_work_units_per_token)
        summary["input_seed"] = args.seed
        parameters = LlmParameters(work_units_per_token=args.llm_work_units_per_token)
        grid = checkpoint_grid_rows(parameters, args.reference_rate)
        cases = []
        # First hold token/WU fixed and vary speed; then expose paired candidates
        # that preserve LLM time while changing image service and state/WU scales.
        for rate, omega in ((10_000, 100), (20_000, 100), (50_000, 100),
                            (10_000, 50), (50_000, 250)):
            case, _ = summarize_attributes(attributes, rate, omega)
            cases.append({key: case[key] for key in (
                "reference_rate_work_units_per_second", "llm_parameters", "total_compute_work_units",
                "llm_5_to_10_seconds_target_met", "classes", "node_service_demand_seconds")})
        summary["rate_and_token_work_sensitivity"] = cases
        representatives = [budget_row(label, budget) for label, budget in representative_budgets(parameters)]
        repository = Path(__file__).resolve().parents[4]
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repository, text=True).strip()
        dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=repository, text=True).strip())
        args.output_dir.mkdir(parents=True, exist_ok=False)
        (args.output_dir / "summary.json").write_text(json_text(summary), encoding="utf-8")
        (args.output_dir / "llm-requests.json").write_text(json_text([
            {"task_id": task["task_id"], "request_json": task["request_json"]}
            for task in attributes if task["task_profile"] == "llm"]), encoding="utf-8")
        write_csv(args.output_dir / "task-budgets.csv", rows)
        write_csv(args.output_dir / "representative-budgets.csv", representatives)
        write_csv(args.output_dir / "checkpoint-grid.csv", grid)
        (args.output_dir / "execution.json").write_text(json_text({
            "code_commit": head, "worktree_dirty": dirty, "python": sys.version,
            "command": [sys.executable, *sys.argv], "input_seed": args.seed,
            "kind": "offline-g1-preview", "llm_config_source": QWEN_CONFIG_URL,
            "task_modeling_reference_commit": TASK_MODELING_COMMIT,
        }), encoding="utf-8")
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"ERROR: {error}\n")
    print(f"WROTE: {len(rows)} offline task budgets; INPUT={summary['total_input_bytes']} B; "
          f"LLM time target={summary['llm_5_to_10_seconds_target_met']}; G1 approval still required")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
