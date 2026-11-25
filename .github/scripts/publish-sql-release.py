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
                "and follow README.md. The original database-v2-20261121 asset is the build input; "
                "it is not the current installation package.\n",
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--directory", required=True, type=Path)
    args = parser.parse_args()
    publish(args.repo, args.commit, args.directory)
