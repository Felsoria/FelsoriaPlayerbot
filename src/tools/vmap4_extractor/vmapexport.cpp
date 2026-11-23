/*
 * Copyright (C) 2008-2015 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _CRT_SECURE_NO_DEPRECATE
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <vector>
#include <list>
#include <errno.h>

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sys/stat.h>
#include <direct.h>
#define mkdir _mkdir
#else
#include <sys/stat.h>
#define ERROR_PATH_NOT_FOUND ERROR_FILE_NOT_FOUND
#endif

#include <map>
#include <fstream>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

//From Extractor
#include "adtfile.h"
#include "wdtfile.h"
#include "dbcfile.h"
#include "wmo.h"
#include "Common.h"
#include "vmapexport.h"

typedef struct
{
    char name[64];
    unsigned int id;
}map_id;

map_id * map_ids;
uint16 *LiqType = 0;
uint32 map_count;
char output_path[128] = ".";
static std::string rawInputPath = ".";
bool preciseVectorData = false;

// Constants

//static const char * szWorkDirMaps = ".\\Maps";
const char* szWorkDirWmo = "./Buildings";
const char* szRawVMAPMagic = "VMAP043";

// Local testing functions

std::string GetRawInputPath(char const* fileName)
{
    std::string relative(fileName);
    std::replace(relative.begin(), relative.end(), '\\', '/');
    return (boost::filesystem::path(rawInputPath) / relative).string();
}

bool FileExists(const char* file)
{
    if (FILE* n = fopen(file, "rb"))
    {
        fclose(n);
        return true;
    }
    return false;
}

void strToLower(char* str)
{
    while (*str)
    {
        *str = tolower(*str);
        ++str;
    }
}

// copied from contrib/extractor/System.cpp
void LoadLiquidTypeDB2Data()
{
    printf("Read LiquidType.db2 file...");
    DBCFile dbc("DBFilesClient\\LiquidType.db2", "nsifffffssssssiiffffffffffffffffffiiiittbbbbbbbbbbi");
    if (!dbc.open())
    {
        printf("Fatal error: Invalid LiquidType.db2 file format!\n");
        exit(1);
    }

    size_t LiqType_count = dbc.getRecordCount();
    size_t LiqType_maxid = dbc.getRecord(LiqType_count - 1).getUInt(0);
    LiqType = new uint16[LiqType_maxid + 1];
    memset(LiqType, 0xff, (LiqType_maxid + 1) * sizeof(uint16));

    for (uint32 x = 0; x < LiqType_count; ++x)
        LiqType[dbc.getRecord(x).getUInt(0)] = dbc.getRecord(x).getUInt8(40);

    printf("Done! (%u LiqTypes loaded)\n", (unsigned int)LiqType_count);
}

bool ExtractWmo()
{
    bool success = true;

    std::ifstream wmoList("wmo_list.txt");
    if (!wmoList)
    {
        printf("\nUnable to open wmo_list.txt! Nothing extracted.\n");
        return false;
    }

    std::set<std::string> wmos;
    for (;;)
    {
        std::string str;
        std::getline(wmoList, str);
        if (str.empty())
            break;

        wmos.insert(std::move(str));
    }

    for (std::string str : wmos)
        success &= ExtractSingleWmo(str);

    if (success)
        printf("\nExtract wmo complete (No (fatal) errors)\n");

    return success;
}

bool ExtractSingleWmo(std::string& fname)
{
    // Copy files from archive

    char szLocalFile[1024];
    const char * plain_name = GetPlainName(fname.c_str());
    sprintf(szLocalFile, "%s/%s", szWorkDirWmo, plain_name);
    FixNameCase(szLocalFile, strlen(szLocalFile));

    if (FileExists(szLocalFile))
        return true;

    int p = 0;
    // Select root wmo files
    char const* rchr = strrchr(plain_name, '_');
    if (rchr != NULL)
    {
        char cpy[4];
        memcpy(cpy, rchr, 4);
        for (int i = 0; i < 4; ++i)
        {
            int m = cpy[i];
            if (isdigit(m))
                p++;
        }
    }

    if (p == 3)
        return true;

    bool file_ok = true;
    std::cout << "Extracting " << fname << std::endl;
    WMORoot froot(fname);
    if (!froot.open())
    {
        fprintf(stderr, "Could not read WMO root %s.\n", fname.c_str());
        return false;
    }
    FILE *output = fopen(szLocalFile, "wb");
    if (!output)
    {
        printf("couldn't open %s for writing!\n", szLocalFile);
        return false;
    }
    froot.ConvertToVMAPRootWmo(output);
    int Wmo_nVertices = 0;
    //printf("root has %d groups\n", froot->nGroups);
    if (froot.nGroups != 0)
    {
        for (uint32 i = 0; i < froot.nGroups; ++i)
        {
            char temp[1024];
            strncpy(temp, fname.c_str(), 1024);
            temp[fname.length() - 4] = 0;
            char groupFileName[1024];
            sprintf(groupFileName, "%s_%03u.wmo", temp, i);
            //printf("Trying to open groupfile %s\n",groupFileName);

            std::string s = groupFileName;
            WMOGroup fgroup(s);
            if (!fgroup.open())
            {
                printf("Could not open all Group file for: %s\n", plain_name);
                file_ok = false;
                break;
            }

            Wmo_nVertices += fgroup.ConvertToVMAPGroupWmo(output, &froot, preciseVectorData);
        }
    }

    file_ok = file_ok && fseek(output, 8, SEEK_SET) == 0 &&
        fwrite(&Wmo_nVertices, sizeof(int), 1, output) == 1 && !ferror(output);
    file_ok = fclose(output) == 0 && file_ok;
    if (!file_ok)
        remove(szLocalFile);
    return file_ok;
}

bool ParseMapFiles()
{
    bool success = true;
    unsigned int processedMaps = 0;
    for (unsigned int i = 0; i < map_count; ++i)
    {
        char path[512];
        snprintf(path, sizeof(path), "World/Maps/%s/%s.wdt", map_ids[i].name, map_ids[i].name);
        std::string const input = GetRawInputPath(path);
        boost::system::error_code error;
        boost::filesystem::file_status const state = boost::filesystem::status(boost::filesystem::path(input), error);
        // Some entries in Map.db2 have no terrain in this client.
        if (state.type() == boost::filesystem::file_not_found)
            continue;
        if (error || !boost::filesystem::is_regular_file(state))
        {
            fprintf(stderr, "Could not read WDT '%s': %s\n", input.c_str(),
                error ? error.message().c_str() : "not a regular file");
            success = false;
            continue;
        }

        WDTFile wdt(path, map_ids[i].name);
        if (!wdt.init(nullptr, map_ids[i].id))
        {
            fprintf(stderr, "Could not convert WDT '%s'.\n", input.c_str());
            success = false;
            continue;
        }

        ++processedMaps;
        printf("Processing Map %u\n[", map_ids[i].id);
        for (int x = 0; x < 64; ++x)
        {
            for (int y = 0; y < 64; ++y)
            {
                if (ADTFile* tile = wdt.GetMap(x, y))
                {
                    if (!tile->init(map_ids[i].id, x, y))
                    {
                        fprintf(stderr, "Could not convert object tile for %s (%d, %d).\n", map_ids[i].name, x, y);
                        success = false;
                    }
                    delete tile;
                }
            }
            printf("#");
            fflush(stdout);
        }
        printf("]\n");
    }

    if (!processedMaps)
        fprintf(stderr, "No WDT files were converted. Check the unpacked World/Maps input.\n");
    return success && processedMaps != 0;
}

bool processArgv(int argc, char ** argv, const char *versionString)
{
    bool result = true;
    preciseVectorData = false;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp("-s", argv[i]) == 0)
        {
            preciseVectorData = false;
        }
        else if (strcmp("-d", argv[i]) == 0)
        {
            if (i + 1 < argc && argv[i + 1][0] != '\0')
                rawInputPath = argv[++i];
            else
            {
                result = false;
            }
        }
        else if (strcmp("-?", argv[i]) == 0)
        {
            result = false;
        }
        else if (strcmp("-l", argv[i]) == 0)
        {
            preciseVectorData = true;
        }
        else
        {
            result = false;
            break;
        }
    }

    if (!result)
    {
        printf("Extract %s.\n", versionString);
        printf("%s [-?][-s][-l][-d <path>]\n", argv[0]);
        printf("   -s : (default) small size (data size optimization), ~500MB less vmap data.\n");
        printf("   -l : large size, ~500MB more vmap data. (might contain more details)\n");
        printf("   -d <path>: Unpacked client data directory (default: current directory).\n");
        printf("   -? : This message.\n");
    }

    return result;
}

int main(int argc, char ** argv)
{
    bool success = true;
    const char *versionString = "V4.03 2015_05";

    // Use command line arguments, when some
    if (!processArgv(argc, argv, versionString))
        return 1;

    for (char const* file : { "DBFilesClient/Map.db2", "DBFilesClient/LiquidType.db2",
        "DBFilesClient/GameObjectDisplayInfo.db2", "DBFilesClient/FileDataComplete.dbc" })
    {
        std::string const path = GetRawInputPath(file);
        if (!FileExists(path.c_str()))
        {
            fprintf(stderr, "Missing unpacked client file '%s'. Use -d to select the unpacked data directory.\n",
                path.c_str());
            return 1;
        }
    }
    if (!FileExists("wmo_list.txt"))
    {
        fprintf(stderr, "Missing wmo_list.txt. Run mapextractor first and use its output directory here.\n");
        return 1;
    }
    if (FileExists("./Buildings/dir") || FileExists("./Buildings/dir_bin"))
    {
        fprintf(stderr, "Buildings already contains converted data. Use a clean extraction directory.\n");
        return 1;
    }

    printf("Extract %s. Beginning work ....\n\n", versionString);
    //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    // Create the working directory
    if (mkdir(szWorkDirWmo
#if defined(__linux__) || defined(__APPLE__)
        , 0711
#endif
        ))
        success = (errno == EEXIST);

    // Extract models, listed in GameObjectDisplayInfo.db2
    if (success)
        success = ExtractGameobjectModels();

    LoadLiquidTypeDB2Data();

    // extract data
    if (success)
        success = ExtractWmo();

    //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    //map.dbc
    if (success)
    {
        printf("Read Map.db2 file...\n");
        DBCFile * dbc = new DBCFile("DBFilesClient\\Map.db2", "nsiifffsssttttttbbbbb");
        if (!dbc->open())
        {
            printf("Fatal error: Invalid Map.db2 file format!\n");
            exit(1);
        }

        map_count = dbc->getRecordCount();
        map_ids = new map_id[map_count];
        for (unsigned int x = 0; x < map_count; ++x)
        {
            map_ids[x].id = dbc->getRecord(x).getUInt(0);

            const char* map_name = dbc->getRecord(x).getString(1);
            size_t max_map_name_length = sizeof(map_ids[x].name);
            if (strlen(map_name) >= max_map_name_length)
            {
                delete dbc;
                delete[] map_ids;
                printf("FATAL ERROR: Map name too long.\n");
                return 1;
            }

            strncpy(map_ids[x].name, map_name, max_map_name_length);
            map_ids[x].name[max_map_name_length - 1] = '\0';
            printf("Map - %s\n", map_ids[x].name);
        }

        delete dbc;
        success = ParseMapFiles();
        delete[] map_ids;
    }

    printf("\n");
    delete[] LiqType;
    if (!success)
    {
        fprintf(stderr, "ERROR: Extract %s did not finish.\n", versionString);
        return 1;
    }

    printf("Extract %s. Work complete.\n", versionString);
    return 0;
}
