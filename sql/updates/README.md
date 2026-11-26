# SQL migrations

Put each database change in a separate SQL file in this directory. Name it
`YYYYMMDD_NNN_database_description.sql`, for example
`20261122_001_world_fix_quest_credit.sql`. Use `auth`, `characters`, `world`, or
`hotfixes` as the database. Each date and sequence number pair must be unique
across all four databases. Use table names relative to the target database.
Submit related SQL and C++ changes in the same pull request.

Treat merged SQL migrations as history. Submit a new file to correct a
previous migration so existing installations can apply the correction too.

## Building a fresh-install archive

The **Build Felsoria SQL base** workflow restores the checksum-pinned
`Felsoria-Developer-V2-SQL-fc893298792a.zip` into a disposable MySQL 5.7.44
container. The fixed input is retained as an asset of the single **Felsoria V2**
release (`felsoria-v2`). During the first migration, builds can still download
it from the old baseline release. Its original SHA-256 is always checked.

The builder checks the baseline's migration IDs and checksums against the
source and applies only later migrations in revision order. Pull requests
run the SQL and Windows checks before merge. Each update to `main`, or a manual
workflow run on `main`, builds the ZIP for that exact source commit.

After both checks pass, publication updates the same release and moves its
`felsoria-v2` tag forward. Its title and release URL stay the same. The new ZIP
and checksum are uploaded and verified before old installation packages are
removed. The fixed baseline stays in the release. The initial migration also
removes superseded automatic releases and their source tags; manual releases,
drafts, immutable releases and custom uploads are preserved.

Publication is serialized. An older build finishing late cannot replace a
newer source revision. A failed upload leaves the previous installation
package in place and can be retried. GitHub does not update the tag, assets and
notes atomically, so installation instructions use the exact source commit in
the release notes and SQL manifest, rather than relying on a mutable tag.

The ZIP contains the four SQL dumps and `database-manifest.json`, with the
source commit, baseline checksum, and the IDs and SHA-256 checksums of all
included migrations. The snapshot contains zero player accounts and
characters. It includes local realm 1 and the client build 26972 metadata.
Create your first account in the worldserver console after setup. The Felsoria
core grants GM levels through `account_access`, using the worldserver console's
`account set gmlevel` command.

## Updating an existing installation

Use the manifest for your installed snapshot to identify the migrations you
already have. Apply the remaining files in revision order to their named
databases. For a fresh installation, see [database setup](../../README.md#2-import-the-database).
