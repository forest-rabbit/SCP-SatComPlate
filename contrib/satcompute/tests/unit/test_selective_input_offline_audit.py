"""Synthetic START/causality gates; no historical data dependency or formal runs."""
from copy import deepcopy
import json
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

API = runpy.run_path(str(Path(__file__).resolve().parents[1] / "support/protection/selective_input_offline_audit.py"))
NS = API["NS"]


def fixture():
    task = dict(task_id="1", task_profile="dense-image", source_node_id="1", compute_node_id="2",
                result_node_id="3", input_bytes="100000000", output_bytes="100", compute_work_units="500000",
                compute_rate_work_units_per_second="100000", arrival_time_ns="0",
                compute_start_time_ns="500000000", baseline_compute_time_ns="5000000000",
                compute_deadline_budget_ns="6500000000", compute_deadline_time_ns="7000000000", task_success="1")
    row = dict(task_id="1", fault_epoch_time_ns="500000000", decision_trigger="TASK_RUNNING", progress_work="0",
               local_node="3", remote_node="4", recovery_rate="100000", committed_delta_permille="100",
               committed_n="10", first_sample_time_ns="1000000000", q_comp_snapshot="0.1", p_fail_before_finish="0.5",
               input_bandwidth_bytes_per_s="1000000000", backup_bandwidth_bytes_per_s="1000000000", t_init_s="0.01",
               proposed_action="START", decision_committed="1", phase_before="OFF", actual_fault_hit="0",
               actual_fault_sampled="0", csv_line=2)
    event = dict(task_id="1", event="START", time_ns="500000000", local_node="3", remote_node="4", attempt_generation="0")
    selection = dict(task_id="1", time_ns="500000000", local_node="3", remote_node="4", actual_admission="ACCEPTED")
    return task, row, event, selection


def outcome():
    task = fixture()[0]
    impact = dict(task_id="1", fault_id="2", fault_type="F1", fault_time_ns="4000000000", fault_node_id="2",
                  compute_deadline_time_ns=task["compute_deadline_time_ns"])
    recovery = dict(task_id="1", fault_id="2", fault_type="compute", fault_time_ns="4000000000", primary_node="2",
                    original_deadline_ns=task["compute_deadline_time_ns"], recovery_node="4", chosen_path="TAIL",
                    recovery_accept_time_ns="4000000001", input_received_time_ns="4100000000",
                    state_ready_time_ns="4050000000", recovery_compute_start_time_ns="4100000000")
    return task, impact, recovery


class SelectiveInputAuditTests(unittest.TestCase):
    def test_role_identity_is_pinned_and_not_poolable(self):
        e = dict(commit=API["EXECUTION"], input_staging_policy="jit", jit_start_benefit=False,
                 command=["satcompute --randomSeed=1 --randomRun=11 --simulationDuration=1300 "
                          "--faultMode=generate --faultProbabilityAudit=0 --inputStagingPolicy=jit --jitStartBenefit=0"])
        API["role_check"](e, "V6START", 800)
        for role, count, update in (("FULL_V7", 800, {}), ("DEFERRED", 800, {}), ("V6START", 799, {}),
                                    ("V6START", 800, {"commit": "different"}),
                                    ("V6START", 800, {"jit_start_benefit": True})):
            with self.assertRaises(ValueError):
                API["role_check"](dict(e, **update), role, count)
        with self.assertRaises(ValueError):
            API["identity_audit"](dict.fromkeys(API["ROLES"], Path("same")))

    def test_successful_start_requires_all_three_evidence_types(self):
        _, row, event, selection = fixture()
        accepted, excluded = API["candidate_starts"]([row], [event], [selection])
        self.assertEqual(len(accepted), 1)
        self.assertEqual(excluded, [])
        for events, selections in (([], []), ([event], [])):
            accepted, excluded = API["candidate_starts"]([row], events, selections)
            self.assertEqual(accepted, [])
            self.assertEqual(len(excluded), 1)

    def test_noncommitted_proposal_is_not_a_candidate(self):
        row = fixture()[1]
        row["decision_committed"] = "0"
        self.assertEqual(API["candidate_starts"]([row], [], []), ([], []))

    def test_duplicate_ambiguous_or_orphan_start_fails(self):
        _, row, event, selection = fixture()
        for d, e, s in (([row, row], [event], [selection]), ([row], [event, event], [selection]),
                        ([row], [event], [selection, selection]), ([], [event], []),
                        ([row], [dict(event, remote_node="9")], [selection]),
                        ([row], [dict(event, time_ns="1")], [selection])):
            with self.assertRaises(ValueError):
                API["candidate_starts"](d, e, s)

    def test_fault_hit_and_trigger_sample_mismatch_rejected(self):
        _, row, event, selection = fixture()
        for update in (dict(actual_fault_hit="1"), dict(actual_fault_sampled="1")):
            with self.assertRaises(ValueError):
                API["candidate_starts"]([dict(row, **update)], [event], [selection])

    def test_epoch_survivor_excludes_current_without_normalizing_to_one(self):
        first, excluded = API["post_commit_window"](NS, 4 * NS + 1, NS, "FAULT_EPOCH")
        self.assertEqual((first, excluded), (2 * NS, True))
        mass, _ = API["future_mass"](.6, .2, excluded, 4 * NS + 1)
        self.assertAlmostEqual(mass, .5)

    def test_non_epoch_keeps_pending_sample_or_next_grid(self):
        for trigger in ("TASK_RUNNING", "CAPACITY_RELEASE"):
            self.assertEqual(API["post_commit_window"](NS, 4 * NS, NS, trigger), (NS, False))
            self.assertEqual(API["post_commit_window"](NS, 4 * NS, 2 * NS, trigger), (2 * NS, False))
            self.assertEqual(API["post_commit_window"](NS + 1, 4 * NS, 2 * NS, trigger), (2 * NS, False))
            with self.assertRaises(ValueError):
                API["post_commit_window"](NS + 1, 4 * NS, 3 * NS, trigger)
        self.assertEqual(API["future_mass"](.3, None, False, 4 * NS)[0], .3)

    def test_unknown_and_epoch_finish_endpoint_are_not_zero(self):
        for total, current, excluded, finish in ((None, .2, True, 4 * NS + 1),
                                                (.6, None, True, 4 * NS + 1),
                                                (.6, .2, True, 4 * NS),
                                                (1.0, 1.0, True, 4 * NS + 1)):
            self.assertIsNone(API["future_mass"](total, current, excluded, finish)[0])
        with self.assertRaises(ValueError):
            API["future_mass"](.1, .2, True, 4 * NS + 1)

    def test_features_ignore_all_future_outcome_information(self):
        task, row, _, _ = fixture()
        before = deepcopy((task, row))
        a = API["feature_snapshot"]("V6START", task, row)
        task.update(fault_time_ns="900", actual_T_catch_ns="123", task_success="0", failure_reason="F3",
                    init_complete_time_ns="10", remote_work_units="999", actual_recovery_target="99",
                    completion_time_ns="10", full_v7_later_risk="1", future_path_bandwidth="99")
        row.update(future_q_trajectory="oracle", future_checkpoints="oracle", predicted_recovery_s="999")
        b = API["feature_snapshot"]("V6START", task, row)
        self.assertEqual(a, b)
        self.assertEqual(before[0]["task_success"], "1")
        self.assertNotIn("task_success", a[0])
        self.assertIsNone(a[1]["future_first_failure_trajectory"])
        self.assertIsNone(a[1]["G_I_s"])

    def test_proposal_is_not_epoch_post_batch_path(self):
        task, row, _, _ = fixture()
        row.update(fault_epoch_time_ns=str(NS), decision_trigger="FAULT_EPOCH")
        snapshot, features, audit = API["feature_snapshot"]("V6START", task, row)
        self.assertEqual(snapshot["proposal_input_bandwidth_bytes_per_s"], "1000000000")
        self.assertNotIn("input_bandwidth_bytes_per_s", snapshot)
        self.assertIsNone(features["input_serialization_s"])
        self.assertIsNone(features["input_to_remaining_ratio"])
        self.assertFalse(snapshot["checkpoint_ready_at_decision"])
        self.assertIsNone(features["checkpoint_state_tail_forecast"])

    def test_single_check_and_pending_current_risk_are_identifiable(self):
        task, row, _, _ = fixture()
        task["baseline_compute_time_ns"] = str(NS)
        _, features, _ = API["feature_snapshot"]("V6START", task, row)
        self.assertEqual(features["q_next"], .5)
        self.assertEqual(json.loads(features["future_first_failure_trajectory"]),
                         [dict(time_ns=NS, q_comp=.5, first_failure_mass=.5)])
        task, row, _, _ = fixture()
        row["fault_epoch_time_ns"] = str(NS)
        _, features, _ = API["feature_snapshot"]("V6START", task, row)
        self.assertEqual(features["q_next"], .1)
        self.assertIsNone(features["future_first_failure_trajectory"])

    def test_local_delivery_has_no_network_bytes_or_density(self):
        task, row, _, _ = fixture()
        task["source_node_id"] = row["remote_node"]
        row.update(fault_epoch_time_ns=str(NS), decision_trigger="FAULT_EPOCH")
        _, features, _ = API["feature_snapshot"]("V6START", task, row)
        for key in ("input_serialization_s", "planned_network_input_bytes", "P_Iimpact", "G_I_s", "P_Iddl"):
            self.assertEqual(features[key], 0)
        self.assertIsNone(features["D_I_ms_per_MB"])

    def test_labels_are_exclusive_and_need_accepted_dependency_evidence(self):
        t, i, r = outcome()
        def label(a=r, b=r, ai=i, bi=i):
            return API["label_candidate"](t, t, ai, bi, a, b)
        self.assertEqual(label()["label"], "NEEDED")
        noncritical = dict(r, input_received_time_ns=r["state_ready_time_ns"],
                           recovery_compute_start_time_ns=r["state_ready_time_ns"])
        self.assertEqual(label(b=noncritical)["label"], "FAULT_NONCRITICAL")
        self.assertEqual(label(None, None, None, None)["label"], "NO_FAULT")
        for bad in (None, dict(r, state_ready_time_ns=""), dict(r, recovery_accept_time_ns="-1"),
                    dict(r, recovery_node="9"), dict(r, original_deadline_ns="8")):
            result = label(b=bad)
            self.assertEqual(result["label"], "UNKNOWN")
            self.assertIsNone(result["input_critical_wait_ns"])
        self.assertEqual(label(None, None)["label"], "UNKNOWN")
        self.assertEqual(label(ai=dict(i, fault_time_ns="1"))["label"], "UNKNOWN")

    def test_used_noncritical_in_jit_does_not_erase_anchor_need(self):
        t, i, r = outcome()
        jit = dict(r, input_received_time_ns=r["state_ready_time_ns"],
                   recovery_compute_start_time_ns=r["state_ready_time_ns"], input_reused="1")
        self.assertEqual(API["label_candidate"](t, t, i, i, jit, r)["label"], "NEEDED")

    def test_gate_stops_before_sweeps_and_retains_unknown(self):
        task, row, _, _ = fixture()
        _, _, audit = API["feature_snapshot"]("V6START", task, row)
        gate = API["coverage_gate"](audit)
        self.assertEqual(gate["status"], "STOP_RECONSTRUCTION_INSUFFICIENT")
        self.assertEqual(gate["score_sweep"], "SKIPPED_RECONSTRUCTION_GATE")
        self.assertEqual(gate["coverage"]["P_F"]["CAUSALLY_RECONSTRUCTIBLE"], 1)
        self.assertEqual(gate["coverage"]["G_I_s"]["UNKNOWN"], 1)

    def test_fail_closed_identity_never_enters_feature_analysis(self):
        function = API["analyze"]
        with patch.dict(function.__globals__, identity_audit=lambda roots: (_ for _ in ()).throw(ValueError("identity mismatch")),
                        feature_snapshot=lambda *args: self.fail("feature analysis ran after identity mismatch")):
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                function({})

    def test_outputs_are_exclusive_unknown_is_empty_and_raw_untouched(self):
        task, row, _, _ = fixture()
        snapshot, features, audit = API["feature_snapshot"]("V6START", task, row)
        result = dict(identity={}, summary={"status": "STOP_RECONSTRUCTION_INSUFFICIENT"},
                      snapshots=[snapshot], features=[features], audit=audit, labels=[])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "raw"
            raw.mkdir()
            evidence = raw / "evidence.csv"
            evidence.write_text("raw sentinel\n")
            before = evidence.read_bytes()
            metadata = API["evidence_metadata"]((raw,))
            for destination in (raw, raw / "nested", root):
                with self.assertRaises(ValueError):
                    API["HISTORY"]["output_guard"](destination, (raw,))
            output = root / "new"
            API["HISTORY"]["output_guard"](output, (raw,))
            API["write_outputs"](output, result)
            self.assertEqual(evidence.read_bytes(), before)
            self.assertEqual(API["evidence_metadata"]((raw,)), metadata)
            self.assertFalse((output / "score-sweeps.csv").exists())
            self.assertFalse((output / "reference-points.csv").exists())
            self.assertEqual(json.loads((output / "summary.json").read_text()), result["summary"])
            with self.assertRaises(FileExistsError):
                API["write_outputs"](output, result)


if __name__ == "__main__":
    unittest.main()
