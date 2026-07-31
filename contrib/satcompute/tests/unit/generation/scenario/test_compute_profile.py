#!/usr/bin/env python3
"""Tests for canonical SatCompute compute-profile generation."""

from __future__ import annotations

import copy
import unittest

from contrib.satcompute.tools.generation.scenario.compute_profile import (
    ComputeNode,
    ComputeProfileError,
    build_compute_profile,
    compute_profile_bytes,
    parse_compute_profile,
)


class ComputeProfileTest(unittest.TestCase):
    def setUp(self) -> None:
        self.profile = build_compute_profile((9, 1, 5), 1_500_000)

    def test_builder_is_canonical(self) -> None:
        self.assertEqual(
            self.profile,
            {
                "schema_version": "0.1",
                "compute_nodes": [
                    {
                        "node_id": 1,
                        "compute_rate_work_units_per_second": 1_500_000,
                    },
                    {
                        "node_id": 5,
                        "compute_rate_work_units_per_second": 1_500_000,
                    },
                    {
                        "node_id": 9,
                        "compute_rate_work_units_per_second": 1_500_000,
                    },
                ],
            },
        )
        self.assertEqual(
            parse_compute_profile(
                self.profile,
                valid_node_ids=range(10),
            ),
            (
                ComputeNode(1, 1_500_000),
                ComputeNode(5, 1_500_000),
                ComputeNode(9, 1_500_000),
            ),
        )

    def test_serialization_is_compact_and_deterministic(self) -> None:
        first = compute_profile_bytes(self.profile)
        second = compute_profile_bytes(copy.deepcopy(self.profile))
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        self.assertNotIn(b" ", first)

    def test_builder_rejects_invalid_inputs(self) -> None:
        cases = (
            ((), 1),
            ((1, 1), 1),
            ((True,), 1),
            ((-1,), 1),
            ((1,), 0),
            ((1,), True),
        )
        for node_ids, rate in cases:
            with self.subTest(node_ids=node_ids, rate=rate):
                with self.assertRaises(ComputeProfileError):
                    build_compute_profile(node_ids, rate)

    def test_root_schema_is_strict(self) -> None:
        invalid = (
            None,
            [],
            {},
            {"schema_version": "0.1", "compute_nodes": [], "extra": 1},
            {"schema_version": "1.0", "compute_nodes": []},
            {"schema_version": "0.1", "compute_nodes": "nodes"},
            {"schema_version": "0.1", "compute_nodes": []},
        )
        for payload in invalid:
            with self.subTest(payload=payload):
                with self.assertRaises(ComputeProfileError):
                    parse_compute_profile(payload)

    def test_node_fields_and_types_are_strict(self) -> None:
        invalid_nodes = (
            None,
            {"node_id": 1},
            {
                "node_id": 1,
                "compute_rate_work_units_per_second": 1,
                "extra": 2,
            },
            {
                "node_id": True,
                "compute_rate_work_units_per_second": 1,
            },
            {
                "node_id": 1,
                "compute_rate_work_units_per_second": True,
            },
            {
                "node_id": 1,
                "compute_rate_work_units_per_second": 0,
            },
        )
        for item in invalid_nodes:
            with self.subTest(item=item):
                payload = {
                    "schema_version": "0.1",
                    "compute_nodes": [item],
                }
                with self.assertRaises(ComputeProfileError):
                    parse_compute_profile(payload)

    def test_order_duplicates_and_unknown_nodes_are_rejected(self) -> None:
        reversed_profile = copy.deepcopy(self.profile)
        reversed_profile["compute_nodes"].reverse()
        with self.assertRaisesRegex(
            ComputeProfileError,
            "strictly increasing",
        ):
            parse_compute_profile(reversed_profile)

        duplicate = copy.deepcopy(self.profile)
        duplicate["compute_nodes"][1]["node_id"] = 1
        with self.assertRaises(ComputeProfileError):
            parse_compute_profile(duplicate)

        with self.assertRaisesRegex(
            ComputeProfileError,
            "unknown node_id=9",
        ):
            parse_compute_profile(
                self.profile,
                valid_node_ids=range(9),
            )


if __name__ == "__main__":
    unittest.main()
