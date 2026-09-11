"""Deferred audit must reject fake INPUT, hidden state bytes and premature recovery."""
from copy import deepcopy
from pathlib import Path
import runpy
import unittest

API = runpy.run_path(str(Path(__file__).resolve().parents[1] / "integration/regression/analyze-input-deferred.py"))


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


if __name__ == "__main__":
    unittest.main()
