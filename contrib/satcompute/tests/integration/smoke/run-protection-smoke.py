#!/usr/bin/env python3
"""N5A-G2: four typed tasks, real UDP/storage timeline, no fault or formal scene."""
import argparse
import csv
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[5]
FIXTURE = ROOT / "contrib/satcompute/tests/fixtures/protection"
OUTPUTS = ("protection-events.csv", "protection-transfers.csv",
           "protection-task-summary.csv", "protection-node-storage-summary.csv")


def rows(directory, name):
    with (directory / name).open() as stream:
        return list(csv.DictReader(stream))


def run(directory, mode, capacity=10_000_000_000):
    args = ["satcompute", "--simulationDuration=15", "--randomSeed=1", "--randomRun=11",
            "--faultMode=none", "--compfrr-shadow=0", "--taskCompletionPolicy=strict",
            "--constellationConfig=contrib/satcompute/tests/fixtures/constellation/connected-16.csv",
            f"--taskTrace={FIXTURE / 'fixed-four-profiles.json'}",
            f"--computeProfile={FIXTURE / 'fixed-compute.json'}",
            f"--protectionMode={mode}", f"--backupStorageBytesPerNode={capacity}",
            "--fixedProtectionDelta=0.05", "--fixedProtectionBatchN=4",
            "--routingMode=global-capacity-aware-hrw", "--islBandwidthBps=10000000000",
            "--delayMode=fixed", "--fixedDelay=0.001", f"--outputDir={directory}"]
    result = subprocess.run([str(ROOT / "ns3"), "run", "--no-build", shlex.join(args)],
                            cwd=ROOT, text=True, capture_output=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr


def verify(directory):
    events = rows(directory, OUTPUTS[0])
    flows = rows(directory, OUTPUTS[1])
    tasks = rows(directory, OUTPUTS[2])
    assert len(tasks) == 4
    assert {int(row["transfer_id"]) for row in rows(directory, "transfer-summary.csv")} == set(range(1, 9))
    ids = [int(row["transfer_id"]) for row in flows]
    assert ids == list(range(9, 9 + len(ids)))
    tuples = [(r["source_node"], r["source_port"], r["destination_node"], r["destination_port"]) for r in flows]
    assert len(set(tuples)) == len(tuples)
    for pool in rows(directory, OUTPUTS[3]):
        assert int(pool["used_bytes"]) == int(pool["reserved_bytes"]) == 0
        assert int(pool["peak_total_bytes"]) <= int(pool["capacity_bytes"])
    assert all(int(e["remote_work_units"]) <= int(e["local_work_units"]) <= int(e["actual_work_units"])
               for e in events)
    by_id = {int(row["transfer_id"]): row for row in flows}
    for row in flows:
        assert int(row["bytes"]) > 0
        assert int(row["start_time_ns"]) == int(row["requested_time_ns"]) + 1
        if row["state"] == "COMPLETED":
            assert int(row["received_bytes"]) == int(row["bytes"])
            assert int(row["received_time_ns"]) > int(row["sender_finished_time_ns"])
        assert row["state"] in ("COMPLETED", "CANCELLED")
        reserved = [e for e in events if e["event"] == "STORAGE_RESERVED" and
                    e["task_id"] == row["task_id"] and
                    e["storage_node"] == row["destination_node"] and
                    e["storage_object_id"] == row["storage_object_id"]]
        assert len(reserved) == 1 and int(reserved[0]["time_ns"]) <= int(row["requested_time_ns"])
    for row in tasks:
        tid = row["task_id"]
        e = [r for r in events if r["task_id"] == tid]
        assert int(row["init_complete_time_ns"]) > int(row["start_time_ns"])
        assert int(row["local_commit_count"]) >= 4 and int(row["remote_commit_count"]) >= 2
        names = {r["event"] for r in e}
        assert {"START", "INIT_STATE_GENERATED", "INIT_BASE_RECEIVED", "INIT_COMPLETE", "ON",
                "L1_CAPTURED", "L1_GENERATED", "L1_COMMITTED_LOCAL", "REMOTE_BATCH_STARTED",
                "REMOTE_BATCH_RECEIVED", "REMOTE_COMMIT", "LOCAL_CLEANUP",
                "COMPUTE_COMPLETE", "PROTECTION_STOP"} <= names
        assert len([r for r in e if r["event"] == "PROTECTION_STOP"]) == 1
        assert not any(f["kind"] == "INIT_STATE" and f["task_id"] == tid for f in flows)
        last = 0
        variable, total = int(row["variable_state_bytes"]), int(row["total_work_units"])
        header = 0 if tid == "4" else (48 if tid == "2" else 44) + len(tid)
        state_bytes = lambda w: (w // 100 * 114688 if tid == "4" else variable * w // total)
        generated = {}
        records = {}
        remote = 0
        for item in e:
            time, work = int(item["time_ns"]), int(item["work_units"])
            if item["event"] == "L1_CAPTURED":
                assert int(item["bytes"]) == state_bytes(work) - state_bytes(last) + header
                records[work] = int(item["bytes"])
                generated[work] = time + int(row["cL_ns"])
                last = work
            elif item["event"] == "L1_GENERATED":
                assert time == generated[work]
            elif item["event"] == "REMOTE_BATCH_STARTED":
                batch = [b for w, b in records.items() if remote < w <= work]
                assert len(batch) == 4 and sum(batch) == int(item["bytes"])
            elif item["event"] == "REMOTE_COMMIT":
                receipt = next(r for r in e if r["event"] == "REMOTE_BATCH_RECEIVED" and
                               r["work_units"] == item["work_units"])
                assert time == int(receipt["time_ns"]) + int(row["cR_ns"])
                assert int(receipt["remote_work_units"]) == remote
                assert int(item["remote_used_bytes"]) == int(item["bytes"])
                assert int(item["remote_reserved_bytes"]) == 0
                remote = work
            elif item["event"] == "LOCAL_CLEANUP":
                assert work <= remote
            elif item["event"] == "TRANSFER_RECEIVED":
                assert int(by_id[int(item["transfer_id"])]["received_time_ns"]) == time
        init = next(r for r in e if r["event"] == "INIT_COMPLETE")
        state = next(r for r in e if r["event"] == "INIT_STATE_LOGICAL_COMPLETE")
        base = next(r for r in e if r["event"] == "INIT_BASE_RECEIVED")
        assert int(init["time_ns"]) == max(int(state["time_ns"]), int(base["time_ns"])) + int(row["cR_ns"])
        assert int(init["remote_used_bytes"]) == int(init["bytes"])
        if tid == "4":
            assert int(init["bytes"]) == 0, "zero-byte remote LLM state has explicit ON"
    return {"tasks": 4, "backup_flows": len(flows),
            "completed_backup_flows": sum(r["state"] == "COMPLETED" for r in flows),
            "cancelled_backup_flows": sum(r["state"] == "CANCELLED" for r in flows),
            "remote_commits_including_init": sum(int(r["remote_commit_count"]) for r in tasks)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-root", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="satcompute-g2-smoke-") as temporary:
        directory = args.output_root or Path(temporary)
        off, on, repeat, full = [directory / n for n in ("off", "fixed", "repeat", "pool-full")]
        run(off, "off")
        run(on, "fixed")
        result = verify(on)
        assert not any((off / name).exists() for name in OUTPUTS)
        business = lambda p: [(r["task_id"], r["compute_start_time_ns"], r["compute_complete_time_ns"])
                              for r in rows(p, "task-summary.csv")]
        assert business(off) == business(on), "cL/cR delayed ordinary compute"
        off_summary = json.loads((off / "run-summary.json").read_text())
        on_summary = json.loads((on / "run-summary.json").read_text())
        for key in ("sent_application_bytes", "received_application_bytes",
                    "declared_application_bytes", "task_count", "completed_task_count"):
            assert off_summary[key] == on_summary[key], f"backup mixed into business {key}"
        link_bits = lambda p: sum(int(r["serialized_bits"])
                                 for r in rows(p, "network-link-window-metrics.csv"))
        assert link_bits(on) > link_bits(off), "real backup did not contribute to link load"
        run(repeat, "fixed")
        for name in OUTPUTS:
            assert (on / name).read_bytes() == (repeat / name).read_bytes(), name
        run(full, "fixed", 1)
        assert not rows(full, OUTPUTS[1]), "capacity failure created a real flow"
        assert all(r["stop_reason"] == "INIT_RESERVATION_FAILED" for r in rows(full, OUTPUTS[2]))
        assert business(off) == business(full), "backup capacity failure affected primary"
        result.update(deterministic=True, uncontended_compute_identical=True, storage_leaks=0)
        print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
