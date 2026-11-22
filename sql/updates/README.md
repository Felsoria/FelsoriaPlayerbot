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

The **Build Felsoria SQL base** workflow restores the verified
`database-v2-20261121` SQL ZIP into a disposable MySQL 5.7.44 container and
replays all migrations in revision order. A merge that changes SQL migrations
produces a downloadable Actions artifact. Publishing a release from a revision
on `main` also attaches the generated ZIP to that release. The baseline
release acts as the input to each new build.

The ZIP contains the four SQL dumps and `database-manifest.json`, with the
source commit, baseline checksum, and the IDs and SHA-256 checksums of all
included migrations. The snapshot contains zero player accounts and
characters. Prepare the realm, client-build metadata and RBAC data for your
installation as needed.

## Updating an existing installation

Use the manifest for your installed snapshot to identify the migrations you
already have. Apply the remaining files in revision order to their named
databases. Import the full ZIP into empty schemas for a fresh installation.
