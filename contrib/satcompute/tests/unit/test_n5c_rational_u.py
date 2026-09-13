"""Causal Rational-U features, deterministic ranks and diagnostic-only zero counts."""
from pathlib import Path
import json
import runpy
import shlex
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parents[1] / "integration/regression"
AUDIT = runpy.run_path(str(HERE / "analyze-n5c-rational-u.py"))
History = AUDIT["OLD"]["History"]
RUN = runpy.run_path(str(HERE / "run-n5c-rational-u.py"))


class RationalUTests(unittest.TestCase):
    def test_actual_service_and_future_invariance(self):
        past = History({1: [(10, 30)]}, {1: [(40, 50)]}, {})
        future = History({1: [(10, 30), (150, 180)]}, {1: [(40, 50), (200, 250)]}, {})
        a = AUDIT["features"](past, 1, 100, 50, 100)
        self.assertEqual(a, AUDIT["features"](future, 1, 100, 50, 100))
        self.assertEqual((a["T_idle_ns"], a["U_global"], a["U_rational"]), (50, .3, .15))
        self.assertEqual(a["U_recent"], 0)
        # A service END at this timestamp has I=0, not an unknown or a new admission rule.
        end = AUDIT["features"](past, 1, 50, 50, 50)
        self.assertEqual(end["T_idle_ns"], 0)
        self.assertEqual(end["U_global"], end["U_rational"])
        # An idle proposal may precede a service START later in the same ns.
        self.assertEqual(AUDIT["features"](past, 1, 40, 50, 40)["T_idle_ns"], 10)
        with self.assertRaises(ValueError):
            AUDIT["features"](past, 1, 45, 50, 45)
        self.assertEqual(AUDIT["features"](past, 1, 45, 50, 45, False)["T_idle_ns"], 0)

    def test_never_busy_exposure_horizon_and_range(self):
        h = History({}, {}, {})
        self.assertEqual(AUDIT["features"](h, 1, 100, 10, 100)["U_rational"], 0)
        self.assertEqual(AUDIT["features"](h, 1, 0, 10, 0)["history_unavailable"], 1)
        for time, horizon, exposure in ((100, 0, 100), (100, -1, 100), (100, 1, 101)):
            with self.assertRaises(ValueError):
                AUDIT["features"](h, 1, time, horizon, exposure)
        h = History({1: [(0, 20)]}, {}, {})
        values = [AUDIT["features"](h, 1, t, 10, t)["U_rational"] for t in range(20, 100)]
        self.assertEqual(values, sorted(values, reverse=True))
        self.assertTrue(all(0 < v <= 1 for v in values))

    def test_raw_zero_diagnostics_not_ranking_thresholds(self):
        s = AUDIT["stats"]([0, 1e-14, 1e-10, .2])
        self.assertEqual((s["exact_zero_count"], s["abs_below_1e_12_count"], s["abs_below_1e_9_count"]), (1, 2, 3))
        def c(node, phi, prop=1, feasible=True):
            return dict(candidate_node=node, Phi_rational=phi, propagation_ns=prop, feasible=feasible)
        # If a near-zero tolerance leaked into rank this would incorrectly choose node 1.
        self.assertEqual(AUDIT["winner"]([c(1, 1e-14), c(2, 0), c(0, 0, 0, False)],
                                         "Phi_rational")["candidate_node"], 2)
        self.assertEqual(AUDIT["winner"]([c(2, .1, 1), c(1, .1, 2)], "Phi_rational")["candidate_node"], 2)
        self.assertEqual(AUDIT["winner"]([c(2, .1), c(1, .1)], "Phi_rational")["candidate_node"], 1)
        self.assertIsNone(AUDIT["winner"]([c(0, 0, 0, False)], "Phi_rational"))

    def test_fixed_single_run_controls_and_execution_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            command = RUN["arguments"](root)
            flags = RUN["OLD"]["flags"](command)
            self.assertEqual((flags["randomRun"], flags["simulationDuration"], flags["n5cVariant"]),
                             ("11", "1300", "rational-U"))
            original = RUN["RECENT"]["arguments"](root, "full", 11)
            self.assertEqual([f for f in command if not f.startswith("--n5cVariant=")],
                             [f for f in original if not f.startswith("--n5cVariant=")])
            metadata = dict(seed=1, run=11, simulation_duration_s=1300, commit="clean", worktree_dirty=False,
                fault_mode="generate", protection_mode="compfrr", placement_mode="n5c", n5c_variant="rational-U",
                input_staging_policy="deferred", remote_busy_recovery_policy="relocate", audit=False, shadow=False,
                command=["ns3", "run", shlex.join(command)])
            (root / "execution-result.json").write_text('{"returncode":0}')
            def verify(value):
                (root / "execution.json").write_text(json.dumps(value))
                return RUN["verify"](root, "clean")
            self.assertEqual(verify(metadata), metadata)
            for field, bad in (("run", 12), ("commit", "stale"), ("worktree_dirty", True),
                               ("fault_mode", "validation-replay"), ("n5c_variant", "full")):
                with self.subTest(field=field), self.assertRaises(ValueError):
                    verify(dict(metadata, **{field: bad}))
            with self.assertRaises(ValueError):
                verify(dict(metadata, command=[metadata["command"][-1].replace("--fixedDelay=0.001", "--fixedDelay=0.008")]))
            (root / "execution-result.json").write_text('{"returncode":1}')
            with self.assertRaises(ValueError):
                verify(metadata)

    def test_independent_audit_rejects_idle_or_horizon_corruption(self):
        r = dict(decision_id="1", candidate_node=13, task_id="140", time_ns=100, T_remaining_ns=50,
                 T_idle_ns=50, freshness=.5, U_global=.3, U_rational=.15, history_unavailable=0)
        reconstruction = dict(candidates=[r], changes=[dict(decision_id="1", winner_rational=13)])
        raw = dict(decision_id="1", candidate_node="13", task_id="140", time_ns="100", horizon_ns="50",
                   continuous_idle_ns="50", freshness="0.5", cumulative_utilization="0.3",
                   rational_pressure="0.15", history_unavailable="0")
        selected = dict(decision_id="1", candidate_node="13", variant="rational-U", selected="1")
        def check(values):
            def fake_rows(root, filename):
                return values if filename == "n5c-rational-u-history.csv" else [selected]
            with patch.dict(AUDIT["audit_actual"].__globals__, rows=fake_rows):
                return AUDIT["audit_actual"](Path("unused"), reconstruction)
        self.assertEqual(check([raw])["checked_candidate_rows"], 1)
        for field, bad in (("horizon_ns", "51"), ("continuous_idle_ns", "49"),
                           ("rational_pressure", "0.14"), ("time_ns", "101")):
            with self.subTest(field=field), self.assertRaises(ValueError):
                check([dict(raw, **{field: bad})])
        for values in ([], [raw, raw]):
            with self.assertRaises(ValueError):
                check(values)


if __name__ == "__main__":
    unittest.main()
