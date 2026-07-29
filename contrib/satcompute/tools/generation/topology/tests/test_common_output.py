#!/usr/bin/env python3
"""Tests for shared topology output and hash contracts."""

from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path

from contrib.satcompute.tools.generation.topology.common.atomic_output import (
    AtomicOutputError,
    atomic_output_directory,
    temporary_output_dir,
)
from contrib.satcompute.tools.generation.topology.common.hash_utils import (
    HashContractError,
    aggregate_data_sha256,
    compact_json_bytes,
    sha256_file,
)


class AtomicOutputTest(unittest.TestCase):
    def test_success_publishes_and_stale_temp_is_cleaned(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-common-atomic-"
        ) as temp:
            root = Path(temp)
            output = root / "output"
            stale = temporary_output_dir(output)
            stale.mkdir()
            (stale / "partial.txt").write_text(
                "partial\n",
                encoding="utf-8",
            )
            with atomic_output_directory(output) as temporary:
                self.assertEqual(temporary, stale)
                self.assertFalse((temporary / "partial.txt").exists())
                (temporary / "complete.txt").write_text(
                    "complete\n",
                    encoding="utf-8",
                )
            self.assertEqual(
                (output / "complete.txt").read_text(encoding="utf-8"),
                "complete\n",
            )
            self.assertFalse(stale.exists())

    def test_failure_cleans_temp_and_does_not_publish(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-common-failure-"
        ) as temp:
            output = Path(temp) / "output"
            with self.assertRaisesRegex(RuntimeError, "checker failed"):
                with atomic_output_directory(output) as temporary:
                    (temporary / "partial.txt").write_text(
                        "partial\n",
                        encoding="utf-8",
                    )
                    raise RuntimeError("checker failed")
            self.assertFalse(output.exists())
            self.assertFalse(temporary_output_dir(output).exists())

    def test_nonempty_formal_directory_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-common-blocked-"
        ) as temp:
            output = Path(temp) / "output"
            output.mkdir()
            marker = output / "keep.txt"
            marker.write_text("keep\n", encoding="utf-8")
            with self.assertRaisesRegex(
                AtomicOutputError,
                "refusing to overwrite non-empty",
            ):
                with atomic_output_directory(output):
                    self.fail("blocked output unexpectedly opened")
            self.assertEqual(marker.read_text(encoding="utf-8"), "keep\n")


class HashUtilsTest(unittest.TestCase):
    def test_compact_json_is_stable_and_newline_terminated(self) -> None:
        self.assertEqual(
            compact_json_bytes({"b": 2, "a": 1}),
            b'{"b":2,"a":1}\n',
        )

    def test_aggregate_hash_matches_the_frozen_algorithm(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-common-hash-"
        ) as temp:
            root = Path(temp)
            (root / "nodes_0s.json").write_bytes(b"nodes\n")
            (root / "topology_0s.json").write_bytes(b"links\n")
            paths = ("topology_0s.json", "nodes_0s.json")
            records = b"".join(
                (
                    relative.encode("utf-8")
                    + b"\0"
                    + hashlib.sha256((root / relative).read_bytes())
                    .hexdigest()
                    .encode("ascii")
                    + b"\n"
                )
                for relative in sorted(paths)
            )
            self.assertEqual(
                aggregate_data_sha256(root, paths),
                hashlib.sha256(records).hexdigest(),
            )
            self.assertEqual(
                sha256_file(root / "nodes_0s.json"),
                hashlib.sha256(b"nodes\n").hexdigest(),
            )

    def test_aggregate_hash_rejects_unsafe_and_duplicate_paths(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="satcompute-common-hash-invalid-"
        ) as temp:
            root = Path(temp)
            with self.assertRaises(HashContractError):
                aggregate_data_sha256(root, ("../outside.json",))
            with self.assertRaises(HashContractError):
                aggregate_data_sha256(root, ("same.json", "same.json"))


if __name__ == "__main__":
    unittest.main()
