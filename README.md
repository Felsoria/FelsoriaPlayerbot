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

The Windows build and initial clean-install checks below used [`f382fbc`](https://github.com/Felsoria/felsoria-source/commit/f382fbc301662bf33c415fd29795c73a6515b981). Use the current configuration templates and matching SQL package for the virtual realms and PlayerBot defaults.

- Visual Studio 2022 built `bnetserver`, `worldserver`, and all four data tools in `RelWithDebInfo`. The TLS startup and missing-data diagnostics also passed. [Windows build and startup checks](https://github.com/Felsoria/felsoria-source/actions/runs/45036612628)
- The generated ZIP was imported into four empty MySQL Server 5.7.44 databases. The realm, client build, empty player data, and migration checksums matched. [SQL reimport](https://github.com/Felsoria/felsoria-source/actions/runs/45036879812)
- On Windows, `bnetserver` started with the generated `auth.sql`, exposed both loopback listeners, and issued a login ticket for a test account over HTTPS. [Battle.net runtime check](https://github.com/Felsoria/felsoria-source/actions/runs/45038339748)

A manual Windows installation at that revision also created a character and entered the Legion world with a 26972 client. Full map extraction from raw client assets still requires manual verification.

## 1. Build on Windows

The Developer V2 profile uses Visual Studio 2022 with **Desktop development with C++** (x64 v143), CMake 3.31, Boost 1.78.0, MySQL 5.7.44 x64 C client SDK, and OpenSSL 3.5.1 x64 development libraries. GitHub Actions builds and tests the same profile. [Windows checks](https://github.com/Felsoria/felsoria-source/actions/workflows/build-sql-base.yml)

Clone into a new folder. For a tagged release, check out its tag before building.

```powershell
New-Item -ItemType Directory -Path 'D:\Felsoria-Local' -Force | Out-Null
git clone https://github.com/Felsoria/felsoria-source.git D:\Felsoria-Local\source
Set-Location D:\Felsoria-Local\source
```

Install the x64 Boost libraries once. If you have the Boost source archive, run `bootstrap.bat` from the Boost directory in the VS2022 x64 developer shell, then run the command below. The static libraries with the shared MSVC runtime go to `stage/lib`.

```powershell
.\b2.exe toolset=msvc-14.3 address-model=64 link=static runtime-link=shared threading=multi variant=release --layout=versioned --with-system --with-filesystem --with-thread --with-program_options --with-iostreams --with-regex stage
```

Run this from the repository root in Windows PowerShell 5.1 or PowerShell 7. It discovers the Developer folders on `D:` and uses the default Boost library layouts, including `stage/lib`.

```powershell
.\Build-Felsoria.ps1
```

For other dependency locations, pass the full paths. `-BoostLibraryDir` is optional when the library folder is in one of the documented layouts. The MySQL root contains `include/mysql.h`, `lib/libmysql.lib`, and `lib/libmysql.dll`. The OpenSSL root contains the development headers and import libraries, with the x64 runtime DLLs in `bin`.

```powershell
.\Build-Felsoria.ps1 -BoostRoot 'D:\Dependencies\boost_1_78_0' `
  -BoostLibraryDir 'D:\Dependencies\boost_1_78_0\stage\lib' `
  -MySqlRoot 'D:\Dependencies\mysql-5.7.44-winx64' -OpenSslRoot 'D:\OpenSSL-Win64'
```

Find the two servers, four data tools, configuration templates, and matching MySQL and OpenSSL DLLs in `build/developer-v2/bin/RelWithDebInfo/`. The build uses `RelWithDebInfo` with the game scripts enabled. A separate CMake build can include the WebAPI target with `-DTOOLS=ON -DWEB_API=ON`.

## 2. Import the database package

Use a `developer-v2-<commit>` release and its generated SQL ZIP. After a change is merged into `main`, the SQL and full Windows checks must pass before that source revision and its SQL package are published together. The release includes a `.zip.sha256` file. The older `database-v2-20261121` release is the build baseline; its original clean ZIP alone is not a current installation package.

For a source install from GitHub Actions, download the `felsoria-database` artifact from the successful build for that revision and unpack the outer ZIP. The generated inner ZIP contains four SQL dumps and `database-manifest.json`. Check `source_commit` and `applied_migrations` in the manifest. For an Actions artifact, use the SHA-256 recorded by the corresponding build. [Releases](https://github.com/Felsoria/felsoria-source/releases) · [SQL builds](https://github.com/Felsoria/felsoria-source/actions/workflows/build-sql-base.yml)

Use a dedicated loopback MySQL 5.7.44 instance with a new data directory. Run this from the source checkout in PowerShell, replacing the paths, port and SHA-256 with those of your test installation and the generated inner ZIP. The command reads the source commit from the checkout and prompts for the MySQL password. [MySQL Community Server archives](https://downloads.mysql.com/archives/community/)

```powershell
.\sql\Import-FelsoriaDatabase.ps1 `
  -MySqlExecutable 'D:\Felsoria-Local\mysql\bin\mysql.exe' `
  -Port 33317 -DataDirectory 'D:\Felsoria-Local\mysql-data' `
  -SqlZip 'D:\Felsoria-Local\downloads\Felsoria-Developer-V2-SQL-<commit>.zip' `
  -Sha256 '<SHA-256 from the SQL build or release>' -SourceCommit (git rev-parse HEAD)
```

The importer checks the ZIP, source commit, server version, port and data directory. It requires all four database names to be unused and stops on the first MySQL error. If an import stops after creating schemas, use a fresh instance to retry. [Updating an existing database](sql/updates/README.md#updating-an-existing-installation)

The generated package seeds realm ID `1` at `127.0.0.1:8085` and client build `26972`. Player accounts, characters, and the dynamic-bot ownership table start empty. Existing installations apply the migrations missing from their installed revision. [Updating an existing database](sql/updates/README.md#updating-an-existing-installation)

## 3. Prepare client data

Use game data from Legion 7.3.5 build **26972**. If you have a compatible extracted data set, copy its `dbc`, `gt`, `maps`, `vmaps`, and `mmaps` directories into the new installation's client data directory. Set `DataDir` in `worldserver.conf` to that directory. The server expects localized DB2s in `dbc/<locale>` and 31 game tables in `gt`. [Game table names](src/server/game/DataStores/GameTables.cpp)

If you need to extract data, use the tools in this order.

1. Create the data directory and copy `build/developer-v2/src/tools/map_extractor/FelsoriaDB2Files.txt` into it. Run `mapextractor.exe -e 2 -i <client-directory> -o <data-directory> -l enUS`. Pass the game directory containing `Data` and select a locale present in that client. A complete extraction exits with code 0. Missing or invalid files return a nonzero exit code; inspect `DB2Extraction-<locale>.csv` and the printed `copied`, `missing`, and `failed` totals. Existing DB2 outputs are preserved, so use a new output directory for a retry. For `enUS`, set `DBC.Locale = 0`.
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

For a local installation, set `BindIP = "127.0.0.1"` in both files and keep `LoginREST.ExternalAddress` and `LoginREST.LocalAddress` at `127.0.0.1`. In `worldserver.conf`, keep `Game.Build.Version = 26972` and match `RealmID = 1` and `WorldServerPort = 8085` to the imported `auth.realmlist` row. The virtual realm aliases use the same worldserver endpoint. Automatic address synchronization covers the physical realm and the configured aliases by default. Installations that deliberately route every realm through that endpoint can set `Felsoria.RealmAddressSync.AllRealms = 1`.

The templates enable the EU Legion virtual realm list and dynamic PlayerBots. The SQL package seeds 267 playable aliases and one tournament entry for the list. The physical realm stays at ID `1`. Bots are created on demand. The default limits are 50 open-world bots and 100 for instances and battlegrounds, with a party reserve of 10. The package includes 425 unassigned Cyrillic bot names for Russian realms.

New installations also enable regional guilds and bot chat. Keep the generated `Felsoria/` directory beside `worldserver` when moving the binaries. It contains the two dialogue catalogs. The matching database package supplies 52 guild definitions and the regional dialogue seeds. At startup, look for `FELSORIA_BOT_TEXTS_V3 loaded texts=1511 languages=6` and `REGIONAL_BOTCHAT_INIT` with `ready=1`. Regional guild creation reports `REGIONAL_GUILD_INIT complete=1 guilds=52`.

Existing installations keep their `.conf` files. Apply the missing migrations and copy the virtual realm and PlayerBot settings you want to use from the new templates. Use the same MySQL instance for AUTH and characters. The AUTH database user needs access to the characters table for character-to-realm mapping. If either server reports `FELSORIA_REALM_DATA_INCOMPATIBLE`, update AUTH with the migrations for this source revision or use a matching generated ZIP on a fresh installation.

The Developer V2 gameplay profile sets `DungeonFinder.DebugJoin = 0` for normal role-complete queues and `Battleground.CastDeserter = 0`. Both `ClientCacheVersion` and `HotfixCacheVersion` are `20261110`. The matching SQL migration also raises older database cache revisions without reducing newer ones. Existing configurations with an explicit, older nonzero cache revision must be updated separately; a zero value uses the migrated database revision.

For Battle.net and its HTTPS login form, provide a PEM certificate chain trusted by your Legion client and its matching private key. The repository ships a public TrinityCore certificate chain. Supply a compatible pair for your client and keep the private key local. A self-signed certificate requires explicit trust in the client. Relative paths resolve from the server's working directory.

```ini
TLS.CertificateFile = "bnetserver.cert.pem"
TLS.PrivateKeyFile = "D:/Felsoria-Local/tls/bnetserver.key.pem"
```

Keep the private key in a local directory outside the source and build trees. Set the configuration path to that file. CMake can copy an optional public certificate from the source tree.

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

Open a pull request with the behavior you changed, how you tested it, and any SQL or configuration changes needed to use it. Put database changes in versioned SQL migration files. For pull requests targeting `main` and updates to `main`, GitHub Actions builds the matching SQL ZIP and checks the Windows build, extractor output and Battle.net startup. [Pull requests](https://github.com/Felsoria/felsoria-source/pulls) · [SQL migrations](sql/updates/README.md)

For a bug report, include the source commit, client build, database snapshot, reproduction steps, and a short log excerpt. For gameplay issues, include the quest ID or instance name, difficulty, and bot class or role where relevant. [Report a bug](https://github.com/Felsoria/felsoria-source/issues)

## Credits and license

Felsoria builds on LegionPlayerBot, LegionCore, and the work of TrinityCore contributors. See the source headers for copyright and attribution notices. [GNU General Public License](COPYING)

