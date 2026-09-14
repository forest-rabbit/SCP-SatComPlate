"""Synthetic lifecycle contracts; no formal runs or historical-output dependency."""
from copy import deepcopy
import json
from pathlib import Path
import runpy
import tempfile
import unittest

API = runpy.run_path(str(Path(__file__).resolve().parents[1] / "support/protection/jit_offline_audit.py"))


def fixture():
    task = dict(task_id="1", source_node_id="1", input_bytes="100", compute_start_time_ns="0",
                compute_work_units="100", compute_rate_work_units_per_second="10",
                task_profile="dense-image", task_success="1", failure_reason="")
    life = dict(task_id="1", input_bytes="100", source_node="1", holder_node="2", delivery_mode="NETWORK",
                input_object_id="7", input_transfer_id="8", requested_ns="10", registered_ns="11",
                ready_ns="30", fault_ns="50", released_ns="60", used="1", adopted="1", failed="0",
                prefetch_total_sent_bytes="100", prefetch_before_fault_sent_bytes="100",
                prefetch_after_fault_sent_bytes="0", prefetch_used_bytes="100", prefetch_unused_bytes="0",
                terminal_reason="RECOVERY_OWNERSHIP_RELEASED")
    recovery = dict(task_id="1", fault_time_ns="50", fault_type="compute", recovery_node="2",
                    input_reused="1", reused_input_object_id="7", reused_input_transfer_id="8",
                    input_object_at_fault="7", input_transfer_at_fault="8", input_state_at_fault="READY",
                    input_state_at_acceptance="READY", input_received_time_ns="51", state_ready_time_ns="60",
                    recovery_compute_start_time_ns="60", recovery_accept_time_ns="51", chosen_path="TAIL",
                    checkpoint_relocation_attempted="0", terminal_reason="RECOVERY_RESULT_COMPLETE",
                    original_deadline_ns="100", logical_completion="1", actual_T_catch_ns="20")
    flow = dict(task_id="1", kind="PREFETCH_INPUT", transfer_id="8", storage_object_id="7", source_node="1",
                destination_node="2", bytes="100", sent_bytes="100", received_bytes="100", state="COMPLETED",
                received_time_ns="30", sender_finished_time_ns="29")
    decision = dict(task_id="1", time_ns="10", admitted="1", input_object_id="7", trigger="FAULT_EPOCH_SURVIVED",
                    p_on="0.4", input_transfer_s="0.1", representative_fault_time_ns="50", next_jit_evaluation_ns="20")
    return life, task, recovery, [flow], [decision]


class JitOfflineAuditTests(unittest.TestCase):
    def run_row(self, data):
        return API["lifecycle_row"](*data)

    def test_ready_used_noncritical_is_not_no_benefit(self):
        row = self.run_row(fixture())
        self.assertEqual(row["category"], "USED_NON_CRITICAL")
        self.assertEqual(row["input_critical_wait_ns"], 0)
        self.assertTrue(row["object_reused"])
        self.assertFalse(row["stream_reused"])
        self.assertEqual(row["input_ready_time_ns"], 30)
        self.assertEqual(row["input_dependency_ready_time_ns"], 51)

    def test_inflight_preserves_postfault_bytes_and_can_be_used(self):
        life, task, r, flows, d = fixture()
        life.update(ready_ns="70", released_ns="70", prefetch_before_fault_sent_bytes="40",
                    prefetch_after_fault_sent_bytes="60")
        r.update(input_state_at_fault="IN_FLIGHT", input_state_at_acceptance="IN_FLIGHT",
                 input_received_time_ns="70", recovery_compute_start_time_ns="70")
        flows[0].update(received_time_ns="70", sender_finished_time_ns="69")
        row = self.run_row((life, task, r, flows, d))
        self.assertEqual(row["category"], "USED_CRITICAL")
        self.assertEqual(row["input_critical_wait_ns"], 10)
        self.assertEqual(row["fraction_sent_before_fault"], .4)
        self.assertEqual(row["prefetch_used_bytes"], 100)
        self.assertTrue(row["stream_reused"])
        self.assertFalse(row["input_ready_before_fault"])

    def test_same_ns_receiver_is_not_prefault_ready(self):
        data = fixture()
        data[0]["ready_ns"] = "50"
        data[3][0]["received_time_ns"] = "50"
        with self.assertRaisesRegex(ValueError, "precede fault strictly"):
            self.run_row(data)

    def test_fractional_predicted_time_is_preserved_not_a_runtime_timestamp(self):
        data = fixture()
        data[4][0]["representative_fault_time_ns"] = "50.00000006"
        self.assertEqual(self.run_row(data)["representative_fault_time_ns"], "50.00000006")

    def test_local_delivery_retains_lifecycle_without_network(self):
        life, task, r, flows, d = fixture()
        life.update(holder_node="1", delivery_mode="LOCAL", input_transfer_id="0",
                    prefetch_total_sent_bytes="0", prefetch_before_fault_sent_bytes="0", prefetch_used_bytes="0")
        r.update(recovery_node="1", reused_input_transfer_id="0", input_transfer_at_fault="0")
        row = self.run_row((life, task, r, [], d))
        self.assertTrue(row["actually_used"])
        self.assertEqual(row["category"], "USED_NON_CRITICAL")
        self.assertEqual(row["prefetch_total_sent_bytes"], 0)
        with self.assertRaisesRegex(ValueError, "local pseudo UDP"):
            self.run_row((life, task, r, flows, d))

    def test_adopted_but_never_computed_is_not_used(self):
        data = fixture()
        data[0].update(used="0", prefetch_used_bytes="0", prefetch_unused_bytes="100")
        data[2]["recovery_compute_start_time_ns"] = ""
        row = self.run_row(data)
        self.assertEqual(row["category"], "FAILED_CANCELLED")
        self.assertTrue(row["adopted"])
        self.assertFalse(row["actually_used"])
        self.assertIsNone(row["input_critical_wait_ns"])

    def test_failure_bytes_kept_no_fault_priority_and_independent_flag(self):
        life, task, r, flows, d = fixture()
        life.update(used="0", adopted="0", failed="1", fault_ns="-1", ready_ns="-1",
                    prefetch_total_sent_bytes="40", prefetch_before_fault_sent_bytes="40",
                    prefetch_used_bytes="0", prefetch_unused_bytes="40", terminal_reason="FLOW_FAILED")
        flows[0].update(state="FAILED", received_time_ns="", received_bytes="20", sent_bytes="40")
        row = self.run_row((life, task, None, flows, d))
        self.assertEqual(row["category"], "NO_FAULT")
        self.assertTrue(row["flow_failed_or_cancelled"])
        self.assertEqual(row["prefetch_total_sent_bytes"], 40)
        with self.assertRaisesRegex(ValueError, "duplicate prefetch"):
            self.run_row((life, task, None, flows * 2, d))

    def test_wrong_target_not_silently_used(self):
        data = fixture()
        data[0].update(used="0", adopted="0", prefetch_used_bytes="0", prefetch_unused_bytes="100")
        data[2].update(recovery_node="3", input_reused="0")
        self.assertEqual(self.run_row(data)["category"], "WRONG_TARGET")
        data[0].update(used="1", prefetch_used_bytes="100", prefetch_unused_bytes="0")
        with self.assertRaises(ValueError):
            self.run_row(data)

    def test_conservation_identity_and_receiver_mismatch_fail(self):
        for mutate in (lambda d: d[0].update(prefetch_after_fault_sent_bytes="1"),
                       lambda d: d[3][0].update(received_bytes="99"),
                       lambda d: d[2].update(reused_input_object_id="99"),
                       lambda d: d[2].update(input_received_time_ns="49"),
                       lambda d: d[4][0].update(time_ns="9")):
            data = fixture()
            mutate(data)
            with self.assertRaises(ValueError):
                self.run_row(data)

    def test_unknown_dependency_is_not_zero(self):
        for value in (None, "", "-1"):
            r = fixture()[2]
            r["state_ready_time_ns"] = value
            result = API["dependency_join"](r)
            self.assertFalse(result["dependency_join_known"])
            self.assertIsNone(result["input_critical_wait_ns"])
            self.assertEqual(API["classify"](True, True, True, None), "USED_UNKNOWN")
        for key in ("state_ready_time_ns", "input_received_time_ns"):
            data = fixture()
            data[2][key] = ""
            row = self.run_row(data)
            self.assertEqual(row["category"], "USED_UNKNOWN")
            self.assertIsNone(row["input_critical_wait_ns"])
            self.assertEqual(row["prefetch_used_bytes"], 100)

    def test_precise_pair_identity_and_missing_catch(self):
        r = fixture()[2]
        good = deepcopy(r)
        good["actual_T_catch_ns"] = "30"
        pair = API["paired_comparison"]([r], [good])
        self.assertEqual(pair["count"], 1)
        self.assertEqual(pair["delta_catch_paired_ns"]["mean"], 10)
        for key, value in (("recovery_node", "9"), ("fault_time_ns", "51"), ("chosen_path", "REDO"),
                           ("original_deadline_ns", "101"), ("actual_T_catch_ns", "")):
            other = deepcopy(r)
            other[key] = value
            pair = API["paired_comparison"]([r], [other])
            self.assertEqual(pair["count"], 0)
            self.assertEqual(len(pair["rejected"]), 1)
        with self.assertRaisesRegex(ValueError, "duplicate task_id"):
            API["paired_comparison"]([r, r], [r])

    def test_recovery_local_has_no_fake_flow(self):
        task, r = fixture()[1:3]
        r.update(recovery_node="1", input_reused="0", input_delivery_mode="LOCAL")
        API["recovery_input_check"](task, r, [])
        with self.assertRaises(ValueError):
            API["recovery_input_check"](task, r, [dict(kind="RECOVERY_INPUT")])

    def test_pairing_refuses_different_execution_controls_or_workloads(self):
        with tempfile.TemporaryDirectory() as directory:
            v7, deferred = Path(directory) / "v7", Path(directory) / "deferred"
            e = dict(commit="historical-code", seed=1, run=11, simulation_duration_s=1300,
                     protection_mode="compfrr", placement_mode="fa-lrl", remote_busy_recovery_policy="relocate",
                     worktree_dirty=False, audit=False, shadow=False, fault_mode="generate")
            for path, mode in ((v7, "jit"), (deferred, "deferred")):
                path.mkdir()
                (path / "execution.json").write_text(json.dumps(dict(e, input_staging_policy=mode,
                    command=[f"satcompute --randomRun=11 --inputStagingPolicy={mode}"])))
                (path / "execution-result.json").write_text('{"returncode":0}')
            fields = ("source_node_id", "compute_node_id", "result_node_id", "input_bytes", "output_bytes",
                      "compute_work_units", "compute_rate_work_units_per_second", "arrival_time_ns",
                      "task_profile", "compute_deadline_budget_ns")
            tasks = {"1": dict.fromkeys(fields, "1")}
            API["fairness"](v7, deferred, tasks, tasks)
            other = deepcopy(tasks)
            other["1"]["compute_work_units"] = "2"
            with self.assertRaisesRegex(ValueError, "logical workload"):
                API["fairness"](v7, deferred, tasks, other)
            original = json.loads((deferred / "execution.json").read_text())
            for changed, message in ((dict(original, commit="new-code"), "commits differ"),
                                     (dict(original, command=["satcompute --randomRun=12"]), "controls differ")):
                (deferred / "execution.json").write_text(json.dumps(changed))
                with self.assertRaisesRegex(ValueError, message):
                    API["fairness"](v7, deferred, tasks, tasks)

    def test_output_never_overwrites_or_nests_raw_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "raw"
            raw.mkdir()
            for output in (raw, raw / "new", root):
                with self.assertRaises(ValueError):
                    API["output_guard"](output, (raw,))
            API["output_guard"](root / "new-audit", (raw,))


if __name__ == "__main__":
    unittest.main()
