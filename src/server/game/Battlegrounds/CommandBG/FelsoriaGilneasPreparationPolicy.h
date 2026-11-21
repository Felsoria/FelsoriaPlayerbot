#ifndef FELSORIA_GILNEAS_PREPARATION_POLICY_H
#define FELSORIA_GILNEAS_PREPARATION_POLICY_H
#include <cstdint>
namespace FelsoriaGilneasPreparation {
static std::int64_t const MountReserveMS=5000;
inline bool BuffWindow(bool waiting,std::int64_t remaining){return waiting&&remaining>0&&remaining<=20000;}
inline bool CastFits(std::int64_t remaining,std::uint32_t castMS,bool channeled){return !channeled&&remaining>MountReserveMS&&std::int64_t(castMS)<=remaining-MountReserveMS;}
class State {
    bool started=false,finished=false;
    std::uint32_t quietSince=0;
public:
    void Reset(){started=false;finished=false;quietSince=0;}
    bool Begun()const{return started;}
    bool Done()const{return finished;}
    void Begin(std::uint32_t now){started=true;quietSince=now;}
    void Activity(std::uint32_t now){quietSince=now;}
    bool Finish(std::uint32_t now,std::int64_t remaining,bool casting){
        if(!started||casting)return false;
        if(remaining<=MountReserveMS||std::uint32_t(now-quietSince)>=5500)finished=true;
        return finished;
    }
};
inline bool NeedsHordePlacement(bool bot,bool sameBG,bool horde,bool alive,bool known,bool pending,float distance,bool transport){
    return bot&&sameBG&&horde&&alive&&!known&&!pending&&(distance>10.0f||transport);
}
} // namespace FelsoriaGilneasPreparation
#endif
