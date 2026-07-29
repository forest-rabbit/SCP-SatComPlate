#!/usr/bin/env python3
"""Tests for deterministic even-plane-slot compute placement."""

from __future__ import annotations

import unittest

from contrib.satcompute.tools.generation.scenario.compute_placement import (
    ComputePlacement,
    ComputePlacementError,
    even_plane_slot_placement,
)


class ComputePlacementTest(unittest.TestCase):
    def test_synthetic_66_compute_22_exact_placement(self) -> None:
        placement = even_plane_slot_placement(6, 11, 22)
        self.assertEqual(
            placement.compute_nodes_per_orbit,
            (3, 4, 4, 3, 4, 4),
        )
        self.assertEqual(
            placement.selected_node_ids,
            (
                1, 5, 9,
                12, 15, 17, 20,
                23, 26, 28, 31,
                34, 38, 42,
                45, 48, 50, 53,
                56, 59, 61, 64,
            ),
        )

    def test_k_less_than_plane_count(self) -> None:
        self.assertEqual(
            even_plane_slot_placement(6, 11, 3),
            ComputePlacement(
                (0, 1, 0, 1, 0, 1),
                (16, 38, 60),
            ),
        )

    def test_one_compute_node_per_plane(self) -> None:
        self.assertEqual(
            even_plane_slot_placement(6, 11, 6),
            ComputePlacement(
                (1, 1, 1, 1, 1, 1),
                (5, 16, 27, 38, 49, 60),
            ),
        )

    def test_every_satellite_can_be_compute_capable(self) -> None:
        placement = even_plane_slot_placement(6, 11, 66)
        self.assertEqual(
            placement.compute_nodes_per_orbit,
            (11, 11, 11, 11, 11, 11),
        )
        self.assertEqual(placement.selected_node_ids, tuple(range(66)))

    def test_non_divisible_allocation_is_exact(self) -> None:
        self.assertEqual(
            even_plane_slot_placement(3, 5, 7),
            ComputePlacement((2, 2, 3), (1, 3, 6, 8, 10, 12, 14)),
        )

    def test_repeated_results_are_identical(self) -> None:
        first = even_plane_slot_placement(7, 13, 37)
        second = even_plane_slot_placement(7, 13, 37)
        self.assertEqual(first, second)
        self.assertEqual(
            max(first.compute_nodes_per_orbit)
            - min(first.compute_nodes_per_orbit),
            1,
        )

    def test_invalid_inputs_are_rejected(self) -> None:
        cases = (
            (0, 11, 1),
            (6, 0, 1),
            (6, 11, 0),
            (6, 11, 67),
            (True, 11, 1),
            (6, False, 1),
            (6, 11, True),
            (6.0, 11, 1),
        )
        for arguments in cases:
            with self.subTest(arguments=arguments):
                with self.assertRaises(ComputePlacementError):
                    even_plane_slot_placement(*arguments)


if __name__ == "__main__":
    unittest.main()
