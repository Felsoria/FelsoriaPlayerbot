#ifndef FELSORIA_GILNEAS_GROUND_POLICY_H
#define FELSORIA_GILNEAS_GROUND_POLICY_H
#include <algorithm>
#include <cmath>
#include <vector>
namespace FelsoriaGilneasGround {
struct Sample {float x,y,z;};
inline bool Valid(Sample const&p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
inline float Gap(Sample const&a,Sample const&b){float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return std::sqrt(x*x+y*y+z*z);}
// Only a short local join may use collision/height data without a mesh corridor.
// The long strategic path still consists of the player's recorded samples.
template<class FloorProbe,class CorridorProbe>
bool BuildLocal(Sample const&start,Sample const&goal,FloorProbe floorProbe,CorridorProbe corridor,
    std::vector<Sample>&out,char const*&reason){
    out.clear();reason="local_invalid";
    if(!Valid(start)||!Valid(goal))return false;
    float dx=goal.x-start.x,dy=goal.y-start.y,dz=goal.z-start.z;
    float horizontal=std::sqrt(dx*dx+dy*dy);
    if(Gap(start,goal)>12.0f||std::abs(dz)>4.0f){reason="local_too_long";return false;}
    unsigned count=std::max(1u,unsigned(std::ceil(horizontal/.6f)));
    float ground=0;
    if(!floorProbe(start,ground)||!std::isfinite(ground)||std::abs(ground-start.z)>1.25f){reason="local_start_floor";return false;}
    out.push_back(start);
    for(unsigned i=1;i<=count;++i){
        float f=float(i)/float(count);Sample expected{start.x+dx*f,start.y+dy*f,start.z+dz*f};
        if(!floorProbe(expected,ground)||!std::isfinite(ground)||std::abs(ground-expected.z)>1.25f){reason="local_floor_gap";out.clear();return false;}
        Sample projected{expected.x,expected.y,ground+.05f};auto const&previous=out.back();
        float x=projected.x-previous.x,y=projected.y-previous.y;
        if(std::abs(projected.z-previous.z)>std::max(.8f,std::sqrt(x*x+y*y)*1.1f+.2f)){
            reason="local_step";out.clear();return false;
        }
        if(!corridor(previous,projected)){reason="local_obstacle";out.clear();return false;}
        if(Gap(previous,projected)>=.25f)out.push_back(projected);
    }
    reason="local_clear";return true;
}
} // namespace FelsoriaGilneasGround
#endif
