# Felsoria Core

**World of Warcraft: Legion 7.3.5 · Build 26972 · Developer V2**

Felsoria Core is a Legion server emulator with integrated PlayerBots, built on LegionCore and LegionPlayerBot. It is designed for solo and small-group play. Bots populate the open world, join dungeon groups, and fill battleground teams.

Developer V2 is a development snapshot. Quest behavior, encounter mechanics, battleground objectives, and localization can vary with the revision and database snapshot.

## Features

- **Dynamic world bots** spawn according to the player's zone, level, and faction territory. Bots in a player's party are kept when the surrounding bot population changes.
- **Dungeon Finder and combat AI** cover tank, healer, and damage roles. Group AI includes tank positioning, area-effect avoidance, and logic for returning to combat after moving out of danger.
- **Battleground AI** includes objective and navigation logic for Warsong Gulch, Arathi Basin, Alterac Valley, and the Battle for Gilneas. Gilneas has custom routes for graveyard exits and water crossings.
- **Legion content fixes** include changes to Demon Hunter quest scripting and Vault of the Wardens. The source also includes a DB2 extractor with locale-specific handling.

## Verified revision

The Windows and database checks below use source commit [`f382fbc`](https://github.com/Felsoria/felsoria-source/commit/f382fbc301662bf33c415fd29795c73a6515b981). Build that revision to pair it with the tested SQL package.

- Visual Studio 2022 built `bnetserver`, `worldserver`, and all four data tools in `RelWithDebInfo`. The TLS startup and missing-data diagnostics also passed. [Windows build and startup checks](https://github.com/Felsoria/felsoria-source/actions/runs/45036612628)
- The generated ZIP was imported into four empty MySQL Server 5.7.44 databases. The realm, client build, empty player data, and migration checksums matched. [SQL reimport](https://github.com/Felsoria/felsoria-source/actions/runs/45036879812)
- On Windows, `bnetserver` started with the generated `auth.sql`, exposed both loopback listeners, and issued a login ticket for a test account over HTTPS. [Battle.net runtime check](https://github.com/Felsoria/felsoria-source/actions/runs/45038339748)

Character creation, world entry, and full map extraction from client assets require manual verification.

## 1. Build on Windows

Install Visual Studio 2022 with **Desktop development with C++** and the x64 v143 toolset, CMake 3.31, Boost 1.78.0, the x64 OpenSSL 3 development libraries, and the MySQL x64 C client headers and library. The Windows CI uses the MySQL 8.4 client SDK and runs the login test against MySQL Server 5.7.44.

Clone into a new folder. To reproduce the verified setup, select the tested revision.

```powershell
New-Item -ItemType Directory -Path 'D:\Felsoria-Local' -Force | Out-Null
git clone https://github.com/Felsoria/felsoria-source.git D:\Felsoria-Local\source
Set-Location D:\Felsoria-Local\source
git switch --detach f382fbc301662bf33c415fd29795c73a6515b981
```

Run `bootstrap.bat` from the Boost directory in the VS2022 x64 developer shell. Then build the six static Boost libraries with the shared MSVC runtime. Their output is in `stage/lib`.

```powershell
.\b2.exe toolset=msvc-14.3 address-model=64 link=static runtime-link=shared threading=multi variant=release --layout=versioned --with-system --with-filesystem --with-thread --with-program_options --with-iostreams --with-regex stage
```

Open PowerShell in the repository root and set the paths to your installed dependencies. `MYSQL_INCLUDE_DIR` must contain `mysql.h` and `MYSQL_LIBRARY` must point to `libmysql.lib`.

```powershell
$boostRoot = 'D:\Dependencies\boost_1_78_0'
$boostLib = Join-Path $boostRoot 'stage\lib'
$opensslRoot = 'D:\OpenSSL-Win64'
$mysqlSdk = 'D:\Dependencies\mysql-8.4'
$mysqlInclude = Join-Path $mysqlSdk 'include'
$mysqlLib = Join-Path $mysqlSdk 'lib\libmysql.lib'

cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DTOOLS=ON `
  "-DBOOST_ROOT:PATH=$boostRoot" "-DBOOST_LIBRARYDIR:PATH=$boostLib" `
  "-DOPENSSL_ROOT_DIR=$opensslRoot" "-DMYSQL_INCLUDE_DIR=$mysqlInclude" "-DMYSQL_LIBRARY=$mysqlLib"
```

After CMake configures successfully, build the two servers and four tools.

```powershell
cmake --build build --config RelWithDebInfo --parallel 2 --target `
  bnetserver worldserver mapextractor vmap4extractor vmap4assembler mmaps_generator
```

Find the executables and server configuration templates in `build/bin/RelWithDebInfo/`. Make the matching x64 OpenSSL and MySQL runtime DLLs available beside the executables or on `PATH`.

## 2. Import the database package

Download `felsoria-database.zip` from the `f382fbc` Actions run. Unpack the outer ZIP and then `Felsoria-Developer-V2-SQL-f382fbc30166.zip`. The inner package contains `auth.sql`, `characters.sql`, `world.sql`, `hotfixes.sql`, and `database-manifest.json`. [Verified SQL download](https://github.com/Felsoria/felsoria-source/actions/runs/45036005386/artifacts/14760021090)

The inner ZIP SHA-256 is `23477a9e5517f87b1af94c261a57e17cfdc644fd245c626dc48e32dada5456eb`. For a different source revision, use its matching generated ZIP and check `source_commit` and `applied_migrations` in `database-manifest.json`. Generated ZIPs are attached when a new GitHub release is published from `main`. The original clean snapshot is the input to the build workflow. [SQL builds](https://github.com/Felsoria/felsoria-source/actions/workflows/build-sql-base.yml) · [SQL migrations](sql/updates/README.md)

Use a dedicated local MySQL instance with a new data directory. MySQL 5.7.44 is the verified import target. In your MySQL session, check that the directory and port belong to your new instance before creating any databases. [MySQL Community Server archives](https://downloads.mysql.com/archives/community/)

```sql
SELECT @@datadir, @@port, @@version;
SHOW DATABASES;
```

Continue once `@@datadir` and `@@port` identify your new instance and `SHOW DATABASES` confirms the four target names are absent. Create the empty databases, then import each dump into the database with the same name. In the MySQL CLI, adjust the example paths to your extracted SQL directory. Stop if any command reports an error.

```sql
CREATE DATABASE auth CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE characters CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE world CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE hotfixes CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE auth;
source D:/Felsoria-Local/SQL/auth.sql
USE characters;
source D:/Felsoria-Local/SQL/characters.sql
USE world;
source D:/Felsoria-Local/SQL/world.sql
USE hotfixes;
source D:/Felsoria-Local/SQL/hotfixes.sql
```

The generated package seeds realm ID `1` at `127.0.0.1:8085` and client build `26972`. Player accounts, characters, and the dynamic-bot ownership table start empty. Existing installations apply the migrations missing from their installed revision. [Updating an existing database](sql/updates/README.md#updating-an-existing-installation)

## 3. Prepare client data

Use game data from Legion 7.3.5 build **26972**. If you have a compatible extracted data set, copy its `dbc`, `gt`, `maps`, `vmaps`, and `mmaps` directories into the new installation's client data directory. Set `DataDir` in `worldserver.conf` to that directory. The server expects localized DB2s in `dbc/<locale>` and 31 game tables in `gt`. [Game table names](src/server/game/DataStores/GameTables.cpp)

If you need to extract data, use the tools in this order.

1. Create the data directory and copy `build/src/tools/map_extractor/FelsoriaDB2Files.txt` into it. Run `mapextractor.exe -e 2 -i <client-directory> -o <data-directory> -l enUS`. Pass the game directory containing `Data` and select a locale present in that client. Check the printed `copied`, `missing`, and `failed` totals and `DB2Extraction-<locale>.csv` before using the output. For `enUS`, set `DBC.Locale = 0`.
2. For terrain and collision data, prepare loose files from the same client with their original paths. Map conversion needs `World/Maps`, `DBFilesClient/Map.db2`, and `DBFilesClient/LiquidType.db2`. Vmap conversion also needs the loose models, `GameObjectDisplayInfo.db2`, and a tool-compatible legacy `FileDataComplete.dbc`.
3. Run `mapextractor.exe -e 1 -i <unpacked-directory> -o <data-directory>`. In the data directory containing `wmo_list.txt`, run `vmap4extractor.exe -d <unpacked-directory>` and then `vmap4assembler.exe Buildings vmaps`.
4. From the same data directory, create `mmaps` and run `mmaps_generator.exe`. Supply the required `gt` files from the game data.

## 4. Configure and start a local realm

Copy `bnetserver.conf.dist` to `bnetserver.conf` and `worldserver.conf.dist` to `worldserver.conf` in the working directory for the servers. Create the configured `LogsDir` directory. Set the five database connection entries to your dedicated MySQL instance, using `host;port;username;password;database`. [Battle.net template](src/server/bnetserver/bnetserver.conf.dist) · [World template](src/server/worldserver/worldserver.conf.dist)

| Config file | Setting | Database |
| --- | --- | --- |
| `bnetserver.conf` | `LoginDatabaseInfo` | `auth` |
| `worldserver.conf` | `LoginDatabaseInfo` | `auth` |
| `worldserver.conf` | `CharacterDatabaseInfo` | `characters` |
| `worldserver.conf` | `WorldDatabaseInfo` | `world` |
| `worldserver.conf` | `HotfixDatabaseInfo` | `hotfixes` |

For a local installation, set `BindIP = "127.0.0.1"` in both files and keep `LoginREST.ExternalAddress` and `LoginREST.LocalAddress` at `127.0.0.1`. In `worldserver.conf`, keep `Game.Build.Version = 26972` and match `RealmID = 1` and `WorldServerPort = 8085` to the imported `auth.realmlist` row. Add `LegionForge.DynamicBots.Enable = 1` if you want dynamic bots.

For Battle.net and its HTTPS login form, provide a PEM certificate chain trusted by your Legion client and its matching private key. The repository ships a public TrinityCore certificate chain. Supply a compatible pair for your client and keep the private key local. A self-signed certificate requires explicit trust in the client. Relative paths resolve from the server's working directory.

```ini
TLS.CertificateFile = "bnetserver.cert.pem"
TLS.PrivateKeyFile = "bnetserver.key.pem"
```

Start `bnetserver.exe` and then `worldserver.exe` from their working directory. When `worldserver` reports it is ready, confirm the configured ports are listening on the correct processes. These are the default ports for the local setup.

| TCP port | Expected listener |
| --- | --- |
| `1119` | Battle.net login |
| `8081` | HTTPS login form |
| `8085` | World connections |
| `8086` | Instance connections |

Check the bound addresses and process IDs in PowerShell.

```powershell
Get-NetTCPConnection -State Listen |
  Where-Object { $_.LocalPort -in 1119,8081,8085,8086 } |
  Select-Object LocalAddress, LocalPort, OwningProcess
```

In the worldserver console, enter `account create player@example.invalid <password>` with your test account details. The account name must contain `@`. In a build `26972` client that reads the portal from `WTF/Config.wtf`, use `SET portal "127.0.0.1"` with the default Battle.net port. Select Felsoria, create a character, and enter the realm.

If the client immediately disconnects with `BLZ51901023`, check the client login endpoint, the Battle.net TLS logs, and whether the client trusts the loaded certificate chain. A successful HTTPS form request alone only verifies that listener. For a port error, use the server logs and the listener table to identify which service failed to start.

## Contributing

Open a pull request with the behavior you changed, how you tested it, and any SQL or configuration changes needed to use it. Put database changes in versioned SQL migration files. GitHub Actions replays the migrations against a fresh database for pull requests targeting `main`. [Pull requests](https://github.com/Felsoria/felsoria-source/pulls) · [SQL migrations](sql/updates/README.md)

For a bug report, include the source commit, client build, database snapshot, reproduction steps, and a short log excerpt. For gameplay issues, include the quest ID or instance name, difficulty, and bot class or role where relevant. [Report a bug](https://github.com/Felsoria/felsoria-source/issues)

## Credits and license

Felsoria builds on LegionPlayerBot, LegionCore, and the work of TrinityCore contributors. See the source headers for copyright and attribution notices. [GNU General Public License](COPYING)
