#ifndef FELSORIA_GILNEAS_WATER_TRAVERSAL_H
#define FELSORIA_GILNEAS_WATER_TRAVERSAL_H
#include "FelsoriaGilneasRecordedData.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace FelsoriaGilneasWater {
// wwhorda raw samples 68..82: swimming landings with airborne samples between.
// Preserve the recorded shore corridor, but let native jumps generate the arcs.
static unsigned const First=593,Last=607,Route=6;
static unsigned const Landings[]={593,596,599,601,603,605,607};
inline bool Interior(unsigned point){return point>First&&point<Last;}
inline bool AllowEdge(unsigned from,unsigned to,unsigned route){return (!Interior(from)&&!Interior(to))||route==Route;}
template<class V>inline bool Matches(V const&a,FelsoriaGilneas::Point const&b){
    float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z<.01f;
}
struct Span {unsigned takeoff=0,landing=0;bool reverse=false;};
template<class V>inline bool FindSpan(std::vector<V>const&path,unsigned cursor,Span&out){
    for(unsigned i=cursor;i<path.size();++i){
        bool reverse=Matches(path[i],FelsoriaGilneas::Points[Last]);
        if(!reverse&&!Matches(path[i],FelsoriaGilneas::Points[First]))continue;
        for(unsigned j=i+1;j<path.size()&&j<=i+32;++j)
            if(Matches(path[j],FelsoriaGilneas::Points[reverse?First:Last])){out={i,j,reverse};return true;}
    }
    return false;
}
inline float Horizontal(FelsoriaGilneas::Point const&a,FelsoriaGilneas::Point const&b){
    float x=a.x-b.x,y=a.y-b.y;return std::sqrt(x*x+y*y);
}
inline bool NextHop(FelsoriaGilneas::Point const&current,bool reverse,float maximum,FelsoriaGilneas::Point&out){
    if(!std::isfinite(current.x)||!std::isfinite(current.y)||!std::isfinite(current.z)||maximum<=0)return false;
    unsigned const count=unsigned(sizeof(Landings)/sizeof(Landings[0]));
    auto point=[&](unsigned i){return FelsoriaGilneas::Points[Landings[reverse?count-1-i:i]];};
    float lengths[7]={},total=0,best=1e20f,along=0;
    for(unsigned i=1;i<count;++i){total+=Horizontal(point(i-1),point(i));lengths[i]=total;}
    for(unsigned i=1;i<count;++i){
        auto a=point(i-1),b=point(i);float dx=b.x-a.x,dy=b.y-a.y,sq=dx*dx+dy*dy;
        float u=sq>0?std::max(0.0f,std::min(1.0f,((current.x-a.x)*dx+(current.y-a.y)*dy)/sq)):0;
        float x=a.x+dx*u-current.x,y=a.y+dy*u-current.y,d=x*x+y*y;
        if(d<best){best=d;along=lengths[i-1]+u*(lengths[i]-lengths[i-1]);}
    }
    if(best>4.0f)return false;
    float target=std::min(total,along+maximum);
    for(unsigned i=1;i<count;++i)if(target<=lengths[i]){
        auto a=point(i-1),b=point(i);float u=(target-lengths[i-1])/(lengths[i]-lengths[i-1]);
        out={a.x+(b.x-a.x)*u,a.y+(b.y-a.y)*u,a.z+(b.z-a.z)*u};
        return Horizontal(current,out)>.15f;
    }
    out=point(count-1);return Horizontal(current,out)>.15f;
}
} // namespace FelsoriaGilneasWater
#endif
