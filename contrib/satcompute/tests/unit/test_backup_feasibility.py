"""Synthetic offline evidence for Pre-N5; never launch the simulator."""

import copy
import csv
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch
import contextlib
import io

TOOL = Path(__file__).resolve().parents[2] / "tools/validation/pre-n5-backup-feasibility/analyze.py"
AUDIT = runpy.run_path(str(TOOL))
Replay, distribution = AUDIT["Replay"], AUDIT["distribution"]
SECOND = 10**9


def task_event(time, task, old, new):
    return dict(simulation_time_ns=str(time), task_id=str(task), from_state=old, to_state=new)


def fault_event(time, node, kind="F1", event="START", healthy=False, communication=True, fault_id=1):
    return dict(simulation_time_ns=str(time), node_id=str(node), event_type=event, fault_id=str(fault_id),
                fault_source=kind if event == "START" else "", fault_type="satellite" if kind == "F3" else "compute",
                satellite_available_after=str(communication).lower(),
                compute_available_after=str(healthy).lower(), communication_available_after=str(communication).lower())


def transitions(task, start, end):
    return [task_event(start, task, "PENDING", "INPUT_TRANSFERRING"),
            task_event(start+1, task, "INPUT_TRANSFERRING", "QUEUED"),
            task_event(start+2, task, "QUEUED", "RUNNING"),
            task_event(end, task, "RUNNING", "FAILED")]


def graph(nodes):
    return [(a, b) for a in nodes for b in nodes if a != b]


def write_csv(path, rows):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=sorted({k for row in rows for k in row}))
        writer.writeheader()
        writer.writerows(rows)


class CandidateTests(unittest.TestCase):
    def replay(self, events=(), faults=(), edges=None):
        return Replay(range(4), {1: {"compute_node_id": "1"}}, events, faults,
                      graph(range(4)) if edges is None else edges)

    def test_multiple_and_self_exclusion(self):
        self.assertEqual(self.replay().candidates(0, 10)["candidates"], [1, 2, 3])

    def test_single_and_zero_candidates(self):
        faults = [fault_event(1, n) for n in (1, 2)]
        self.assertEqual(self.replay(faults=faults).candidates(0, 10)["candidates"], [3])
        result = self.replay(faults=[*faults, fault_event(2, 3)]).candidates(0, 10)
        self.assertEqual(result["candidates"], [])
        self.assertEqual(result["excluded_unhealthy"], 3)

    def test_queued_counts_as_busy_before_dispatch(self):
        events = transitions(1, 1, 10)
        replay = self.replay(events)
        self.assertNotIn(1, replay.candidates(0, 2)["candidates"])
        self.assertNotIn(1, replay.candidates(0, 3)["candidates"])
        self.assertIn(1, replay.candidates(0, 10)["candidates"])

    def test_f3_and_current_unreachable_exclusion(self):
        faults = [fault_event(10, 1, kind="F3", communication=False)]
        edges = [(0, 1), (1, 2), (0, 3)]
        replay = self.replay(faults=faults, edges=edges)
        self.assertEqual(replay.candidates(0, 9)["candidates"], [1, 2, 3])
        self.assertEqual(replay.candidates(0, 10)["candidates"], [3])
        self.assertEqual(replay.candidates(0, 10)["excluded_unreachable"], 1)

    def test_future_fault_and_future_task_do_not_filter_past(self):
        future = self.replay(transitions(1, 20, 30), [fault_event(40, 2, kind="F3", communication=False)])
        self.assertEqual(future.candidates(0, 10), self.replay().candidates(0, 10))

    def test_fault_pre_state_not_post_state(self):
        replay = self.replay(transitions(1, 1, 10), [fault_event(10, 2)])
        self.assertEqual(replay.candidates(0, 10, before=True)["candidates"], [2, 3])
        self.assertEqual(replay.candidates(0, 10)["candidates"], [1, 3])

    def test_overlap_recovery_uses_aggregate_after_state(self):
        faults = [fault_event(1, 1), fault_event(2, 1, kind="F2", fault_id=2),
                  fault_event(3, 1, event="RECOVERY"),
                  fault_event(4, 1, kind="F2", event="RECOVERY", healthy=True, fault_id=2)]
        replay = self.replay(faults=faults)
        self.assertNotIn(1, replay.candidates(0, 3)["candidates"])
        self.assertIn(1, replay.candidates(0, 4)["candidates"])

    def test_ambiguous_start_is_rejected_not_sorted_arbitrarily(self):
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            self.replay(transitions(1, 1, 10)).candidates(0, 2, strict_start=True)
        self.replay(transitions(1, 1, 10)).candidates(1, 2, strict_start=True)

    def test_persistent_task_endpoint_and_temporary_health(self):
        replay = self.replay(transitions(1, 9, 20), [fault_event(8, 2), fault_event(9, 2, event="RECOVERY", healthy=True)])
        self.assertIn(1, replay.persistent(0, 1, 10, before_end=True))
        self.assertNotIn(1, replay.persistent(0, 1, 10))
        self.assertNotIn(2, replay.persistent(0, 1, 10))
        self.assertIn(2, replay.candidates(0, 11)["candidates"])

    def test_persistent_includes_network_path_failure(self):
        replay = self.replay(faults=[fault_event(8, 1, kind="F3", communication=False)],
                             edges=[(0, 1), (1, 2), (0, 3)])
        self.assertEqual(replay.persistent(0, 1, 9), [3])

    def test_missing_state_and_bad_transition_rejected(self):
        bad = fault_event(1, 1)
        del bad["compute_available_after"]
        with self.assertRaisesRegex(ValueError, "boolean"):
            self.replay(faults=[bad])
        with self.assertRaisesRegex(ValueError, "transition"):
            self.replay([task_event(1, 1, "PENDING", "RUNNING")])

    def test_quantiles_and_zero_count(self):
        d = distribution([0, 0, 2, 4])
        self.assertEqual((d["count"], d["zero_count"], d["min"], d["median"], d["max"]), (4, 2, 0, 1, 4))
        self.assertAlmostEqual(d["p90"], 3.4)
        self.assertEqual(distribution([]), {"count": 0, "zero_count": 0})


class NativeLinkTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.replay = Replay([0, 1], {}, [], [fault_event(3*SECOND//2, 1, "F3", communication=False)], [])
        self.summary, self.windows = [], []
        for a, b in ((0, 1), (1, 0)):
            common = dict(source_node_id=a, destination_node_id=b, mean_link_capacity_bps=10**10)
            self.summary.append(dict(common, measurement_duration_s=2, available_time_s=1.5))
            for start, available in ((0, 1), (1, .5)):
                self.windows.append(dict(common, window_start_s=start, window_end_s=start+1, available_time_s=available))

    def validate(self):
        write_csv(self.root / "link-summary.csv", self.summary)
        write_csv(self.root / "link-window-metrics.csv", self.windows)
        return AUDIT["validate_native_links"](self.root, self.replay, 2*SECOND)

    def test_exact_fractional_fault_boundary_and_native_graph(self):
        self.assertEqual(self.validate()["validated_windows"], 4)
        self.assertEqual(self.replay.candidates(0, 3*SECOND//2, before=True)["candidates"], [1])
        self.assertEqual(self.replay.candidates(0, 3*SECOND//2)["candidates"], [])

    def test_missing_window_or_unexplained_link_change_rejected(self):
        self.windows.pop()
        with self.assertRaisesRegex(ValueError, "coverage"):
            self.validate()
        self.windows[1]["available_time_s"] = .4
        with self.assertRaisesRegex(ValueError, "unexplained"):
            self.validate()

    def test_partial_initial_window_is_not_guessed(self):
        self.windows[0]["available_time_s"] = .5
        with self.assertRaisesRegex(ValueError, "ambiguous initial"):
            self.validate()


def synthetic_records():
    tasks, shadow, events, decisions, faults, impacts, actual, task_events = {}, {}, [], [], [], [], [], []
    for task_id, primary, kind, mode, arrival, start, planned, end in (
            (1, 0, "F1", "ON", 1, 10, 15, 20),
            (2, 1, "F2", "INITIALIZING", 21, 25, 30, 26),
            (3, 2, "F3", "OFF", 31, None, None, 40)):
        tasks[task_id] = dict(compute_node_id=str(primary), final_state="FAILED", failure_time_ns=str(end))
        shadow[task_id] = dict(ever_start=str(start is not None).lower(), init_complete=str(mode == "ON").lower(),
                              start_time_ns=str(start) if start is not None else "", init_planned_complete_time_ns=str(planned),
                              init_complete_time_ns=str(planned) if mode == "ON" else "", shadow_mode_at_fault=mode,
                              real_fault_time_ns=str(end), real_fault_type=kind)
        if start is not None:
            events.append(dict(task_id=str(task_id), event="START", time_ns=str(start)))
            decisions.append(dict(task_id=str(task_id), time_ns=str(start), mode_before="OFF", mode_after="INITIALIZING", start_triggered="true"))
        if mode == "ON":
            events.append(dict(task_id=str(task_id), event="ON", time_ns=str(planned)))
        faults.append(dict(task_id=str(task_id), fault_id=str(task_id), fault_time=str(end), fault_type=kind,
                           mode_at_fault=mode, primary_f1_f2=str(kind != "F3").lower()))
        impacts.append(dict(task_id=str(task_id), fault_id=str(task_id), fault_time_ns=str(end), fault_type=kind,
                            fault_node_id=str(primary), task_state_before_fault="RUNNING", task_state_before_impact="RUNNING",
                            impact_type="RUNNING_INTERRUPTED_PERMANENT" if kind == "F3" else "RUNNING_INTERRUPTED"))
        actual.append(fault_event(end, primary, kind, communication=kind != "F3", fault_id=task_id))
        task_events.extend(transitions(task_id, arrival, end))
    replay = Replay(range(4), tasks, task_events, actual, graph(range(4)))
    return replay, tasks, shadow, events, decisions, faults, impacts, actual


class EvidenceTests(unittest.TestCase):
    def test_end_to_end_modes_lead_times_and_f3_separation(self):
        summary, tables = AUDIT["audit_records"](*synthetic_records())
        self.assertEqual((summary["gate_a"], summary["gate_b"]), ("PASS", "PASS"))
        self.assertEqual(summary["victim_modes"], {"ON": 1, "INITIALIZING": 1})
        self.assertEqual(summary["primary_victims"], 2)
        self.assertEqual(summary["f3_appendix"][0]["task_id"], 3)
        miss = summary["initialization_misses"][0]
        self.assertIsNone(miss["actual_init_margin_s"])
        self.assertIsNone(miss["init_complete_time_ns"])
        self.assertEqual(miss["planned_init_margin_s"], -4/SECOND)
        self.assertEqual(len(tables["backup-candidates-at-fault.csv"]), 2)

    def test_read_recorded_mode_not_reclassify_initialization(self):
        records = list(synthetic_records())
        records[5][1]["mode_at_fault"] = "ON"
        with self.assertRaisesRegex(ValueError, "mode mismatch"):
            AUDIT["audit_records"](*records)

    def test_zero_candidates_produce_failed_gates(self):
        records = list(synthetic_records())
        records[0].edges = set()
        summary, _ = AUDIT["audit_records"](*records)
        self.assertEqual((summary["gate_a"], summary["gate_b"]), ("FAIL", "FAIL"))

    def test_duplicate_and_mismatched_start_rejected(self):
        records = list(synthetic_records())
        records[3].append(copy.copy(records[3][0]))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            AUDIT["audit_records"](*records)
        records = list(synthetic_records())
        records[4][0]["time_ns"] = "11"
        with self.assertRaisesRegex(ValueError, "START time"):
            AUDIT["audit_records"](*records)

    def test_repeat_output_byte_identical_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            left, right = Path(tmp)/"a", Path(tmp)/"b"
            one = AUDIT["audit_records"](*synthetic_records())
            two = AUDIT["audit_records"](*synthetic_records())
            self.assertEqual(one, two)
            for target, result in ((left, one), (right, two)):
                AUDIT["write_results"](target, *result)
            self.assertEqual({p.name: p.read_bytes() for p in left.iterdir()},
                             {p.name: p.read_bytes() for p in right.iterdir()})
            with self.assertRaises(FileExistsError):
                AUDIT["write_results"](left, *one)

    def test_missing_csv_field_and_nanosecond_precision(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/"missing.csv"
            write_csv(path, [{"task_id": 1}])
            with self.assertRaisesRegex(ValueError, "missing columns"):
                list(AUDIT["csv_rows"](path, "task_id time_ns"))
        self.assertEqual(AUDIT["seconds_ns"]("1027.055770726"), 1027055770726)
        with self.assertRaises(ValueError):
            AUDIT["seconds_ns"]("0.0000000005")

    def test_task_event_summary_timestamp_disagreement_rejected(self):
        task = dict(compute_node_id="1", final_state="FAILED", arrival_time_ns="1",
                    queue_enter_time_ns="2", compute_start_time_ns="3", failure_time_ns="10")
        events = [{**e, "node_id": "1"} for e in transitions(1, 1, 10)]
        replay = Replay([0, 1], {1: task}, events, [], [(0, 1), (1, 0)])
        AUDIT["validate_task_history"](replay, {1: task}, events, 20)
        task["failure_time_ns"] = "11"
        with self.assertRaisesRegex(ValueError, "timestamp mismatch"):
            AUDIT["validate_task_history"](replay, {1: task}, events, 20)

    def test_csv_rows_remain_unchanged_by_analysis(self):
        records = synthetic_records()
        original = copy.deepcopy(records[1:])
        AUDIT["audit_records"](*records)
        self.assertEqual(records[1:], original)

    def test_missing_source_stops_without_launching_any_process(self):
        with tempfile.TemporaryDirectory() as tmp, \
                patch("sys.argv", ["analyze", "--source", str(Path(tmp)/"missing")]), \
                patch("subprocess.check_output") as execute, contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as caught:
                AUDIT["main"]()
            self.assertEqual(caught.exception.code, 2)
            execute.assert_not_called()


if __name__ == "__main__":
    unittest.main()
