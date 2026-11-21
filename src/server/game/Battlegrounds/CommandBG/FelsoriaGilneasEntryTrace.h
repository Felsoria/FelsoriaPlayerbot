#ifndef FELSORIA_GILNEAS_ENTRY_TRACE_H
#define FELSORIA_GILNEAS_ENTRY_TRACE_H
#include "Config.h"
#include <fstream>
#include <map>
#include <mutex>
#include <string>
namespace FelsoriaGilneasEntry {
inline void Record(unsigned now,unsigned account,char const*stage,unsigned guid,unsigned ready,unsigned step,unsigned elapsed){
    struct Trace {unsigned at=0,count=0;std::string stage;};
    static std::mutex mutex;static std::map<unsigned,Trace>states;
    std::lock_guard<std::mutex>lock(mutex);auto&state=states[account];
    if(std::string(stage)=="reserved")state=Trace();
    if(state.count>=32||(state.stage==stage&&unsigned(now-state.at)<5000))return;
    ++state.count;state.at=now;state.stage=stage;
    std::string directory=sConfigMgr->GetStringDefault("LogsDir","");
    if(!directory.empty()&&directory.back()!='/'&&directory.back()!='\\')directory+='/';
    std::string filename="Felsoria-Gilneas-Entry-V2.6.log",path=directory+filename;
    std::ofstream file(path,std::ios::app|std::ios::ate);
    if(!file){path=filename;file.clear();file.open(path,std::ios::app|std::ios::ate);}
    if(!file)return;
    if(file.tellp()>std::streamoff(512*1024)){file.close();file.open(path,std::ios::trunc);}
    if(file)file<<"V2.6 ms="<<now<<" account="<<account<<" stage="<<stage<<" guid="<<guid
        <<" ready="<<ready<<" preparation_step="<<step<<" elapsed_ms="<<elapsed<<'\n';
}
} // namespace FelsoriaGilneasEntry
#endif
