#!/usr/bin/env python3
"""Build a fresh-install SQL archive in a disposable MySQL container."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


DATABASES = ("auth", "characters", "world", "hotfixes")
BASE_TAG = "database-v2-20261121"
BASE_ASSET = "Felsoria-Developer-V2-SQL-Clean-20261121.zip"
BASE_SHA256 = "f09cc8d556d49e60fc618cd5ead7fb5d41fb15adf7bd7b21c362681e9c1abbb4"
MIGRATION_NAME = re.compile(r"(\d{8}_\d{3})_(auth|characters|world|hotfixes)_[a-z0-9_]+\.sql")
DUMP_OPTIONS = (
    "--single-transaction", "--skip-lock-tables", "--quick", "--hex-blob",
    "--set-gtid-purged=OFF", "--skip-dump-date", "--routines", "--events",
)


def digest(path):
    with path.open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()


def migrations(directory):
    found = {}
    for path in sorted(directory.rglob("*")):
        if not path.is_file() or path.suffix.lower() != ".sql":
            continue
        match = MIGRATION_NAME.fullmatch(path.name)
        if not match or path.parent != directory:
            raise ValueError(f"Invalid migration path: {path}")
        revision, database = match.groups()
        if revision in found:
            raise ValueError(f"Duplicate migration revision: {revision}")
        found[revision] = (database, path)
    return [(revision, *found[revision]) for revision in sorted(found)]


def run_mysql(container, program, *arguments, **streams):
    return subprocess.run(
        [
            "docker", "exec", "-i", container, program,
            "--no-defaults", "--protocol=socket", "--user=root",
            "--default-character-set=utf8mb4", *arguments,
        ],
        check=True,
        **streams,
    )


def wait_for_mysql(container):
    # The official image initializes using a temporary server without TCP.
    # Checking TCP inside the network-isolated container waits for the final server.
    for _ in range(90):
        probe = subprocess.run(
            ["docker", "exec", container, "mysql", "--no-defaults",
             "--protocol=TCP", "--host=127.0.0.1", "--user=root", "--execute=SELECT 1"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if probe.returncode == 0:
            return
        time.sleep(2)
    raise RuntimeError("The isolated MySQL container did not become ready.")


def check_empty_player_data(container):
    result = run_mysql(
        container, "mysql", "--batch", "--skip-column-names",
        "--execute=SELECT (SELECT COUNT(*) FROM auth.account), "
        "(SELECT COUNT(*) FROM auth.battlenet_accounts), "
        "(SELECT COUNT(*) FROM characters.characters)",
        capture_output=True,
        text=True,
    )
    if result.stdout.split() != ["0", "0", "0"]:
        raise ValueError("Account or character records cannot be included in the base archive.")


def check_bootstrap(container):
    result = run_mysql(
        container, "mysql", "--batch", "--skip-column-names",
        "--execute=SELECT COUNT(*) FROM auth.realmlist AS realm "
        "JOIN auth.build_info AS client ON client.build = realm.gamebuild "
        "WHERE realm.id = 1 AND realm.port > 0 AND client.build = 26972 "
        "AND client.majorVersion = 7 AND client.minorVersion = 3 AND client.bugfixVersion = 5 "
        "AND client.winAuthSeed REGEXP '^[[:xdigit:]]{32}$' "
        "AND client.win64AuthSeed REGEXP '^[[:xdigit:]]{32}$' "
        "AND client.mac64AuthSeed REGEXP '^[[:xdigit:]]{32}$'",
        capture_output=True,
        text=True,
    )
    if result.stdout.strip() != "1":
        raise ValueError("The base archive needs realm 1 and client build 26972 login metadata.")


def write_archive(output, files):
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".zip.part")
    with ZipFile(temporary, "w", compression=ZIP_DEFLATED) as package:
        for path in files:
            # Fixed ZIP timestamps allow repeated builds of the same source to reproduce the asset.
            entry = ZipInfo(path.name)
            entry.compress_type = ZIP_DEFLATED
            with path.open("rb") as source, package.open(entry, "w", force_zip64=True) as dest:
                shutil.copyfileobj(source, dest, length=1024 * 1024)
    temporary.replace(output)


def build(baseline, update_dir, output, source_commit, container):
    baseline_digest = digest(baseline)
    if baseline_digest != BASE_SHA256:
        raise ValueError("The baseline ZIP does not match its pinned SHA-256.")
    pending = migrations(update_dir)
    expected_files = [f"{database}.sql" for database in DATABASES]
    with tempfile.TemporaryDirectory(prefix="felsoria-sql-") as temp_dir:
        work = Path(temp_dir)
        with ZipFile(baseline) as seed:
            if sorted(seed.namelist()) != sorted(expected_files):
                raise ValueError("The baseline ZIP does not contain exactly the four expected dumps.")
            create = " ".join(
                f"CREATE DATABASE `{database}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
                for database in DATABASES
            )
            run_mysql(container, "mysql", f"--execute={create}")
            for database in DATABASES:
                print(f"Importing baseline: {database}", flush=True)
                staged = work / f"{database}.sql"
                with seed.open(staged.name) as source, staged.open("wb") as handle:
                    shutil.copyfileobj(source, handle)
                with staged.open("rb") as sql:
                    run_mysql(container, "mysql", "--binary-mode", f"--database={database}", stdin=sql)

        applied = []
        for revision, database, path in pending:
            print(f"Applying {path.name} to {database}", flush=True)
            patch_digest = digest(path)
            with path.open("rb") as sql:
                run_mysql(container, "mysql", "--binary-mode", f"--database={database}", stdin=sql)
            applied.append({
                "id": revision, "database": database, "file": path.name, "sha256": patch_digest
            })

        check_empty_player_data(container)
        check_bootstrap(container)
        dumps = []
        for database in DATABASES:
            print(f"Exporting {database}", flush=True)
            path = work / f"{database}.sql"
            with path.open("wb") as sql:
                run_mysql(container, "mysqldump", *DUMP_OPTIONS, database, stdout=sql)
            dumps.append(path)

        manifest = work / "database-manifest.json"
        manifest.write_text(json.dumps({
            "baseline": {"tag": BASE_TAG, "asset": BASE_ASSET, "sha256": baseline_digest},
            "source_commit": source_commit,
            "applied_migrations": applied,
        }, indent=2) + "\n", encoding="utf-8")
        write_archive(output, [*dumps, manifest])
        print(f"Built {output.name}: {len(applied)} migrations, SHA-256 {digest(output)}", flush=True)


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--baseline", type=Path, required=True)
    cli.add_argument("--updates", type=Path, required=True)
    cli.add_argument("--output", type=Path, required=True)
    cli.add_argument("--commit", required=True)
    cli.add_argument("--container", required=True)
    args = cli.parse_args()
    wait_for_mysql(args.container)
    build(args.baseline, args.updates, args.output, args.commit, args.container)


if __name__ == "__main__":
    main()
