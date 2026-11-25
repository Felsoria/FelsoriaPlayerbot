#!/usr/bin/env python3
"""Check package/source identity and safe retries without publishing a release."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

spec = importlib.util.spec_from_file_location("publisher", Path(__file__).with_name("publish-sql-release.py"))
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)
COMMIT = "1" * 40


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.asset = self.root / f"Felsoria-Developer-V2-SQL-{COMMIT[:12]}.zip"
        self.package()
        self.remote = patch.object(publisher.subprocess, "run").start()
        self.gh = patch.object(publisher, "gh").start()
        self.addCleanup(patch.stopall)

    def package(self, commit=COMMIT, complete=True):
        with ZipFile(self.asset, "w") as archive:
            for name in ("auth.sql", "characters.sql", "world.sql", "hotfixes.sql"):
                if complete or name != "hotfixes.sql":
                    archive.writestr(name, "-- fixture\n")
            archive.writestr("database-manifest.json", json.dumps({"source_commit": commit}))

    def publish(self):
        publisher.publish("Felsoria/felsoria-source", COMMIT, self.root)

    def existing(self, assets=(), **changes):
        result = {"target_commitish": COMMIT, "draft": False, "assets": list(assets)}
        result.update(changes)
        self.remote.return_value = subprocess.CompletedProcess([], 0, json.dumps(result), "")

    def test_create_pins_source_and_includes_checksum(self):
        self.remote.return_value = subprocess.CompletedProcess([], 1, "", "HTTP 404: Not Found")
        self.publish()
        args = self.gh.call_args.args
        self.assertEqual(args[:3], ("release", "create", "developer-v2-" + COMMIT[:12]))
        self.assertEqual(args[args.index("--target") + 1], COMMIT)
        self.assertIn("--prerelease", args)
        self.assertEqual(self.asset.with_suffix(".zip.sha256").read_text(),
                         f"{publisher.sha256(self.asset)}  {self.asset.name}\n")

    def test_wrong_source_never_publishes(self):
        self.package(commit="2" * 40)
        with self.assertRaises(ValueError): self.publish()
        self.remote.assert_not_called()
        self.gh.assert_not_called()

    def test_incomplete_package_never_publishes(self):
        self.package(complete=False)
        with self.assertRaises(ValueError): self.publish()
        self.gh.assert_not_called()

    def test_server_error_is_not_missing_release(self):
        self.remote.return_value = subprocess.CompletedProcess([], 1, "", "HTTP 503: unavailable")
        with self.assertRaises(RuntimeError): self.publish()
        self.gh.assert_not_called()

    def test_existing_source_cannot_be_retargeted(self):
        self.existing(target_commitish="2" * 40)
        with self.assertRaises(ValueError): self.publish()
        self.gh.assert_not_called()

    def test_draft_is_not_published_by_retry(self):
        self.existing(draft=True)
        with self.assertRaises(ValueError): self.publish()
        self.gh.assert_not_called()

    def test_partial_upload_can_finish(self):
        self.existing()
        self.publish()
        self.assertEqual(self.gh.call_count, 2)
        for call in self.gh.call_args_list:
            self.assertEqual(call.args[:2], ("release", "upload"))

    def test_matching_release_is_unchanged(self):
        checksum = self.asset.with_suffix(".zip.sha256")
        checksum.write_text(f"{publisher.sha256(self.asset)}  {self.asset.name}\n")
        self.existing({"name": p.name, "digest": f"sha256:{publisher.sha256(p)}"}
                      for p in (self.asset, checksum))
        self.publish()
        self.gh.assert_not_called()

    def test_conflict_blocks_all_uploads(self):
        self.existing([{"name": self.asset.name + ".sha256", "digest": "sha256:wrong"}])
        with self.assertRaises(ValueError): self.publish()
        self.gh.assert_not_called()

    def test_invalid_commit_never_contacts_remote(self):
        with self.assertRaises(ValueError): publisher.publish("Felsoria/felsoria-source", "main", self.root)
        self.remote.assert_not_called()


class LegacyCleanupTests(unittest.TestCase):
    def setUp(self):
        self.remote = patch.object(publisher.subprocess, "run").start()
        self.gh = patch.object(publisher, "gh").start()
        self.addCleanup(patch.stopall)
        self.assets = [
            {"id": number, "name": name, "digest": "sha256:" + digest}
            for number, (name, digest) in enumerate(publisher.LEGACY_ASSETS.items(), start=100)
        ]
        self.legacy = {"id": 42, "tag_name": publisher.LEGACY_TAG, "assets": self.assets}
        self.baseline = {
            "draft": False, "target_commitish": publisher.BASELINE_COMMIT,
            "assets": [{"name": publisher.BASELINE_ASSET, "digest": "sha256:" + publisher.BASELINE_SHA256}],
        }
        self.remaining = []
        self.remote.return_value = subprocess.CompletedProcess([], 0, json.dumps(self.legacy), "")
        self.gh.side_effect = self.reply

    def reply(self, *args):
        if "--method" in args:
            return subprocess.CompletedProcess([], 0, "", "")
        value = self.baseline if args[-1].endswith(publisher.BASELINE_TAG) else {"assets": self.remaining}
        return subprocess.CompletedProcess([], 0, json.dumps(value), "")

    def deletes(self):
        return [call.args[-1] for call in self.gh.call_args_list if "--method" in call.args]

    def cleanup(self):
        publisher.retire_legacy_assets("Felsoria/FelsoriaPlayerbot")

    def test_only_approved_uploads_and_empty_release_are_deleted(self):
        self.cleanup()
        self.assertEqual(self.deletes(), [
            "repos/Felsoria/FelsoriaPlayerbot/releases/assets/100",
            "repos/Felsoria/FelsoriaPlayerbot/releases/assets/101",
            "repos/Felsoria/FelsoriaPlayerbot/releases/42",
        ])

    def test_other_uploads_are_preserved(self):
        self.remaining = [{"name": "unrelated.txt"}]
        self.cleanup()
        self.assertEqual(len(self.deletes()), 2)
        self.assertTrue(all("/releases/assets/" in path for path in self.deletes()))

    def test_changed_legacy_upload_blocks_all_deletes(self):
        self.assets[1]["digest"] = "sha256:changed"
        self.remote.return_value = subprocess.CompletedProcess([], 0, json.dumps(self.legacy), "")
        with self.assertRaises(ValueError):
            self.cleanup()
        self.assertEqual(self.deletes(), [])

    def test_missing_baseline_blocks_all_deletes(self):
        self.baseline["assets"] = []
        with self.assertRaises(ValueError):
            self.cleanup()
        self.assertEqual(self.deletes(), [])

    def test_changed_baseline_blocks_all_deletes(self):
        self.baseline["assets"][0]["digest"] = "sha256:changed"
        with self.assertRaises(ValueError):
            self.cleanup()
        self.assertEqual(self.deletes(), [])

    def test_wrong_baseline_source_blocks_all_deletes(self):
        self.baseline["target_commitish"] = "0" * 40
        with self.assertRaises(ValueError):
            self.cleanup()
        self.assertEqual(self.deletes(), [])

    def test_draft_baseline_blocks_all_deletes(self):
        self.baseline["draft"] = True
        with self.assertRaises(ValueError):
            self.cleanup()
        self.assertEqual(self.deletes(), [])

    def test_cleanup_retry_accepts_missing_legacy_release(self):
        self.remote.return_value = subprocess.CompletedProcess([], 1, "", "HTTP 404: Not Found")
        self.cleanup()
        self.gh.assert_not_called()

    def test_server_error_never_deletes_anything(self):
        self.remote.return_value = subprocess.CompletedProcess([], 1, "", "HTTP 503: unavailable")
        with self.assertRaises(RuntimeError):
            self.cleanup()
        self.gh.assert_not_called()


if __name__ == "__main__":
    unittest.main()
