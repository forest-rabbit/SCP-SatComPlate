#!/usr/bin/env python3
"""Validate every legacy-only SatCompute path has an explicit disposition."""

from __future__ import annotations

import csv
import hashlib
import unittest

from contrib.satcompute.tests.support.paths import REPOSITORY_ROOT


AUDIT_PATH = REPOSITORY_ROOT / "docs/plans/ns3-33-to-48-file-audit.tsv"
EXPECTED_FIELDS = (
    "legacy_path",
    "action",
    "replacement_paths",
    "rationale",
)
EXPECTED_LEGACY_ONLY_COUNT = 70
EXPECTED_LEGACY_ONLY_PATHS_SHA256 = (
    "83be4f5013eae055225a982fdc0122070d6bfd34da2e70aacc2ebbe337e90153"
)
ALLOWED_ACTIONS = frozenset(("replaced", "removed"))


class LegacyFileAuditTest(unittest.TestCase):
    def test_every_legacy_only_path_has_live_evidence(self):
        with AUDIT_PATH.open(newline="", encoding="utf-8") as source:
            reader = csv.DictReader(source, delimiter="\t")
            self.assertEqual(tuple(reader.fieldnames or ()), EXPECTED_FIELDS)
            rows = list(reader)

        self.assertEqual(len(rows), EXPECTED_LEGACY_ONLY_COUNT)
        paths = [row["legacy_path"] for row in rows]
        self.assertEqual(len(paths), len(set(paths)))
        self.assertEqual(paths, sorted(paths))
        path_blob = ("\n".join(paths) + "\n").encode("utf-8")
        self.assertEqual(
            hashlib.sha256(path_blob).hexdigest(),
            EXPECTED_LEGACY_ONLY_PATHS_SHA256,
        )

        for row in rows:
            legacy_path = row["legacy_path"]
            with self.subTest(legacy_path=legacy_path):
                self.assertTrue(legacy_path.startswith("contrib/satcompute/"))
                self.assertFalse((REPOSITORY_ROOT / legacy_path).exists())
                self.assertIn(row["action"], ALLOWED_ACTIONS)
                self.assertTrue(row["rationale"].strip())
                replacements = tuple(
                    path
                    for path in row["replacement_paths"].split(";")
                    if path
                )
                self.assertTrue(replacements)
                for replacement in replacements:
                    self.assertTrue(
                        (REPOSITORY_ROOT / replacement).is_file(),
                        replacement,
                    )


if __name__ == "__main__":
    unittest.main()
