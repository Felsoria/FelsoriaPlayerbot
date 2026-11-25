#!/usr/bin/env python3
"""Publish the matching SQL package after the approved main build passes."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from zipfile import ZipFile


BASELINE_TAG = "developer-v2-fc893298792a"
BASELINE_COMMIT = "fc893298792a09cbcbc172659b717b6af6cd890d"
BASELINE_ASSET = "Felsoria-Developer-V2-SQL-fc893298792a.zip"
BASELINE_SHA256 = "16a7b8088776f8152be6f0345e9caa22b01c067fd609086f7fc9344ab3863068"
LEGACY_TAG = "database-v2-20261121"
LEGACY_ASSETS = {
    "Felsoria-Developer-V2-Bot-SQL.zip":
        "dc7d86a8922aa2116b8a76d20402c43a4adba741d862acf5ff925bbc12907100",
    "Felsoria-Developer-V2-SQL-Clean-20261121.zip":
        "f09cc8d556d49e60fc618cd5ead7fb5d41fb15adf7bd7b21c362681e9c1abbb4",
}


def gh(*args):
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True)


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def publish(repository, commit, directory):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("A complete source commit is required.")
    asset = directory / f"Felsoria-Developer-V2-SQL-{commit[:12]}.zip"
    with ZipFile(asset) as archive:
        if set(archive.namelist()) != {
            "auth.sql", "characters.sql", "world.sql", "hotfixes.sql", "database-manifest.json"
        }:
            raise ValueError("The release package is incomplete.")
        if json.loads(archive.read("database-manifest.json"))["source_commit"] != commit:
            raise ValueError("The SQL package belongs to a different source commit.")

    checksum = asset.with_suffix(".zip.sha256")
    checksum.write_text(f"{sha256(asset)}  {asset.name}\n", encoding="utf-8")
    tag = f"developer-v2-{commit[:12]}"
    probe = subprocess.run(
        ["gh", "api", f"repos/{repository}/releases/tags/{tag}"],
        capture_output=True, text=True,
    )
    if probe.returncode:
        if "HTTP 404" not in probe.stderr:
            raise RuntimeError(f"Cannot check the existing release: {probe.stderr}")
        with tempfile.TemporaryDirectory() as work:
            notes = Path(work) / "notes.md"
            notes.write_text(
                f"Source: {commit}\n\n"
                "Built from the pinned SQL baseline plus the migrations in this source revision. "
                "The SQL and Windows build checks passed before publication.\n\n"
                "Download the SQL ZIP and its SHA-256 file. Use the source archive for this tag "
                "and follow README.md.\n",
                encoding="utf-8",
            )
            gh("release", "create", tag, str(asset), str(checksum), "--repo", repository,
               "--target", commit, "--prerelease", "--title", f"Felsoria Developer V2 – {commit[:12]}",
               "--notes-file", str(notes))
        return

    release = json.loads(probe.stdout)
    if release["target_commitish"] != commit or release["draft"]:
        raise ValueError("An existing release has a different source or publication state.")
    existing = {item["name"]: item for item in release["assets"]}
    missing = []
    for path in (asset, checksum):
        if path.name not in existing:
            missing.append(path)
            continue
        digest = existing[path.name].get("digest")
        if digest:
            if digest != f"sha256:{sha256(path)}":
                raise ValueError(f"Existing release asset differs: {path.name}")
        else:
            with tempfile.TemporaryDirectory() as work:
                gh("release", "download", tag, "--repo", repository, "--pattern", path.name, "--dir", work)
                if sha256(Path(work) / path.name) != sha256(path):
                    raise ValueError(f"Existing release asset differs: {path.name}")
    for path in missing:
        gh("release", "upload", tag, str(path), "--repo", repository)
    print(f"Verified release {tag}; existing assets were preserved.")


def retire_legacy_assets(repository):
    """Retire only the two superseded uploads, after publication has succeeded."""
    probe = subprocess.run(
        ["gh", "api", f"repos/{repository}/releases/tags/{LEGACY_TAG}"],
        capture_output=True, text=True,
    )
    if probe.returncode:
        if "HTTP 404" in probe.stderr:
            print("The legacy database release has already been retired.")
            return
        raise RuntimeError(f"Cannot check the legacy release: {probe.stderr}")
    release = json.loads(probe.stdout)
    if release.get("tag_name") != LEGACY_TAG or release.get("immutable"):
        raise ValueError("The legacy release cannot be safely retired.")
    obsolete = [item for item in release["assets"] if item["name"] in LEGACY_ASSETS]
    for item in obsolete:
        if item.get("digest") != f"sha256:{LEGACY_ASSETS[item['name']]}":
            raise ValueError(f"Legacy upload differs from the approved archive: {item['name']}")
    if not obsolete and release["assets"]:
        return  # Preserve any unrelated uploads.

    # Keep the exact, tested input needed by all future SQL builds.
    baseline = json.loads(gh("api", f"repos/{repository}/releases/tags/{BASELINE_TAG}").stdout)
    retained = [item for item in baseline["assets"] if item["name"] == BASELINE_ASSET]
    if (baseline.get("draft") or baseline.get("target_commitish") != BASELINE_COMMIT
            or len(retained) != 1 or retained[0].get("digest") != f"sha256:{BASELINE_SHA256}"):
        raise ValueError("The retained database baseline is not available or has changed.")
    for item in obsolete:
        gh("api", "--method", "DELETE", f"repos/{repository}/releases/assets/{item['id']}")

    # Drop the superseded release only if no other uploads remain. Its git tag stays.
    remaining = json.loads(gh("api", f"repos/{repository}/releases/{release['id']}").stdout)
    if not remaining["assets"]:
        gh("api", "--method", "DELETE", f"repos/{repository}/releases/{release['id']}")
    print(f"Retired {len(obsolete)} legacy SQL uploads; the tested database baseline was retained.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--retire-legacy-assets", action="store_true")
    args = parser.parse_args()
    publish(args.repo, args.commit, args.directory)
    if args.retire_legacy_assets:
        retire_legacy_assets(args.repo)
