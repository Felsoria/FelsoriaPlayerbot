#ifndef FELSORIA_CORDANA_POLICY_H
#define FELSORIA_CORDANA_POLICY_H
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace FelsoriaCordana {
// Encounter-only policy: no changes to other dungeon or outdoor AI.
inline bool threshold(std::uint64_t health,std::uint64_t maximum,std::uint32_t damage,unsigned percent){
 if(!maximum || !percent)return false;
 std::uint64_t remaining=damage>=health?0:health-damage;
 return double(remaining)*100.0<=double(maximum)*percent;
}
inline bool elapsed(std::uint32_t now,std::uint32_t then,std::uint32_t span){return std::uint32_t(now-then)>=span;}
inline std::uint32_t until(std::uint32_t now,std::uint32_t then,std::uint32_t span){
 std::uint32_t age=now-then;return age>=span?1:span-age;
}
struct WavePlan {
 unsigned count=0,interval=0;
};
inline WavePlan waves(bool normal,bool mythic,bool secondary){
 if(normal)return {};
 if(secondary)return {mythic?2u:1u,0};
 return {mythic?4u:2u,mythic?7000u:17000u};
}
struct WallPoint {float x,y,z,yaw;};
inline std::vector<WallPoint> wall(unsigned side,unsigned gap){
 std::vector<WallPoint> points;side%=4;gap=std::max(6u,std::min(18u,gap));
 // Retain the existing real platform, 2.5 yard spacing and five omitted slots.
 for(unsigned i=0;i<25;++i){
  unsigned slot=i+1;if(slot>=gap-2 && slot<=gap+2)continue;
  float d=float(i)*2.5f;
  switch(side){
   case 0:points.push_back({4020.11f,-267.21f-d,-281.11f,0.0f});break;
   case 1:points.push_back({4081.06f-d,-327.91f,-281.11f,1.57079632679f});break;
   case 2:points.push_back({4081.06f,-267.21f-d,-281.11f,3.14159265359f});break;
   case 3:points.push_back({4081.06f-d,-267.21f,-281.11f,4.71238898038f});break;
  }
 }
 return points;
}
inline unsigned nextWall(unsigned side,bool clockwise){return (side+(clockwise?1u:3u))%4u;}
constexpr float GhostSpeed=4.0f;
constexpr float GhostTravel=55.0f;
constexpr std::uint32_t FirstPhaseMinimum=35000;
constexpr std::uint32_t FirstPhaseFailsafe=55000;
constexpr std::uint32_t CreepingCooldown=70000;
}
#endif
