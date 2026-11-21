#ifndef FELSORIA_GILNEAS_ENTRY_POLICY_H
#define FELSORIA_GILNEAS_ENTRY_POLICY_H
#include <cstdint>
namespace FelsoriaGilneasEntry {
inline bool Fast(unsigned purpose,unsigned queue){return purpose==3&&queue==120;}
inline unsigned CreationPriority(unsigned purpose,unsigned queue,bool criticalRdf){
    return purpose==2?(criticalRdf?0u:1u):Fast(purpose,queue)?2u:purpose==3?3u:4u;
}
inline unsigned LoginPriority(unsigned purpose,unsigned queue){return purpose==2?0u:Fast(purpose,queue)?1u:2u;}
inline bool LoginDelayPassed(unsigned purpose,unsigned queue,std::uint32_t elapsed){return Fast(purpose,queue)||elapsed>1500;}
} // namespace FelsoriaGilneasEntry
#endif
