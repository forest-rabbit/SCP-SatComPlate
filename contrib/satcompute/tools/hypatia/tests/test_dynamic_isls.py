#!/usr/bin/env python3
"""Tests for deterministic plus-grid dynamic ISL contracts."""

from __future__ import annotations

import math
import sys
import unittest
from dataclasses import replace
from pathlib import Path


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

from configuration import load_config  # noqa: E402
from dynamic_isls import (  # noqa: E402
    INTER_PLANE,
    INTRA_PLANE,
    DynamicIslError,
    IslEdge,
    build_candidate_isls,
    clearance_limited_max_distance_m,
    validate_clearance_limit,
)


PRESET = TOOL_DIR / "config" / "synthetic-66.json"


class DynamicIslCandidateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.config = load_config(PRESET)

    def test_synthetic_66_candidate_counts_and_seam(self) -> None:
        without_seam = build_candidate_isls(self.config)
        with_seam = build_candidate_isls(
            replace(self.config, seam_enabled=True)
        )
        self.assertEqual(len(without_seam), 121)
        self.assertEqual(len(with_seam), 132)
        self.assertEqual(
            sum(candidate.kind == INTRA_PLANE for candidate in without_seam),
            66,
        )
        self.assertEqual(
            sum(candidate.kind == INTER_PLANE for candidate in without_seam),
            55,
        )
        self.assertEqual(
            sum(candidate.kind == INTER_PLANE for candidate in with_seam),
            66,
        )
        self.assertEqual(
            build_candidate_isls(self.config),
            without_seam,
        )

    def test_small_constellations_have_no_invalid_or_duplicate_edges(self) -> None:
        cases = (
            (1, 1, False),
            (1, 2, False),
            (1, 3, False),
            (2, 1, False),
            (2, 1, True),
            (2, 2, True),
        )
        for num_orbits, satellites_per_orbit, seam_enabled in cases:
            with self.subTest(
                num_orbits=num_orbits,
                satellites_per_orbit=satellites_per_orbit,
                seam_enabled=seam_enabled,
            ):
                config = replace(
                    self.config,
                    num_orbits=num_orbits,
                    satellites_per_orbit=satellites_per_orbit,
                    seam_enabled=seam_enabled,
                )
                candidates = build_candidate_isls(config)
                edges = tuple(candidate.edge for candidate in candidates)
                self.assertEqual(len(edges), len(set(edges)))
                self.assertEqual(edges, tuple(sorted(edges)))
                for edge in edges:
                    self.assertLess(edge.node1_id, edge.node2_id)
                    self.assertGreaterEqual(edge.node1_id, 0)
                    self.assertLess(
                        edge.node2_id,
                        config.expected_satellite_count,
                    )

    def test_edge_constructor_rejects_invalid_endpoints(self) -> None:
        edge = IslEdge.canonical(3, 1, 4)
        self.assertEqual((edge.node1_id, edge.node2_id), (1, 3))
        invalid_arguments = (
            (-1, 1, 4),
            (1, 1, 4),
            (1, 4, 4),
            (True, 1, 4),
        )
        for arguments in invalid_arguments:
            with self.subTest(arguments=arguments):
                with self.assertRaises(DynamicIslError):
                    IslEdge.canonical(*arguments)

    def test_clearance_limit_is_general_and_enforced(self) -> None:
        exact_limit = clearance_limited_max_distance_m(780.0)
        self.assertAlmostEqual(exact_limit, 6174589.541014042)
        self.assertEqual(validate_clearance_limit(self.config), 6174589)
        with self.assertRaisesRegex(
            DynamicIslError,
            "exceeds.*clearance limit",
        ):
            validate_clearance_limit(
                replace(self.config, max_isl_distance_m=6174590)
            )

        self.assertEqual(clearance_limited_max_distance_m(80.0), 0.0)
        self.assertGreater(
            clearance_limited_max_distance_m(1200.0),
            exact_limit,
        )
        for altitude in (-1.0, math.nan, math.inf, True):
            with self.subTest(altitude=altitude):
                with self.assertRaises(DynamicIslError):
                    clearance_limited_max_distance_m(altitude)


if __name__ == "__main__":
    unittest.main()
