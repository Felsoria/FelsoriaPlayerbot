#ifndef FELSORIA_GILNEAS_COMBAT_POLICY_H
#define FELSORIA_GILNEAS_COMBAT_POLICY_H
#include "FelsoriaGilneasNavigation.h"
namespace FelsoriaGilneasCombat {
using Point=FelsoriaGilneas::Point;
static float const Radius=28.0f,PositionRadius=23.0f;
inline float Flat(Point const&a,Point const&b){float x=a.x-b.x,y=a.y-b.y;return std::sqrt(x*x+y*y);}
inline bool BaseBattle(Point const&player,Point const&enemy,Point const&flag){
    return Flat(player,flag)<=42&&std::abs(player.z-flag.z)<=6&&
        Flat(enemy,flag)<=36&&std::abs(enemy.z-flag.z)<=6;
}
inline bool TargetAllowed(Point const&enemy,Point const&flag){return Flat(enemy,flag)<=36&&std::abs(enemy.z-flag.z)<=6;}
inline bool Inside(Point const&point,Point const&flag){return Flat(point,flag)<=Radius&&std::abs(point.z-flag.z)<=5;}
inline Point Bound(Point const&requested,Point const&flag){
    float d=Flat(requested,flag);if(d<=PositionRadius&&std::abs(requested.z-flag.z)<=5)return requested;
    float x=requested.x-flag.x,y=requested.y-flag.y;if(d<.1f){x=1;y=0;d=1;}
    float radius=std::min(PositionRadius,d);return {flag.x+x/d*radius,flag.y+y/d*radius,flag.z};
}
// Transit fights are brief self-defence, not a trigger for every passing
// squad to join an endless midfield brawl. A new strategic order preserves
// this per-bot cooldown; only death or leaving the match clears it.
struct TransitFight {
    bool active=false,cooling=false;unsigned started=0,lastThreat=0,cooldownAt=0;Point origin{};
};
inline bool TransitActive(TransitFight const&s,Point const&current,unsigned now){
    return s.active&&!s.cooling&&now-s.started<8000u&&now-s.lastThreat<2500u&&
        Flat(current,s.origin)<=12.0f&&std::abs(current.z-s.origin.z)<=6.0f;
}
inline void ObserveTransit(TransitFight&s,Point const&current,unsigned now,bool directThreat){
    if(s.cooling&&now-s.cooldownAt>=10000u)s.cooling=false;
    if(s.active){
        if(directThreat)s.lastThreat=now;
        if(!TransitActive(s,current,now)){s.active=false;s.cooling=true;s.cooldownAt=now;}
        return;
    }
    if(directThreat&&!s.cooling){s.active=true;s.started=s.lastThreat=now;s.origin=current;}
}
inline bool TransitTarget(TransitFight const&s,Point const&current,Point const&enemy,unsigned now,bool attacking,bool selected){
    return TransitActive(s,current,now)&&(attacking||selected)&&Flat(current,enemy)<=18.0f&&std::abs(current.z-enemy.z)<=6.0f;
}
inline Point BoundTransit(Point const&requested,Point const&origin){
    float d=Flat(requested,origin);if(d<=10.0f&&std::abs(requested.z-origin.z)<=5.0f)return requested;
    float radius=std::min(10.0f,d);if(d<.01f)return origin;
    return {origin.x+(requested.x-origin.x)*radius/d,origin.y+(requested.y-origin.y)*radius/d,origin.z};
}
} // namespace FelsoriaGilneasCombat
#endif
