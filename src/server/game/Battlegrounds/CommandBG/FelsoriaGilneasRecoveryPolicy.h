#ifndef FELSORIA_GILNEAS_RECOVERY_POLICY_H
#define FELSORIA_GILNEAS_RECOVERY_POLICY_H
#include "FelsoriaGilneasNavigation.h"
#include <map>
#include <queue>
#include <tuple>
namespace FelsoriaGilneasRecovery {
using Point=FelsoriaGilneas::Point;
struct Step {Point point{};bool jump=false;};
enum class Result {Searching,Found,Failed};
static float const MaximumDrop=32.0f;
static Point const Graveyards[]={{1034.82f,1335.58f,13.0f},{887.57f,937.337f,23.77f},
    {1252.23f,836.547f,27.78f},{909.46f,1337.36f,27.64f},{1401.38f,977.12f,7.43f}};
inline float Flat(Point const&a,Point const&b){float x=a.x-b.x,y=a.y-b.y;return std::sqrt(x*x+y*y);}
// A walking spline can pass several samples between AI ticks. Recognize
// progress along the polyline instead of requiring a stop at every sample.
inline unsigned WalkingProgress(std::vector<Step>const&path,unsigned cursor,Point const&current){
    if(cursor>=path.size()||path[cursor].jump)return cursor;
    unsigned first=cursor?cursor-1:0,last=cursor;
    while(last+1<path.size()&&!path[last+1].jump)++last;
    if(FelsoriaGilneas::Distance(current,path[last].point)<1.1f)return last+1;
    float best=2.5f*2.5f;unsigned progress=cursor;
    for(unsigned i=first;i<last;++i){
        auto a=path[i].point,b=path[i+1].point;
        float x=b.x-a.x,y=b.y-a.y,z=b.z-a.z,length=x*x+y*y+z*z;
        float u=length>.00001f?std::max(0.0f,std::min(1.0f,((current.x-a.x)*x+(current.y-a.y)*y+(current.z-a.z)*z)/length)):0;
        Point projected{a.x+x*u,a.y+y*u,a.z+z*u};float d=FelsoriaGilneas::Distance(current,projected);d*=d;
        if(d<best-.0001f){best=d;progress=std::max(cursor,i+1+(u>.995f?1u:0u));}
    }
    return std::min(progress,last+1);
}
inline unsigned Graveyard(Point const&p){unsigned found=5;float best=32.0f;
    for(unsigned i=0;i<5;++i){float d=Flat(p,Graveyards[i]);if(d<best&&std::abs(p.z-Graveyards[i].z)<7.0f){best=d;found=i;}}return found;}
inline bool OnRoad(Point const&p,unsigned team){
    for(unsigned i=0;i<FelsoriaGilneas::PointCount;++i){auto q=FelsoriaGilneas::Points[i];
        if(!FelsoriaGilneasWater::Interior(i)&&Flat(q,FelsoriaGilneas::Starts[1-team])>=40&&Flat(p,q)<2.6f&&std::abs(p.z-q.z)<1.2f)return true;}
    return false;
}
inline std::vector<Point> Targets(Point const&start,unsigned team){
    std::vector<Point>points;
    for(unsigned i=0;i<FelsoriaGilneas::PointCount;++i){auto p=FelsoriaGilneas::Points[i];
        if(!FelsoriaGilneasWater::Interior(i)&&Flat(p,start)<=170&&Flat(p,FelsoriaGilneas::Starts[1-team])>=40)points.push_back(p);}
    std::stable_sort(points.begin(),points.end(),[&](Point const&a,Point const&b){return FelsoriaGilneas::Distance(a,start)<FelsoriaGilneas::Distance(b,start);});
    return points;
}
class Search {
    struct Node {Point point;float cost;unsigned parent;int x,y;bool jump;};
    Point origin{};std::vector<Point>targets;std::vector<Node>nodes;
    std::map<std::tuple<int,int,int>,unsigned>known;
    std::map<std::tuple<int,int,int>,std::pair<bool,float>>floors;
    std::priority_queue<std::pair<float,unsigned>,std::vector<std::pair<float,unsigned>>,std::greater<std::pair<float,unsigned>>>open;
    std::vector<Step>path;Result state=Result::Failed;unsigned expanded=0;
    float Heuristic(Point const&p)const{float best=1e20f;for(auto const&t:targets)best=std::min(best,Flat(p,t));return best;}
    void Finish(unsigned index,Point const&target,bool jump){
        path.clear();for(unsigned at=index;;at=nodes[at].parent){path.push_back({nodes[at].point,nodes[at].jump});if(at==0)break;}
        std::reverse(path.begin(),path.end());
        if(FelsoriaGilneas::Distance(path.back().point,target)>.1f)path.push_back({target,jump});
        state=Result::Found;
    }
public:
    void Begin(Point const&start,std::vector<Point> goals){
        origin=start;targets=std::move(goals);nodes.clear();known.clear();floors.clear();path.clear();expanded=0;open=decltype(open)();
        nodes.push_back({start,0,0,0,0,false});known[{0,0,int(std::lround(start.z*2))}]=0;
        state=targets.empty()?Result::Failed:Result::Searching;if(state==Result::Searching)open.push({Heuristic(start),0});
    }
    Result State()const{return state;}std::vector<Step>const&Path()const{return path;}
    unsigned Expanded()const{return expanded;}
    template<class Floor,class Walk,class Jump>Result Advance(Floor floor,Walk walk,Jump jump,unsigned budget=32){
        if(state!=Result::Searching)return state;
        for(unsigned work=0;work<budget&&!open.empty()&&expanded<1400;++work){
            auto top=open.top();open.pop();unsigned index=top.second;Node current=nodes[index];
            if(top.first>current.cost+Heuristic(current.point)+.01f)continue;
            ++expanded;
            for(auto const&goal:targets)if(Flat(current.point,goal)<3.0f&&std::abs(current.point.z-goal.z)<3.0f){
                if(FelsoriaGilneas::Distance(current.point,goal)<.15f||walk(current.point,goal)){Finish(index,goal,false);return state;}
                if(jump(current.point,goal)){Finish(index,goal,true);return state;}
            }
            for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y){
                if(!x&&!y)continue;
                int gx=current.x+x,gy=current.y+y;
                if(gx*gx+gy*gy>40*40)continue;
                Point next{origin.x+float(gx)*4,origin.y+float(gy)*4,current.point.z};
                auto probeKey=std::make_tuple(gx,gy,int(std::lround(current.point.z)));
                auto probe=floors.find(probeKey);
                if(probe==floors.end()){
                    float z=next.z;bool valid=floor(next.x,next.y,current.point.z,z)&&std::isfinite(z)&&z>-99999;
                    probe=floors.emplace(probeKey,std::make_pair(valid,z)).first;
                }
                if(!probe->second.first)continue;
                next.z=probe->second.second;
                float dz=next.z-current.point.z;bool airborne=false;
                if(dz>2.5f||dz< -MaximumDrop)continue;
                float minimum=current.cost+FelsoriaGilneas::Distance(current.point,next);
                auto key=std::make_tuple(gx,gy,int(std::lround(next.z*2)));auto it=known.find(key);unsigned at;
                if(it!=known.end()&&nodes[it->second].cost<=minimum)continue;
                if(!walk(current.point,next)){if(dz>1.0f||!jump(current.point,next))continue;airborne=true;}
                float cost=minimum+(airborne?3.0f:0.0f);
                if(it!=known.end()){at=it->second;if(nodes[at].cost<=cost)continue;nodes[at]={next,cost,index,gx,gy,airborne};}
                else {if(nodes.size()>=2400)continue;at=unsigned(nodes.size());known[key]=at;nodes.push_back({next,cost,index,gx,gy,airborne});}
                open.push({cost+Heuristic(next),at});
            }
        }
        if(open.empty()||expanded>=1400)state=Result::Failed;
        return state;
    }
};
enum class MoveResult {Running,Finished,Rejected};
} // namespace FelsoriaGilneasRecovery
#endif
