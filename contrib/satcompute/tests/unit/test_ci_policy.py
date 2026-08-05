"""Repository policy tests for the manual SatCompute phase gate."""

from __future__ import annotations

import re
import stat
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
WORKFLOW_DIRECTORY = REPOSITORY_ROOT / ".github/workflows"
WORKFLOW_PATH = WORKFLOW_DIRECTORY / "phase_gate.yml"


class SatComputeCiPolicyTest(unittest.TestCase):
    """Keep GitHub verification targeted to project-owned code and tests."""

    def test_repository_has_one_workflow_and_one_named_job(self) -> None:
        workflows = sorted(WORKFLOW_DIRECTORY.glob("*.y*ml"))
        self.assertEqual(workflows, [WORKFLOW_PATH])
        content = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertRegex(content, r"(?m)^name: SatCompute CI$")
        jobs_section = content.split("\njobs:\n", maxsplit=1)[1]
        jobs = re.findall(r"(?m)^  ([A-Za-z0-9_-]+):\s*$", jobs_section)
        self.assertEqual(jobs, ["satcompute"])
        self.assertRegex(content, r"(?m)^    name: SatCompute CI \(.+\)$")

    def test_workflow_is_a_manual_phase_gate(self) -> None:
        content = WORKFLOW_PATH.read_text(encoding="utf-8")
        trigger_section = content.split("\npermissions:\n", maxsplit=1)[0]
        self.assertIn("workflow_dispatch:", trigger_section)
        self.assertNotRegex(trigger_section, r"(?m)^  (push|pull_request):")
        self.assertIn('description: "Completed migration phase', trigger_section)

    def test_workflow_never_enables_or_runs_upstream_suites(self) -> None:
        content = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn(
            "./ns3 configure --enable-modules=satcompute -G Ninja",
            content,
        )
        for forbidden in ("--enable-examples", "--enable-tests", "test.py"):
            self.assertNotIn(forbidden, content)

    def test_workflow_runs_only_maintained_project_test_entrypoints(self) -> None:
        content = WORKFLOW_PATH.read_text(encoding="utf-8")
        for command in (
            "python3 -m unittest discover",
            "contrib/satcompute/tests/unit/run-cpp-tests.sh",
            "contrib/satcompute/tests/integration/smoke/run-all.sh",
            "contrib/satcompute/tests/integration/regression/run-all.sh",
        ):
            self.assertIn(command, content)

    def test_legacy_layered_integration_entrypoints_are_preserved(self) -> None:
        suites = {
            "contrib/satcompute/tests/integration/smoke/run-all.sh": (
                "run-routing-smoke.sh",
                "run-capacity-aware-smoke.sh",
                "run-task-smoke.sh",
                "run-diagnostics-smoke.sh",
                "run-topology-smoke.sh",
            ),
            "contrib/satcompute/tests/integration/regression/run-all.sh": (
                "run-full-routing-regression.sh",
                "run-full-workload-regression.sh",
            ),
        }
        for relative_runner, children in suites.items():
            runner = REPOSITORY_ROOT / relative_runner
            content = runner.read_text(encoding="utf-8")
            self.assertTrue(runner.stat().st_mode & stat.S_IXUSR)
            for child_name in children:
                child = runner.parent / child_name
                self.assertTrue(child.is_file(), child)
                self.assertTrue(child.stat().st_mode & stat.S_IXUSR, child)
                self.assertIn(child_name, content)


if __name__ == "__main__":
    unittest.main()
