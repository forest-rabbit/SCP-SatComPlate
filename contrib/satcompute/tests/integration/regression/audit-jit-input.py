#!/usr/bin/env python3
"""Independent V7 ledger audit. No simulator, model tuning, RNG or raw-evidence rewrites."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path


def require(value, message):
    if not value:
        raise ValueError(message)


def rows(root, name):
    with (root / name).open() as stream:
        result = list(csv.DictReader(stream))
    require(all(None not in row and None not in row.values() for row in result), f"malformed {name}")
    return result


def n(row, key):
    return int(row.get(key) or 0)


def validate(lifecycles, flows, recoveries, events, decisions, frequency, pools):
    by_task = {r["task_id"]: r for r in lifecycles}
    require(len(by_task) == len(lifecycles), "duplicate INPUT lifecycle")
    transfers = {r["transfer_id"]: r for r in flows}
    require(len(transfers) == len(flows), "duplicate physical flow identity")
    prefetch = [f for f in flows if f["kind"] == "PREFETCH_INPUT"]
    require(all(f["kind"] != "INIT_BASE" for f in flows), "JIT initialized original INPUT eagerly")
    require(all(count == 1 for count in Counter(f["task_id"] for f in prefetch).values()),
            "established prefetch was automatically restarted")
    require(all(f["task_id"] in by_task for f in prefetch), "orphan prefetch flow")
    recovery = {r["task_id"]: r for r in recoveries}
    require(len(recovery) == len(recoveries), "duplicate recovery")
    owned_events = defaultdict(list)
    for e in events:
        owned_events[e["task_id"]].append(e)
    total = before = after = used = admitted = local_count = 0
    for task, s in by_task.items():
        require(s["stage"] == "RELEASED", "unreleased INPUT lifecycle")
        sent, normal, fault, useful, unused = (n(s, k) for k in (
            "prefetch_total_sent_bytes", "prefetch_before_fault_sent_bytes", "prefetch_after_fault_sent_bytes",
            "prefetch_used_bytes", "prefetch_unused_bytes"))
        require(0 <= sent == normal + fault == useful + unused <= n(s, "input_bytes"), "INPUT byte partition")
        require(useful == (sent if s["used"] == "1" else 0), "used bytes inconsistent lifecycle scope")
        require(s["used"] in ("0", "1") and s["adopted"] in ("0", "1"), "invalid lifecycle flags")
        local = s["delivery_mode"] == "LOCAL"
        require(local == (s["source_node"] == s["holder_node"]), "local INPUT endpoints")
        established = n(s, "registered_ns") >= 0
        admitted += established; local_count += established and local
        require(n(s, "released_ns") >= n(s, "requested_ns"), "release before request")
        if n(s, "fault_ns") < 0:
            require(fault == 0, "post-fault bytes without primary fault")
        if local:
            require(n(s, "input_transfer_id") == sent == normal == fault == useful == unused == 0,
                    "LocalDelivery fabricated UDP bytes")
        elif established:
            f = transfers.get(s["input_transfer_id"])
            require(f and f["kind"] == "PREFETCH_INPUT" and f["task_id"] == task, "missing original prefetch ID")
            require(f["storage_object_id"] == s["input_object_id"] and f["source_node"] == s["source_node"] and
                    f["destination_node"] == s["holder_node"] and n(f, "bytes") == n(s, "input_bytes"),
                    "INPUT identity/endpoints/full S mismatch")
            require(n(f, "sent_bytes") == sent, "lost actual prefetch bytes after release/fault")
            require(f["state"] in ("COMPLETED", "FAILED", "CANCELLED"), "live prefetch flow")
            if n(s, "ready_ns") >= 0:
                require(f["state"] == "COMPLETED" and n(f, "received_bytes") == n(s, "input_bytes") and
                        n(f, "received_time_ns") == n(s, "ready_ns"), "READY without full original receiver")
        else:
            require(not n(s, "input_transfer_id") and sent == 0, "unadmitted request has traffic")
        if established:
            reserved = [e for e in owned_events[task] if e["event"] == "STORAGE_RESERVED" and
                        e["storage_object_id"] == s["input_object_id"] and e["storage_node"] == s["holder_node"]]
            require(len(reserved) == 1 and n(reserved[0], "bytes") == n(s, "input_bytes") and
                    n(reserved[0], "time_ns") <= n(s, "registered_ns"), "INPUT missing independent S reservation")
            initialized = [e for e in owned_events[task] if e["event"] == "INIT_COST_COMMITTED"]
            require(len(initialized) == 1 and n(initialized[0], "time_ns") < n(s, "requested_ns"),
                    "JIT before physical state initialization")
        if s["used"] == "1":
            r = recovery.get(task)
            require(r and r["input_reused"] == "1" and r["recovery_compute_start_time_ns"] and
                    s["adopted"] == "1", "used INPUT never consumed by real recovery compute")
        total += sent; before += normal; after += fault; used += useful
    states = Counter()
    reuse = Counter()
    for task, r in recovery.items():
        state = r["input_state_at_fault"]
        states[state if state in ("READY", "IN_FLIGHT") else "ABSENT"] += 1
        require(r.get("input_staging_policy") == "jit", "wrong recovery staging identity")
        inputs = [f for f in flows if f["task_id"] == task and f["kind"] == "RECOVERY_INPUT"]
        if r["input_reused"] == "1":
            s = by_task[task]
            require(not inputs, "duplicate full recovery INPUT despite same-target reuse")
            require(r["recovery_node"] == s["holder_node"] and
                    r["reused_input_object_id"] == s["input_object_id"] and
                    r["reused_input_transfer_id"] == s["input_transfer_id"], "handoff changed target/object/flow")
            require(r["input_state_at_acceptance"] in ("READY", "IN_FLIGHT"), "invalid reused state")
            reuse[r["input_state_at_acceptance"]] += 1
            if r["input_state_at_acceptance"] == "READY":
                require(r["input_received_time_ns"] == r["recovery_accept_time_ns"], "READY did not remove input wait")
            elif r["input_received_time_ns"]:
                require(n(r, "input_received_time_ns") == n(s, "ready_ns"), "in-flight handoff used synthetic completion")
        if state == "READY":
            require(n(by_task[task], "ready_ns") < n(r, "fault_time_ns"), "same-ns arrival labeled prefault READY")
        if r["recovery_compute_start_time_ns"]:
            require(r["input_received_time_ns"] and r["state_ready_time_ns"] and
                    n(r, "recovery_compute_start_time_ns") ==
                    max(n(r, "input_received_time_ns"), n(r, "state_ready_time_ns")), "input/state dependency barrier")
    for d in decisions:
        require(d["trigger"] in ("INITIALIZATION_COMMITTED", "FAULT_EPOCH_SURVIVED", "CAPACITY_RELEASE"),
                "new illegal JIT timer/trigger")
        require(n(d, "next_jit_evaluation_ns") > n(d, "time_ns"), "JIT next event not future")
        r = recovery.get(d["task_id"])
        require(not r or n(d, "time_ns") < n(r, "fault_time_ns"), "JIT retroactively protected current fault")
        if d["representative_fault_time_ns"]:
            expected = float(d["p_on"]) > 0 and float(d["representative_fault_time_ns"]) <= \
                n(d, "next_jit_evaluation_ns") + float(d["input_transfer_s"]) * 1e9
            require((d["jit_should_prefetch"] == "1") == expected, "event-aware JIT timing mismatch")
    for d in frequency:
        if d.get("delta_j_input"):
            gain, baseline, loss = (float(d[k]) for k in ("delta_j_input", "j_input_deferred", "j_input_prefetch"))
            require(math.isclose(gain, baseline-loss, abs_tol=1e-12) and -1e-12 <= gain <= baseline+1e-12,
                    "START INPUT gain inconsistent")
    require(all(n(p, "final_used_bytes") == n(p, "final_reserved_bytes") == 0 for p in pools), "storage leak")
    return dict(status="PASS", jit_evaluations=len(decisions), jit_triggers=sum(d["jit_should_prefetch"] == "1" for d in decisions),
        jit_admitted=admitted, fault_ready=states["READY"], fault_in_flight=states["IN_FLIGHT"], fault_absent=states["ABSENT"],
        ready_reuse_count=reuse["READY"], in_flight_reuse_count=reuse["IN_FLIGHT"],
        prefetch_total_sent_bytes=total, prefetch_before_fault_sent_bytes=before, prefetch_after_fault_sent_bytes=after,
        prefetch_used_bytes=used, prefetch_unused_bytes=total-used,
        new_recovery_input_bytes=sum(n(f, "sent_bytes") for f in flows if f["kind"] == "RECOVERY_INPUT"),
        duplicate_full_input_detected=0, final_storage_leaks=0, final_live_jit_flows=0, local_prefetch_count=local_count)


def audit(root):
    result = validate(*(rows(root, name) for name in (
        "input-staging-lifecycles.csv", "protection-transfers.csv", "recovery-summary.csv", "protection-events.csv",
        "jit-input-decisions.csv", "frequency-decisions.csv", "protection-node-storage-summary.csv")))
    observed = json.loads((root / "v7-jit-audit.json").read_text())
    for key in set(result) & set(observed):
        require(result[key] == observed[key], f"runtime/independent audit disagrees: {key}")
    require(observed["quiescent"] and observed["final_live_jit_objects"] == 0, "runtime not quiescent")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = audit(args.root)
    if args.output:
        require(not args.output.exists(), "refusing to overwrite existing audit")
        args.output.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
