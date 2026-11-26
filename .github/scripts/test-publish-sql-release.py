#!/usr/bin/env python3
"""Exercise rolling publication and interrupted retries without changing GitHub."""
import copy
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
OLD, NEW, NEXT = "1" * 40, "2" * 40, "3" * 40
REPO = "Felsoria/FelsoriaPlayerbot"


class RollingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.asset = self.package(NEW)
        self.next_id = 100
        self.releases = [self.release(publisher.BASELINE_COMMIT, 1), self.release(OLD, 2)]
        self.releases[0]["assets"][0]["digest"] = "sha256:" + publisher.BASELINE_SHA256
        self.refs = {r["tag_name"]: r["target_commitish"] for r in self.releases}
        self.main = NEXT
        self.calls = []
        self.break_when = None
        self.gh = patch.object(publisher, "gh", side_effect=self.remote).start()
        patch.object(publisher, "is_ancestor", side_effect=self.ancestor).start()
        patch.object(publisher.subprocess, "run", side_effect=self.git).start()
        self.addCleanup(patch.stopall)

    def package(self, commit, complete=True, source=None):
        asset = self.root / f"Felsoria-Developer-V2-SQL-{commit[:12]}.zip"
        with ZipFile(asset, "w") as archive:
            for database in ("auth", "characters", "world", "hotfixes"):
                if complete or database != "hotfixes":
                    archive.writestr(database + ".sql", "-- fixture\n")
            archive.writestr("database-manifest.json", json.dumps({"source_commit": source or commit}))
        return asset

    def asset_record(self, name, digest=None):
        self.next_id = getattr(self, "next_id", 100) + 1
        return {"id": self.next_id, "name": name, "state": "uploaded",
                "digest": digest or "sha256:" + "a" * 64,
                "uploader": {"login": "github-actions[bot]"}}

    def release(self, commit, number):
        name = f"Felsoria-Developer-V2-SQL-{commit[:12]}.zip"
        return {"id": number, "tag_name": f"developer-v2-{commit[:12]}",
                "target_commitish": commit, "author": {"login": "github-actions[bot]"},
                "draft": False, "immutable": False, "prerelease": True,
                "assets": [self.asset_record(name), self.asset_record(name + ".sha256")]}

    def ancestor(self, older, newer):
        history = [publisher.BASELINE_COMMIT, OLD, NEW, NEXT]
        return older in history and newer in history and history.index(older) <= history.index(newer)

    def git(self, args, **kwargs):
        self.assertEqual(args[:3], ["git", "rev-parse", "--verify"])
        short = args[3].split("^")[0]
        history = [publisher.BASELINE_COMMIT, OLD, NEW, NEXT]
        found = next((s for s in history if s.startswith(short)), None)
        return subprocess.CompletedProcess(args, 0 if found else 1, found or "", "")

    def remote(self, *args, **kwargs):
        method = args[args.index("--method") + 1] if "--method" in args else "GET"
        if self.break_when and self.break_when(args, method):
            raise subprocess.CalledProcessError(1, args, stderr="HTTP 503: fixture failure")
        if args[0] == "api":
            endpoint = args[1].removeprefix(f"repos/{REPO}/")
            payload = json.loads(kwargs["input"]) if "input" in kwargs else None
            result = self.api(endpoint, method, payload)
        elif args[:2] == ("release", "upload"):
            self.calls.append(("upload", args[2], Path(args[3]).name))
            release = next(r for r in self.releases if r["tag_name"] == args[2])
            path = Path(args[3])
            release["assets"].append(self.asset_record(path.name, "sha256:" + publisher.sha256(path)))
            result = None
        elif args[:2] == ("release", "delete"):
            self.calls.append(("delete-release", args[2]))
            self.releases = [r for r in self.releases if r["tag_name"] != args[2]]
            self.refs.pop(args[2], None)
            self.assertIn("--cleanup-tag", args)
            result = None
        elif args[:2] == ("release", "download"):
            self.calls.append(("download", args[2]))
            directory = Path(args[args.index("--dir") + 1])
            (directory / publisher.BASELINE_ASSET).write_bytes(b"fixture baseline")
            result = None
        else:
            raise AssertionError(f"Unexpected gh command: {args}")
        return subprocess.CompletedProcess(args, 0, json.dumps(copy.deepcopy(result)), "")

    def api(self, endpoint, method, payload):
        if method != "GET":
            self.calls.append((method, endpoint, payload))
        if endpoint == "git/ref/heads/main":
            return {"object": {"type": "commit", "sha": self.main}}
        if endpoint.startswith("git/ref/tags/"):
            tag = endpoint.removeprefix("git/ref/tags/")
            if tag not in self.refs:
                raise subprocess.CalledProcessError(1, [], stderr="HTTP 404: Not Found")
            value = self.refs[tag]
            return {"object": value if isinstance(value, dict) else {"type": "commit", "sha": value}}
        if endpoint == "git/refs" and method == "POST":
            self.refs[payload["ref"].removeprefix("refs/tags/")] = payload["sha"]
            return {}
        if endpoint.startswith("git/refs/tags/"):
            tag = endpoint.removeprefix("git/refs/tags/")
            if method == "PATCH":
                self.assertFalse(payload["force"])
                self.refs[tag] = payload["sha"]
            elif method == "DELETE":
                del self.refs[tag]
            return {}
        if endpoint.startswith("releases/tags/"):
            tag = endpoint.removeprefix("releases/tags/")
            release = next((r for r in self.releases if r["tag_name"] == tag), None)
            if release is None:
                raise subprocess.CalledProcessError(1, [], stderr="HTTP 404: Not Found")
            return release
        if endpoint.startswith("releases?"):
            page = int(endpoint.split("page=")[-1])
            return self.releases[(page - 1) * 100:page * 100]
        if endpoint.startswith("releases/assets/") and method == "DELETE":
            number = int(endpoint.rsplit("/", 1)[1])
            for r in self.releases:
                r["assets"] = [a for a in r["assets"] if a["id"] != number]
            return None
        if endpoint.startswith("releases/"):
            number = int(endpoint.rsplit("/", 1)[1])
            release = next(r for r in self.releases if r["id"] == number)
            if method == "PATCH":
                release.update(payload)
            return release
        raise AssertionError((endpoint, method, payload))

    def publish(self, commit=NEW):
        publisher.publish(REPO, commit, self.root)

    def assert_single(self, commit=NEW):
        self.assertEqual(len(self.releases), 1)
        release = self.releases[0]
        self.assertEqual(release["id"], 1)
        self.assertEqual(release["tag_name"], publisher.ROLLING_TAG)
        self.assertEqual(release["name"], "Felsoria V2")
        self.assertEqual(publisher.release_source(release), commit)
        self.assertEqual(self.refs, {publisher.ROLLING_TAG: commit})
        current = f"Felsoria-Developer-V2-SQL-{commit[:12]}.zip"
        self.assertEqual({a["name"] for a in release["assets"]},
                         {publisher.BASELINE_ASSET, publisher.BASELINE_ASSET + ".sha256",
                          current, current + ".sha256"})
        self.assertIn(f"/archive/{commit}.zip", release["body"])
        publisher.baseline_asset(release)

    def test_first_publication_reuses_baseline_release_and_keeps_build_input(self):
        self.publish()
        self.assert_single()

    def test_next_build_updates_same_release_and_removes_previous_package(self):
        self.publish()
        self.package(NEXT)
        self.publish(NEXT)
        self.assert_single(NEXT)

    def test_retry_keeps_same_release_and_does_not_reupload(self):
        self.publish()
        self.calls.clear()
        self.publish()
        self.assert_single()
        self.assertFalse(any(c[0] == "upload" for c in self.calls))

    def test_late_older_build_never_replaces_newer_package(self):
        self.publish()
        self.package(OLD)
        self.calls.clear()
        self.publish(OLD)
        self.assert_single()
        self.assertEqual(self.calls, [])

    def test_upload_failure_keeps_previous_source_and_all_old_releases(self):
        self.break_when = lambda a, m: a[:2] == ("release", "upload") and a[3].endswith(".sha256")
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertEqual(self.releases[0]["tag_name"], publisher.BASELINE_TAG)
        self.assertEqual(len(self.releases), 2)
        self.assertNotIn(publisher.ROLLING_TAG, self.refs)
        self.break_when = None
        self.publish()
        self.assert_single()

    def test_retry_after_tag_move_and_failed_release_edit(self):
        self.break_when = lambda a, m: a[0] == "api" and a[1].endswith("/releases/1") and m == "PATCH"
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertEqual(self.refs[publisher.ROLLING_TAG], NEW)
        self.assertEqual(len(self.releases), 2)
        self.break_when = None
        self.publish()
        self.assert_single()

    def test_retry_after_cleanup_failure(self):
        self.break_when = lambda a, m: a[:2] == ("release", "delete")
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertEqual(self.releases[0]["tag_name"], publisher.ROLLING_TAG)
        publisher.baseline_asset(self.releases[0])
        self.break_when = None
        self.publish()
        self.assert_single()

    def test_wrong_package_source_never_contacts_github(self):
        self.package(NEW, source=OLD)
        with self.assertRaises(ValueError): self.publish()
        self.gh.assert_not_called()

    def test_missing_dump_never_contacts_github(self):
        self.package(NEW, complete=False)
        with self.assertRaises(ValueError): self.publish()
        self.gh.assert_not_called()

    def test_invalid_commit_never_contacts_github(self):
        with self.assertRaises(ValueError): self.publish("main")
        self.gh.assert_not_called()

    def test_moved_legacy_tag_blocks_all_changes(self):
        self.refs[self.releases[1]["tag_name"]] = NEXT
        with self.assertRaises(ValueError): self.publish()
        self.assertEqual(self.calls, [])

    def test_annotated_baseline_tag_blocks_all_changes(self):
        self.refs[publisher.BASELINE_TAG] = {"type": "tag", "sha": OLD}
        with self.assertRaises(ValueError): self.publish()
        self.assertEqual(self.calls, [])

    def test_baseline_digest_conflict_blocks_all_changes(self):
        self.releases[0]["assets"][0]["digest"] = "sha256:" + "b" * 64
        with self.assertRaises(ValueError): self.publish()
        self.assertEqual(self.calls, [])

    def test_existing_conflicting_asset_blocks_all_uploads(self):
        self.releases[0]["assets"].append(self.asset_record(self.asset.name + ".sha256"))
        with self.assertRaises(ValueError): self.publish()
        self.assertEqual(self.calls, [])

    def test_unrelated_source_cannot_be_published(self):
        self.main = "f" * 40
        with self.assertRaises(ValueError): self.publish()
        self.assertEqual(self.calls, [])

    def test_draft_or_immutable_anchor_blocks_all_changes(self):
        for key in ("draft", "immutable"):
            with self.subTest(key=key):
                self.releases[0][key] = True
                with self.assertRaises(ValueError): self.publish()
                self.assertEqual(self.calls, [])
                self.releases[0][key] = False

    def test_manual_release_and_custom_upload_are_preserved(self):
        self.releases[1]["author"]["login"] = "Felsoria"
        self.releases[0]["assets"].append(self.asset_record("custom.zip"))
        self.publish()
        self.assertEqual(len(self.releases), 2)
        self.assertIn("custom.zip", {a["name"] for a in self.releases[0]["assets"]})

    def test_manual_old_installation_upload_is_preserved(self):
        name = f"Felsoria-Developer-V2-SQL-{OLD[:12]}.zip"
        extra = [self.asset_record(name), self.asset_record(name + ".sha256")]
        for asset in extra: asset["uploader"]["login"] = "Felsoria"
        self.releases[0]["assets"].extend(extra)
        self.publish()
        self.assertTrue(set(a["id"] for a in extra) <= set(a["id"] for a in self.releases[0]["assets"]))

    def test_download_before_and_after_migration_uses_same_pinned_bytes(self):
        import hashlib
        digest = hashlib.sha256(b"fixture baseline").hexdigest()
        with patch.object(publisher, "BASELINE_SHA256", digest):
            self.releases[0]["assets"][0]["digest"] = "sha256:" + digest
            self.assertEqual(publisher.download_baseline(REPO, self.root).read_bytes(), b"fixture baseline")
            self.publish()
            self.assertEqual(publisher.download_baseline(REPO, self.root).read_bytes(), b"fixture baseline")
        self.assertEqual([c[1] for c in self.calls if c[0] == "download"],
                         [publisher.BASELINE_TAG, publisher.ROLLING_TAG])

    def test_downloaded_bytes_are_checked_not_only_api_digest(self):
        with self.assertRaises(ValueError): publisher.download_baseline(REPO, self.root)

    def test_api_server_error_is_not_treated_as_missing_rolling_release(self):
        self.break_when = lambda a, m: a[0] == "api" and "/releases/tags/" in a[1]
        with self.assertRaises(subprocess.CalledProcessError): self.publish()
        self.assertEqual(self.calls, [])

    def test_unexpected_rolling_tag_blocks_uploads(self):
        self.refs[publisher.ROLLING_TAG] = NEXT
        with self.assertRaises(ValueError): self.publish()
        self.assertEqual(self.calls, [])

    def test_changed_upload_digest_prevents_tag_move_and_cleanup(self):
        original = self.api
        def corrupt(endpoint, method, payload):
            value = copy.deepcopy(original(endpoint, method, payload))
            if endpoint == "releases/1" and method == "GET":
                for item in value["assets"]:
                    if item["name"] == self.asset.name:
                        item["digest"] = "sha256:" + "b" * 64
            return value
        with patch.object(self, "api", side_effect=corrupt):
            with self.assertRaises(ValueError): self.publish()
        self.assertNotIn(publisher.ROLLING_TAG, self.refs)
        self.assertEqual(len(self.releases), 2)

    def test_failed_post_publication_check_preserves_legacy_release(self):
        original = self.api
        def corrupt(endpoint, method, payload):
            value = copy.deepcopy(original(endpoint, method, payload))
            if (endpoint == "releases/1" and method == "GET"
                    and value["tag_name"] == publisher.ROLLING_TAG):
                value["body"] = "Source: " + OLD + "\n"
            return value
        with patch.object(self, "api", side_effect=corrupt):
            with self.assertRaises(ValueError): self.publish()
        self.assertEqual(len(self.releases), 2)
        self.assertIn(publisher.BASELINE_TAG, self.refs)
        self.assertFalse(any(c[0] == "delete-release" for c in self.calls))

    def test_missing_source_notes_on_rolling_release_block_changes(self):
        self.publish()
        self.releases[0]["body"] = "manually edited"
        self.calls.clear()
        with self.assertRaises(ValueError): self.publish()
        self.assertEqual(self.calls, [])


class GitAncestryTests(unittest.TestCase):
    def test_real_git_history_and_missing_commit(self):
        import contextlib
        with tempfile.TemporaryDirectory() as root, contextlib.chdir(root):
            subprocess.run(["git", "init", "-q"], check=True)
            def commit():
                subprocess.run(["git", "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                                "commit", "--allow-empty", "-qm", "fixture"], check=True)
                return subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
            first, second = commit(), commit()
            self.assertTrue(publisher.is_ancestor(first, second))
            self.assertFalse(publisher.is_ancestor(second, first))
            with self.assertRaises(RuntimeError): publisher.is_ancestor("f" * 40, second)


if __name__ == "__main__":
    unittest.main()
