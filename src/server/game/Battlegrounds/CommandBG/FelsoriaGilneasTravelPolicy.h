#ifndef FELSORIA_GILNEAS_TRAVEL_POLICY_H
#define FELSORIA_GILNEAS_TRAVEL_POLICY_H
#include "FelsoriaGilneasNavigation.h"
namespace FelsoriaGilneasTravel {
using Point=FelsoriaGilneas::Point;
inline bool Protected(Point const&p){
    using namespace FelsoriaGilneas;
    // Keep the complete recorded Alliance bridge route and Horde water
    // takeoffs/landings exact; these must not acquire lateral offsets.
    auto const&bridge=Routes[1];
    for(unsigned i=bridge.first;i<bridge.first+bridge.count;++i)
        if(Distance(p,Points[i])<7.0f)return true;
    for(unsigned i=FelsoriaGilneasWater::First;i<=FelsoriaGilneasWater::Last;++i)
        if(Distance(p,Points[i])<9.0f)return true;
    return false;
}
inline float Smooth(float value){value=std::max(0.0f,std::min(1.0f,value));return value*value*(3-2*value);}
// Each bot keeps one stable lane for this match. Failed probes and protected
// points taper nearby offsets to zero over 12 yards, avoiding a random zigzag.
template<class Probe>std::vector<Point> Spread(std::vector<Point>const&input,unsigned salt,Probe probe){
    using FelsoriaGilneas::Distance;
    if(input.size()<4)return input;
    unsigned noise=FelsoriaGilneas::Noise(salt);
    float lane=(noise&1?1.0f:-1.0f)*(.65f+1.55f*float((noise>>1)%1000)/999.0f);
    unsigned size=unsigned(input.size());std::vector<float>room(size,12.0f);
    std::vector<Point>normal(size);room.front()=room.back()=0;
    for(unsigned i=1;i+1<size;++i){
        auto const&a=input[i-1];auto const&b=input[i];auto const&c=input[i+1];
        float ax=b.x-a.x,ay=b.y-a.y,bx=c.x-b.x,by=c.y-b.y;
        float al=std::sqrt(ax*ax+ay*ay),bl=std::sqrt(bx*bx+by*by);
        if(al<.1f||bl<.1f||Protected(b)||std::abs(b.z-a.z)>.6f*al||std::abs(c.z-b.z)>.6f*bl||
            (ax*bx+ay*by)/(al*bl)<.7f){room[i]=0;continue;}
        float dx=ax/al+bx/bl,dy=ay/al+by/bl,length=std::sqrt(dx*dx+dy*dy);
        if(length<.1f){room[i]=0;continue;}
        normal[i]={-dy/length,dx/length,0};
        Point candidate{b.x+normal[i].x*lane,b.y+normal[i].y*lane,b.z};
        if(!probe(a,b,c,candidate))room[i]=0;
    }
    for(unsigned i=1;i<size;++i)room[i]=std::min(room[i],room[i-1]+Distance(input[i-1],input[i]));
    for(unsigned i=size-1;i>0;--i)room[i-1]=std::min(room[i-1],room[i]+Distance(input[i-1],input[i]));
    std::vector<Point>out=input;
    for(unsigned i=1;i+1<size;++i){
        float offset=lane*Smooth(room[i]/12.0f);if(std::abs(offset)<.02f)continue;
        auto const&b=input[i];Point candidate{b.x+normal[i].x*offset,b.y+normal[i].y*offset,b.z};
        if(probe(out[i-1],b,input[i+1],candidate))out[i]=candidate;
    }
    return out;
}
} // namespace FelsoriaGilneasTravel
#endif
