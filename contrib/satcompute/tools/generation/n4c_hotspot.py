"""C800 placement only: native ECEF slices, no orbit dynamics or workload remapping."""

from bisect import bisect_right
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path

REGIONS = (
    ("north-america", -130, -60, 20, 55),
    ("europe", -10, 40, 35, 60),
    ("east-asia", 100, 145, 20, 50),
)
BUSINESS = ("task_id", "task_profile", "input_bytes", "output_bytes", "compute_work_units")


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
            raise ValueError("G3 requires the complete native 66-satellite slice")
        result[time_ns] = nodes
    if not result or min(result) != 0:
        raise ValueError("native slices must start at zero")
    return result


def read_none_tasks(directory):
    """Read business timing only; selection never opens fault/probability/state output."""
    with (Path(directory) / "task-summary.csv").open() as stream:
        tasks = list(csv.DictReader(stream))
    if not tasks or any(t["final_state"] != "COMPLETED" for t in tasks):
        raise ValueError("controlled F3 selection requires a completed none baseline")
    for name in ("fault-events.csv", "fault-model-state.csv"):
        if (Path(directory) / name).exists():
            raise ValueError("F3 selection input must be none, not a fault run")
    return tasks


def node_obligations(task, node):
    """Closed unsafe intervals, including already-arrived future RESULT obligations."""
    arrival = int(task["arrival_time_ns"])
    if int(task["compute_node_id"]) == node:
        # INPUT, queue, compute and the RESULT's source all depend on this node.
        return [(arrival, int(task["result_transfer_complete_time_ns"]), "compute")]
    intervals = []
    if int(task["source_node_id"]) == node:
        intervals.append((arrival, int(task["input_transfer_complete_time_ns"]), "source"))
    if int(task["result_node_id"]) == node:
        # Not yet transferring is not safe: an already-arrived task still needs this result endpoint.
        intervals.append((arrival, int(task["result_transfer_complete_time_ns"]), "result"))
    return intervals


def ordinary_use(tasks, node, time_ns, victim_id):
    evidence = []
    for task in tasks:
        if int(task["task_id"]) == victim_id:
            continue
        obligations = node_obligations(task, node)
        if obligations and max(end for _, end, _ in obligations) < time_ns:
            role = obligations[-1][2]
            evidence.append((int(task["task_id"]), role,
                             max(end for _, end, _ in obligations)))
    return min(evidence) if evidence else None


def select_f3_from_none(tasks, seed):
    """Choose a large victim from actual none intervals, never from risk or seeds' outcomes."""
    if not tasks or any(t["final_state"] != "COMPLETED" for t in tasks):
        raise ValueError("F3 selection requires completed none tasks")
    candidates = []
    for task in tasks:
        if int(task["input_bytes"]) <= 200_000_000:
            continue
        node, task_id = int(task["compute_node_id"]), int(task["task_id"])
        if node in (int(task["source_node_id"]), int(task["result_node_id"])):
            continue
        work, rate = int(task["compute_work_units"]), int(task["compute_rate_work_units_per_second"])
        start, finish = int(task["compute_start_time_ns"]), int(task["compute_complete_time_ns"])
        if start < 0 or rate != 100000 or finish <= start:
            raise ValueError("invalid none compute timing/rate")
        blocked = [interval for other in tasks if int(other["task_id"]) != task_id
                   for interval in node_obligations(other, node)]
        # Prefer a legal 60--80% interval; fall back only to the strict >50% requirement.
        for priority, low_work, high_work in (
                (0, (6 * work + 9) // 10, 8 * work // 10),
                (1, work // 2 + 1, work - 1)):
            lower = start + (low_work * 10**9 + rate - 1) // rate
            upper = min(finish - 1, start + ((high_work + 1) * 10**9 + rate - 1) // rate - 1)
            windows = [(lower, upper)] if lower <= upper else []
            for blocked_start, blocked_end, _ in blocked:
                remaining = []
                for a, b in windows:
                    if blocked_end < a or blocked_start > b:
                        remaining.append((a, b))
                    else:
                        if a < blocked_start:
                            remaining.append((a, blocked_start - 1))
                        if blocked_end < b:
                            remaining.append((blocked_end + 1, b))
                windows = remaining
            target = start + (((7 * work + 9) // 10) * 10**9 + rate - 1) // rate
            for a, b in windows:
                time_ns = max(a, min(b, target))
                ordinary = ordinary_use(tasks, node, time_ns, task_id)
                if ordinary is None:
                    continue
                progress = min(work, (time_ns - start) * rate // 10**9) / work
                if not .5 < progress < 1:
                    raise AssertionError("F3 candidate has invalid WU progress")
                # With 109 GB, ordinary files can also exceed 200 MB. Explicitly
                # prefer the frozen 500 MB / 1 GB tails, then dense/compression.
                preference = (int(int(task["input_bytes"]) not in (500_000_000, 1_000_000_000)),
                              int(task.get("task_profile") not in ("dense-image", "compression")), priority)
                candidates.append((preference, stable_hash(seed, "f3-large-victim", task_id, node),
                                   task_id, abs(time_ns-target), time_ns, {
                    "node_id": node, "time_ns": time_ns, "victim_task_id": task_id,
                    "ordinary_task_id": ordinary[0], "ordinary_role": ordinary[1],
                    "large_victim": True, "input_bytes": int(task["input_bytes"]),
                    "compute_work_units": work, "none_compute_start_time_ns": start,
                    "none_compute_finish_time_ns": finish, "none_progress": progress,
                    "ordinary_release_time_ns": ordinary[2],
                    "construction": "actual none business intervals; prefer frozen 500MB/1GB tails, dense/compression, 60-80% WU progress, then stable hash/task id; no risk input"}))
            if any(c[2] == task_id for c in candidates):
                break
    if not candidates:
        raise ValueError("none baseline has no large single-victim F3 window with ordinary pre-use")
    chosen = min(candidates, key=lambda c: c[:5])[-1]
    return {**chosen, "legal_candidate_task_count": len({c[2] for c in candidates})}


def build_hotspot(base, positions, seed, hot_weight=4, regional_limit=0, regions=REGIONS,
                  f3_plan=None, none_tasks=None, workload_candidate="C800"):
    if not isinstance(hot_weight, int) or hot_weight < 1 or regional_limit < 0:
        raise ValueError("positive integer hotspot weight and non-negative regional limit required")
    tasks = [dict(t) for t in base["tasks"]]
    if len({t["task_id"] for t in tasks}) != 800 or len(tasks) != 800:
        raise ValueError("requires C800 with unique task IDs")
    budgets = {"C800": 81_750_000_000, "C800-109G": 109_000_000_000}
    if workload_candidate not in budgets:
        raise ValueError("unknown C800 workload candidate")
    if Counter(t["task_profile"] for t in tasks) != {
        "dense-image": 240, "sparse-inference": 240, "compression": 240, "llm": 80
    } or sum(t["input_bytes"] for t in tasks) != budgets[workload_candidate]:
        raise ValueError("C800 business budgets differ")
    times = sorted(positions)

    def sample(time_ns):
        index = bisect_right(times, time_ns) - 1
        if index < 0 or time_ns - times[index] >= 1_000_000_000:
            raise ValueError("native preceding slice is missing or at least 1 second old")
        return times[index], positions[times[index]]

    if (f3_plan is None) != (none_tasks is None):
        raise ValueError("final F3 placement requires its actual none evidence")
    f3_time = f3_plan["time_ns"] if f3_plan else None
    f3_node = f3_plan["node_id"] if f3_plan else None
    counts = {role: Counter() for role in ("source", "result")}
    placements = []
    fallback = Counter()
    max_age = 0
    region_totals = defaultdict(lambda: {"task_count": 0, "input_bytes": 0, "work_units": 0})
    for task in sorted(tasks, key=lambda t: (t["arrival_time_ns"], t["task_id"])):
        arrival = task["arrival_time_ns"]
        available = [n for n in range(66) if not f3_plan or arrival < f3_time or n != f3_node]
        if not 1_000_000_000 <= arrival <= 600_000_000_000:
            raise ValueError("arrival outside frozen window")
        slice_time, pos = sample(arrival)
        max_age = max(max_age, arrival - slice_time)
        hot = set()
        for name, west, east, south, north in regions:
            candidates = [n for n in available if region_of(*pos[n], regions) == name]
            if not candidates:
                fallback[name] += 1
                continue
            candidates.sort(key=lambda n: (
                ((pos[n][0] - (south+north)/2)/(north-south))**2 +
                ((pos[n][1] - (west+east)/2)/(east-west))**2, n))
            hot.update(candidates[:regional_limit] if regional_limit else candidates)
        weights = [hot_weight if n in hot else 1 for n in available]
        ticket = stable_hash(seed, task["task_id"], "compute") % sum(weights)
        for n, weight in zip(available, weights):
            if ticket < weight:
                compute = n
                break
            ticket -= weight
        task["compute_node_id"] = compute
        for role in ("source", "result"):
            options = [n for n in available if n != compute]
            node = min(options, key=lambda n: (counts[role][n],
                       stable_hash(seed, task["task_id"], role, n), n))
            task[role + "_node_id"] = node
            counts[role][node] += 1
        latitude, longitude = pos[compute]
        region = region_of(latitude, longitude, regions)
        totals = region_totals[region]
        totals["task_count"] += 1
        totals["input_bytes"] += task["input_bytes"]
        totals["work_units"] += task["compute_work_units"]
        placements.append({"task_id": task["task_id"], "arrival_time_ns": arrival,
                           "position_time_ns": slice_time, "compute_node_id": compute,
                           "latitude_deg": latitude, "longitude_deg": longitude,
                           "region": region, "weighted_hot_candidate": compute in hot})
    output = {"tasks": sorted(tasks, key=lambda t: t["task_id"])}
    original = {t["task_id"]: t for t in base["tasks"]}
    for task in output["tasks"]:
        if any(task[k] != original[task["task_id"]][k] for k in (*BUSINESS, "arrival_time_ns")):
            raise AssertionError("placement changed frozen business/arrival")
        if f3_plan and task["arrival_time_ns"] >= f3_time and any(
                task[k] == f3_node for k in ("source_node_id", "compute_node_id", "result_node_id")):
            raise AssertionError("post-F3 task uses permanently failed node")
    if none_tasks is not None:
        observed = {int(t["task_id"]): t for t in none_tasks}
        if len(observed) != 800:
            raise ValueError("none evidence is not C800")
        for task in output["tasks"]:
            previous = observed[task["task_id"]]
            if any(task[k] != int(previous[k]) for k in
                   ("arrival_time_ns", "input_bytes", "output_bytes", "compute_work_units")) or \
                    task["task_profile"] != previous["task_profile"]:
                raise ValueError("none evidence changed task business")
            if task["arrival_time_ns"] < f3_time and any(task[k] != int(previous[k]) for k in
                    ("source_node_id", "compute_node_id", "result_node_id")):
                raise ValueError("final placement changed pre-F3 endpoints; wrong none/seed/weight")
    for totals in region_totals.values():
        totals["compute_service_demand_s"] = totals["work_units"] / 100_000
    manifest = {"profile": "n4c-hotspot", "seed": seed, "regions": regions,
                "hotspot_weight": hot_weight, "background_weight": 1,
                "regional_candidate_limit": regional_limit,
                "position_rule": "preceding native ECEF slice, age < 1 s; spherical lat/lon",
                "maximum_position_age_ns": max_age,
                "empty_region_fallback_counts": dict(fallback),
                "fallback_rule": "remaining weighted candidates; background always available",
                "business_attributes_and_arrivals_unchanged": True,
                "f3": f3_plan,
                "by_region": dict(region_totals), "placements": placements}
    if workload_candidate != "C800":
        manifest["workload_candidate"] = workload_candidate
    return output, manifest
