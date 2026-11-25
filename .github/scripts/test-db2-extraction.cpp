// Exercise the production extractor against an in-memory CASC storage.
// Declarations come from the bundled CascLib headers used by the real build.
#include "../../src/tools/map_extractor/FelsoriaDB2ExtractV26.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{
struct Entry
{
    std::string name;
    DWORD mask;
    bool enumerated = true, readable = true, valid = true;
};
struct OpenFile { std::vector<unsigned char> bytes; std::size_t offset = 0; };
std::vector<Entry> entries;
std::size_t searchIndex;
DWORD storageBuild = 26972;

void Check(bool value, char const* message)
{
    if (!value) throw std::runtime_error(message);
}
void Found(CASC_FIND_DATA* data)
{
    std::memset(data, 0, sizeof(*data));
    std::string name = "DBFilesClient\\" + entries[searchIndex].name;
    std::strcpy(data->szFileName, name.c_str());
    data->dwLocaleFlags = entries[searchIndex].mask;
}
std::string Read(std::string const& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
int Extract(std::string const& output, std::string locale = "enUS")
{
    FelsoriaDB2V26::MakeDirectory(output);
    std::ofstream list(output + "/FelsoriaDB2Files.txt");
    list << "DBFilesClient\\One.db2\nDBFilesClient\\Two.db2\n";
    list.close();
    std::vector<std::string> args = {"mapextractor", "-e", "2", "-i", output, "-o", output, "-l", locale};
    std::vector<char*> argv;
    for (std::string& arg : args) argv.push_back(&arg[0]);
    return FelsoriaDB2V26::Run(int(argv.size()), argv.data());
}
void Reset(DWORD mask = CASC_LOCALE_ENUS)
{
    entries = {{"One.db2", mask}, {"Two.db2", mask}};
    storageBuild = 26972;
}
}

extern "C"
{
bool WINAPI CascOpenStorage(TCHAR const*, DWORD, HANDLE* storage) { *storage = &entries; return true; }
bool WINAPI CascCloseStorage(HANDLE) { return true; }
bool WINAPI CascGetStorageInfo(HANDLE, CASC_STORAGE_INFO_CLASS type, void* out, size_t, size_t* needed)
{
    if (type != CascStorageGameBuild) return false;
    *static_cast<DWORD*>(out) = storageBuild; *needed = sizeof(DWORD); return true;
}
HANDLE WINAPI CascFindFirstFile(HANDLE, char const*, CASC_FIND_DATA* data, TCHAR const*)
{
    searchIndex = 0;
    while (searchIndex < entries.size() && !entries[searchIndex].enumerated) ++searchIndex;
    if (searchIndex == entries.size()) return INVALID_HANDLE_VALUE;
    Found(data); return &searchIndex;
}
bool WINAPI CascFindNextFile(HANDLE, CASC_FIND_DATA* data)
{
    while (++searchIndex < entries.size() && !entries[searchIndex].enumerated) {}
    if (searchIndex == entries.size()) return false;
    Found(data); return true;
}
bool WINAPI CascFindClose(HANDLE) { return true; }
bool WINAPI CascOpenFile(HANDLE, char const* name, DWORD, DWORD, HANDLE* handle)
{
    for (Entry const& entry : entries)
        if ("DBFilesClient\\" + entry.name == name && entry.readable)
        {
            OpenFile* file = new OpenFile;
            file->bytes.resize(84, 0);
            std::memcpy(file->bytes.data(), entry.valid ? "WDC1" : "BAD!", 4);
            *handle = file; return true;
        }
    return false;
}
DWORD WINAPI CascGetFileSize(HANDLE handle, DWORD* high)
{
    *high = 0; return DWORD(static_cast<OpenFile*>(handle)->bytes.size());
}
bool WINAPI CascReadFile(HANDLE handle, void* out, DWORD wanted, DWORD* read)
{
    OpenFile& file = *static_cast<OpenFile*>(handle);
    *read = DWORD(std::min<std::size_t>(wanted, file.bytes.size() - file.offset));
    std::memcpy(out, file.bytes.data() + file.offset, *read); file.offset += *read; return true;
}
bool WINAPI CascCloseFile(HANDLE handle) { delete static_cast<OpenFile*>(handle); return true; }
#ifndef PLATFORM_WINDOWS
int GetLastError() { return 2; }
void SetLastError(int) {}
#endif
}
ULONGLONG CalcFileNameHash(char const*) { return 0; }
int FelsoriaCASCInspectEncoding(HANDLE, char const*, DWORD*, DWORD*) { return 0; }

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "Pass an empty fixture directory.");
        std::string root = argv[1];
        Check(FelsoriaDB2V26::MakeDirectory(root), "Cannot create fixture directory.");
        Reset(); Check(Extract(root + "/complete") == 0, "Complete enUS extraction failed.");
        Check(Read(root + "/complete/dbc/enUS/One.db2").size() == 84, "Copied DB2 differs.");
        Reset(); entries[1].enumerated = false;
        Check(Extract(root + "/missing") == 6, "Missing enumeration reported success.");
        Check(Read(root + "/missing/DB2Extraction-enUS.csv").find("NotEnumeratedByCasc") != std::string::npos,
            "Missing-file diagnostic lost.");
        Reset(); entries[1].readable = false;
        Check(Extract(root + "/unreadable") == 6, "Missing local data reported success.");
        Check(Read(root + "/unreadable/DB2Extraction-enUS.csv").find("MissingLocalData") != std::string::npos,
            "Unreadable-file diagnostic lost.");
        Reset(); entries[1].valid = false;
        Check(Extract(root + "/invalid") == 6, "Invalid DB2 reported success.");
        Check(Read(root + "/invalid/dbc/enUS/Two.db2").empty(), "Invalid DB2 was installed.");
        Reset(); entries[0].readable = entries[1].readable = false;
        Check(Extract(root + "/none") == 5, "No-copy exit code changed.");
        Reset(CASC_LOCALE_ENUS | CASC_LOCALE_FRFR);
        Check(Extract(root + "/fallback", "frFR") == 7, "Fallback-only locale was certified.");
        Reset(CASC_LOCALE_FRFR);
        Check(Extract(root + "/localized", "frFR") == 0, "Complete localized extraction failed.");
        Reset(); storageBuild = 12340;
        Check(Extract(root + "/wrong-build") == 4, "Wrong client build was accepted.");
        Reset();
        std::string saved = Read(root + "/complete/dbc/enUS/One.db2");
        Check(Extract(root + "/complete") == 5, "Existing outputs should not be overwritten.");
        Check(Read(root + "/complete/dbc/enUS/One.db2") == saved, "An existing DB2 changed.");
        Check(Extract(root + "/bad-locale", "invalid") == 2, "Invalid locale was accepted.");
        std::cout << "10 production DB2 extraction scenarios passed.\n";
    }
    catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
