#!/usr/bin/env python3
"""Audit real controlled JIT evidence and prove corruption detection."""
import argparse
import copy
from pathlib import Path
import runpy

API = runpy.run_path(str(Path(__file__).parents[1] / "regression/audit-jit-input.py"))
FILES = ("input-staging-lifecycles.csv", "protection-transfers.csv", "recovery-summary.csv",
         "protection-events.csv", "jit-input-decisions.csv", "frequency-decisions.csv",
         "protection-node-storage-summary.csv")


def verify(root):
    directories = sorted(p for p in root.iterdir() if p.is_dir())
    assert len(directories) == 15, "missing controlled/online JIT fixture"
    data = {}
    for directory in directories:
        API["audit"](directory)
        data[directory.name] = [API["rows"](directory, name) for name in FILES]
    same_ns = copy.deepcopy(data["ready"])
    initialized = next(e for e in same_ns[3] if e["event"] == "INIT_COST_COMMITTED")
    initialized["time_ns"] = same_ns[0][0]["requested_ns"]
    API["validate"](*same_ns)
    data["same-ns-initialization"] = same_ns

    def reverse_initialization_order(d):
        i = next(i for i, e in enumerate(d[3]) if e["event"] == "INIT_COST_COMMITTED")
        j = next(i for i, e in enumerate(d[3]) if e["event"] == "PREFETCH_INPUT_REQUESTED")
        d[3][i], d[3][j] = d[3][j], d[3][i]

    corruptions = [
        ("same-ns-initialization", reverse_initialization_order),
        ("same-ns-initialization", lambda d: next(e for e in d[3] if e["event"] == "INIT_COST_COMMITTED").update(
            time_ns=str(int(d[0][0]["requested_ns"])+1))),
        ("inflight", lambda d: d[0][0].update(prefetch_total_sent_bytes="1")),
        ("inflight", lambda d: d[0][0].update(prefetch_after_fault_sent_bytes="0")),
        ("inflight", lambda d: d[0][0].update(input_object_id="999999")),
        ("inflight", lambda d: d[2][0].update(reused_input_transfer_id="999999")),
        ("inflight", lambda d: d[2][0].update(recovery_compute_start_time_ns="1")),
        ("inflight", lambda d: d[1].append({**next(f for f in d[1] if f["kind"] == "PREFETCH_INPUT"),
                                           "transfer_id": "999999", "kind": "RECOVERY_INPUT"})),
        ("ready", lambda d: d[0][0].update(stage="READY")),
        ("ready", lambda d: d[6][0].update(final_reserved_bytes="1")),
        ("local-ready", lambda d: d[0][0].update(input_transfer_id="999999")),
        ("same-ns-local", lambda d: d[2][0].update(input_state_at_fault="READY")),
        ("online-f1", lambda d: next(r for r in d[5] if r.get("delta_j_input")).update(delta_j_input="99")),
    ]
    for case, mutate in corruptions:
        changed = copy.deepcopy(data[case]); mutate(changed)
        try:
            API["validate"](*changed)
        except ValueError:
            continue
        raise AssertionError(f"corrupt JIT evidence accepted: {case}")
    print(f"JIT audit: {len(directories)} fixtures and same-ns initialization passed; "
          f"{len(corruptions)} corruptions rejected")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    verify(parser.parse_args().root)
