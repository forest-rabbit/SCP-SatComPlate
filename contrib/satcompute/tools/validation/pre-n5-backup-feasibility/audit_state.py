"""Causal event-history queries; no simulator, orbital model, or reservations."""

from bisect import bisect_left, bisect_right


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(row, key):
    try:
        return int(row[key])
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError(f"missing/invalid integer {key}: {row.get(key)!r}") from error


def boolean(row, key):
    value = row.get(key)
    require(value in ("true", "false"), f"missing/invalid boolean {key}: {value!r}")
    return value == "true"


class History:
    def __init__(self, initial):
        self.initial = initial
        self.times = []
        self.values = []

    def add(self, time, value):
        require(time >= 0 and (not self.times or time >= self.times[-1]),
                "non-monotonic history")
        if value != (self.values[-1] if self.values else self.initial):
            self.times.append(time)
            self.values.append(value)

    def at(self, time, before=False):
        index = (bisect_left if before else bisect_right)(self.times, time) - 1
        return self.initial if index < 0 else self.values[index]

    def changes_at(self, time):
        return bisect_left(self.times, time) != bisect_right(self.times, time)

    def all_between(self, start, end, value, before_end=False):
        """Include START post-state and each transition; optionally exclude end events."""
        lo = bisect_right(self.times, start)
        hi = (bisect_left if before_end else bisect_right)(self.times, end)
        return self.at(start) == value and all(v == value for v in self.values[lo:hi])


ALLOWED = {
    "PENDING": {"INPUT_TRANSFERRING", "FAILED"},
    "INPUT_TRANSFERRING": {"QUEUED", "FAILED"},
    "QUEUED": {"RUNNING", "FAILED"},
    "RUNNING": {"RESULT_TRANSFERRING", "FAILED"},
    "RESULT_TRANSFERRING": {"COMPLETED", "FAILED"},
    "COMPLETED": set(), "FAILED": set(),
}


class Replay:
    """Reconstruct observable compute state and communication state from past events.

    Input edges are native *initial active* directed links, not a Python constellation.
    The file adapter must prove that natural links stay fixed; communication outages
    then remove all incident edges at their recorded event time.
    """

    def __init__(self, nodes, tasks, task_events, fault_events, edges):
        self.nodes = set(nodes)
        require(bool(self.nodes), "empty compute-node universe")
        self.tasks = tasks
        self.edges = set(edges)
        require(all(a in self.nodes and b in self.nodes and a != b for a, b in self.edges),
                "invalid native link endpoint")
        self.busy = {n: History(False) for n in self.nodes}
        self.healthy = {n: History(True) for n in self.nodes}
        self.communication = {n: History(True) for n in self.nodes}
        self.task_state = {i: History("PENDING") for i in tasks}
        states = {i: "PENDING" for i in tasks}
        queued = {n: set() for n in self.nodes}
        running = {n: set() for n in self.nodes}
        previous = -1
        for event in task_events:
            time, task = integer(event, "simulation_time_ns"), integer(event, "task_id")
            require(time >= previous and time >= 0, "task events are not chronological")
            previous = time
            require(task in tasks, f"unknown task {task}")
            node = integer(tasks[task], "compute_node_id")
            require(node in self.nodes, f"unknown compute node {node}")
            old, new = event["from_state"], event["to_state"]
            require(old == states[task] and new in ALLOWED[old],
                    f"invalid task transition for {task}: {old} -> {new}")
            if old == "QUEUED":
                queued[node].remove(task)
            if old == "RUNNING":
                running[node].remove(task)
            if new == "QUEUED":
                queued[node].add(task)
            if new == "RUNNING":
                running[node].add(task)
            require(len(running[node]) <= 1, f"multiple RUNNING tasks on {node}")
            states[task] = new
            self.task_state[task].add(time, new)
            # ComputeService::IsIdle = !m_hasCurrentTask && m_queue.empty().
            self.busy[node].add(time, bool(queued[node] or running[node]))
        previous = -1
        for event in fault_events:
            time, node = integer(event, "simulation_time_ns"), integer(event, "node_id")
            require(time >= previous and time >= 0, "fault events are not chronological")
            previous = time
            require(node in self.nodes, f"unknown fault node {node}")
            require(event["event_type"] in {"NOTICE", "START", "RECOVERY"},
                    "unknown fault event type")
            satellite = boolean(event, "satellite_available_after")
            compute = boolean(event, "compute_available_after")
            communication = boolean(event, "communication_available_after")
            require(satellite or (not compute and not communication),
                    "inconsistent satellite-down state")
            # Consume platform aggregate after-state, not a single-fault recovery flag.
            self.healthy[node].add(time, satellite and compute)
            self.communication[node].add(time, satellite and communication)

    def reachable(self, primary, time, before=False):
        available = {n for n in self.nodes if self.communication[n].at(time, before)}
        if primary not in available:
            return set()
        adjacency = {n: [] for n in available}
        for a, b in self.edges:
            if a in available and b in available:
                adjacency[a].append(b)
        reached, pending = {primary}, [primary]
        while pending:
            for node in adjacency[pending.pop()]:
                if node not in reached:
                    reached.add(node)
                    pending.append(node)
        return reached

    def candidates(self, primary, time, before=False, strict_start=False):
        require(primary in self.nodes and time >= 0, "invalid candidate query")
        if strict_start:
            # CSVs have no shared ns-3 event UID. Do not invent a cross-file tie order.
            ambiguous = any(self.busy[n].changes_at(time) or self.healthy[n].changes_at(time)
                            for n in self.nodes - {primary})
            ambiguous |= any(h.changes_at(time) for h in self.communication.values())
            require(not ambiguous, f"ambiguous same-nanosecond START state at {time}")
        peers = self.nodes - {primary}
        healthy = {n for n in peers if self.healthy[n].at(time, before)}
        idle = {n for n in healthy if not self.busy[n].at(time, before)}
        reachable = self.reachable(primary, time, before)
        candidates = sorted(idle & reachable)
        return {
            "candidates": candidates,
            "excluded_unhealthy": len(peers - healthy),
            "excluded_busy_or_queued": len(healthy - idle),
            "excluded_unreachable": len(idle - reachable),
        }

    def persistent(self, primary, start, end, before_end=False):
        require(end >= start, "negative protection observation interval")
        candidates = set(self.candidates(primary, start, strict_start=True)["candidates"])
        candidates = {n for n in candidates
                      if self.busy[n].all_between(start, end, False, before_end)
                      and self.healthy[n].all_between(start, end, True, before_end)}
        # Connectivity can change only at communication-state transitions, as
        # established by the adapter's complete native link-window validation.
        times = {t for h in self.communication.values() for t in h.times
                 if start < t < end or (not before_end and start < t == end)}
        for time in sorted(times):
            candidates &= self.reachable(primary, time)
        return sorted(candidates)


def distribution(values):
    values = sorted(values)
    if not values:
        return {"count": 0, "zero_count": 0}
    result = {"count": len(values), "zero_count": values.count(0),
              "min": values[0], "max": values[-1]}
    for name, percentile in (("p10", .1), ("p25", .25), ("median", .5), ("p75", .75), ("p90", .9)):
        position = (len(values) - 1) * percentile
        lower = int(position)
        result[name] = values[lower] + (values[min(lower + 1, len(values)-1)] - values[lower]) * (position-lower)
    return result
