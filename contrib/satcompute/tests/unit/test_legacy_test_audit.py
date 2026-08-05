#!/usr/bin/env python3
"""Freeze dispositions for ns-3.33 test paths not retained verbatim."""

from __future__ import annotations

import unittest

from contrib.satcompute.tests.support.paths import REPOSITORY_ROOT


# The legacy tree is frozen at branch legacy/ns-3.33, commit
# f4c7bff6674f9eeaae30e080d52c38c7fb601e21. Every omitted path below has at
# least one maintained v0.3 replacement. Legacy paths present on main are
# intentionally excluded from this map and remain covered by the tree audit.
REPLACEMENTS = {
    "contrib/satcompute/tests/unit/analysis/topology_interval/test_downsample_scenario.py": (
        "contrib/satcompute/tests/unit/analysis/topology_interval/test_downsample_trace.py",
    ),
    "contrib/satcompute/tests/unit/analysis/topology_interval/test_report.py": (
        "contrib/satcompute/tests/unit/analysis/topology_interval/test_ecmp_candidates.py",
        "contrib/satcompute/tests/unit/analysis/topology_interval/test_edge_state.py",
    ),
    "contrib/satcompute/tests/unit/analysis/topology_interval/test_route_probe.py": (
        "contrib/satcompute/tests/unit/routing-compatibility-test.cc",
        "contrib/satcompute/tests/unit/routing-policy-factory-test.cc",
    ),
    "contrib/satcompute/tests/unit/analysis/topology_interval/test_run_interval_study.py": (
        "contrib/satcompute/tests/integration/smoke/interval_analysis.py",
        "contrib/satcompute/tests/unit/topology-replay-equivalence-test.cc",
    ),
    "contrib/satcompute/tests/unit/generation/scenario/_helpers.py": (
        "contrib/satcompute/tests/support/fixtures.py",
    ),
    "contrib/satcompute/tests/unit/generation/scenario/test_check_scenario.py": (
        "contrib/satcompute/tests/unit/generation/scenario/test_input_bundle_schema.py",
    ),
    "contrib/satcompute/tests/unit/generation/scenario/test_configuration.py": (
        "contrib/satcompute/tests/unit/generation/scenario/test_input_bundle.py",
    ),
    "contrib/satcompute/tests/unit/generation/scenario/test_generate_scenario.py": (
        "contrib/satcompute/tests/unit/generation/scenario/test_input_bundle.py",
    ),
    "contrib/satcompute/tests/unit/generation/topology/__init__.py": (
        "contrib/satcompute/tests/unit/test_topology_generation_checker.py",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_check_export.py": (
        "contrib/satcompute/tests/unit/test_topology_generation_checker.py",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_common_output.py": (
        "contrib/satcompute/tests/unit/test_topology_generation_checker.py",
        "contrib/satcompute/tests/unit/topology-trace-exporter-test.cc",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_configuration.py": (
        "contrib/satcompute/tests/unit/test_constellation_schema.py",
        "contrib/satcompute/tests/unit/para-test.cc",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_dynamic_export.py": (
        "contrib/satcompute/tests/unit/topology-trace-exporter-test.cc",
        "contrib/satcompute/tests/unit/topology-replay-equivalence-test.cc",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_dynamic_isls.py": (
        "contrib/satcompute/tests/unit/online-topology-controller-test.cc",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_hypatia_smoke.py": (
        "contrib/satcompute/tests/unit/online-orbit-foundation-test.cc",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_mean_motion.py": (
        "contrib/satcompute/tests/unit/online-orbit-foundation-test.cc",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_orbit_positions.py": (
        "contrib/satcompute/tests/unit/online-orbit-foundation-test.cc",
        "contrib/satcompute/tests/unit/topology-trace-exporter-test.cc",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_resolve_constellation.py": (
        "contrib/satcompute/tests/unit/constellation-definition-test.cc",
        "contrib/satcompute/tests/unit/test_constellation_schema.py",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_satcompute_schema.py": (
        "contrib/satcompute/tests/unit/test_topology_trace_schemas.py",
        "contrib/satcompute/tests/unit/test_constellation_schema.py",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_static_topology.py": (
        "contrib/satcompute/tests/unit/topology-trace-exporter-test.cc",
    ),
    "contrib/satcompute/tests/unit/generation/topology/test_walker_tles.py": (
        "contrib/satcompute/tests/unit/online-orbit-foundation-test.cc",
    ),
    "contrib/satcompute/tests/unit/visualization/orbit/test_scenario_reader.py": (
        "contrib/satcompute/tests/unit/visualization/orbit/test_trace_reader.py",
    ),
}


class LegacyTestAuditTest(unittest.TestCase):
    def test_every_deliberately_omitted_test_has_live_replacements(self):
        self.assertEqual(len(REPLACEMENTS), 22)
        for legacy_path, replacements in REPLACEMENTS.items():
            with self.subTest(legacy_path=legacy_path):
                self.assertFalse((REPOSITORY_ROOT / legacy_path).exists())
                self.assertTrue(replacements)
                for replacement in replacements:
                    self.assertTrue(
                        (REPOSITORY_ROOT / replacement).is_file(),
                        replacement,
                    )


if __name__ == "__main__":
    unittest.main()
