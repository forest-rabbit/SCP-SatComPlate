"""Deferred audit must reject fake INPUT, hidden state bytes and premature recovery."""
from copy import deepcopy
from pathlib import Path
import runpy
import unittest

API = runpy.run_path(str(Path(__file__).resolve().parents[1] / "integration/regression/analyze-input-deferred.py"))
START = runpy.run_path(str(Path(__file__).resolve().parents[1] / "integration/regression/analyze-riskweighted-start.py"))


class DeferredAuditTests(unittest.TestCase):
    def fixture(self, path="REMOTE_REDO"):
        task = dict(input_bytes="1000", compute_work_units="1000", source_node_id="1", task_profile="dense-image")
        protected = dict(variable_state_bytes="1500", cR_ns="5")
        r = dict(input_staging_policy="deferred", recovery_accept_time_ns="10", input_start_time_ns="10",
                 input_received_time_ns="20", input_delivery_mode="NETWORK", recovery_node="0",
                 chosen_path=path, tail_start_time_ns="", state_start_time_ns="", tail_commit_time_ns="",
                 phase_at_fault="ON", remote_work_units="200", committed_remote_bytes="300",
                 checkpoint_state_bytes="300", committed_remote_object_id="1",
                 recovery_compute_start_time_ns="20", state_ready_time_ns="10")
        flows = [dict(kind="RECOVERY_INPUT", bytes="1000", source_node="1", destination_node="0", storage_object_id="0")]
        events = [dict(event="RECOVERY_INPUT_STARTED", bytes="1000", time_ns="10")]
        return task, protected, r, flows, events

    def test_direct_wait_and_logical_zero_state(self):
        args = self.fixture()
        API["dependency_check"](*args)
        args[2].update(remote_work_units="0", committed_remote_bytes="0", checkpoint_state_bytes="0")
        API["dependency_check"](*args)
        args[2]["committed_remote_object_id"] = ""
        with self.assertRaises(ValueError):
            API["dependency_check"](*args)

    def test_duplicate_or_partial_input_rejected(self):
        for kind in ("duplicate", "partial", "wrong-node", "backup-object"):
            args = self.fixture()
            if kind == "duplicate": args[4].append(deepcopy(args[4][0]))
            if kind == "partial": args[4][0]["bytes"] = "800"
            if kind == "wrong-node": args[3][0]["destination_node"] = "3"
            if kind == "backup-object": args[3][0]["storage_object_id"] = "5"
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                API["dependency_check"](*args)

    def test_local_input_has_logical_bytes_without_udp(self):
        task, p, r, flows, events = self.fixture()
        task["source_node_id"] = r["recovery_node"]
        r["input_delivery_mode"] = "LOCAL"
        API["dependency_check"](task, p, r, [], events)
        with self.assertRaises(ValueError):
            API["dependency_check"](task, p, r, flows, events)

    def test_input_and_state_barrier_and_one_tail_merge(self):
        args = self.fixture("MIGRATE_TAIL")
        args[2].update(tail_start_time_ns="10", state_start_time_ns="10", tail_received_time_ns="18",
                       state_received_time_ns="30", tail_commit_time_ns="35", state_ready_time_ns="35",
                       tail_bytes="100", checkpoint_relocation_bytes="300", recovery_compute_start_time_ns="35")
        args[4].append(dict(event="RECOVERY_TAIL_COMMIT", bytes="100", time_ns="35"))
        API["dependency_check"](*args)
        for key, value in (("recovery_compute_start_time_ns", "20"), ("state_start_time_ns", "20"),
                           ("checkpoint_relocation_bytes", "1100"), ("tail_commit_time_ns", "40")):
            bad = deepcopy(args)
            bad[2][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                API["dependency_check"](*bad)

    def test_input_wait_unknown_is_not_a_zero_sample(self):
        self.assertIsNone(API["distribution"]([])["p50"])
        self.assertEqual(API["distribution"]([1, 3])["max"], 3)

    def test_historical_and_current_llm_state_use_the_recorded_token_unit(self):
        for unit in (100, 400):
            task = dict(compute_work_units=str(1000*unit))
            protected = dict(variable_state_bytes=str(1000*114688))
            self.assertEqual(API["BASE"]["ACCOUNTING"]["llm_state_bytes"](task, protected, 2*unit-1), 114688)


class RiskWeightedAuditTests(unittest.TestCase):
    def fixture(self, deferred=True):
        task = dict(task_id="1", task_profile="dense-image", input_bytes="1000000000",
                    compute_work_units="1500000", compute_rate_work_units_per_second="100000")
        recovery = START["variable_bytes"](task)*3*.05/(2e9)+.008*3/4+1500000*.05/200000
        joff = .6*(7.5+(0 if deferred else 1))
        row = dict(selected_score=str(.08+.6*recovery), proposed_action="START", recovery_rate="100000",
            proposed_delta_permille="50", proposed_n="4", progress_ratio="0", replay_available="1",
            input_bandwidth_bytes_per_s="1000000000", backup_bandwidth_bytes_per_s="1000000000",
            predicted_recovery_s=str(recovery), rmax_s="5", phase_before="OFF", p_fail_before_finish=".8",
            p_fail_after_init_ready=".6", representative_progress_after_ready=".5", init_ready_time_ns="10010000000",
            fault_epoch_time_ns="10000000000", t_init_s=".01", j_off_start_window=str(joff), j_off=str(joff),
            predicted_normal_s=".08", j_start=str(.09+.6*recovery), q_current_sample=".2")
        return row, task

    def test_new_scores_use_future_progress_and_cancel_deferred_input(self):
        for deferred in (True, False):
            row, task = self.fixture(deferred)
            self.assertTrue(START["decision_check"](row, task, deferred))
        for key, value in (("p_fail_after_init_ready", ".9"), ("j_off_start_window", "0"),
                           ("j_start", "1"), ("predicted_normal_s", ".04"), ("rmax_s", ".5")):
            row, task = self.fixture()
            row[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                START["decision_check"](row, task, True)

    def test_no_ready_mass_and_no_placeholder_progress(self):
        row, task = self.fixture()
        row.update(p_fail_after_init_ready="0", representative_progress_after_ready="", j_off="0",
                   j_off_start_window="0", selected_score=".08", j_start=".09", proposed_action="NONE")
        START["decision_check"](row, task, True)
        for key, value in (("representative_progress_after_ready", "0"), ("proposed_action", "START")):
            bad = dict(row, **{key:value})
            with self.assertRaises(ValueError): START["decision_check"](bad, task, True)

    def test_on_keeps_current_sample_and_interval_cost(self):
        row, task = self.fixture()
        normal = .08/15
        row.update(phase_before="ON", p_fail_after_init_ready="", representative_progress_after_ready="",
                   predicted_normal_s=str(normal), selected_score=str(normal+.2*float(row["predicted_recovery_s"])),
                   proposed_action="UPDATE")
        START["decision_check"](row, task, True)
        row.update(decision_trigger="CAPACITY_RELEASE", q_comp_snapshot=row["q_current_sample"],
                   q_current_sample="")
        START["decision_check"](row, task, True)
        row["selected_score"] = str(normal+.8*float(row["predicted_recovery_s"]))
        with self.assertRaises(ValueError): START["decision_check"](row, task, True)

    def test_ready_estimate_rounds_up_to_a_whole_nanosecond(self):
        row, task = self.fixture()
        row.update(t_init_s=".0100000001", init_ready_time_ns="10010000001")
        START["decision_check"](row, task, True)
        row["init_ready_time_ns"] = "10010000000"
        with self.assertRaises(ValueError): START["decision_check"](row, task, True)


if __name__ == "__main__":
    unittest.main()
