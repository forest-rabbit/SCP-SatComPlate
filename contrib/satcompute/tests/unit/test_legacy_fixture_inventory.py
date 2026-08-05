# SPDX-License-Identifier: GPL-2.0-only

import hashlib
import json
import unittest
from pathlib import Path


FIXTURE_ROOT = Path(__file__).resolve().parents[1] / "fixtures"
MANIFEST_PATH = FIXTURE_ROOT / "legacy-workload-manifest.json"


class LegacyFixtureInventoryTest(unittest.TestCase):
    def test_restored_legacy_fixture_hashes(self):
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(manifest["schema_version"], "0.1")
        self.assertEqual(manifest["source_branch"], "legacy/ns-3.33")
        self.assertEqual(manifest["file_count"], 54)
        self.assertEqual(len(manifest["files"]), 54)

        paths = [entry["path"] for entry in manifest["files"]]
        self.assertEqual(paths, sorted(paths))
        self.assertEqual(len(paths), len(set(paths)))
        for entry in manifest["files"]:
            fixture = FIXTURE_ROOT / entry["path"]
            self.assertTrue(fixture.is_file(), f"missing legacy fixture: {fixture}")
            digest = hashlib.sha256(fixture.read_bytes()).hexdigest()
            self.assertEqual(digest, entry["sha256"], entry["path"])


if __name__ == "__main__":
    unittest.main()
