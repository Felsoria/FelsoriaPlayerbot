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

def verify_baseline(repository):
    baseline = json.loads(gh("api", f"repos/{repository}/releases/tags/{BASELINE_TAG}").stdout)
    retained = [item for item in baseline["assets"] if item["name"] == BASELINE_ASSET]
    if (baseline.get("draft") or baseline.get("target_commitish") != BASELINE_COMMIT
            or len(retained) != 1 or retained[0].get("digest") != f"sha256:{BASELINE_SHA256}"):
        raise ValueError("The retained database baseline is not available or has changed.")


def automatic_release(release):
    """Recognize complete database prereleases created by this workflow only."""
    commit = release.get("target_commitish", "")
    if (not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}", commit)
            or release.get("tag_name") != f"developer-v2-{commit[:12]}"
            or (release.get("author") or {}).get("login") != "github-actions[bot]"
            or release.get("draft") or release.get("immutable")
            or not release.get("prerelease")):
        return False
    name = f"Felsoria-Developer-V2-SQL-{commit[:12]}.zip"
    assets = release.get("assets", [])
    return (len(assets) == 2 and {item["name"] for item in assets} == {name, name + ".sha256"}
            and all(item.get("state") == "uploaded"
                    and re.fullmatch(r"sha256:[0-9a-f]{64}", item.get("digest") or "")
                    for item in assets))


def release_identity(release):
    return (release.get("id"), release.get("tag_name"), release.get("target_commitish"),
            release.get("draft"), release.get("immutable"), release.get("prerelease"),
            (release.get("author") or {}).get("login"),
            sorted((item["name"], item.get("digest"), item.get("state"))
                   for item in release.get("assets", [])))


def is_ancestor(older, newer):
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", older, newer],
        capture_output=True, text=True,
    )
    if result.returncode not in (0, 1):
        raise RuntimeError(f"Cannot verify release ancestry: {result.stderr}")
    return result.returncode == 0


def prune_old_releases(repository, commit):
    """Keep the pinned build input and the newest complete release on main."""
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("A complete source commit is required.")
    verify_baseline(repository)
    if not is_ancestor(commit, "origin/main"):
        raise ValueError("The publishing commit is not on main.")
    releases = []
    page = 1
    while True:
        batch = json.loads(gh(
            "api", f"repos/{repository}/releases?per_page=100&page={page}"
        ).stdout)
        if not isinstance(batch, list):
            raise ValueError("The release list could not be verified.")
        releases.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    candidates = [release for release in releases
                  if automatic_release(release)
                  and is_ancestor(release["target_commitish"], "origin/main")]
    current = [release for release in candidates if release["target_commitish"] == commit]
    if len(current) != 1:
        raise ValueError("The matching published database release is not complete.")
    newest = current[0]
    for release in candidates:
        if is_ancestor(newest["target_commitish"], release["target_commitish"]):
            newest = release
    obsolete = [release for release in candidates
                if release["tag_name"] not in (BASELINE_TAG, newest["tag_name"])
                and is_ancestor(release["target_commitish"], newest["target_commitish"])]

    # Check every deletion before changing anything. A moved tag or an edited
    # release is preserved for the owner to inspect instead of being removed.
    for release in [newest, *obsolete]:
        live = json.loads(gh("api", f"repos/{repository}/releases/{release['id']}").stdout)
        if release_identity(live) != release_identity(release):
            raise ValueError(f"Release changed during retention checks: {release['tag_name']}")
        ref = json.loads(gh(
            "api", f"repos/{repository}/git/ref/tags/{release['tag_name']}"
        ).stdout)
        obj = ref.get("object", {})
        if obj.get("type") != "commit" or obj.get("sha") != release["target_commitish"]:
            raise ValueError(f"Release tag moved or is annotated: {release['tag_name']}")
    for release in obsolete:
        gh("release", "delete", release["tag_name"], "--yes", "--cleanup-tag", "--repo", repository)
    print(f"Retained {BASELINE_TAG} and {newest['tag_name']}; "
          f"removed {len(obsolete)} superseded automatic releases and their tags.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--prune-old-releases", action="store_true")
    args = parser.parse_args()
    publish(args.repo, args.commit, args.directory)
    if args.prune_old_releases:
        prune_old_releases(args.repo, args.commit)
