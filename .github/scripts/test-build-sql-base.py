#!/usr/bin/env python3
"""Check that a generated baseline never replays or rewrites installed SQL history."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

spec = importlib.util.spec_from_file_location("builder", Path(__file__).with_name("build-sql-base.py"))
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class BaselineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.updates = self.root / "updates"
        self.updates.mkdir()
        for name in ("20261122_001_auth_first.sql", "20261122_002_world_second.sql"):
            (self.updates / name).write_text("SELECT 1;\n")
        self.installed = [builder.migration_record(*item) for item in builder.migrations(self.updates)]
        self.archive = self.root / "baseline.zip"
        self.package()

    def package(self, commit=builder.BASE_COMMIT, installed=None, complete=True):
        with ZipFile(self.archive, "w") as archive:
            for database in builder.DATABASES:
                if complete or database != "hotfixes":
                    archive.writestr(database + ".sql", "-- fixture\n")
            archive.writestr("database-manifest.json", json.dumps({
                "source_commit": commit,
                "applied_migrations": self.installed if installed is None else installed,
            }))

    def history(self):
        with ZipFile(self.archive) as archive:
            return builder.baseline_history(archive, builder.migrations(self.updates))

    def test_existing_migrations_are_not_replayed(self):
        applied, pending = self.history()
        self.assertEqual(applied, self.installed)
        self.assertEqual(pending, [])

    def test_only_later_migrations_are_pending(self):
        new = self.updates / "20261125_001_characters_new.sql"
        new.write_text("SELECT 2;\n")
        applied, pending = self.history()
        self.assertEqual(applied, self.installed)
        self.assertEqual(pending, [("20261125_001", "characters", new)])

    def test_modified_history_is_rejected(self):
        (self.updates / self.installed[0]["file"]).write_text("SELECT 99;\n")
        with self.assertRaises(ValueError):
            self.history()

    def test_deleted_history_is_rejected(self):
        (self.updates / self.installed[0]["file"]).unlink()
        with self.assertRaises(ValueError):
            self.history()

    def test_backdated_migration_is_rejected(self):
        (self.updates / "20261121_001_auth_backdated.sql").write_text("SELECT 0;\n")
        with self.assertRaises(ValueError):
            self.history()

    def test_reordered_manifest_is_rejected(self):
        self.package(installed=list(reversed(self.installed)))
        with self.assertRaises(ValueError):
            self.history()

    def test_empty_history_is_rejected(self):
        self.package(installed=[])
        with self.assertRaises(ValueError):
            self.history()

    def test_wrong_source_is_rejected(self):
        self.package(commit="0" * 40)
        with self.assertRaises(ValueError):
            self.history()

    def test_missing_dump_fails_before_creating_databases(self):
        self.package(complete=False)
        with patch.object(builder, "BASE_SHA256", builder.digest(self.archive)), \
                patch.object(builder, "run_mysql") as mysql:
            with self.assertRaises(ValueError):
                builder.build(self.archive, self.updates, self.root / "result.zip", "1" * 40, "fixture")
            mysql.assert_not_called()

    def test_changed_baseline_fails_before_creating_databases(self):
        with patch.object(builder, "run_mysql") as mysql:
            with self.assertRaises(ValueError):
                builder.build(self.archive, self.updates, self.root / "result.zip", "1" * 40, "fixture")
            mysql.assert_not_called()


if __name__ == "__main__":
    unittest.main()
