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

class RetentionTests(unittest.TestCase):
    OLD = "2" * 40
    MIDDLE = "3" * 40
    NEW = "4" * 40
    REPO = "Felsoria/FelsoriaPlayerbot"

    def setUp(self):
        self.baseline = self.release(publisher.BASELINE_COMMIT, 1)
        self.baseline["assets"][0]["digest"] = "sha256:" + publisher.BASELINE_SHA256
        self.old = self.release(self.OLD, 2)
        self.middle = self.release(self.MIDDLE, 3)
        self.new = self.release(self.NEW, 4)
        # API/tag ordering is not chronological.
        self.releases = [self.baseline, self.new, self.old, self.middle]
        self.rank = {publisher.BASELINE_COMMIT: 0, self.OLD: 1, self.MIDDLE: 2, self.NEW: 3}
        self.outside_main = set()
        self.live = {}
        self.refs = {}
        self.pages = None
        self.deleted = []
        self.remote = patch.object(publisher, "gh", side_effect=self.reply).start()
        self.ancestry = patch.object(publisher, "is_ancestor", side_effect=self.ancestor).start()
        self.addCleanup(patch.stopall)

    def release(self, commit, number):
        name = f"Felsoria-Developer-V2-SQL-{commit[:12]}.zip"
        return {
            "id": number, "tag_name": f"developer-v2-{commit[:12]}", "target_commitish": commit,
            "author": {"login": "github-actions[bot]"},
            "draft": False, "immutable": False, "prerelease": True,
            "assets": [{"name": filename, "state": "uploaded", "digest": "sha256:" + "a" * 64}
                       for filename in (name, name + ".sha256")],
        }

    def ancestor(self, older, newer):
        if newer == "origin/main":
            return older not in self.outside_main
        return self.rank[older] <= self.rank[newer]

    def reply(self, *args):
        if args[:2] == ("release", "delete"):
            tag = args[2]
            if tag == publisher.BASELINE_TAG:
                raise AssertionError("Attempted to delete the pinned baseline")
            if "--cleanup-tag" not in args:
                raise AssertionError("An obsolete source tag would be left behind")
            self.deleted.append(tag)
            self.releases = [r for r in self.releases if r["tag_name"] != tag]
            return subprocess.CompletedProcess(args, 0, "", "")
        endpoint = args[-1]
        if "/releases?per_page=100&page=" in endpoint:
            page = int(endpoint.rsplit("&page=", 1)[1])
            pages = self.pages or [self.releases]
            value = pages[page - 1] if page <= len(pages) else []
        elif endpoint.endswith("/releases/tags/" + publisher.BASELINE_TAG):
            value = self.baseline
        elif "/git/ref/tags/" in endpoint:
            tag = endpoint.rsplit("/", 1)[1]
            release = next(r for r in self.releases if r["tag_name"] == tag)
            value = {"object": self.refs.get(tag, {
                "type": "commit", "sha": release["target_commitish"], "url": "https://api.github.com/fixture"
            })}
        elif "/releases/" in endpoint:
            number = int(endpoint.rsplit("/", 1)[1])
            value = self.live.get(number, next(r for r in self.releases if r["id"] == number))
        else:
            raise AssertionError(f"Unexpected API request: {args}")
        return subprocess.CompletedProcess(args, 0, json.dumps(value), "")

    def prune(self, commit=None):
        publisher.prune_old_releases(self.REPO, commit or self.NEW)

    def test_keeps_baseline_and_newest_and_removes_two_old_tags(self):
        self.prune()
        self.assertEqual(set(self.deleted), {self.old["tag_name"], self.middle["tag_name"]})
        self.assertEqual({r["tag_name"] for r in self.releases},
                         {publisher.BASELINE_TAG, self.new["tag_name"]})

    def test_older_build_finishing_late_keeps_the_newer_revision(self):
        self.prune(self.MIDDLE)
        self.assertEqual({r["tag_name"] for r in self.releases},
                         {publisher.BASELINE_TAG, self.new["tag_name"]})

    def test_changed_baseline_blocks_all_deletions(self):
        self.baseline["assets"][0]["digest"] = "sha256:changed"
        with self.assertRaises(ValueError):
            self.prune()
        self.assertEqual(self.deleted, [])

    def test_missing_current_package_blocks_all_deletions(self):
        self.releases.remove(self.new)
        with self.assertRaises(ValueError):
            self.prune()
        self.assertEqual(self.deleted, [])

    def test_partial_current_upload_blocks_all_deletions(self):
        self.new["assets"].pop()
        with self.assertRaises(ValueError):
            self.prune()
        self.assertEqual(self.deleted, [])

    def test_manual_release_is_preserved(self):
        self.old["author"]["login"] = "Felsoria"
        self.prune()
        self.assertNotIn(self.old["tag_name"], self.deleted)

    def test_extra_upload_is_preserved(self):
        self.old["assets"].append({"name": "custom-data.zip"})
        self.prune()
        self.assertNotIn(self.old["tag_name"], self.deleted)

    def test_draft_release_is_preserved(self):
        self.old["draft"] = True
        self.prune()
        self.assertNotIn(self.old["tag_name"], self.deleted)

    def test_immutable_release_is_preserved(self):
        self.old["immutable"] = True
        self.prune()
        self.assertNotIn(self.old["tag_name"], self.deleted)

    def test_stable_release_is_preserved(self):
        self.old["prerelease"] = False
        self.prune()
        self.assertNotIn(self.old["tag_name"], self.deleted)

    def test_partial_old_upload_is_preserved(self):
        self.old["assets"].pop()
        self.prune()
        self.assertNotIn(self.old["tag_name"], self.deleted)

    def test_release_outside_main_is_preserved(self):
        self.outside_main.add(self.OLD)
        self.prune()
        self.assertNotIn(self.old["tag_name"], self.deleted)

    def test_moved_tag_blocks_all_deletions(self):
        self.refs[self.middle["tag_name"]] = {"type": "commit", "sha": "f" * 40}
        with self.assertRaises(ValueError):
            self.prune()
        self.assertEqual(self.deleted, [])

    def test_annotated_tag_blocks_all_deletions(self):
        self.refs[self.old["tag_name"]] = {"type": "tag", "sha": "f" * 40}
        with self.assertRaises(ValueError):
            self.prune()
        self.assertEqual(self.deleted, [])

    def test_edited_release_blocks_all_deletions(self):
        self.live[self.middle["id"]] = self.release(self.MIDDLE, self.middle["id"])
        self.live[self.middle["id"]]["assets"][0]["digest"] = "sha256:" + "b" * 64
        with self.assertRaises(ValueError):
            self.prune()
        self.assertEqual(self.deleted, [])

    def test_unknown_git_history_blocks_all_deletions(self):
        self.ancestry.side_effect = RuntimeError("Missing Git history")
        with self.assertRaises(RuntimeError):
            self.prune()
        self.assertEqual(self.deleted, [])

    def test_all_release_pages_are_checked(self):
        unrelated = [{"target_commitish": ""} for _ in range(99)]
        self.pages = [[self.baseline, *unrelated], [self.new, self.old, self.middle]]
        self.prune()
        self.assertEqual(len(self.deleted), 2)

    def test_retry_after_cleanup_makes_no_deletions(self):
        self.prune()
        self.deleted.clear()
        self.prune()
        self.assertEqual(self.deleted, [])

    def test_invalid_commit_never_contacts_github(self):
        with self.assertRaises(ValueError):
            self.prune("main")
        self.remote.assert_not_called()


class GitAncestryTests(unittest.TestCase):
    def test_real_git_history_and_missing_commit(self):
        import contextlib
        with tempfile.TemporaryDirectory() as root, contextlib.chdir(root):
            subprocess.run(["git", "init", "-q"], check=True)
            def commit():
                subprocess.run(["git", "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                                "commit", "--allow-empty", "-qm", "fixture"], check=True)
                return subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
            first = commit()
            second = commit()
            subprocess.run(["git", "update-ref", "refs/remotes/origin/main", second], check=True)
            self.assertTrue(publisher.is_ancestor(first, second))
            self.assertFalse(publisher.is_ancestor(second, first))
            self.assertTrue(publisher.is_ancestor(second, "origin/main"))
            with self.assertRaises(RuntimeError):
                publisher.is_ancestor("f" * 40, second)


if __name__ == "__main__":
    unittest.main()
