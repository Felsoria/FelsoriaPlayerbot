#!/usr/bin/env python3
"""Publish the matching SQL package after the approved main build passes."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from zipfile import ZipFile


ROLLING_TAG = "felsoria-v2"

BASELINE_TAG = "developer-v2-fc893298792a"
BASELINE_COMMIT = "fc893298792a09cbcbc172659b717b6af6cd890d"
BASELINE_ASSET = "Felsoria-Developer-V2-SQL-fc893298792a.zip"
BASELINE_SHA256 = "16a7b8088776f8152be6f0345e9caa22b01c067fd609086f7fc9344ab3863068"


def gh(*args, **kwargs):
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True, **kwargs)


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


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


def api(repository, endpoint, method="GET", payload=None, optional=False):
    args = ["api", f"repos/{repository}/{endpoint}"]
    if method != "GET":
        args += ["--method", method]
    kwargs = {}
    if payload is not None:
        args += ["--input", "-"]
        kwargs["input"] = json.dumps(payload)
    try:
        result = gh(*args, **kwargs)
    except subprocess.CalledProcessError as error:
        if optional and "HTTP 404" in (error.stderr or ""):
            return None
        raise
    return json.loads(result.stdout) if result.stdout.strip() else None


def baseline_asset(release):
    assets = [item for item in release.get("assets", []) if item["name"] == BASELINE_ASSET]
    if (len(assets) != 1 or assets[0].get("state") != "uploaded"
            or assets[0].get("digest") != f"sha256:{BASELINE_SHA256}"):
        raise ValueError("The pinned database baseline is missing or changed.")
    return assets[0]


def managed_release(release):
    if ((release.get("author") or {}).get("login") != "github-actions[bot]"
            or release.get("draft") or release.get("immutable")
            or not release.get("prerelease")
            or not re.fullmatch(r"[0-9a-f]{40}", release.get("target_commitish", ""))):
        raise ValueError("The release is not an editable automatic database release.")
    baseline_asset(release)


def release_source(release):
    # GitHub does not retarget an existing tag through target_commitish.
    # The notes record the installed SQL revision; the actual tag is checked separately.
    if release["tag_name"] == ROLLING_TAG:
        match = re.match(r"Source: ([0-9a-f]{40})\n", release.get("body") or "")
        if not match:
            raise ValueError("The rolling release has no exact source revision.")
        return match[1]
    return release["target_commitish"]


def download_baseline(repository, directory):
    """Use the single release, with a verified fallback during the first migration."""
    release = api(repository, f"releases/tags/{ROLLING_TAG}", optional=True)
    if release is None:
        release = api(repository, f"releases/tags/{BASELINE_TAG}")
    managed_release(release)
    directory.mkdir(parents=True, exist_ok=True)
    gh("release", "download", release["tag_name"], "--repo", repository,
       "--pattern", BASELINE_ASSET, "--dir", str(directory), "--clobber")
    path = directory / BASELINE_ASSET
    if sha256(path) != BASELINE_SHA256:
        raise ValueError("The downloaded baseline does not match its pinned SHA-256.")
    return path


def validate_package(commit, directory):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("A complete source commit is required.")
    asset = directory / f"Felsoria-Developer-V2-SQL-{commit[:12]}.zip"
    with ZipFile(asset) as archive:
        if set(archive.namelist()) != {
            "auth.sql", "characters.sql", "world.sql", "hotfixes.sql", "database-manifest.json"
        }:
            raise ValueError("The release package is incomplete.")
        if json.loads(archive.read("database-manifest.json")).get("source_commit") != commit:
            raise ValueError("The SQL package belongs to a different source commit.")
    checksum = asset.with_suffix(".zip.sha256")
    checksum.write_text(f"{sha256(asset)}  {asset.name}\n", encoding="utf-8")
    return asset, checksum


def tag_commit(repository, tag):
    ref = api(repository, f"git/ref/tags/{tag}", optional=True)
    if ref is None:
        return None
    obj = ref.get("object", {})
    if obj.get("type") != "commit" or not re.fullmatch(r"[0-9a-f]{40}", obj.get("sha", "")):
        raise ValueError(f"The tag is not a lightweight source tag: {tag}")
    return obj["sha"]


def legacy_releases(repository, commit, anchor_id):
    obsolete = []
    page = 1
    while True:
        releases = api(repository, f"releases?per_page=100&page={page}")
        if not isinstance(releases, list):
            raise ValueError("The release list could not be verified.")
        obsolete.extend(item for item in releases
                        if item["id"] != anchor_id and automatic_release(item)
                        and is_ancestor(item["target_commitish"], commit))
        if len(releases) < 100:
            return obsolete
        page += 1


def verify_assets(release, paths):
    assets = {item["name"]: item for item in release["assets"]}
    for path in paths:
        item = assets.get(path.name, {})
        if item.get("state") != "uploaded" or item.get("digest") != f"sha256:{sha256(path)}":
            raise ValueError(f"The uploaded package could not be verified: {path.name}")


def old_assets(release, commit, keep):
    """Prune only complete automatic SQL pairs from older commits in local history."""
    result = []
    assets = {item["name"]: item for item in release["assets"]}
    for name, asset in assets.items():
        match = re.fullmatch(r"Felsoria-Developer-V2-SQL-([0-9a-f]{12})\.zip", name)
        if not match or name in keep or name + ".sha256" not in assets:
            continue
        pair = [asset, assets[name + ".sha256"]]
        if not all(item.get("state") == "uploaded"
                   and (item.get("uploader") or {}).get("login") == "github-actions[bot]"
                   and re.fullmatch(r"sha256:[0-9a-f]{64}", item.get("digest") or "")
                   for item in pair):
            continue
        source = subprocess.run(["git", "rev-parse", "--verify", match[1] + "^{commit}"],
                                capture_output=True, text=True)
        if source.returncode == 0 and is_ancestor(source.stdout.strip(), commit):
            result.extend(pair)
    return result


def publish(repository, commit, directory):
    paths = validate_package(commit, directory)
    main = api(repository, "git/ref/heads/main").get("object", {})
    if main.get("type") != "commit" or not re.fullmatch(r"[0-9a-f]{40}", main.get("sha", "")):
        raise ValueError("The current main revision could not be verified.")
    if not is_ancestor(commit, main["sha"]):
        raise ValueError("The publishing commit is not on main.")
    release = api(repository, f"releases/tags/{ROLLING_TAG}", optional=True)
    if release is None:
        # Reuse the baseline release itself: its build input never leaves this release.
        release = api(repository, f"releases/tags/{BASELINE_TAG}")
        if release.get("target_commitish") != BASELINE_COMMIT:
            raise ValueError("The legacy baseline source has changed.")
    managed_release(release)
    previous = release_source(release)
    if previous != commit and is_ancestor(commit, previous):
        print("A newer build is already published; this older run will not replace it.")
        return
    if not is_ancestor(previous, commit):
        raise ValueError("The release source is not an ancestor of this build.")
    ref = tag_commit(repository, release["tag_name"])
    if ref != previous and ref != commit:
        raise ValueError("The release tag moved unexpectedly.")
    rolling_ref = tag_commit(repository, ROLLING_TAG)
    if rolling_ref is not None and rolling_ref not in (previous, commit):
        raise ValueError("The rolling tag moved unexpectedly.")

    obsolete = legacy_releases(repository, commit, release["id"])
    # Validate legacy deletions before touching the rolling release.
    for item in obsolete:
        live = api(repository, f"releases/{item['id']}")
        if (release_identity(live) != release_identity(item)
                or tag_commit(repository, item["tag_name"]) != item["target_commitish"]):
            raise ValueError(f"Legacy release changed: {item['tag_name']}")

    existing = {item["name"]: item for item in release["assets"]}
    for path in paths:
        if path.name in existing:
            verify_assets(release, [path])
    for path in paths:
        if path.name not in existing:
            gh("release", "upload", release["tag_name"], str(path), "--repo", repository)
    staged = api(repository, f"releases/{release['id']}")
    managed_release(staged)
    if staged["tag_name"] != release["tag_name"] or release_source(staged) != previous:
        raise ValueError("The release source changed during the upload.")
    verify_assets(staged, paths)
    keep = {BASELINE_ASSET, BASELINE_ASSET + ".sha256", *(path.name for path in paths)}
    outdated = old_assets(staged, commit, keep)

    if rolling_ref is None:
        api(repository, "git/refs", "POST", {"ref": f"refs/tags/{ROLLING_TAG}", "sha": commit})
    elif rolling_ref != commit:
        api(repository, f"git/refs/tags/{ROLLING_TAG}", "PATCH", {"sha": commit, "force": False})
    body = (
        f"Source: {commit}\n\n"
        "This release is updated after the SQL and Windows build checks pass.\n\n"
        f"Download **{paths[0].name}** and its SHA-256 file for installation. "
        f"[Matching source ZIP](https://github.com/{repository}/archive/{commit}.zip). "
        "The source commit is also recorded in database-manifest.json.\n\n"
        f"`{BASELINE_ASSET}` is the fixed build input; contributors' builds use it automatically. "
        "Follow README.md to install the current package.\n"
    )
    api(repository, f"releases/{release['id']}", "PATCH", {
        "tag_name": ROLLING_TAG,
        "name": "Felsoria V2", "body": body, "prerelease": True,
    })
    published = api(repository, f"releases/{release['id']}")
    managed_release(published)
    verify_assets(published, paths)
    if (published["tag_name"] != ROLLING_TAG or release_source(published) != commit
            or tag_commit(repository, ROLLING_TAG) != commit):
        raise ValueError("The matching source and database were not published; cleanup stopped.")

    # Never remove the old packages until the new source, ZIP and baseline are verified.
    for item in outdated:
        api(repository, f"releases/assets/{item['id']}", "DELETE")
    for item in obsolete:
        live = api(repository, f"releases/{item['id']}", optional=True)
        if live is None:
            continue
        if (release_identity(live) != release_identity(item)
                or tag_commit(repository, item["tag_name"]) != item["target_commitish"]):
            raise ValueError(f"Legacy release changed before cleanup: {item['tag_name']}")
        gh("release", "delete", item["tag_name"], "--yes", "--cleanup-tag", "--repo", repository)
    baseline_ref = tag_commit(repository, BASELINE_TAG)
    if baseline_ref is not None:
        if baseline_ref != BASELINE_COMMIT:
            raise ValueError("The obsolete baseline tag moved; it was preserved.")
        api(repository, f"git/refs/tags/{BASELINE_TAG}", "DELETE")
    print(f"Updated the single {ROLLING_TAG} release to {commit}; the pinned baseline was retained.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--commit")
    parser.add_argument("--download-baseline", action="store_true")
    args = parser.parse_args()
    if args.download_baseline:
        download_baseline(args.repo, args.directory)
    elif args.commit:
        publish(args.repo, args.commit, args.directory)
    else:
        parser.error("--commit is required when publishing")
