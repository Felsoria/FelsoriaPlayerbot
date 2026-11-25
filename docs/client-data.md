# Legion client data

Use data from Legion 7.3.5 build **26972**. An existing compatible extracted set
can be reused: put its `dbc`, `gt`, `maps`, `vmaps`, and `mmaps` directories under
the configured `DataDir`. The server expects localized DB2s in `dbc/<locale>`
and 31 game tables in `gt`.

For a new extraction, use the tools built with this source:

1. Create a new data directory. Copy
   `build/developer-v2/src/tools/map_extractor/FelsoriaDB2Files.txt` into it.
   Run `mapextractor.exe -e 2 -i <client-directory> -o <data-directory> -l enUS`.
   The client directory must contain `Data`. Use a locale present in the client;
   for `enUS`, set `DBC.Locale = 0`. A complete DB2 extraction exits with code 0.
   Check `DB2Extraction-<locale>.csv` if files are missing.
2. Terrain and collision conversion needs loose files with their original paths.
   Map conversion needs `World/Maps`, `DBFilesClient/Map.db2`, and
   `DBFilesClient/LiquidType.db2`. Vmap conversion also needs the loose models,
   `GameObjectDisplayInfo.db2`, and a tool-compatible legacy `FileDataComplete.dbc`.
3. Run `mapextractor.exe -e 1 -i <unpacked-directory> -o <data-directory>`.
   From the data directory containing `wmo_list.txt`, run
   `vmap4extractor.exe -d <unpacked-directory>`, then
   `vmap4assembler.exe Buildings vmaps`.
4. Create `mmaps` in the data directory and run `mmaps_generator.exe` there.
   Supply the matching `gt` files from the game data.

The Windows runtime test reused an existing extracted data set. Extraction of
every asset from a raw client has not been verified end to end.
