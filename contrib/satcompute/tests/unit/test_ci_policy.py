"""Repository policy tests for the single SatCompute GitHub check."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
WORKFLOW_DIRECTORY = REPOSITORY_ROOT / ".github/workflows"
WORKFLOW_PATH = WORKFLOW_DIRECTORY / "per_commit.yml"


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
        self.assertRegex(content, r"(?m)^    name: SatCompute CI$")

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


if __name__ == "__main__":
    unittest.main()
