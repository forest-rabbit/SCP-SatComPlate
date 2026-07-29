#!/usr/bin/env python3
"""Tests for canonical SatCompute topology JSON helpers."""

from __future__ import annotations

import math
import unittest

from contrib.satcompute.tools.generation.topology.common.satcompute_schema import (
    SatComputeLink,
    SatComputeSchemaError,
    distance_delay_us,
    nodes_payload,
    parse_nodes_payload,
    parse_topology_payload,
    satcompute_json_bytes,
    topology_payload,
)


class SatComputeNodesSchemaTest(unittest.TestCase):
    def test_nodes_are_exact_and_ordered(self) -> None:
        payload = nodes_payload(3)
        self.assertEqual(
            payload,
            {
                "nodes": [
                    {"node_id": 0, "node_type": "sat"},
                    {"node_id": 1, "node_type": "sat"},
                    {"node_id": 2, "node_type": "sat"},
                ]
            },
        )
        self.assertEqual(parse_nodes_payload(payload), (0, 1, 2))

    def test_invalid_nodes_are_rejected(self) -> None:
        invalid_payloads = (
            [],
            {"nodes": [], "metadata": {}},
            {"nodes": []},
            {"nodes": [{"node_id": 0, "node_type": "ground"}]},
            {"nodes": [{"node_id": True, "node_type": "sat"}]},
            {
                "nodes": [
                    {"node_id": 1, "node_type": "sat"},
                    {"node_id": 0, "node_type": "sat"},
                ]
            },
            {
                "nodes": [
                    {"node_id": 0, "node_type": "sat"},
                    {"node_id": 0, "node_type": "sat"},
                ]
            },
        )
        for payload in invalid_payloads:
            with self.subTest(payload=payload):
                with self.assertRaises(SatComputeSchemaError):
                    parse_nodes_payload(payload)


class SatComputeLinksSchemaTest(unittest.TestCase):
    def test_topology_builder_sorts_and_parser_accepts(self) -> None:
        links = (
            SatComputeLink(1, 2, 8000, 10000000),
            SatComputeLink(0, 1, 7000, 10000000),
        )
        payload = topology_payload(links, 3)
        self.assertEqual(
            [(item["node1_id"], item["node2_id"]) for item in payload["links"]],
            [(0, 1), (1, 2)],
        )
        self.assertEqual(
            parse_topology_payload(payload, (0, 1, 2)),
            tuple(sorted(links)),
        )
        self.assertTrue(satcompute_json_bytes(payload).endswith(b"\n"))

    def test_invalid_link_values_are_rejected(self) -> None:
        invalid_arguments = (
            (0, 0, 0, 1),
            (1, 0, 0, 1),
            (-1, 1, 0, 1),
            (0, 1, -1, 1),
            (0, 1, 0, 0),
            (True, 1, 0, 1),
        )
        for arguments in invalid_arguments:
            with self.subTest(arguments=arguments):
                with self.assertRaises(SatComputeSchemaError):
                    SatComputeLink(*arguments)

    def test_invalid_topology_payloads_are_rejected(self) -> None:
        valid = {
            "node1_id": 0,
            "node2_id": 1,
            "type": "sat",
            "delay": 8000,
            "link_bandwidth": 10000000,
        }
        invalid_payloads = (
            {"links": [], "metadata": {}},
            {"links": []},
            {"links": [{**valid, "type": "ground"}]},
            {"links": [{**valid, "node2_id": 2}]},
            {"links": [valid, dict(valid)]},
            {
                "links": [
                    {**valid, "node1_id": 1, "node2_id": 2},
                    valid,
                ]
            },
            {"links": [{**valid, "delay": True}]},
            {"links": [{**valid, "link_bandwidth": 0}]},
            {"links": [{**valid, "extra": 1}]},
        )
        for payload in invalid_payloads:
            with self.subTest(payload=payload):
                with self.assertRaises(SatComputeSchemaError):
                    parse_topology_payload(payload, (0, 1))

    def test_builder_rejects_duplicate_and_unknown_endpoints(self) -> None:
        with self.assertRaisesRegex(SatComputeSchemaError, "duplicate"):
            topology_payload(
                (
                    SatComputeLink(0, 1, 1, 1),
                    SatComputeLink(0, 1, 2, 1),
                ),
                2,
            )
        with self.assertRaisesRegex(SatComputeSchemaError, "absent"):
            topology_payload((SatComputeLink(0, 2, 1, 1),), 2)


class SatComputeDelayTest(unittest.TestCase):
    def test_distance_delay_is_one_way_and_python_rounded(self) -> None:
        self.assertEqual(distance_delay_us(0.0), 0)
        self.assertEqual(distance_delay_us(299792.458), 1000)
        self.assertEqual(distance_delay_us(149.896229), 0)
        self.assertEqual(distance_delay_us(449.688687), 2)

    def test_invalid_distance_is_rejected(self) -> None:
        for value in (-1.0, math.nan, math.inf, True):
            with self.subTest(value=value):
                with self.assertRaises(SatComputeSchemaError):
                    distance_delay_us(value)


if __name__ == "__main__":
    unittest.main()
