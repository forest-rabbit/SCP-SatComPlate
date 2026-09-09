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
from statistics import NormalDist, mean

from task_workload_model import (
    IMAGE_REFERENCES, QWEN_CONFIG_URL, TASK_MODELING_COMMIT, TASK_PROFILES,
    LlmParameters, TaskBudget, ceil_div, state_budget_points, image_budget, image_reference,
    llm_budget, require_uint, service_time_ns, uniform_unit_ends,
)


GENERATOR = runpy.run_path(str(Path(__file__).with_name("generate-task-workload.py")))
STABLE_VALUE = GENERATOR["deterministic_value"]
ALLOCATE = GENERATOR["bounded_weighted_allocation"]
LARGEST_REMAINDER = GENERATOR["largest_remainder"]
TOTAL_INPUT_BYTES = 81_750_000_000
# G1 composition variants retain 81.75 GB; C800-109G is an isolated G3 intensity variant.
# Entries specify total tasks, 1 GB count, and 500 MB count.
COMPOSITIONS = {"V2-1500": (1500, 15, 30), "C1000": (1000, 10, 20),
                "C800": (800, 10, 20), "C600": (600, 10, 20), "C800-109G": (800, 10, 20),
                "C800-TruncNormal": (800, 10, 10), "C800-TruncNormal-v3": (800, 5, 10)}
REFERENCE_RATE = 100_000
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


def truncated_normal_size(seed: str, task_id: int, profile: str, *, v3: bool = False) -> int:
    """One inverse-CDF draw per ID; decimal bytes, no budget redistribution."""
    normal = NormalDist()
    mu, sigma, maximum = (240, 130, 1000) if v3 else (180, 80, 500)
    lower, upper = normal.cdf((50-mu)/sigma), normal.cdf((maximum-mu)/sigma)
    # Midpoints of 2**52 bins are strictly inside (0, 1) in binary64.
    u = ((STABLE_VALUE(seed, task_id, "n4c-input-truncnorm") >> 12) + .5) / 2**52
    size = int((mu + sigma * normal.inv_cdf(lower + u * (upper - lower))) * 1_000_000)
    size = max(50_000_000, min(maximum * 1_000_000 - 1, size))
    return size - size % 8 if profile in ("dense-image", "compression") else size


def describe_truncnormal(summary: dict, attributes: list[dict], rows: list[dict]) -> None:
    """Disclose this G3 candidate without changing historical preview summaries."""
    ordinary = [t["input_bytes"] for t in attributes
                if t["task_profile"] != "llm" and t["input_bytes"] < 500_000_000]
    ordered = sorted(ordinary)
    def quantile(p):
        index = (len(ordered) - 1) * p
        lo = int(index)
        return ordered[lo] + (ordered[min(lo + 1, len(ordered) - 1)] - ordered[lo]) * (index - lo)
    normal = NormalDist()
    expected_mb = 180 + 80 * (normal.pdf(-1.625) - normal.pdf(4)) / (normal.cdf(4) - normal.cdf(-1.625))
    service = [r["reference_service_time_ns"] / 1e9 for r in rows]
    summary.update(purpose="offline-g3-final-candidate-lightweight-review", simulation_duration_s=1200,
        truncated_normal={"mu_mb": 180, "sigma_mb": 80, "lower_bytes": 50_000_000,
            "upper_exclusive_bytes": 500_000_000, "expected_mean_mb": expected_mb,
            "expected_image_input_bytes": 700 * expected_mb * 1e6 + 15_000_000_000,
            "ordinary_input_bytes": sum(ordinary), "count": len(ordinary),
            "input_size_bytes": {"min": min(ordinary), "p10": quantile(.1), "median": quantile(.5),
                "mean": mean(ordinary), "p90": quantile(.9), "p95": quantile(.95), "max": max(ordinary)},
            "fixed_tail_task_ids": {str(size): [t["task_id"] for t in attributes if t["input_bytes"] == size]
                                    for size in (500_000_000, 1_000_000_000)},
            "sampling": "FNV-1a n4c-input-truncnorm, top-52-bit midpoint, NormalDist inverse CDF; floor bytes; raw arrays floor to 8 B; no total correction"},
        service_time_partition={"lt_1s": sum(t < 1 for t in service),
            "1s_le_t_lt_2s": sum(1 <= t < 2 for t in service),
            "2s_le_t_lt_5s": sum(2 <= t < 5 for t in service),
            "5s_le_t_le_10s": sum(5 <= t <= 10 for t in service),
            "gt_10s": sum(t > 10 for t in service)})
    summary["assumptions"][1] = "Ordinary images: TN(180,80;50,500) decimal MB; raw arrays aligned to 8 B; no global budget correction."


def describe_truncnormal_v3(summary: dict, attributes: list[dict], rows: list[dict]) -> None:
    """Track anchor IDs explicitly: natural ordinary samples may exceed 500 MB."""
    ordinary = [t["input_bytes"] for t in attributes if t["task_profile"] != "llm"
                and not t.get("fixed_tail_anchor", False)]
    ordered = sorted(ordinary)
    def quantile(p):
        index = (len(ordered)-1)*p
        lo = int(index)
        return ordered[lo] + (ordered[min(lo+1, len(ordered)-1)]-ordered[lo])*(index-lo)
    normal = NormalDist()
    a, b = (50-240)/130, (1000-240)/130
    expected_mb = 240 + 130*(normal.pdf(a)-normal.pdf(b))/(normal.cdf(b)-normal.cdf(a))
    service = [r["reference_service_time_ns"] for r in rows]
    summary.update(purpose="offline-g3-candidate-v3-review-not-frozen", simulation_duration_s=1300,
        truncated_normal={"mu_mb": 240, "sigma_mb": 130, "lower_bytes": 50_000_000,
            "upper_exclusive_bytes": 1_000_000_000, "expected_mean_mb": expected_mb,
            "expected_image_input_bytes": 705 * expected_mb * 1e6 + 10_000_000_000,
            "ordinary_input_bytes": sum(ordinary), "count": len(ordinary),
            "input_size_bytes": {"min": min(ordinary), "mean": mean(ordinary), "max": max(ordinary),
                **{name: quantile(p) for name, p in (("p10", .1), ("p25", .25), ("median", .5),
                    ("p75", .75), ("p90", .9), ("p95", .95), ("p99", .99))}},
            "size_counts": {"lt_100MB": sum(s < 100_000_000 for s in ordinary),
                "lt_150MB": sum(s < 150_000_000 for s in ordinary),
                "gt_500MB": sum(s > 500_000_000 for s in ordinary),
                "gt_750MB": sum(s > 750_000_000 for s in ordinary)},
            "fixed_tail_task_ids": {str(size): [t["task_id"] for t in attributes
                if t.get("fixed_tail_anchor", False) and t["input_bytes"] == size]
                for size in (500_000_000, 1_000_000_000)},
            "sampling": "Original FNV-1a n4c-input-truncnorm quantiles retained; inverse CDF; raw arrays floor to 8 B; no total correction",
            "anchor_rule": "Keep original ten 500MB IDs; retain first four compression and first dense 1GB IDs in original n4c-tail order"},
        service_time_partition={"lt_1s": sum(t < 10**9 for t in service),
            "1s_le_t_lt_2s": sum(10**9 <= t < 2*10**9 for t in service),
            "2s_le_t_lt_5s": sum(2*10**9 <= t < 5*10**9 for t in service),
            "5s_le_t_le_10s": sum(5*10**9 <= t <= 10*10**9 for t in service),
            "10s_lt_t_lt_15s": sum(10*10**9 < t < 15*10**9 for t in service),
            "eq_15s": sum(t == 15*10**9 for t in service)})
    summary["assumptions"][1] = "Ordinary images: TN(240,130;50,1000) decimal MB; fixed anchors identified by ID; no global budget correction."


def preview_attributes(seed: str, candidate: str = "V2-1500") -> list[dict]:
    """Allocate only task attributes; endpoints/deadline/arrival remain G2 work."""
    if not isinstance(seed, str) or not seed:
        raise ValueError("seed must be a non-empty string")
    if not isinstance(candidate, str) or candidate not in COMPOSITIONS:
        raise ValueError("unknown workload composition candidate")
    task_count, one_gb_count, half_gb_count = COMPOSITIONS[candidate]
    total_input_bytes = 109_000_000_000 if candidate == "C800-109G" else TOTAL_INPUT_BYTES
    counts = dict(zip(TASK_PROFILES, (task_count // 10 * ratio for ratio in (3, 3, 3, 1))))
    ids = sorted(range(1, task_count + 1), key=lambda tid: (STABLE_VALUE(seed, tid, "n4c-class"), tid))
    attributes = {}
    offset = 0
    for profile, count in counts.items():
        for task_id in ids[offset:offset + count]:
            attributes[task_id] = {"task_id": task_id, "task_profile": profile}
        offset += count

    for task_id, task in attributes.items():
        if task["task_profile"] != "llm":
            continue
        prompt = 128 + STABLE_VALUE(seed, task_id, "n4c-prompt-tokens") % 129
        total = 5000 + STABLE_VALUE(seed, task_id, "n4c-total-tokens") % 5001
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
    for size, count in ((1_000_000_000, one_gb_count), (500_000_000, half_gb_count)):
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
        if candidate == "C800-TruncNormal-v3" and size == 1_000_000_000:
            # Reserve the original ten 1GB slots before locating the unchanged
            # 500MB anchors. Removed 1GB IDs rejoin the ordinary distribution.
            tail_offset = LARGEST_REMAINDER(10, {"compression": 7500, "dense-image": 2500},
                                           ("compression", "dense-image"))

    ordinary = sorted(tid for tid, task in attributes.items()
                      if task["task_profile"] != "llm" and tid not in tail_ids)
    if candidate in ("C800-TruncNormal", "C800-TruncNormal-v3"):
        for tid in ordinary:
            attributes[tid]["input_bytes"] = truncated_normal_size(
                seed, tid, attributes[tid]["task_profile"], v3=candidate == "C800-TruncNormal-v3")
        if candidate == "C800-TruncNormal-v3":
            for tid in tail_ids:
                attributes[tid]["fixed_tail_anchor"] = True
        return [attributes[tid] for tid in sorted(attributes)]
    remaining = total_input_bytes - sum(task.get("input_bytes", 0) for task in attributes.values())
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
    if sum(task["input_bytes"] for task in result) != total_input_bytes:
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
        "k_variable_bytes": budget.k_variable_bytes, "h_bytes": budget.header_bytes,
        "rho_variable": None if rho is None else float(rho),
        "sigma_variable_bytes_per_work_unit": float(sigma),
        "sigma_variable_numerator": sigma.numerator, "sigma_variable_denominator": sigma.denominator,
    }


def service_bands(rows: list[dict]) -> dict:
    """First three counts are cumulative; other bins have explicit endpoints."""
    times = [row["reference_service_time_ns"] for row in rows]
    counts = {
        "lt_0_5s": sum(t < 500_000_000 for t in times),
        "lt_1s": sum(t < 1_000_000_000 for t in times),
        "lt_2s": sum(t < 2_000_000_000 for t in times),
        "2s_le_t_lt_5s": sum(2_000_000_000 <= t < 5_000_000_000 for t in times),
        "5s_le_t_le_10s": sum(5_000_000_000 <= t <= 10_000_000_000 for t in times),
        "gt_10s": sum(t > 10_000_000_000 for t in times),
    }
    return {key: {"count": count, "ratio": count / len(times) if times else 0}
            for key, count in counts.items()}


def service_demand(rows: list[dict]) -> dict:
    """Sum compute demand, not observed node utilization or wall-clock time."""
    return {"count": len(rows), "total_work_units": sum(row["compute_work_units"] for row in rows),
            "total_service_demand_seconds": sum(row["reference_service_time_ns"] for row in rows) / 1e9}


def population_summary(rows: list[dict]) -> dict:
    """Describe input and service distributions; empty subsets have no quantiles."""
    return {"count": len(rows), "input_bytes": sum(row["input_bytes"] for row in rows),
            "input_size_bytes": distribution([row["input_bytes"] for row in rows]) if rows else None,
            "service_time_seconds": distribution([row["reference_service_time_ns"] / 1e9
                                                   for row in rows]) if rows else None,
            "service_bands": service_bands(rows)}


def summarize_attributes(attributes: list[dict], reference_rate: int = REFERENCE_RATE,
                         llm_wu_per_token: int = REFERENCE_LLM_WU_PER_TOKEN,
                         simulation_seconds: int = 1000) -> tuple[dict, list[dict]]:
    """Produce service demand estimates; do not simulate queues or thermal state."""
    require_uint(reference_rate, "reference_rate", 1)
    require_uint(simulation_seconds, "simulation_seconds", 1)
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
        class_summaries[profile] = {
            **population_summary(group),
            "output_bytes": sum(row["output_bytes"] for row in group),
            "work_units": distribution([row["compute_work_units"] for row in group]),
            "total_work_units": sum(row["compute_work_units"] for row in group),
            "total_service_demand_seconds": service_demand(group)["total_service_demand_seconds"],
            "total_variable_state_bytes": sum(row["k_variable_bytes"] for row in group),
        }
    nodes = []
    for node_id in range(COMPUTE_NODE_COUNT):
        group = [row for row in rows if row["preview_compute_node_id"] == node_id]
        demand = sum(row["reference_service_time_ns"] for row in group) / 1e9
        nodes.append({"node_id": node_id, "task_count": len(group),
                      "service_demand_seconds": demand,
                      f"demand_over_{simulation_seconds}_second_capacity": demand / simulation_seconds})
    llm_group = [row for row in rows if row["task_profile"] == "llm"]
    image_group = [row for row in rows if row["task_profile"] != "llm"]
    anchor_ids = {t["task_id"] for t in attributes if t.get("fixed_tail_anchor", False)}
    if not anchor_ids:
        anchor_ids = {r["task_id"] for r in image_group if r["input_bytes"] in (500_000_000, 1_000_000_000)}
    large_images = [row for row in image_group if row["task_id"] in anchor_ids]
    ordinary_images = [row for row in image_group if row["task_id"] not in anchor_ids]
    input_bytes = sum(row["input_bytes"] for row in rows)
    large_bytes = sum(row["input_bytes"] for row in large_images)
    total_ns = sum(row["reference_service_time_ns"] for row in rows)
    llm_ns = sum(row["reference_service_time_ns"] for row in llm_group)
    summary = {
        "purpose": "offline-g1-candidate-not-tasktrace-not-runtime-validation",
        "reference_rate_work_units_per_second": reference_rate,
        "llm_parameters": asdict(parameters), "llm_config_source": QWEN_CONFIG_URL,
        "image_reference_commit": TASK_MODELING_COMMIT,
        "image_reference_bytes": [asdict(reference) for reference in IMAGE_REFERENCES],
        "task_count": len(rows), "total_input_bytes": input_bytes,
        "total_output_bytes": sum(row["output_bytes"] for row in rows),
        "total_compute_work_units": sum(row["compute_work_units"] for row in rows),
        "total_variable_state_bytes": sum(row["k_variable_bytes"] for row in rows),
        "service_demand": {"images": service_demand(image_group), "llm": service_demand(llm_group),
                           "all": service_demand(rows), "llm_share": llm_ns / total_ns},
        "image_service_bands": service_bands(image_group),
        "image_population": population_summary(image_group),
        "ordinary_images": {
            "all": population_summary(ordinary_images),
            "by_profile": {profile: population_summary([row for row in ordinary_images
                                                        if row["task_profile"] == profile])
                           for profile in TASK_PROFILES if profile != "llm"}},
        "large_image_tasks": {"count": len(large_images), "input_bytes": large_bytes,
                              "input_ratio": large_bytes / input_bytes},
        "llm_5_to_10_seconds_target_met": bool(llm_group) and all(
            5_000_000_000 <= row["reference_service_time_ns"] <= 10_000_000_000 for row in llm_group),
        "tail_counts": {str(size): sum(row["input_bytes"] == size for row in large_images)
                        for size in (1_000_000_000, 500_000_000)},
        "tail_counts_by_profile": {
            profile: {str(size): sum(row["input_bytes"] == size and row["task_profile"] == profile
                                    for row in large_images) for size in (1_000_000_000, 500_000_000)}
            for profile in TASK_PROFILES},
        "classes": class_summaries, "node_demand_preview": nodes,
        "node_service_demand_seconds": distribution([node["service_demand_seconds"] for node in nodes]),
        "assumptions": [
            "Image W=ceil(3*S*a_z/2000), a_z=1; state/output are reference-ratio budgets, not measurements.",
            "Ordinary images: FNV weights 10..100, bounds 1048576..300000000 B; raw arrays aligned to 8 B.",
            "LLM: synthetic P=128..256 and P+G=5000..10000, equal WU/token, no tokenizer or inference.",
            "Larger LLM token counts increase total KV bytes; this is not merely WU normalization.",
            "Short-task lt bands are cumulative, not a partition; no minimum duration is imposed.",
            "LLM RESULT: generated uint32 token IDs; raw KV state only, H=0 is a scenario assumption.",
            "Sparse safe points: equal-sized synthetic files at the measured mean file size, not actual DOTA files.",
            "Node IDs 0..65 round-robin by task ID only estimate demand; no geography, arrival, queue or deadline.",
            "Only 5/10/20-percent state conservation checks; no L1/batches, n/delta search or N5 execution.",
            "No faults, network transfers, checkpoint execution, or N5 algorithms were run.",
        ],
    }
    return summary, rows


def representative_budgets(parameters: LlmParameters) -> list[tuple[str, TaskBudget]]:
    """Reference measurements and representative decimal sizes for G1 review."""
    result = []
    for reference in IMAGE_REFERENCES:
        label = REFERENCE_TASK_LABELS[reference.profile]
        for size in (reference.input_bytes, 10_000_000, 50_000_000, 100_000_000,
                     500_000_000, 1_000_000_000):
            result.append((f"{reference.profile}-{size}", image_budget(reference.profile, size, label)))
    for tokens in (5000, 7500, 10000):
        result.append((f"llm-{tokens}", llm_budget(768, 200, tokens - 200, parameters)))
    return result


def state_budget_check_rows(parameters: LlmParameters) -> list[dict]:
    """Check only 5/10/20-percent partitions; do not generate N5 L1 records."""
    representatives = [(ref.profile, image_budget(ref.profile, ref.input_bytes,
                                                  REFERENCE_TASK_LABELS[ref.profile]))
                       for ref in IMAGE_REFERENCES]
    representatives.append(("llm", llm_budget(768, 200, 7300, parameters)))
    rows = []
    for label, budget in representatives:
        ends, layout = preview_unit_ends(budget)
        for interval in (50, 100, 200):
            records = state_budget_points(budget, ends, interval)
            if (sum(row.delta_work_units for row in records) != budget.compute_work_units or
                sum(row.delta_variable_bytes for row in records) != budget.k_variable_bytes):
                raise AssertionError("state budget check lost WU or variable bytes")
            rows.append({"task_profile": label, "legal_unit_assumption": layout,
                         "check_step_percent": interval / 10,
                         "budget_point_count": len(records), "k_variable_bytes": budget.k_variable_bytes,
                         "h_bytes": budget.header_bytes,
                         "accounted_variable_plus_repeated_h_bytes": sum(row.delta_total_bytes for row in records),
                         "max_alignment_overshoot_percentage_points": max(
                             float(Fraction(row.completed_extent * 100, budget.extent)) -
                             row.nominal_progress_per_mille / 10 for row in records),
                         "max_work_rounding_error_wu": max(
                             float(row.completed_work_units -
                                   Fraction(budget.compute_work_units * row.completed_extent, budget.extent))
                             for row in records)})
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
    parser.add_argument("--candidate", choices=tuple(COMPOSITIONS), default="V2-1500",
                        help="Offline budgets only; C800-109G is a G3 intensity variant, other candidates retain G1 budgets")
    parser.add_argument("--reference-rate", type=int, default=REFERENCE_RATE, help="Candidate WU/s per node")
    parser.add_argument("--llm-work-units-per-token", type=int, default=REFERENCE_LLM_WU_PER_TOKEN)
    args = parser.parse_args()
    try:
        if args.output_dir.exists():
            raise ValueError("output directory already exists; choose a new path")
        attributes = preview_attributes(args.seed, args.candidate)
        summary, rows = summarize_attributes(attributes, args.reference_rate, args.llm_work_units_per_token,
                                             1300 if args.candidate == "C800-TruncNormal-v3" else
                                             1200 if args.candidate == "C800-TruncNormal" else 1000)
        summary["input_seed"] = args.seed
        summary["workload_candidate"] = args.candidate
        if args.candidate == "C800-109G":
            summary["purpose"] = "offline-g3-stress-variant-not-runtime-validation"
        if args.candidate == "C800-TruncNormal":
            describe_truncnormal(summary, attributes, rows)
        if args.candidate == "C800-TruncNormal-v3":
            describe_truncnormal_v3(summary, attributes, rows)
        parameters = LlmParameters(work_units_per_token=args.llm_work_units_per_token)
        checks = state_budget_check_rows(parameters)
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
        write_csv(args.output_dir / "state-budget-checks.csv", checks)
        (args.output_dir / "execution.json").write_text(json_text({
            "code_commit": head, "worktree_dirty": dirty, "python": sys.version,
            "command": [sys.executable, *sys.argv], "input_seed": args.seed,
            "workload_candidate": args.candidate,
            "kind": "offline-g3-stress-preview" if args.candidate in ("C800-109G", "C800-TruncNormal", "C800-TruncNormal-v3") else "offline-g1-preview",
            "llm_config_source": QWEN_CONFIG_URL,
            "task_modeling_reference_commit": TASK_MODELING_COMMIT,
        }), encoding="utf-8")
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"ERROR: {error}\n")
    gate = "G3 runtime validation still required" if args.candidate in ("C800-109G", "C800-TruncNormal", "C800-TruncNormal-v3") else "G1 approval still required"
    print(f"WROTE: {len(rows)} offline task budgets; INPUT={summary['total_input_bytes']} B; "
          f"LLM time target={summary['llm_5_to_10_seconds_target_met']}; {gate}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
