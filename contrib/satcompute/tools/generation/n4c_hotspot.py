"""C800 placement only: native ECEF slices, no orbit dynamics or workload remapping."""

from bisect import bisect_right
from collections import Counter, defaultdict
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


def build_hotspot(base, positions, seed, hot_weight=4, regional_limit=0, regions=REGIONS):
    if not isinstance(hot_weight, int) or hot_weight < 1 or regional_limit < 0:
        raise ValueError("positive integer hotspot weight and non-negative regional limit required")
    tasks = [dict(t) for t in base["tasks"]]
    if len({t["task_id"] for t in tasks}) != 800 or len(tasks) != 800:
        raise ValueError("requires C800 with unique task IDs")
    if Counter(t["task_profile"] for t in tasks) != {
        "dense-image": 240, "sparse-inference": 240, "compression": 240, "llm": 80
    } or sum(t["input_bytes"] for t in tasks) != 81_750_000_000:
        raise ValueError("C800 business budgets differ")
    times = sorted(positions)

    def sample(time_ns):
        index = bisect_right(times, time_ns) - 1
        if index < 0 or time_ns - times[index] >= 1_000_000_000:
            raise ValueError("native preceding slice is missing or at least 1 second old")
        return times[index], positions[times[index]]

    # Early small-request LLM: INPUT can finish well before a one-second offset.
    # An early victim has no prior compute queue; other endpoint use remains normal.
    # Choose a northern native track, physically outside SAA before the event.
    victim = min((t for t in tasks if t["task_profile"] == "llm"),
                 key=lambda t: (t["arrival_time_ns"], t["task_id"]))
    f3_time = victim["arrival_time_ns"] + 1_000_000_000
    eligible = [n for n in range(66)
                if all(positions[t][n][0] > 10 for t in times if t <= f3_time)]
    if not eligible:
        raise ValueError("no northern early controlled F3 candidate")
    f3_node = min(eligible, key=lambda n: (stable_hash(seed, "f3-node", n), n))
    pre_tasks = [t for t in tasks if t["task_id"] != victim["task_id"]
                 and victim["arrival_time_ns"] < t["arrival_time_ns"] < f3_time]
    if not pre_tasks:
        raise ValueError("no ordinary pre-F3 endpoint task; choose another deterministic window")
    ordinary = min(pre_tasks, key=lambda t: (t["input_bytes"], t["arrival_time_ns"], t["task_id"]))
    counts = {role: Counter() for role in ("source", "result")}
    placements = []
    fallback = Counter()
    max_age = 0
    region_totals = defaultdict(lambda: {"task_count": 0, "input_bytes": 0, "work_units": 0})
    for task in sorted(tasks, key=lambda t: (t["arrival_time_ns"], t["task_id"])):
        arrival = task["arrival_time_ns"]
        available = [n for n in range(66) if arrival < f3_time or n != f3_node]
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
        if task["task_id"] == victim["task_id"]:
            compute = f3_node
        else:
            # During the victim's controlled interval, avoid a second queued
            # compute endpoint; source use is allowed and verified with none.
            compute_options = [n for n in available if not
                (n == f3_node and victim["arrival_time_ns"] <= arrival < f3_time)]
            weights = [hot_weight if n in hot else 1 for n in compute_options]
            ticket = stable_hash(seed, task["task_id"], "compute") % sum(weights)
            for n, weight in zip(compute_options, weights):
                if ticket < weight:
                    compute = n
                    break
                ticket -= weight
        task["compute_node_id"] = compute
        for role in ("source", "result"):
            options = [n for n in available if n != compute and
                       not (role == "result" and n == f3_node and
                            arrival + (task["compute_work_units"] * 10**9 + 99999) // 100000 >= f3_time)]
            node = min(options, key=lambda n: (counts[role][n],
                       stable_hash(seed, task["task_id"], role, n), n))
            if task["task_id"] == ordinary["task_id"] and role == "source":
                node = f3_node
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
        if task["arrival_time_ns"] >= f3_time and any(
                task[k] == f3_node for k in ("source_node_id", "compute_node_id", "result_node_id")):
            raise AssertionError("post-F3 task uses permanently failed node")
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
                "f3": {"node_id": f3_node, "time_ns": f3_time, "victim_task_id": victim["task_id"],
                       "ordinary_task_id": ordinary["task_id"], "ordinary_role": "source",
                       "construction": "early LLM victim plus ordinary pre-F3 INPUT source; verify completed endpoint use in none and generate; no probability shielding"},
                "by_region": dict(region_totals), "placements": placements}
    return output, manifest
