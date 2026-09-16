#!/usr/bin/env python3
"""Generate the final 800-task scene with one explicit controlled F3 size override."""
import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
from dataclasses import asdict
import json
import math
from pathlib import Path
from statistics import NormalDist
from task_workload_model import TASK_PROFILES, LlmParameters, image_budget, llm_budget

UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
WORKLOAD_SEED = "n4c-g1-66"
PLACEMENT_SEED = "n4c-g3-hotspot"
HOTSPOT_WEIGHT = 64
REGIONAL_CANDIDATE_LIMIT = 1
ARRIVAL_WINDOW_NS = (1_000_000_000, 1_050_000_000_000)
SIMULATION_SECONDS = 1300
COMPUTE_RATE = 100_000
CONTROLLED_F3_TASK_ID = 120
CONTROLLED_F3_INPUT_BYTES = 800_000_000
REGIONS = (
    ("north-america", -130, -60, 20, 55),
    ("europe", -10, 40, 35, 60),
    ("east-asia", 100, 145, 20, 50),
)


def read_json(path):
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def require_integer(value, name, minimum, maximum):
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or not minimum <= value <= maximum
    ):
        raise ValueError(f"{name} must be an integer in [{minimum}, {maximum}]")


def read_satellite_ids(path):
    root = read_json(path)
    if not isinstance(root, dict) or set(root) not in (
        {"nodes"},
        {"simulation_time_ns", "nodes"},
    ):
        raise ValueError("nodes file must be a topology node slice")
    if not isinstance(root["nodes"], list) or len(root["nodes"]) < 3:
        raise ValueError("nodes file must contain at least three satellites")

    satellite_ids = []
    for node in root["nodes"]:
        if not isinstance(node, dict) or set(node) not in (
            {"node_id", "node_type"},
            {"node_id", "node_type", "x", "y", "z"},
        ):
            raise ValueError("invalid satellite node object")
        require_integer(node["node_id"], "node_id", 0, UINT32_MAX)
        if node["node_type"] != "sat":
            raise ValueError("only satellite nodes are supported")
        satellite_ids.append(node["node_id"])
    if len(set(satellite_ids)) != len(satellite_ids):
        raise ValueError("nodes file contains duplicate node IDs")
    return sorted(satellite_ids)


def read_compute_profile(path, satellite_ids):
    root = read_json(path)
    if not isinstance(root, dict) or set(root) != {"compute_nodes"}:
        raise ValueError("ComputeProfile root must contain only compute_nodes")
    if not isinstance(root["compute_nodes"], list) or not root["compute_nodes"]:
        raise ValueError("ComputeProfile compute_nodes must be non-empty")

    valid_ids = set(satellite_ids)
    compute_nodes = []
    seen = set()
    for node in root["compute_nodes"]:
        if not isinstance(node, dict) or set(node) != {
            "node_id",
            "compute_rate_work_units_per_second",
        }:
            raise ValueError("invalid ComputeProfile node object")
        node_id = node["node_id"]
        rate = node["compute_rate_work_units_per_second"]
        require_integer(node_id, "compute node_id", 0, UINT32_MAX)
        require_integer(rate, "compute rate", 1, UINT64_MAX)
        if node_id not in valid_ids or node_id in seen:
            raise ValueError("ComputeProfile contains an unknown or duplicate node")
        seen.add(node_id)
        compute_nodes.append(
            {
                "node_id": node_id,
                "compute_rate_work_units_per_second": rate,
            }
        )
    return sorted(compute_nodes, key=lambda item: item["node_id"])


def deterministic_value(seed, task_id, field_name):
    value = 14695981039346656037
    for byte in f"{seed}\0{task_id}\0{field_name}".encode("utf-8"):
        value ^= byte
        value = (value * 1099511628211) & UINT64_MAX
    return value


def stable_hash(seed, *parts):
    value = 14695981039346656037
    for byte in (seed + ":" + ":".join(map(str, parts))).encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def region_of(latitude, longitude, regions=REGIONS):
    for name, west, east, south, north in regions:
        if west <= longitude <= east and south <= latitude <= north:
            return name
    return "background"


def read_positions(directory):
    """Convert supplied native ECEF vectors to spherical lat/lon, not an orbit model."""
    result = {}
    for path in sorted(Path(directory).glob("nodes_*s.json")):
        data = json.loads(path.read_text())
        time_ns = data["simulation_time_ns"]
        if not isinstance(time_ns, int) or time_ns < 0 or time_ns in result:
            raise ValueError("native slice time is invalid or duplicated")
        nodes = {}
        for node in data["nodes"]:
            x, y, z = (float(node[k]) for k in ("x", "y", "z"))
            if not all(math.isfinite(v) for v in (x, y, z)) or x*x + y*y + z*z == 0:
                raise ValueError("invalid native ECEF position")
            node_id = node["node_id"]
            if node_id in nodes:
                raise ValueError("duplicate satellite in native slice")
            nodes[node_id] = (math.degrees(math.atan2(z, math.hypot(x, y))),
                              math.degrees(math.atan2(y, x)))
        if set(nodes) != set(range(66)):
            raise ValueError("Final scene requires the complete native 66-satellite slice")
        result[time_ns] = nodes
    if not result or min(result) != 0:
        raise ValueError("native slices must start at zero")
    return result


def attributes(workload_seed=WORKLOAD_SEED):
    """Keep the accepted class/tail IDs and inverse-CDF sample keys unchanged."""
    if not isinstance(workload_seed, str) or not workload_seed or "\0" in workload_seed:
        raise ValueError("workload_seed must be non-empty without NUL")
    ids = sorted(range(1, 801),
                 key=lambda tid: (deterministic_value(workload_seed, tid, "n4c-class"), tid))
    tasks, offset = {}, 0
    for profile, count in zip(TASK_PROFILES, (240, 240, 240, 80)):
        for tid in ids[offset:offset + count]:
            tasks[tid] = {"task_id": tid, "task_profile": profile}
        offset += count
    # Keep the accepted LLM WU budgets, not the old token counts. Largest-remainder
    # allocation preserves their exact sum while aligning each task to whole tokens.
    llm_work = {tid: 100 * (5000 + deterministic_value(workload_seed, tid, "n4c-total-tokens") % 5001)
                for tid, task in tasks.items() if task["task_profile"] == "llm"}
    per_token = LlmParameters().work_units_per_token
    if sum(llm_work.values()) % per_token:
        raise ValueError("LLM total WU must admit exact whole-token conservation")
    token_counts = {tid: work // per_token for tid, work in llm_work.items()}
    extra = sum(llm_work.values()) // per_token - sum(token_counts.values())
    for tid in sorted(llm_work, key=lambda tid: (-(llm_work[tid] % per_token), tid))[:extra]:
        token_counts[tid] += 1
    for tid, task in tasks.items():
        if task["task_profile"] != "llm":
            continue
        prompt = 128 + deterministic_value(workload_seed, tid, "n4c-prompt-tokens") % 129
        total = token_counts[tid]
        repeats = 10 + deterministic_value(workload_seed, tid, "n4c-request-length") % 21
        request = {"prompt": "Summarize these observations: " + "cloud, coast, vegetation; " * repeats,
                   "max_new_tokens": total - prompt}
        request_text = json.dumps(request, sort_keys=True, separators=(",", ":"))
        task.update(input_bytes=len(request_text.encode("utf-8")), prompt_tokens=prompt,
                    generation_tokens=total-prompt)
    # Fixed rank ranges preserve accepted anchor identities, including intentional gaps.
    for profile, one_count, half_start, half_count in (
        ("compression", 4, 8, 8), ("dense-image", 1, 2, 2)
    ):
        ordered = sorted((tid for tid, task in tasks.items() if task["task_profile"] == profile),
                         key=lambda tid: (deterministic_value(workload_seed, tid, "n4c-tail"), tid))
        for size, chosen in ((1_000_000_000, ordered[:one_count]),
                             (500_000_000, ordered[half_start:half_start+half_count])):
            for tid in chosen:
                tasks[tid].update(input_bytes=size, fixed_tail_anchor=True)
    normal = NormalDist()
    lower, upper = normal.cdf((50-240)/130), normal.cdf((1000-240)/130)
    for tid, task in tasks.items():
        if "input_bytes" in task:
            continue
        u = ((deterministic_value(workload_seed, tid, "n4c-input-truncnorm") >> 12)+.5)/2**52
        size = int((240+130*normal.inv_cdf(lower+u*(upper-lower)))*1_000_000)
        size = max(50_000_000, min(1_000_000_000-1, size))
        task["input_bytes"] = size-size % 8 if task["task_profile"] in ("dense-image", "compression") else size
    # Explicit B-selected controlled case, not another truncated-normal sample.
    # No arrival, endpoint, fault parameter or random key is changed.
    target = tasks[CONTROLLED_F3_TASK_ID]
    if target["task_profile"] != "compression" or target.get("fixed_tail_anchor"):
        raise ValueError("controlled task 120 must be a non-anchor compression task")
    target.update(original_input_bytes=target["input_bytes"],
                  input_bytes=CONTROLLED_F3_INPUT_BYTES, controlled_f3_size=True)
    return [tasks[tid] for tid in sorted(tasks)]


def budget_for(task):
    """Use shared TaskModeling-derived mappings rather than duplicate WU/state formulas."""
    if task["task_profile"] == "llm":
        return llm_budget(task["input_bytes"], task["prompt_tokens"], task["generation_tokens"])
    return image_budget(task["task_profile"], task["input_bytes"], str(task["task_id"]))


def arrivals(task_ids, workload_seed):
    """Original stratified-uniform permutation and nanosecond jitter."""
    start, end = ARRIVAL_WINDOW_NS
    span = end-start+1
    ordered = sorted(task_ids,
                     key=lambda tid: (deterministic_value(workload_seed, tid, "arrival-permutation"), tid))
    result = {}
    for stratum, tid in enumerate(ordered):
        lower = start+span*stratum//len(ordered)
        upper = start+span*(stratum+1)//len(ordered)-1
        result[tid] = lower+deterministic_value(workload_seed, tid, "arrival-jitter") % (upper-lower+1)
    return result


def place_tasks(tasks, positions, placement_seed=PLACEMENT_SEED):
    """Geographic placement only; no fault input, F3 selector or endpoint remapping."""
    if not isinstance(placement_seed, str) or not placement_seed or "\0" in placement_seed:
        raise ValueError("placement_seed must be non-empty without NUL")
    times = sorted(positions)
    counts = {role: Counter() for role in ("source", "result")}
    placements, fallback = [], Counter()
    totals = defaultdict(lambda: {"task_count": 0, "input_bytes": 0, "work_units": 0})
    for task in sorted(tasks, key=lambda t: (t["arrival_time_ns"], t["task_id"])):
        arrival = task["arrival_time_ns"]
        index = bisect_right(times, arrival)-1
        if index < 0 or arrival-times[index] >= 1_000_000_000:
            raise ValueError("native preceding slice is missing or at least 1 second old")
        slice_time, pos = times[index], positions[times[index]]
        if set(pos) != set(range(66)):
            raise ValueError("placement requires all 66 satellites")
        hot = set()
        for name, west, east, south, north in REGIONS:
            candidates = [n for n in range(66) if region_of(*pos[n]) == name]
            if not candidates:
                fallback[name] += 1
                continue
            candidates.sort(key=lambda n: (
                ((pos[n][0]-(south+north)/2)/(north-south))**2 +
                ((pos[n][1]-(west+east)/2)/(east-west))**2, n))
            hot.update(candidates[:REGIONAL_CANDIDATE_LIMIT])
        weights = [HOTSPOT_WEIGHT if n in hot else 1 for n in range(66)]
        ticket = stable_hash(placement_seed, task["task_id"], "compute") % sum(weights)
        for compute, weight in enumerate(weights):
            if ticket < weight:
                break
            ticket -= weight
        task["compute_node_id"] = compute
        for role in ("source", "result"):
            options = [n for n in range(66) if n != compute]
            node = min(options, key=lambda n: (counts[role][n],
                       stable_hash(placement_seed, task["task_id"], role, n), n))
            task[role+"_node_id"] = node
            counts[role][node] += 1
        latitude, longitude = pos[compute]
        region = region_of(latitude, longitude)
        totals[region]["task_count"] += 1
        totals[region]["input_bytes"] += task["input_bytes"]
        totals[region]["work_units"] += task["compute_work_units"]
        placements.append({"task_id": task["task_id"], "arrival_time_ns": arrival,
                           "position_time_ns": slice_time, "compute_node_id": compute,
                           "latitude_deg": latitude, "longitude_deg": longitude,
                           "region": region, "weighted_hot_candidate": compute in hot})
    return {"placement_seed": placement_seed, "regions": REGIONS, "hotspot_weight": HOTSPOT_WEIGHT,
            "regional_candidate_limit": REGIONAL_CANDIDATE_LIMIT, "background_weight": 1,
            "position_rule": "preceding native ECEF slice, age < 1 s; spherical lat/lon",
            "empty_region_fallback_counts": dict(fallback), "by_region": dict(totals),
            "placements": placements}


def build_final_workload(satellite_ids, compute_nodes, positions,
                         workload_seed=WORKLOAD_SEED, placement_seed=PLACEMENT_SEED):
    """Reconstruct the final TaskTrace; never read or copy the frozen trace."""
    if sorted(satellite_ids) != list(range(66)):
        raise ValueError("final scene requires satellite IDs 0..65")
    if (sorted(n["node_id"] for n in compute_nodes) != list(range(66)) or
            any(n["compute_rate_work_units_per_second"] != COMPUTE_RATE for n in compute_nodes)):
        raise ValueError("final scene requires all 66 nodes at 100000 WU/s")
    attrs = attributes(workload_seed)
    times = arrivals([t["task_id"] for t in attrs], workload_seed)
    budgets = [budget_for(t) for t in attrs]
    tasks = [dict(task_id=a["task_id"], task_profile=b.task_profile, input_bytes=b.input_bytes,
                  output_bytes=b.output_bytes, compute_work_units=b.compute_work_units,
                  source_node_id=0, compute_node_id=0, result_node_id=0, arrival_time_ns=times[a["task_id"]])
             for a, b in zip(attrs, budgets)]
    placement = place_tasks(tasks, positions, placement_seed)
    target = next(a for a in attrs if a.get("controlled_f3_size"))
    controlled = dict(task_id=target["task_id"], original_input_bytes=target["original_input_bytes"],
                      input_bytes=target["input_bytes"],
                      rule="B-only size-selected controlled F3 protection case; not an unbiased performance sample")
    anchors = {str(size): [t["task_id"] for t in attrs if t.get("fixed_tail_anchor") and t["input_bytes"] == size]
               for size in (500_000_000, 1_000_000_000)}
    summary = dict(workload_seed=workload_seed, placement_seed=placement_seed, task_count=len(tasks),
                   class_counts=dict(Counter(t["task_profile"] for t in tasks)), ordinary_image_count=704,
                   fixed_tail_task_ids=anchors, arrival_window_s=[1, 1050], simulation_duration_s=1300,
                   compute_rate_work_units_per_second=COMPUTE_RATE,
                   total_input_bytes=sum(b.input_bytes for b in budgets),
                   total_output_bytes=sum(b.output_bytes for b in budgets),
                   total_compute_work_units=sum(b.compute_work_units for b in budgets),
                   total_variable_state_bytes=sum(b.k_variable_bytes for b in budgets),
                   llm_parameters=asdict(LlmParameters()),
                   llm_work_conservation=dict(total_work_units=sum(b.compute_work_units for b in budgets
                                                                   if b.task_profile == "llm"),
                       total_tokens=sum(b.extent for b in budgets if b.task_profile == "llm"),
                       rounding="largest remainder; descending remainder, ascending task ID",
                       max_abs_task_work_delta=max(abs(budget_for(a).compute_work_units -
                           100 * (5000 + deterministic_value(workload_seed, a["task_id"], "n4c-total-tokens") % 5001))
                           for a in attrs if a["task_profile"] == "llm")),
                   controlled_f3_task=controlled,
                   truncated_normal={"mu_mb": 240, "sigma_mb": 130, "lower_mb": 50,
                                     "upper_exclusive_mb": 1000, "fixed_tail_task_ids": anchors},
                   placement=placement)
    return {"tasks": tasks}, summary


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workload-seed", default=WORKLOAD_SEED)
    parser.add_argument("--placement-seed", default=PLACEMENT_SEED)
    parser.add_argument("--nodes-file", type=Path, required=True)
    parser.add_argument("--position-slices", type=Path, required=True)
    parser.add_argument("--compute-profile", type=Path, required=True)
    parser.add_argument("--output-task-trace", type=Path, required=True)
    parser.add_argument("--output-workload-summary", type=Path, required=True)
    args = parser.parse_args()
    try:
        destinations = (args.output_task_trace.resolve(), args.output_workload_summary.resolve())
        if destinations[0] == destinations[1] or any(p.exists() for p in destinations):
            raise ValueError("outputs must be distinct new files; frozen inputs are never overwritten")
        nodes = read_satellite_ids(args.nodes_file)
        profile = read_compute_profile(args.compute_profile, nodes)
        trace, summary = build_final_workload(nodes, profile, read_positions(args.position_slices),
                                              args.workload_seed, args.placement_seed)
        write_json(destinations[0], trace)
        write_json(destinations[1], summary)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(2, f"ERROR: {error}\n")
    print(f"PASS: {len(trace['tasks'])} tasks; INPUT={summary['total_input_bytes']} B; "
          f"WU={summary['total_compute_work_units']}")


if __name__ == "__main__":
    main()
