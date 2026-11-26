# FelsoriaPlayerbot

**World of Warcraft: Legion 7.3.5 — build 26972**

FelsoriaPlayerbot is a further-developed version of [NormalZero's LegionPlayerBot](https://github.com/normalzero/LegionPlayerBot). It adds Felsoria's bot improvements, gameplay fixes, regional guilds, bot chat, and virtual realms for solo and small-group play.

## Required software

- Windows 10/11 x64 and Git.
- Visual Studio 2022 with **Desktop development with C++**, MSVC v143, and a Windows SDK.
- CMake 3.31.
- Boost 1.78.0: x64 static libraries built with v143.
- MySQL Community Server 5.7.44 x64, including its C headers and libraries.
- OpenSSL 3.5.1 x64, including development headers, libraries, and runtime DLLs.
- A compatible Legion 7.3.5 client, build **26972**, and matching extracted server data.
- Optional: HeidiSQL for importing and managing the databases.

## Quick installation

### 1. Build the source

Open the [Felsoria V2 release](https://github.com/Felsoria/FelsoriaPlayerbot/releases), download the installation SQL ZIP, and use its **Matching source ZIP** link. The same release is updated after each successful build. The older SQL ZIP marked as the fixed build input is only needed by the automated build.

For a Git checkout, replace `<source-commit>` below with the full **Source** commit shown in the release notes. It is also recorded in the SQL ZIP's `database-manifest.json`. This keeps your source and database matched even if the release updates while you download.

```powershell
git clone https://github.com/Felsoria/FelsoriaPlayerbot.git
cd FelsoriaPlayerbot
git checkout <source-commit>
```

If Boost is not built yet, open the VS2022 x64 developer shell in its folder, run `bootstrap.bat`, then:

```text
b2.exe toolset=msvc-14.3 address-model=64 link=static runtime-link=shared threading=multi variant=release --layout=versioned --with-system --with-filesystem --with-thread --with-program_options --with-iostreams --with-regex stage
```

From the Felsoria source folder, run the following in PowerShell. Adjust the dependency paths to your installation.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build-Felsoria.ps1 `
  -BoostRoot 'D:\Dependencies\boost_1_78_0' `
  -MySqlRoot 'D:\Dependencies\mysql-5.7.44-winx64' `
  -OpenSslRoot 'D:\OpenSSL-Win64'
```

The output is in `build/developer-v2/bin/RelWithDebInfo/`. Keep its DLLs and `Felsoria/` folder beside the executables.

### 2. Import the database

Start a dedicated MySQL 5.7.44 instance. Extract the SQL ZIP, create four empty databases using `utf8mb4`, and import each dump into its matching database:

| Database | File |
| --- | --- |
| `auth` | `auth.sql` |
| `characters` | `characters.sql` |
| `world` | `world.sql` |
| `hotfixes` | `hotfixes.sql` |

The release package already includes its SQL updates. Use HeidiSQL or the MySQL command-line client. A guarded [PowerShell importer](sql/Import-FelsoriaDatabase.ps1) is also included.

### 3. Configure the servers

In the output folder, copy `bnetserver.conf.dist` to `bnetserver.conf` and `worldserver.conf.dist` to `worldserver.conf`.

- Set `LoginDatabaseInfo` in both files and the other three database connections in `worldserver.conf`. The format is `host;port;username;password;database`.
- Put the matching extracted `dbc`, `gt`, `maps`, `vmaps`, and `mmaps` folders in your data directory, then set `DataDir` to it. [Client data preparation](docs/client-data.md)
- Set `Game.Build.Version = 26972` in both files. Keep `RealmID = 1` and the default local addresses and ports for the first local test.
- Configure `TLS.CertificateFile` and `TLS.PrivateKeyFile` in `bnetserver.conf` with a matching certificate and private key trusted by your patched client. The private key is not included.

Default ports: MySQL `3306`, Battle.net `1119`, HTTPS login `8081`, world `8085`, instances `8086`. Use your actual MySQL port in the connection strings.

### 4. Start and play

Start `bnetserver.exe`, then `worldserver.exe`. Wait for `Project Felsoria (worldserver) ready...`.

Create an account in the **worldserver console**:

```text
account create player@example.com YOUR_PASSWORD
```

With the client closed, set this in its `WTF/Config.wtf`:

```text
SET portal "127.0.0.1"
```

Launch the patched client and log in with your new account. Bots are created on demand around players and for dungeon and battleground groups. Regional guilds are created automatically after startup.

## Credits and license

Original PlayerBot project: **NormalZero / LegionPlayerBot**. Thanks also to the LegionCore and TrinityCore contributors.

See [COPYING](COPYING) and the source headers for license and copyright notices. Submit improvements through pull requests.
