#ifndef FELSORIA_GILNEAS_NAVIGATION_H
#define FELSORIA_GILNEAS_NAVIGATION_H
#include "FelsoriaGilneasRecordedData.h"
#include "FelsoriaGilneasWaterTraversal.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <set>
#include <utility>
#include <vector>
namespace FelsoriaGilneas {
inline float Distance(Point const&a,Point const&b) {
    float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return std::sqrt(x*x+y*y+z*z);
}
inline unsigned Noise(unsigned n) {n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);}
inline Point ReadyPoint(unsigned team,unsigned slot,unsigned salt) {
    // Stable, irregular disk around the supplied GPS; inside both start gates.
    unsigned n=Noise(salt+slot*104729u);
    float a=2.39996323f*float(slot)+float(n%1000)*.00065f;
    float r=2.0f+3.1f*std::sqrt(float((n>>10)%1000)/999.0f);
    return {Starts[team].x+r*std::cos(a),Starts[team].y+r*std::sin(a),Starts[team].z};
}
// A flag is a destination. Switching between two other bases must use a
// road junction rather than enter an unrelated flag's capture area.
inline bool TransitPointAllowed(unsigned start,unsigned goal,unsigned point){
    unsigned destination=3;
    for(unsigned n=0;n<3;++n)if(goal==Goals[n])destination=n;
    if(destination==3)return true;
    auto withinFlagArea=[](Point const&a,Point const&b,float radius){float x=a.x-b.x,y=a.y-b.y;
        return x*x+y*y<radius*radius&&std::abs(a.z-b.z)<8.0f;};
    for(unsigned n=0;n<3;++n){
        if(n==destination||withinFlagArea(Points[start],Points[Goals[n]],45.0f))continue;
        if(withinFlagArea(Points[point],Points[Goals[n]],24.0f))return false;
    }
    return true;
}
inline bool FindPath(unsigned start,unsigned goal,unsigned team,
    std::vector<float>const&risk,float const*routeWeight,
    std::set<std::pair<unsigned,unsigned>>const&blocked,
    std::vector<unsigned>&out,float*total=nullptr) {
    out.clear();if(start>=PointCount||goal>=PointCount||team>1)return false;
    float const infinity=1e30f;
    std::vector<float>cost(PointCount,infinity);std::vector<unsigned>parent(PointCount,PointCount);
    typedef std::pair<float,unsigned> Entry;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>>open;
    cost[start]=0;open.emplace(0.0f,start);
    while(!open.empty()) {
        auto top=open.top();open.pop();unsigned v=top.second;
        if(top.first>cost[v])continue;
        if(v==goal)break;
        for(unsigned i=Offsets[v];i<Offsets[v+1];++i) {
            Edge const&e=Edges[i];
            if(blocked.count({v,e.to})||!FelsoriaGilneasWater::AllowEdge(v,e.to,e.route))continue;
            if(!TransitPointAllowed(start,goal,e.to))continue;
            // Never route a field squad through the opposing starting pen.
            if(Distance(Points[e.to],Starts[1-team])<40.0f)continue;
            float crowd=e.route<RouteCount?std::max(0.0f,routeWeight[e.route]-1.0f):0.0f;
            float danger=risk.size()==PointCount?std::max(risk[v],risk[e.to]):0.0f;
            // Spread squads only among reasonably short alternatives. Nearby
            // enemies must not turn an LH -> WW order into a Mines detour.
            float weight=1.0f+std::min(.10f,crowd*.04f+std::min(5.0f,std::max(0.0f,danger))*.01f);
            float step=std::max(.05f,Distance(Points[v],Points[e.to]))*weight;
            if(cost[v]+step<cost[e.to]) {
                cost[e.to]=cost[v]+step;parent[e.to]=v;open.emplace(cost[e.to],e.to);
            }
        }
    }
    if(cost[goal]>=infinity)return false;
    for(unsigned v=goal;;v=parent[v]) {out.push_back(v);if(v==start)break;if(parent[v]>=PointCount){out.clear();return false;}}
    std::reverse(out.begin(),out.end());if(total)*total=cost[goal];return true;
}
inline unsigned EdgeRoute(unsigned from,unsigned to) {
    for(unsigned i=Offsets[from];i<Offsets[from+1];++i)if(Edges[i].to==to)return Edges[i].route;
    return RouteCount;
}
} // namespace FelsoriaGilneas
#endif
