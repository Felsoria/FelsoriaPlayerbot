// The runner appends the production MoveObjectiveTo body to this engine boundary.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using uint32=std::uint32_t;
namespace G3D {
struct Vector3 {
    float x,y,z;
    Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
    Vector3 operator-(Vector3 v)const{return{x-v.x,y-v.y,z-v.z};}
    float squaredLength()const{return x*x+y*y+z*z;}
};
}
struct Position {
    float x=0,y=0,z=0;
    float GetPositionX()const{return x;}float GetPositionY()const{return y;}float GetPositionZ()const{return z;}
    float GetExactDist(float a,float b,float c)const{return std::sqrt((x-a)*(x-a)+(y-b)*(y-b)+(z-c)*(z-c));}
};
enum {UNIT_STATE_CASTING=1,UNIT_STATE_STUNNED=2,UNIT_STATE_CONFUSED=4,UNIT_STATE_FLEEING=8,
      MOVEMENTFLAG_FALLING=1,MOVEMENTFLAG_FALLING_FAR=2,PATHFIND_NORMAL=1};
namespace Mechanics {enum{MECHANIC_ROOT=1};}
struct BG{bool supported=true;};
struct CommandObjectiveBG{static bool Supports(BG const* bg){return bg&&bg->supported;}};
struct Session{uint32 GetAccountId()const{return 1;}};
struct Spline{bool finalized=false;bool Finalized()const{return finalized;}};
struct Player:Position {
    BG bg;Session session;Spline spline;Spline* movespline=&spline;
    bool alive=true,root=false,flying=false,falling=false,transport=false,vehicle=false;
    unsigned state=0;uint32 instance=1;
    BG* GetBattleground(){return &bg;}bool isAlive()const{return alive;}
    bool HasUnitState(unsigned mask)const{return(state&mask)!=0;}bool HasAuraWithMechanic(unsigned)const{return root;}
    void* GetTransport()const{return transport?(void*)this:nullptr;}void* GetVehicleBase()const{return vehicle?(void*)this:nullptr;}
    bool IsFlying()const{return flying;}bool HasUnitMovementFlag(unsigned)const{return falling;}
    uint32 GetInstanceId()const{return instance;}Session* GetSession(){return &session;}
    G3D::Vector3 GetVector3()const{return{x,y,z};}
};
struct BotBGAI{bool blocked=false;bool IsNotSelect(Player*)const{return blocked;}};
struct PathParameter{
    PathParameter(uint32,Player*){}
    float targetPosX=0,targetPosY=0,targetPosZ=0,offset=0;bool findOK=false;
    std::vector<G3D::Vector3> finishPaths;G3D::Vector3 destPosition;
};
uint32 clockMs=1000;
uint32 getMSTime(){return clockMs;}
bool calculated=true;
unsigned pathType=PATHFIND_NORMAL,pathCalls=0;
std::vector<G3D::Vector3> points;
G3D::Vector3 endPoint;
struct Pathfinding{
    Pathfinding(PathParameter*,void*,void*){}
    bool CalculatePath(float,float,float,bool,bool){++pathCalls;return calculated;}
    unsigned GetPathType()const{return pathType;}
    std::vector<G3D::Vector3>const& GetPath()const{return points;}
    G3D::Vector3 GetActualEndPosition()const{return endPoint;}
};
struct BotBGAIMovement{
    Player* m_Player;BotBGAI* m_BGAI;uint32 m_ObjectiveMoveAt=0,m_ObjectiveInstance=0;
    bool m_ObjectiveMoving=false;Position m_ObjectiveGoal;unsigned launches=0;
    BotBGAIMovement(Player* p,BotBGAI* ai):m_Player(p),m_BGAI(ai){}
    bool MoveObjectiveTo(Position const&,float);
    void ApplyFinishPath(PathParameter* p){if(!p->findOK)throw std::runtime_error("Invalid route launched");++launches;}
};
unsigned checks=0;
void Check(bool value,char const* text){++checks;if(!value)throw std::runtime_error(text);}
void Reset(){clockMs=1000;calculated=true;pathType=PATHFIND_NORMAL;pathCalls=0;points={{0,0,0},{10,0,0}};endPoint={10,0,0};}
int main(){try{
    Position goal;goal.x=10;
    {
        Reset();Player p;BotBGAI ai;BotBGAIMovement move(&p,&ai);
        Check(move.MoveObjectiveTo(goal,1)&&move.launches==1,"Complete route was not launched.");
        clockMs+=100;Check(move.MoveObjectiveTo(goal,1)&&move.launches==1,"Active route was restarted every tick.");
        p.instance=2;Check(move.MoveObjectiveTo(goal,1)&&move.launches==2,"Cached route escaped into another instance.");
    }
    for(unsigned type:{0u,2u,3u,4u,8u,16u,32u}){
        Reset();pathType=type;Player p;BotBGAI ai;BotBGAIMovement move(&p,&ai);
        Check(!move.MoveObjectiveTo(goal,1)&&!move.launches,"Incomplete, failed or shortcut path launched.");
    }
    for(unsigned count:{0u,1u,257u}){
        Reset();points.resize(count);Player p;BotBGAI ai;BotBGAIMovement move(&p,&ai);
        Check(!move.MoveObjectiveTo(goal,1)&&!move.launches,"Invalid path length launched.");
    }
    for(unsigned scenario=0;scenario<6;++scenario){
        Reset();Player p;BotBGAI ai;BotBGAIMovement move(&p,&ai);Position target=goal;
        float nan=std::numeric_limits<float>::quiet_NaN();
        if(scenario==0)calculated=false;
        if(scenario==1)points[0].x=nan;
        if(scenario==2)endPoint.x=nan;
        if(scenario==3)endPoint.x=100;
        if(scenario==4)points[0].z=20;
        if(scenario==5)target.x=nan;
        Check(!move.MoveObjectiveTo(target,1)&&!move.launches,"Invalid geometry launched.");
    }
    for(unsigned scenario=0;scenario<13;++scenario){
        Reset();Player p;BotBGAI ai;BotBGAIMovement move(&p,&ai);
        if(scenario==0)p.alive=false;
        if(scenario==1)p.root=true;
        if(scenario==2)p.flying=true;
        if(scenario==3)p.falling=true;
        if(scenario==4)p.transport=true;
        if(scenario==5)p.vehicle=true;
        if(scenario==6)p.bg.supported=false;
        if(scenario==7)ai.blocked=true;
        if(scenario>=8&&scenario<=11)p.state=1u<<(scenario-8);
        if(scenario==12)p.x=10;
        Check(!move.MoveObjectiveTo(goal,1)&&!move.launches,"Movement replaced a protected state or moved after arrival.");
    }
    {
        Reset();calculated=false;Player p;BotBGAI ai;BotBGAIMovement move(&p,&ai);
        Check(!move.MoveObjectiveTo(goal,1),"Failed path handled as moving.");
        clockMs+=100;calculated=true;Check(!move.MoveObjectiveTo(goal,1)&&pathCalls==1,"Failed path retried without a bound.");
        clockMs+=1600;Check(move.MoveObjectiveTo(goal,1)&&move.launches==1,"Recovered navigation was not retried.");
    }
    std::cout<<checks<<" native objective movement checks passed.\n";
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
