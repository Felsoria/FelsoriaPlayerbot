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

1. Set `BOOST_ROOT` to the root of your Boost installation, with x64 libraries for MSVC available.
2. Check the OpenSSL paths in [`dep/openssl/CMakeLists.txt`](dep/openssl/CMakeLists.txt). The current snapshot points to `D:/OpenSSL-Win64`. Adjust the include and library paths to match your installation.
3. If CMake cannot find MySQL, set `MYSQL_INCLUDE_DIR` to the directory containing `mysql.h` and `MYSQL_LIBRARY` to the path of `libmysql.lib` when configuring.
4. Set up a TLS certificate and matching private key for your login endpoint. Place them in `src/server/bnetserver/` as `bnetserver.cert.pem` and `bnetserver.key.pem` for the build to copy. Keep the private key local.

Run the following commands in PowerShell from the repository root.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo --target bnetserver worldserver
```

The server executables are written to `build/bin/RelWithDebInfo/`. You can enable the extraction tool targets by adding `-DTOOLS=ON` to the configure command.

## Set up a local realm

The repository contains the server source and a custom SQL migration. To run it, supply a compatible Legion 7.3.5 database snapshot, a build 26972 client, and extracted server data from the same client build. The Felsoria development setup uses `ADB_735.10` as its database baseline.

1. Import compatible base schemas and data for `auth`, `characters`, `world`, and `hotfixes`. Configure a realm entry in `auth.realmlist`.
2. Copy [`bnetserver.conf.dist`](src/server/bnetserver/bnetserver.conf.dist) and [`worldserver.conf.dist`](src/server/worldserver/worldserver.conf.dist) into the runtime directory as `bnetserver.conf` and `worldserver.conf`. Set the database connections and local realm addresses, match `RealmID` to `auth.realmlist`, and keep `Game.Build.Version` at `26972` in both files.
3. Set `DataDir` in `worldserver.conf` to the extracted client data directory. Include DB2 tables, game tables, maps, vmaps, and mmaps. Create the directories configured under `LogsDir`.
4. To enable dynamic bots, apply the [bot ownership migration](sql/custom/2026_09_25_legionforge_dynamic_bot_owned.sql) to the `characters` database, then set `LegionForge.DynamicBots.Enable = 1` under `[worldserver]`.
5. Start MySQL, then `bnetserver.exe` and `worldserver.exe`. Once the world server is ready, connect using the Legion client configured for your realm's login endpoint.

## Contributing

Open a [GitHub issue](https://github.com/Felsoria/felsoria-source/issues) to report a bug. Include the commit or release, client build, database snapshot, and steps to reproduce. For quests, encounters, or bot behavior, include the quest ID or instance name, difficulty, bot class and role where relevant, and a short log excerpt.

To submit a fix, open a [pull request](https://github.com/Felsoria/felsoria-source/pulls) with the behavior it changes, how you tested it, and any SQL or configuration changes needed to use it.

## Credits and license

Felsoria builds on LegionPlayerBot, LegionCore, and the work of TrinityCore contributors. See [COPYING](COPYING) for the GNU General Public License text and the source headers for copyright and attribution notices.
