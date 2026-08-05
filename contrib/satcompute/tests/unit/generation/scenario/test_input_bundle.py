#!/usr/bin/env python3
"""Tests for deterministic v0.3 independent-input bundles."""

import json
from pathlib import Path
import tempfile
import unittest

from contrib.satcompute.tests.unit.visualization.orbit._helpers import write_trace
from contrib.satcompute.tools.generation.scenario.check_scenario import (
    InputBundleCheckError,
    check_input_bundle,
)
from contrib.satcompute.tools.generation.scenario.generate_scenario import (
    InputBundleGenerationError,
    generate_input_bundle,
)


SATCOMPUTE_ROOT = Path(__file__).resolve().parents[4]
TASK_FIXTURES = SATCOMPUTE_ROOT / "tests" / "fixtures" / "task"
TRANSFER_FIXTURES = (
    SATCOMPUTE_ROOT / "tests" / "fixtures" / "traffic" / "transfers"
)


class InputBundleTest(unittest.TestCase):
    def test_task_bundle_is_exact_checked_and_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / "trace"
            write_trace(trace)
            outputs = (root / "task-a", root / "task-b")
            for output in outputs:
                summary = generate_input_bundle(
                    "task-unit",
                    trace,
                    output,
                    compute_profile=TASK_FIXTURES / "compute-profile-single.json",
                    task_trace=TASK_FIXTURES / "task-single.json",
                )
                self.assertEqual(summary["workload_mode"], "task")
                self.assertEqual(summary["compute_node_count"], 1)
                self.assertEqual(summary["task_count"], 1)
                self.assertEqual(check_input_bundle(output)["task_count"], 1)
                self.assertEqual(
                    (output / "compute-profile.json").read_bytes(),
                    (TASK_FIXTURES / "compute-profile-single.json").read_bytes(),
                )
                self.assertEqual(
                    (output / "task-trace.json").read_bytes(),
                    (TASK_FIXTURES / "task-single.json").read_bytes(),
                )
            self.assertEqual(
                {path.name for path in outputs[0].iterdir()},
                {path.name for path in outputs[1].iterdir()},
            )
            for path in outputs[0].iterdir():
                self.assertEqual(path.read_bytes(), (outputs[1] / path.name).read_bytes())

            manifest = json.loads(
                (outputs[0] / "input-bundle-manifest.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertIsNone(manifest["inputs"]["fault_trace"])
            self.assertEqual(manifest["topology_provenance"]["satellite_ids"], [0, 1, 2, 3])

    def test_transfer_bundle_is_mutually_exclusive_and_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / "trace"
            write_trace(trace)
            output = root / "transfer"
            summary = generate_input_bundle(
                "transfer-unit",
                trace,
                output,
                transfer_trace=TRANSFER_FIXTURES / "engine-basic.json",
            )
            self.assertEqual(summary["workload_mode"], "transfer")
            self.assertEqual(summary["transfer_count"], 2)
            self.assertEqual(check_input_bundle(output)["transfer_count"], 2)
            self.assertEqual(
                {path.name for path in output.iterdir()},
                {"input-bundle-manifest.json", "transfer-trace.json"},
            )

    def test_mode_overlap_and_existing_output_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / "trace"
            write_trace(trace)
            with self.assertRaisesRegex(InputBundleGenerationError, "together"):
                generate_input_bundle(
                    "invalid",
                    trace,
                    root / "missing-task",
                    compute_profile=TASK_FIXTURES / "compute-profile-single.json",
                )
            with self.assertRaisesRegex(InputBundleGenerationError, "exactly one"):
                generate_input_bundle(
                    "invalid",
                    trace,
                    root / "mixed",
                    compute_profile=TASK_FIXTURES / "compute-profile-single.json",
                    task_trace=TASK_FIXTURES / "task-single.json",
                    transfer_trace=TRANSFER_FIXTURES / "engine-basic.json",
                )
            with self.assertRaisesRegex(InputBundleGenerationError, "overlap"):
                generate_input_bundle(
                    "invalid",
                    trace,
                    trace / "nested",
                    transfer_trace=TRANSFER_FIXTURES / "engine-basic.json",
                )
            existing = root / "existing"
            existing.mkdir()
            with self.assertRaisesRegex(InputBundleGenerationError, "already exists"):
                generate_input_bundle(
                    "invalid",
                    trace,
                    existing,
                    transfer_trace=TRANSFER_FIXTURES / "engine-basic.json",
                )

    def test_tamper_and_extra_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / "trace"
            write_trace(trace)
            output = root / "transfer"
            generate_input_bundle(
                "tamper-unit",
                trace,
                output,
                transfer_trace=TRANSFER_FIXTURES / "engine-basic.json",
            )
            transfer = output / "transfer-trace.json"
            transfer.write_bytes(transfer.read_bytes() + b" ")
            with self.assertRaisesRegex(InputBundleCheckError, "hash differs"):
                check_input_bundle(output)

            transfer.write_bytes(
                (TRANSFER_FIXTURES / "engine-basic.json").read_bytes()
            )
            (output / "extra.json").write_text("{}\n", encoding="utf-8")
            with self.assertRaisesRegex(InputBundleCheckError, "inventory"):
                check_input_bundle(output)


if __name__ == "__main__":
    unittest.main()
