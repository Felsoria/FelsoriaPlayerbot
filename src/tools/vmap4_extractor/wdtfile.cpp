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

#include "vmapexport.h"
#include "wdtfile.h"
#include "adtfile.h"
#include <cstdio>

char * wdtGetPlainName(char * FileName)
{
    char * szTemp;

    if((szTemp = strrchr(FileName, '\\')) != NULL)
        FileName = szTemp + 1;
    return FileName;
}

WDTFile::WDTFile(char* file_name, char* file_name1):WDT(file_name), gnWMO(0)
{
    filename.append(file_name1,strlen(file_name1));
}

bool WDTFile::init(char* /*map_id*/, unsigned int mapID)
{
    if (WDT.isEof())
        return false;

    char fourcc[5];
    uint32 size;

    std::string dirname = std::string(szWorkDirWmo) + "/dir_bin";
    FILE *dirfile;
    dirfile = fopen(dirname.c_str(), "ab");
    if (!dirfile)
        return false;

    bool hasMain = false;
    while (!WDT.isEof())
    {
        if (WDT.read(fourcc, 4) != 4 || WDT.read(&size, 4) != 4 || size > WDT.getSize() - WDT.getPos())
        {
            fclose(dirfile);
            return false;
        }

        flipcc(fourcc);
        fourcc[4] = 0;

        size_t nextpos = WDT.getPos() + size;

        if (!strcmp(fourcc, "MAIN"))
        {
            uint32 flags[64][64][2];
            if (size != sizeof(flags) || WDT.read(flags, sizeof(flags)) != sizeof(flags))
            {
                fclose(dirfile);
                return false;
            }

            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    _hasTerrainTile[y][x] = (flags[y][x][0] & 1) != 0;
            hasMain = true;
        }
        else if (!strcmp(fourcc, "MWMO") && size)
        {
            std::vector<char> buf(size);
            WDT.read(buf.data(), size);
            size_t offset = 0;
            while (offset < buf.size())
            {
                char* name = buf.data() + offset;
                char const* end = static_cast<char const*>(memchr(name, '\0', buf.size() - offset));
                if (!end)
                {
                    fclose(dirfile);
                    return false;
                }

                offset += end - name + 1;
                char* plainName = wdtGetPlainName(name);
                if (!strchr(plainName, '.'))
                {
                    fclose(dirfile);
                    return false;
                }
                FixNameCase(plainName, strlen(plainName));
                gWmoInstansName.push_back(plainName);
            }
        }
        else if (!strcmp(fourcc, "MODF") && size)
        {
            if (size % 64)
            {
                fclose(dirfile);
                return false;
            }

            gnWMO = static_cast<int>(size / 64);
            for (int i = 0; i < gnWMO; ++i)
            {
                uint32 id;
                if (WDT.read(&id, sizeof(id)) != sizeof(id) || id >= gWmoInstansName.size())
                {
                    fclose(dirfile);
                    return false;
                }
                WMOInstance inst(WDT, gWmoInstansName[id].c_str(), mapID, 65, 65, dirfile);
            }
        }
        WDT.seek(static_cast<int>(nextpos));
    }

    WDT.close();
    bool const complete = !ferror(dirfile) && hasMain;
    return fclose(dirfile) == 0 && complete;
}

WDTFile::~WDTFile(void)
{
    WDT.close();
}

ADTFile* WDTFile::GetMap(int x, int z)
{
    if (!(x >= 0 && z >= 0 && x < 64 && z < 64) || !_hasTerrainTile[z][x])
        return NULL;

    char name[512];

    sprintf(name, "World\\Maps\\%s\\%s_%d_%d_obj0.adt", filename.c_str(), filename.c_str(), x, z);
    return new ADTFile(name);
}
