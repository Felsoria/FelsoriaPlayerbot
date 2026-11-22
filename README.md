# Felsoria Core

**World of Warcraft: Legion 7.3.5 · Build 26972 · Developer V2**

Felsoria Core is a Legion server emulator with integrated PlayerBots, built on LegionCore and LegionPlayerBot. It is designed for solo and small-group play. Bots populate the open world, join dungeon groups, and fill battleground teams.

## Features

- **Dynamic world bots** spawn according to the player's zone, level, and faction territory. Bots in a player's party are kept when the surrounding bot population changes.
- **Dungeon Finder and combat AI** cover tank, healer, and damage roles. Group AI includes tank positioning, area-effect avoidance, and logic for returning to combat after moving out of danger.
- **Battleground AI** includes objective and navigation logic for Warsong Gulch, Arathi Basin, Alterac Valley, and the Battle for Gilneas. Gilneas has custom routes for graveyard exits and water crossings.
- **Legion content fixes** include changes to Demon Hunter quest scripting and Vault of the Wardens. The source also includes a DB2 extractor with locale-specific handling.

Developer V2 is a development snapshot. Quest behavior, encounter mechanics, battleground objectives, and localization can vary with the revision and database snapshot.

## Build on Windows

The development setup uses Windows x64, Visual Studio 2022 with **Desktop development with C++** (MSVC v143), CMake 3.31, Boost 1.78, OpenSSL 3.x, and MySQL 8.4 with its x64 client headers and library.

### Prepare dependencies

1. Point `BOOST_ROOT` at Boost 1.78 and `BOOST_LIBRARYDIR` at its compiled x64 MSVC libraries. The six static libraries are `system`, `filesystem`, `thread`, `program_options`, `iostreams` and `regex`. Use the shared MSVC runtime. A Boost source archive contains the headers. To build the required libraries, run `bootstrap.bat` in the Boost directory from the VS2022 x64 developer shell, then run the following command and use the resulting `stage/lib` directory.

   ```powershell
   .\b2.exe toolset=msvc-14.3 address-model=64 link=static runtime-link=shared threading=multi variant=release --layout=versioned --with-system --with-filesystem --with-thread --with-program_options --with-iostreams --with-regex stage
   ```
2. Install the x64 OpenSSL 3 development package. Set `OPENSSL_ROOT_DIR` at configure time if CMake needs the install location.
3. If CMake cannot find MySQL, set `MYSQL_INCLUDE_DIR` to the directory containing `mysql.h` and `MYSQL_LIBRARY` to the path of `libmysql.lib`.

Run this in PowerShell from the repository root. Set the OpenSSL path to the location you installed.

```powershell
$boostRoot = 'D:\Dependencies\boost_1_78_0'
$boostLib = Join-Path $boostRoot 'stage\lib'
$opensslRoot = 'D:\OpenSSL-Win64'
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DBOOST_ROOT=$boostRoot" "-DBOOST_LIBRARYDIR=$boostLib" "-DOPENSSL_ROOT_DIR=$opensslRoot" -DTOOLS=ON
cmake --build build --config RelWithDebInfo --target bnetserver worldserver mapextractor vmap4extractor vmap4assembler mmaps_generator
```

The executables are written to `build/bin/RelWithDebInfo/`. For dynamically linked dependencies, make the matching x64 OpenSSL and MySQL runtime DLLs available next to the executables or on `PATH`.

### Prepare the client data

Use data from Legion 7.3.5 build **26972**. If you already have a compatible extracted data set, copy its `dbc`, `gt`, `maps`, `vmaps` and `mmaps` directories to the new realm's data directory.

1. To extract DB2 directly from the game installation, copy `build/src/tools/map_extractor/FelsoriaDB2Files.txt` into your data directory and run `mapextractor.exe -e 2 -i <client-directory> -o <data-directory> -l enUS`. Use a locale present in your client and set its corresponding numeric `DBC.Locale` in `worldserver.conf`.
2. The map and vmap converters require game files already unpacked from the client, with their original paths preserved. Prepare `World/Maps`, the referenced WMO and model files, and the tool-compatible loose tables under `DBFilesClient`. Map conversion reads `Map.db2` and `LiquidType.db2`. Vmap conversion also reads `GameObjectDisplayInfo.db2` and `FileDataComplete.dbc`. For an installation with only CASC archives, use a compatible data set or unpack the files before conversion.
3. Run `mapextractor.exe -e 1 -i <unpacked-directory> -o <data-directory>`. It writes the terrain tiles to `maps` and writes `wmo_list.txt` in the data directory. From the data directory, run `vmap4extractor.exe -d <unpacked-directory>` to produce `Buildings`, then run `vmap4assembler.exe Buildings vmaps`.
4. Create the `mmaps` subdirectory and run `mmaps_generator.exe` with the data directory as its working directory. Supply the matching `gt` game tables from the same client build. The [game table loader](src/server/game/DataStores/GameTables.cpp) lists the required files.
5. Set `DataDir` in `worldserver.conf` to the directory containing `dbc/<locale>`, `gt`, `maps`, `vmaps` and `mmaps`.

## Set up a local realm

1. Download the latest generated SQL ZIP from [Releases](https://github.com/Felsoria/felsoria-source/releases), or the `felsoria-database` artifact from a [successful SQL build](https://github.com/Felsoria/felsoria-source/actions/workflows/build-sql-base.yml). The [original clean ZIP](https://github.com/Felsoria/felsoria-source/releases/tag/database-v2-20261121) is also available. Import `auth.sql`, `characters.sql`, `world.sql` and `hotfixes.sql` into four empty databases on a dedicated local MySQL instance. The snapshot was built and tested with MySQL Server 5.7.44. The MySQL client development library used for compilation is a separate dependency.
2. The generated ZIP seeds local realm ID `1` on `127.0.0.1:8085` and client build `26972`. For the original clean ZIP, apply the [realm and client-build migration](sql/updates/20261122_001_auth_local_realm_and_build.sql) to the empty install's `auth` database after import. The base includes the dynamic bot ownership table. Enable dynamic bots by setting `LegionForge.DynamicBots.Enable = 1` in `worldserver.conf`.
3. Copy [`bnetserver.conf.dist`](src/server/bnetserver/bnetserver.conf.dist) and [`worldserver.conf.dist`](src/server/worldserver/worldserver.conf.dist) to your runtime directory as `bnetserver.conf` and `worldserver.conf`. Set the database connections to the new MySQL instance, set `BindIP = "127.0.0.1"` in both files for local play, keep `Game.Build.Version = 26972`, and match `RealmID` and `WorldServerPort` to `auth.realmlist`. Create each configured log directory.
4. Configure the certificate chain trusted by the Legion client and its matching private key in `bnetserver.conf`. The server uses the same pair for the Battle.net and REST listeners. Keep the private key in local storage.

   ```ini
   TLS.CertificateFile = "bnetserver.cert.pem"
   TLS.PrivateKeyFile = "bnetserver.key.pem"
   ```

   The defaults resolve from the server's working directory. For a client that already connects to a working local installation, use that installation's matching certificate and key. A newly generated self-signed certificate requires explicit trust in the client. If the REST form responds over HTTPS but the game disconnects with `BLZ51901023`, check the client's login endpoint and its trust of the chain loaded by `bnetserver`.

5. Run `bnetserver.exe` and `worldserver.exe` with the runtime directory as their working directory. After world initialization, enter `account create player@example.invalid <password>` in the worldserver console, substituting your own password. Connect your 26972 client to the local login endpoint and verify that you can create a character and enter the realm.

## Contributing

Open a [GitHub issue](https://github.com/Felsoria/felsoria-source/issues) to report a bug. Include the commit or release, client build, database snapshot, and steps to reproduce. For quests, encounters, or bot behavior, include the quest ID or instance name, difficulty, bot class and role where relevant, and a short log excerpt.

To submit a fix, open a [pull request](https://github.com/Felsoria/felsoria-source/pulls) with the behavior it changes, how you tested it, and any SQL or configuration changes needed to use it.

Add database changes as [versioned SQL migrations](sql/updates/README.md). GitHub Actions replays the migrations against a fresh database for each pull request targeting `main` and produces a fresh install ZIP.

## Credits and license

Felsoria builds on LegionPlayerBot, LegionCore, and the work of TrinityCore contributors. See [COPYING](COPYING) for the GNU General Public License text and the source headers for copyright and attribution notices.
