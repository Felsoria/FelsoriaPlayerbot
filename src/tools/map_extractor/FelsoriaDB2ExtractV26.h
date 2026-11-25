/*
 * Project Felsoria: DB2-only CASC extraction for Legion 7.3.5 build 26972.
 * GPL-2.0-or-later, like the map extractor that includes this file.
 * Uses the bundled CascLib 1.x API; each storage is opened with ONE CASC locale.
 */
#ifndef FELSORIA_DB2_EXTRACT_V26_H
#define FELSORIA_DB2_EXTRACT_V26_H
#include "CascLib.h"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

// Existing CascLib internal helper, declared in CascLib/src/common/Common.h.
// Using the same normalization/hash function avoids guessing file name hashes.
ULONGLONG CalcFileNameHash(char const* fileName);
int FelsoriaCASCInspectEncoding(HANDLE storage,char const* contentKey,DWORD* keyCount,DWORD* localCount);

namespace FelsoriaDB2V26
{
struct Locale { char const* name; DWORD mask; };
static Locale const Locales[] = {
    {"enUS",CASC_LOCALE_ENUS},{"koKR",CASC_LOCALE_KOKR},
    {"frFR",CASC_LOCALE_FRFR},{"deDE",CASC_LOCALE_DEDE},
    {"zhCN",CASC_LOCALE_ZHCN},{"zhTW",CASC_LOCALE_ZHTW},
    {"esES",CASC_LOCALE_ESES},{"esMX",CASC_LOCALE_ESMX},
    {"ruRU",CASC_LOCALE_RURU},{"ptBR",CASC_LOCALE_PTBR},
    {"itIT",CASC_LOCALE_ITIT}
};

inline bool IsDB2Only(int argc,char** argv)
{
    for(int i=1;i+1<argc;++i)
        if(!std::strcmp(argv[i],"-e") && !std::strcmp(argv[i+1],"2"))return true;
    return false;
}
inline std::string Lower(std::string s)
{
    for(char& c:s)if(c>='A' && c<='Z')c=char(c-'A'+'a');
    return s;
}
inline std::string Join(std::string const& a,std::string const& b)
{
    return a+(a.empty() || a.back()=='/' || a.back()=='\\'?"":"/")+b;
}
inline bool Directory(std::string const& p)
{
#ifdef _WIN32
    struct _stat st;
    return _stat(p.c_str(),&st)==0 && (st.st_mode&_S_IFDIR)!=0;
#else
    struct stat st;
    return stat(p.c_str(),&st)==0 && S_ISDIR(st.st_mode);
#endif
}
inline bool MakeDirectory(std::string const& p)
{
    if(Directory(p))return true;
#ifdef _WIN32
    return _mkdir(p.c_str())==0;
#else
    return mkdir(p.c_str(),0755)==0;
#endif
}
inline std::string Quote(std::string const& s)
{
    std::string q="\"";
    for(char c:s){if(c=='"')q+='"';q+=c;}
    return q+'"';
}
inline std::uint32_t U32(unsigned char const* p)
{
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|
           (std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
struct Storage
{
    HANDLE value=nullptr;
    ~Storage(){if(value)CascCloseStorage(value);}
};
struct File
{
    HANDLE value=nullptr;
    ~File(){if(value)CascCloseFile(value);}
};
inline bool OpenStorage(std::string const& path,DWORD mask,HANDLE* result)
{
#if defined(_WIN32) && defined(UNICODE)
    int n=MultiByteToWideChar(CP_ACP,0,path.c_str(),-1,nullptr,0);
    if(!n)return false;
    std::vector<wchar_t> wide(static_cast<std::size_t>(n));
    if(!MultiByteToWideChar(CP_ACP,0,path.c_str(),-1,wide.data(),n))return false;
    return CascOpenStorage(wide.data(),mask,result);
#else
    return CascOpenStorage(path.c_str(),mask,result);
#endif
}
inline HANDLE FindFiles(HANDLE storage,CASC_FIND_DATA* found,std::string const& list)
{
#if defined(_WIN32) && defined(UNICODE)
    int n=MultiByteToWideChar(CP_ACP,0,list.c_str(),-1,nullptr,0);
    if(!n)return INVALID_HANDLE_VALUE;
    std::vector<wchar_t> wide(static_cast<std::size_t>(n));
    if(!MultiByteToWideChar(CP_ACP,0,list.c_str(),-1,wide.data(),n))return INVALID_HANDLE_VALUE;
    return CascFindFirstFile(storage,"DBFilesClient\\*.db2",found,wide.data());
#else
    return CascFindFirstFile(storage,"DBFilesClient\\*.db2",found,list.c_str());
#endif
}
inline bool SafeFileName(std::string const& name)
{
    if(name.size()<5 || Lower(name.substr(name.size()-4))!=".db2")return false;
    for(char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                        (c>='0'&&c<='9')||c=='_'||c=='.'))return false;
    return name.find("..") == std::string::npos;
}

inline bool CopyDB2File(HANDLE source,std::string const& target,DWORD size,
                     std::uint32_t& headerLocale,std::string& failure)
{
    unsigned char header[40];DWORD got=0;
    if(size<sizeof(header) || !CascReadFile(source,header,sizeof(header),&got) || got!=sizeof(header))
    {failure="InvalidOrUnreadableHeader";return false;}
    DWORD minimum=0;
    if(!std::memcmp(header,"WDB5",4))minimum=48;
    else if(!std::memcmp(header,"WDB6",4))minimum=56;
    else if(!std::memcmp(header,"WDC1",4))minimum=84;
    if(!minimum || size<minimum){failure="UnsupportedDB2Header";return false;}
    headerLocale=U32(header+36); // Recorded verbatim: not a core LocaleConstant index.
    std::ifstream existing(target.c_str(),std::ios::binary);
    if(existing.good()){failure="OutputAlreadyExists";return false;}
    std::string part=target+".part";
    FILE* out=std::fopen(part.c_str(),"wb");
    if(!out){failure="CannotCreateOutput";return false;}
    bool ok=std::fwrite(header,1,sizeof(header),out)==sizeof(header);
    DWORD left=size-static_cast<DWORD>(sizeof(header));
    unsigned char buffer[65536];
    while(ok && left)
    {
        DWORD want=left<sizeof(buffer)?left:static_cast<DWORD>(sizeof(buffer));got=0;
        if(!CascReadFile(source,buffer,want,&got) || !got || got>want)
        {failure="CascReadFailed";ok=false;break;}
        if(std::fwrite(buffer,1,got,out)!=got){failure="OutputWriteFailed";ok=false;break;}
        left-=got;
    }
    if(std::fclose(out)!=0){failure="OutputCloseFailed";ok=false;}
    if(!ok){if(failure.empty())failure="OutputWriteFailed";std::remove(part.c_str());return false;}
    if(std::rename(part.c_str(),target.c_str())!=0)
    {failure="OutputRenameFailed";std::remove(part.c_str());return false;}
    return true;
}

inline std::uint64_t U64(unsigned char const* p)
{
    return std::uint64_t(U32(p))|(std::uint64_t(U32(p+4))<<32);
}
inline std::string Hex(unsigned char const* p,std::size_t n)
{
    char const* digits="0123456789abcdef";std::string s;s.reserve(n*2);
    for(std::size_t i=0;i<n;++i){s+=digits[p[i]>>4];s+=digits[p[i]&15];}
    return s;
}
inline int AuditRawRoot(std::string const& input,std::string const& output,DWORD mask,
                        std::map<std::string,std::string> const& files)
{
    Storage storage;
    if(!OpenStorage(Join(input,"Data"),mask,&storage.value))
    {std::printf("ROOT audit: storage open failed (%u).\n",unsigned(GetLastError()));return 4;}
    DWORD build=0;std::size_t needed=0;
    if(!CascGetStorageInfo(storage.value,CascStorageGameBuild,&build,sizeof(build),&needed) || build!=26972)
    {std::printf("ROOT audit: expected build 26972, received %u.\n",unsigned(build));return 4;}
    File root;
    if(!CascOpenFile(storage.value,"ROOT",0,0,&root.value))
    {std::printf("ROOT audit: cannot open unfiltered ROOT alias (%u).\n",unsigned(GetLastError()));return 4;}
    DWORD high=0;DWORD size=CascGetFileSize(root.value,&high);
    if(size==CASC_INVALID_SIZE || high || size<12 || size>512u*1024u*1024u)
    {std::printf("ROOT audit: unexpected ROOT size.\n");return 4;}
    std::vector<unsigned char> raw(size);DWORD pos=0;
    while(pos<size)
    {
        DWORD want=size-pos;if(want>65536)want=65536;DWORD got=0;
        if(!CascReadFile(root.value,raw.data()+pos,want,&got) || !got || got>want)
        {std::printf("ROOT audit: read failed at offset %u (%u).\n",unsigned(pos),unsigned(GetLastError()));return 4;}
        pos+=got;
    }
    std::map<std::uint64_t,std::string> hashes;
    for(auto const& file:files)
    {
        std::string full="DBFilesClient\\"+file.second;
        if(!hashes.emplace(::CalcFileNameHash(full.c_str()),file.second).second)
        {std::printf("ROOT audit: duplicate filename hash in requested list.\n");return 4;}
    }
    std::ofstream blocks(Join(output,"RootBlocks.csv").c_str(),std::ios::binary);
    std::ofstream matches(Join(output,"RootDB2Entries.csv").c_str(),std::ios::binary);
    if(!blocks || !matches){std::printf("ROOT audit: cannot create reports.\n");return 3;}
    blocks<<"Block,Offset,Files,LocaleMask,ContentFlags,AudioBit,LegacySkip100,LegacySkip80\n";
    matches<<"File,Block,FileDataId,LocaleMask,ContentFlags,AudioBit,LegacySkip100,LegacySkip80,EncodingKey,HeaderProbe,DB2Magic,HeaderLocale,ProbeError,EncodingLookup,EncodingKeyCount,LocalIndexKeyCount\n";
    std::set<std::string> seen;
    std::set<std::string> localeFiles[sizeof(Locales)/sizeof(Locales[0])];
    std::set<std::string> legacyFiles[sizeof(Locales)/sizeof(Locales[0])];
    std::set<std::string> specificFiles[sizeof(Locales)/sizeof(Locales[0])];
    std::set<std::string> zeroMaskFiles;
    std::size_t offset=0;unsigned block=0;std::uint64_t total=0,matched=0;std::size_t padding=0;
    while(offset<raw.size())
    {
        if(raw.size()-offset<12)
        {
            if(std::all_of(raw.begin()+offset,raw.end(),[](unsigned char c){return c==0;}))
            {padding=raw.size()-offset;break;}
            std::printf("ROOT audit: truncated block header at %u.\n",unsigned(offset));return 9;
        }
        unsigned char const* header=raw.data()+offset;
        DWORD count=U32(header),flags=U32(header+4),locales=U32(header+8);
        std::uint64_t span=12+std::uint64_t(count)*28;
        if(span>raw.size()-offset)
        {std::printf("ROOT audit: unsupported/corrupt legacy ROOT block %u at offset %u.\n",block,unsigned(offset));return 9;}
        bool skip100=(flags&0x100)!=0,skip80=(flags&0x80)!=0;
        blocks<<block<<','<<offset<<','<<count<<','<<locales<<','<<flags<<','<<(flags>>31)<<','<<skip100<<','<<skip80<<'\n';
        unsigned char const* deltas=header+12;
        unsigned char const* entries=deltas+std::size_t(count)*4;
        std::uint64_t nextId=0;
        for(DWORD i=0;i<count;++i)
        {
            std::uint64_t id=nextId+U32(deltas+std::size_t(i)*4);
            if(id>0xFFFFFFFFull){std::printf("ROOT audit: file ID overflow.\n");return 9;}
            nextId=id+1;
            unsigned char const* entry=entries+std::size_t(i)*24;
            auto name=hashes.find(U64(entry+16));
            if(name==hashes.end())continue;
            ++matched;seen.insert(name->second);
            // Probe the encoding key directly, bypassing the root name/locale
            // filters. Read only the header; never publish or rewrite this data.
            std::string key=Hex(entry,16),probeStatus="OpenFailed",magic;
            DWORD encodingKeys=0,localKeys=0;
            int lookup=::FelsoriaCASCInspectEncoding(storage.value,key.c_str(),&encodingKeys,&localKeys);
            char const* lookupName=lookup==0?"ContentKeyMissing":(lookup==1?"LocalIndexMissing":(lookup==2?"LocalIndexPresent":"InvalidQuery"));
            DWORD probeError=0;std::uint32_t headerLocale=0;
            File probe;
            if(CascOpenFile(storage.value,key.c_str(),0,CASC_OPEN_BY_ENCODING_KEY,&probe.value))
            {
                unsigned char fileHeader[40];DWORD got=0;
                if(CascReadFile(probe.value,fileHeader,sizeof(fileHeader),&got) && got==sizeof(fileHeader))
                {
                    probeStatus="HeaderRead";headerLocale=U32(fileHeader+36);
                    if(!std::memcmp(fileHeader,"WDB5",4)||!std::memcmp(fileHeader,"WDB6",4)||!std::memcmp(fileHeader,"WDC1",4))
                        magic.assign(reinterpret_cast<char const*>(fileHeader),4);
                    else probeStatus="UnsupportedHeader";
                }
                else {probeStatus="ReadFailed";probeError=GetLastError();}
            }
            else probeError=GetLastError();
            matches<<Quote(name->second)<<','<<block<<','<<id<<','<<locales<<','<<flags<<','<<(flags>>31)<<','<<skip100<<','<<skip80<<','<<Quote(key)
                   <<','<<Quote(probeStatus)<<','<<Quote(magic)<<','<<headerLocale<<','<<probeError
                   <<','<<Quote(lookupName)<<','<<encodingKeys<<','<<localKeys<<'\n';
            if(!locales)zeroMaskFiles.insert(name->second);
            for(std::size_t l=0;l<sizeof(Locales)/sizeof(Locales[0]);++l)
            {
                if(!(locales&Locales[l].mask))continue;
                localeFiles[l].insert(name->second);
                if(!skip100 && !skip80)legacyFiles[l].insert(name->second);
                if(!(locales&CASC_LOCALE_ENUS))specificFiles[l].insert(name->second);
            }
        }
        total+=count;offset+=static_cast<std::size_t>(span);++block;
    }
    blocks.close();matches.close();
    if(!blocks || !matches){std::printf("ROOT audit: report write failed.\n");return 3;}
    if(seen.empty()){std::printf("ROOT audit: no known server filename hashes matched; the index format is not certified.\n");return 9;}
    std::ofstream summary(Join(output,"RootLocaleSummary.csv").c_str(),std::ios::binary);
    summary<<"Locale,CascMask,Requested,RawRootFiles,LegacyVisibleFiles,RawSpecificFiles,ZeroMaskCommonFiles,Build\n";
    for(std::size_t l=0;l<sizeof(Locales)/sizeof(Locales[0]);++l)
        summary<<Locales[l].name<<','<<Locales[l].mask<<','<<files.size()<<','<<localeFiles[l].size()<<','<<legacyFiles[l].size()<<','<<specificFiles[l].size()<<','<<zeroMaskFiles.size()<<','<<build<<'\n';
    summary.close();if(!summary)return 3;
    std::ofstream info(Join(output,"RootAuditInfo.txt").c_str(),std::ios::binary);
    info<<"Version=2.8\nBuild="<<build<<"\nRootBytes="<<size<<"\nBlocks="<<block<<"\nTotalEntries="<<total
        <<"\nRequestedServerFiles="<<files.size()<<"\nMatchedUniqueFiles="<<seen.size()<<"\nMatchedEntries="<<matched
        <<"\nTrailingZeroBytes="<<padding<<"\nRawRootParsed=True\nClientFilesChanged=False\nServerDB2FilesChanged=False\n";
    info.close();if(!info)return 3;
    std::printf("ROOT audit complete: %u blocks, %llu entries, %u/%u server filenames matched.\n",
                block,static_cast<unsigned long long>(total),unsigned(seen.size()),unsigned(files.size()));
    return 0;
}

inline int Run(int argc,char** argv)
{
    std::string input=".",output=".",locale;
    bool auditRoot=false;
    for(int i=1;i<argc;++i)
    {
        std::string key=argv[i];
        if(i+1>=argc){std::printf("DB2 extraction: missing option value.\n");return 2;}
        std::string value=argv[++i];
        if(key=="-i")input=value;
        else if(key=="-o")output=value;
        else if(key=="-l")locale=value;
        else if(key=="-a"){if(value!="root")return 2;auditRoot=true;}
        else if(key=="-e"){if(value!="2")return 2;}
        else if(key!="-f"){std::printf("DB2 extraction: unsupported option %s\n",key.c_str());return 2;}
    }
    DWORD mask=0;
    for(Locale const& l:Locales)if(locale==l.name)mask=l.mask;
    if(!mask){std::printf("DB2 extraction requires a supported explicit -l locale.\n");return 2;}
    std::string listPath=Join(output,"FelsoriaDB2Files.txt");
    std::ifstream list(listPath.c_str());
    if(!list){std::printf("Missing server DB2 list: %s\n",listPath.c_str());return 2;}
    std::map<std::string,std::string> files;
    std::string line;
    while(std::getline(list,line))
    {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty())continue;
        std::string name=line.substr(line.find_last_of("/\\")==std::string::npos?0:line.find_last_of("/\\")+1);
        if(line!="DBFilesClient\\"+name || !SafeFileName(name))
        {std::printf("Invalid DB2 list entry.\n");return 2;}
        if(!files.emplace(Lower(name),name).second)
        {std::printf("Duplicate DB2 list entry.\n");return 2;}
    }
    if(files.empty() || list.bad()){std::printf("Empty/unreadable server DB2 list.\n");return 2;}
    if(auditRoot)return AuditRawRoot(input,output,mask,files);
    std::ofstream report(Join(output,"DB2Extraction-"+locale+".csv").c_str(),std::ios::binary);
    if(!report){std::printf("Cannot create extraction report.\n");return 3;}
    report<<"File,Locale,CascMask,RootLocaleMask,HeaderLocale,Build,Status,Error\n";
    Storage storage;
    std::string storagePath=Join(input,"Data");
    if(!OpenStorage(storagePath,mask,&storage.value))
    {std::printf("CASC open failed: locale %s, error %u, path %s\n",locale.c_str(),unsigned(GetLastError()),storagePath.c_str());return 4;}
    DWORD build=0;std::size_t needed=0;
    if(!CascGetStorageInfo(storage.value,CascStorageGameBuild,&build,sizeof(build),&needed) || build!=26972)
    {std::printf("CASC build mismatch: expected 26972, received %u. No DB2 files extracted.\n",unsigned(build));return 4;}
    std::printf("Felsoria DB2 extractor V2.8: locale %s, CASC mask %u, build %u, requested %u files.\n",
                locale.c_str(),unsigned(mask),unsigned(build),unsigned(files.size()));
    std::fflush(stdout);
    std::map<std::string,DWORD> roots;
    CASC_FIND_DATA found={};HANDLE search=FindFiles(storage.value,&found,listPath);
    if(search && search!=INVALID_HANDLE_VALUE)
    {
        do {
            std::string name=found.szFileName;
            std::size_t separator=name.find_last_of("/\\");
            if(separator!=std::string::npos)name=name.substr(separator+1);
            if(SafeFileName(name))roots[Lower(name)]=found.dwLocaleFlags;
        } while(CascFindNextFile(search,&found));
        CascFindClose(search);
    }
    else std::printf("CASC file enumeration returned no files for %s (error %u).\n",locale.c_str(),unsigned(GetLastError()));
    if(!MakeDirectory(Join(output,"dbc")) || !MakeDirectory(Join(Join(output,"dbc"),locale)))
    {std::printf("Cannot create output locale directory.\n");return 3;}
    unsigned copied=0,localized=0,missing=0,failed=0;
    for(auto const& entry:files)
    {
        std::string const& name=entry.second;auto root=roots.find(entry.first);
        DWORD rootMask=root==roots.end()?0:root->second;
        std::string status,error;std::uint32_t headerLocale=0;
        if(root==roots.end() || !(rootMask&mask)){status="NotEnumeratedByCasc";++missing;}
        else
        {
            File file;std::string archive="DBFilesClient\\"+name;
            if(!CascOpenFile(storage.value,archive.c_str(),mask,0,&file.value))
            {status="MissingLocalData";error=std::to_string(unsigned(GetLastError()));++missing;}
            else
            {
                DWORD high=0;DWORD size=CascGetFileSize(file.value,&high);
                if(size==CASC_INVALID_SIZE || high || size>1024u*1024u*1024u)
                {status="InvalidFileSize";++failed;}
                else if(!CopyDB2File(file.value,Join(Join(Join(output,"dbc"),locale),name),size,headerLocale,error))
                {status="ReadOrWriteFailed";++failed;}
                else
                {
                    status="Extracted";++copied;
                    if(locale=="enUS" || !(rootMask&CASC_LOCALE_ENUS))++localized;
                    if(copied==1 || copied%100==0){std::printf("DB2 %s: %u files extracted.\n",locale.c_str(),copied);std::fflush(stdout);}
                }
            }
        }
        report<<Quote(name)<<','<<Quote(locale)<<','<<mask<<','<<rootMask<<','<<headerLocale<<','<<build<<','<<Quote(status)<<','<<Quote(error)<<'\n';
    }
    report.close();
    if(!report){std::printf("Extraction report write failed.\n");return 3;}
    std::printf("DB2 %s summary: copied=%u locale-specific=%u missing=%u failed=%u requested=%u\n",
                locale.c_str(),copied,localized,missing,failed,unsigned(files.size()));
    if(!copied)return 5;
    if(missing || failed)
    {
        std::printf("DB2 extraction incomplete: %u missing, %u failed. See the per-file CSV report.\n",missing,failed);
        return 6;
    }
    if(!localized){std::printf("No locale-specific files were available. This locale is not certified.\n");return 7;}
    return 0;
}
}
#endif

