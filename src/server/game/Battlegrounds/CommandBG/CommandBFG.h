#ifndef FELSORIA_COMMAND_BFG_H
#define FELSORIA_COMMAND_BFG_H
#include "CommandBG.h"
#include "BotAI.h"
#include "BattlegroundBattleForGilneas.h"
#include "FelsoriaGilneasNavigation.h"
#include "MoveSpline.h"
#include "Config.h"
#include "WorldSession.h"
#include "FelsoriaGilneasGroundPolicy.h"
#include "FelsoriaGilneasPreparationPolicy.h"
#include "FelsoriaGilneasRecoveryPolicy.h"
#include "FelsoriaGilneasExitRoutes.h"
#include "FelsoriaGilneasTravelPolicy.h"
#include "FelsoriaGilneasCombatPolicy.h"
#include "FelsoriaGilneasStrategyPolicy.h"
#include "ObjectAccessor.h"
#include <fstream>
#include <sstream>
#include <mutex>
#include <map>
#include <set>
#include <vector>
// Header-only so the existing CMake project needs no new compilation unit.
class CommandBFG : public CommandBG {
    using Point=FelsoriaGilneas::Point;
    struct Task {
        unsigned goal=3,route=9;
        uint32 assigned=0,attempt=0,progressAt=0,warning=0,battleAt=0,repositionAt=0;
        unsigned battle=3;
        uint32 capAt=0,capTick=0,retryAt=0,blockedSince=0;
        uint64 health=0;
        Position progress,capOrigin;
        bool combat=false;char const*capReason="none";
        std::vector<G3D::Vector3>path;
        std::set<std::pair<unsigned,unsigned>>blocked;
    };
    struct MountAudit {char const*reason="not_called";unsigned spell=0,result=0;};
    std::map<uint64,MountAudit>m_mountAudit;
    std::map<uint64,Task>m_tasks;
    std::map<uint64,FelsoriaGilneasCombat::TransitFight>m_transitCombat;
    std::map<uint64,Point>m_ready;
    std::map<uint64,std::vector<G3D::Vector3>>m_readyPaths;
    std::map<uint64,uint32>m_readyRetry;
    uint64 m_cappers[3]={};uint32 m_capUntil[3]={};
    uint32 m_planAt=0,m_strategySignature=~0u;
    FelsoriaGilneasStrategy::Allocation m_strategy;
    uint32 m_auditAt=0;unsigned m_auditPhase=~0u,m_auditCount=0,m_prepAuditMask=0;
    std::map<uint64,uint32>m_aiTicks,m_orderTicks;std::map<uint64,bool>m_aiGate;
    struct Recovery {
        bool active=false,recorded=false,fighting=false;unsigned grave=5,cursor=0,failures=0;
        uint32 since=0,progressAt=0,retryAt=0,reportedAt=0;Point progress{};
        char const*reason="none";std::vector<FelsoriaGilneasRecovery::Step>path;
    };
    struct ExitPlan {
        bool begun=false;Point origin{};unsigned candidate=0;
        uint32 advanced=0,failedAt=0;unsigned floorReads=0,floorRejected=0,walkRejected=0,jumpRejected=0;
        std::vector<Point>targets,nativeTargets;
        FelsoriaGilneasRecovery::Search search;
        std::vector<FelsoriaGilneasRecovery::Step>path;
    };
    using ExitKey=std::pair<unsigned,std::set<uint32>>;
    std::map<ExitKey,ExitPlan>m_exits;
    std::map<uint64,Recovery>m_recovery;
    std::map<uint64,bool>m_life;
    std::map<uint64,unsigned>m_traceCount;
    static Point Location(Player const*p){return {p->GetPositionX(),p->GetPositionY(),p->GetPositionZ()};}
    static Point UnitLocation(Unit const*p){return {p->GetPositionX(),p->GetPositionY(),p->GetPositionZ()};}
    static G3D::Vector3 Vector(Point const&p){return G3D::Vector3(p.x,p.y,p.z);}
    static bool Visible(Player*p,Player*q){return q&&p!=q&&q->isAlive()&&q->IsInWorld()&&q->GetMap()==p->GetMap()&&
        p->InSamePhase(q)&&p->InSamePhaseId(q)&&p->canSeeOrDetect(q)&&p->IsWithinLOSInMap(q);}
    BattlegroundBattleForGilneas* BG()const{return dynamic_cast<BattlegroundBattleForGilneas*>(m_pBattleground);}
    bool Enabled()const{return m_pBattleground&&m_pBattleground->GetMapId()==761&&!m_pBattleground->IsArena()&&
        !m_pBattleground->IsRated()&&!m_pBattleground->IsWargame()&&!m_pBattleground->IsBrawl()&&
        sConfigMgr->GetBoolDefault("Felsoria.Gilneas.Enable",true);}
    bool Debug()const{return sConfigMgr->GetBoolDefault("Felsoria.Gilneas.Debug",false);}
    Point FlagPoint(unsigned node)const{
        if(auto*bg=BG())if(GameObject*flag=bg->GetBFGFlag(uint8(node))){auto v=flag->GetVector3();return {v.x,v.y,v.z};}
        return FelsoriaGilneas::Points[FelsoriaGilneas::Goals[node]];
    }
    void Trace(Player*p,char const*event,unsigned detail=0){
        if(!p||!m_pBattleground||m_traceCount[p->GetGUIDLow()]++>=48)return;
        auto recovery=m_recovery.find(p->GetGUIDLow());auto task=m_tasks.find(p->GetGUIDLow());
        std::ostringstream line;line<<"V5.0 ms="<<getMSTime()<<" instance="<<m_pBattleground->GetInstanceID()
            <<" team="<<unsigned(m_TeamID)<<" guid="<<p->GetGUIDLow()<<" event="<<event<<" detail="<<detail
            <<" grave="<<(recovery==m_recovery.end()?5u:recovery->second.grave)
            <<" cursor="<<(recovery==m_recovery.end()?0u:recovery->second.cursor)
            <<" steps="<<(recovery==m_recovery.end()?0u:unsigned(recovery->second.path.size()))
            <<" battle="<<(task==m_tasks.end()?3u:task->second.battle)
            <<" goal="<<(task==m_tasks.end()?3u:task->second.goal)<<" route="<<(task==m_tasks.end()?9u:task->second.route)
            <<" score="<<m_pBattleground->GetTeamScore(m_TeamID)<<" enemy_score="<<m_pBattleground->GetTeamScore(TeamId(1-unsigned(m_TeamID)))
            <<" urgent="<<unsigned(m_strategy.urgent)<<" primary="<<m_strategy.primary<<" owned="<<m_strategy.owned<<" enemy_owned="<<m_strategy.enemyOwned
            <<" quota="<<m_strategy.quota[0]<<','<<m_strategy.quota[1]<<','<<m_strategy.quota[2]
            <<" movement="<<(dynamic_cast<BotBGAI*>(p->GetAI())?dynamic_cast<BotBGAI*>(p->GetAI())->GetAIMovement()->GetBFGMovementReason():"no_ai")
            <<" casting="<<unsigned(p->HasUnitState(UNIT_STATE_CASTING))<<" root="<<unsigned(p->HasAuraType(SPELL_AURA_MOD_ROOT))
            <<" falling="<<unsigned(p->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR))
            <<" spline_done="<<unsigned(!p->movespline||p->movespline->Finalized())
            <<" x="<<p->GetPositionX()<<" y="<<p->GetPositionY()<<" z="<<p->GetPositionZ();
        if(recovery!=m_recovery.end()){
            auto const&r=recovery->second;auto plan=m_exits.find(ExitKey(r.grave,p->GetPhases()));
            line<<" recovery="<<r.reason<<" recorded="<<unsigned(r.recorded);
            if(r.cursor<r.path.size()){auto const&next=r.path[r.cursor];line<<" next="<<next.point.x<<','<<next.point.y<<','<<next.point.z<<" jump="<<unsigned(next.jump);}
            if(plan!=m_exits.end())line<<" floor_reads="<<plan->second.floorReads<<" floor_rejected="<<plan->second.floorRejected
                <<" walk_rejected="<<plan->second.walkRejected<<" jump_rejected="<<plan->second.jumpRejected;
        }
        line<<'\n';
        std::string directory=sConfigMgr->GetStringDefault("LogsDir","");
        if(!directory.empty()&&directory.back()!='/'&&directory.back()!='\\')directory+='/';
        std::string name="Felsoria-Gilneas-Activity-V5.0.log",path=directory+name;
        static std::mutex fileMutex;std::lock_guard<std::mutex>lock(fileMutex);
        std::ofstream file(path,std::ios::app|std::ios::ate);
        if(!file){path=name;file.clear();file.open(path,std::ios::app|std::ios::ate);}
        if(file&&file.tellp()>std::streamoff(512*1024)){file.close();file.open(path,std::ios::trunc);}
        if(file)file<<line.str();
    }
    // The battleground owns the authoritative FULL GUIDs. Do not rebuild a
    // Legion player GUID from only its low counter when resolving this roster.
    std::vector<Player*> TeamPlayers(bool botsOnly)const{
        std::vector<Player*>result;if(!m_pBattleground)return result;
        for(auto const&slot:m_pBattleground->GetPlayers()){
            Player*p=ObjectAccessor::FindPlayer(slot.first);
            if(p&&p->IsInWorld()&&p->GetBattleground()==m_pBattleground&&p->GetBGTeamId()==m_TeamID&&
                (!botsOnly||p->IsPlayerBot()))result.push_back(p);
        }
        return result;
    }
    Player* FindBot(uint64 guid)const{
        for(Player*p:TeamPlayers(true))if(p->GetGUIDLow()==guid)return p;
        return nullptr;
    }
    struct StartPlacement {bool done=false,chosen=false;uint32 attempted=0;Point goal{};char const*reason="pending";};
    std::map<uint64,StartPlacement>m_startPlacement;
    bool EnsureStartPlacement(Player*p){
        if(!Enabled()||!p||!p->IsPlayerBot()||!p->IsInWorld()||p->GetMapId()!=761||
            p->GetBattleground()!=m_pBattleground||p->GetBGTeamId()!=m_TeamID||!p->isAlive())return false;
        auto status=m_pBattleground->GetStatus();if(status!=STATUS_WAIT_JOIN&&status!=STATUS_IN_PROGRESS)return false;
        auto&placement=m_startPlacement[p->GetGUIDLow()];
        if(placement.done)return true;
        if(p->IsBeingTeleportedNear())p->GetSession()->HandleBotMoveTeleportAck();
        if(p->IsBeingTeleported()){placement.reason="port_pending";return false;}
        float distance=FelsoriaGilneas::Distance(Location(p),FelsoriaGilneas::Starts[m_TeamID]);
        if(!FelsoriaGilneasPreparation::NeedsHordePlacement(true,true,m_TeamID==TEAM_HORDE,true,false,false,distance,p->GetTransport()!=nullptr)){
            placement.done=true;placement.reason="land_start";return true;
        }
        uint32 now=getMSTime();
        if(placement.attempted&&now-placement.attempted<2000)return false;
        placement.attempted=now?now:1;
        if(!placement.chosen){
            using FelsoriaGilneasGround::Sample;auto center=FelsoriaGilneas::Starts[m_TeamID];
            auto floorProbe=[&](Sample const&v,float&floor){
                float a=p->GetMap()->GetHeight(p->GetPhases(),v.x,v.y,v.z-1.5f,true,8.0f);
                float b=p->GetMap()->GetHeight(p->GetPhases(),v.x,v.y,v.z+.25f,true,8.0f);
                bool av=std::isfinite(a)&&a>-99999.0f&&std::abs(a-v.z)<=1.25f;
                bool bv=std::isfinite(b)&&b>-99999.0f&&std::abs(b-v.z)<=1.25f;
                if(!av&&!bv)return false;
                floor=av&&(!bv||std::abs(a-v.z)<=std::abs(b-v.z))?a:b;return true;
            };
            auto corridor=[&](Sample const&a,Sample const&b){return
                p->GetMap()->isInLineOfSight(a.x,a.y,a.z+.6f,b.x,b.y,b.z+.6f,p->GetPhases())&&
                p->GetMap()->isInLineOfSight(a.x,a.y,a.z+1.8f,b.x,b.y,b.z+1.8f,p->GetPhases());};
            for(unsigned attempt=0;attempt<64;++attempt){
                Point goal=FelsoriaGilneas::ReadyPoint(unsigned(m_TeamID),unsigned(m_ready.size())+attempt,m_pBattleground->GetInstanceID()*131u);
                bool crowded=false;for(auto const&spot:m_ready)if(FelsoriaGilneas::Distance(goal,spot.second)<1.6f){crowded=true;break;}
                if(crowded)continue;
                std::vector<Sample>local;char const*reason="none";
                if(!FelsoriaGilneasGround::BuildLocal({center.x,center.y,center.z},{goal.x,goal.y,goal.z},floorProbe,corridor,local,reason)||local.empty())continue;
                auto const&last=local.back();Point projected{last.x,last.y,last.z};
                crowded=false;for(auto const&spot:m_ready)if(FelsoriaGilneas::Distance(projected,spot.second)<1.6f){crowded=true;break;}
                if(crowded)continue;
                placement.goal=projected;placement.chosen=true;
                m_ready[p->GetGUIDLow()]=placement.goal;m_readyPaths[p->GetGUIDLow()].clear();break;
            }
            if(!placement.chosen){placement.reason="start_no_ground_spot";return false;}
        }
        if(auto*ai=dynamic_cast<BotBGAI*>(p->GetAI())){ai->Dismount();ai->GetAIMovement()->ClearMovement();}
        auto const&goal=placement.goal;
        // Explicit start placement requested for Horde bots only. SafeTeleport
        // performs the native transport detach; this is not route recovery.
        if(!p->SafeTeleport(761,goal.x,goal.y,goal.z,FelsoriaGilneas::StartFacing[m_TeamID])){
            placement.reason="start_teleport_failed";return false;
        }
        if(p->IsBeingTeleportedNear())p->GetSession()->HandleBotMoveTeleportAck();
        if(p->IsBeingTeleported()){placement.reason="start_ack_pending";return false;}
        placement.done=FelsoriaGilneas::Distance(Location(p),goal)<2.0f&&p->GetTransport()==nullptr;
        placement.reason=placement.done?"placed_on_land":"start_not_arrived";
        return placement.done;
    }
    void BeginRecovery(Player*p,bool revived){
        if(!Enabled()||!p||!p->IsPlayerBot()||!p->isAlive()||!p->IsInWorld()||p->GetMapId()!=761||
            p->GetBattleground()!=m_pBattleground||p->GetBGTeamId()!=m_TeamID||m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS)return;
        auto*ai=dynamic_cast<BotBGAI*>(p->GetAI());if(!ai)return;
        uint64 guid=p->GetGUIDLow();uint32 now=getMSTime();auto&recovery=m_recovery[guid];
        if(!revived&&(recovery.active||(recovery.retryAt&&int32(recovery.retryAt-now)>0)))return;
        if(revived){m_traceCount[guid]=0;ai->GetAIMovement()->ResetBFGAfterResurrection();}
        auto task=m_tasks.find(guid);if(task!=m_tasks.end()){Cancel(p,task->second);m_tasks.erase(task);}
        recovery=Recovery();recovery.grave=BG()->GetBFGGraveyardIndex(p);m_planAt=0;
        recovery.recorded=FelsoriaGilneasExits::HasRoute(recovery.grave);
        if(recovery.grave>=5||(!recovery.recorded&&FelsoriaGilneasRecovery::OnRoad(Location(p),unsigned(m_TeamID)))){
            recovery.reason=recovery.grave>=5?"revived_outside_known_graveyard":"revived_on_road";
            if(revived)Trace(p,recovery.reason);return;
        }
        ai->GetAIMovement()->ResetBFGAfterResurrection();ai->Dismount();
        p->AttackStop();p->SetSelection(ObjectGuid::Empty);
        recovery.active=true;recovery.since=recovery.progressAt=now;recovery.progress=Location(p);recovery.reason="egress_planning";
        Trace(p,revived?"revived_egress_begin":"missed_revive_egress_begin",recovery.grave);
    }
    void AdvanceExitPlan(Player*p,ExitPlan&plan){
        auto*ai=dynamic_cast<BotBGAI*>(p->GetAI());if(!ai)return;auto*move=ai->GetAIMovement();uint32 now=getMSTime();
        if(plan.failedAt&&now-plan.failedAt>=12000)plan=ExitPlan();
        if(!plan.path.empty()||plan.failedAt||(plan.advanced&&now-plan.advanced<150))return;
        plan.advanced=now?now:1;
        if(!plan.begun){
            plan.begun=true;plan.origin=Location(p);float floor=0;
            if(move->ProbeBFGRecoveryFloor(plan.origin.x,plan.origin.y,plan.origin.z,floor)&&std::abs(floor-plan.origin.z)<=2.2f)plan.origin.z=floor+.03f;
            plan.targets=FelsoriaGilneasRecovery::Targets(plan.origin,unsigned(m_TeamID));
            for(auto const&point:plan.targets){
                bool separate=true;for(auto const&chosen:plan.nativeTargets)if(FelsoriaGilneasRecovery::Flat(point,chosen)<18){separate=false;break;}
                if(separate)plan.nativeTargets.push_back(point);if(plan.nativeTargets.size()>=10)break;
            }
            plan.search.Begin(plan.origin,plan.targets);
        }
        if(plan.candidate<plan.nativeTargets.size()){
            std::vector<G3D::Vector3>path;
            if(move->BuildBFGGroundLeg(Vector(plan.origin),Vector(plan.nativeTargets[plan.candidate++]),path)&&path.size()>=2){
                // Native winding exits may be much longer than the direct
                // distance. The complete mesh corridor remains authoritative.
                for(auto const&v:path)plan.path.push_back({{v.x,v.y,v.z},false});
                Trace(p,"egress_native_plan",unsigned(plan.path.size()));return;
            }
            Trace(p,"egress_native_reject",plan.candidate);return;
        }
        auto floor=[&](float x,float y,float fromZ,float&z){++plan.floorReads;bool valid=move->ProbeBFGRecoveryFloor(x,y,fromZ,z);if(!valid)++plan.floorRejected;return valid;};
        auto walk=[&](Point const&a,Point const&b){bool valid=move->CanBFGRecoveryWalk(a,b);if(!valid)++plan.walkRejected;return valid;};
        auto jump=[&](Point const&a,Point const&b){bool valid=move->CanBFGRecoveryJump(a,b);if(!valid)++plan.jumpRejected;return valid;};
        auto result=plan.search.Advance(floor,walk,jump,24);
        if(result==FelsoriaGilneasRecovery::Result::Found){plan.path=plan.search.Path();Trace(p,"egress_collision_plan",unsigned(plan.path.size()));}
        if(result==FelsoriaGilneasRecovery::Result::Failed){plan.failedAt=now?now:1;Trace(p,"egress_no_safe_exit",plan.search.Expanded());}
    }
    bool JoinExitPath(Player*p,std::vector<FelsoriaGilneasRecovery::Step>const&source,unsigned cursor,
        std::vector<FelsoriaGilneasRecovery::Step>&out){
        using namespace FelsoriaGilneasRecovery;
        auto*ai=dynamic_cast<BotBGAI*>(p->GetAI());if(!ai||cursor>=source.size())return false;
        auto*move=ai->GetAIMovement();Point current=Location(p);
        if(source[cursor].jump){out=source;return true;}
        unsigned stop=cursor;while(stop+1<source.size()&&!source[stop+1].jump)++stop;
        std::vector<std::pair<float,unsigned>>candidates;
        for(unsigned i=cursor;i<=stop;++i)candidates.emplace_back(FelsoriaGilneas::Distance(current,source[i].point),i);
        std::sort(candidates.begin(),candidates.end());
        for(unsigned attempt=0;attempt<candidates.size()&&attempt<5;++attempt){
            unsigned first=candidates[attempt].second;Point goal=source[first].point;
            std::vector<Step>joined;joined.push_back({current,false});
            if(FelsoriaGilneas::Distance(current,goal)>.35f){
                std::vector<G3D::Vector3>leg;
                if(move->CanBFGRecoveryWalk(current,goal))joined.push_back({goal,false});
                else if(move->BuildBFGGroundLeg(Vector(current),Vector(goal),leg)){
                    float length=0;for(unsigned i=1;i<leg.size();++i)length+=(leg[i]-leg[i-1]).length();
                    if(leg.size()<2||length>std::max(18.0f,candidates[attempt].first*2.2f+5.0f))continue;
                    for(auto const&v:leg)if(FelsoriaGilneas::Distance(joined.back().point,{v.x,v.y,v.z})>.2f)joined.push_back({{v.x,v.y,v.z},false});
                }else if(move->CanBFGRecoveryJump(current,goal))joined.push_back({goal,true});
                else continue;
            }
            // Retain the exact takeoff even when the bot is already close.
            // Dropping it can make the following jump start too early.
            if(FelsoriaGilneas::Distance(joined.back().point,goal)>.01f)joined.push_back({goal,false});
            joined.insert(joined.end(),source.begin()+first+1,source.end());out=std::move(joined);return true;
        }
        return false;
    }
    bool RecoveryTick(Player*p){
        auto it=m_recovery.find(p->GetGUIDLow());if(it==m_recovery.end()||!it->second.active)return false;
        Recovery&r=it->second;auto*ai=dynamic_cast<BotBGAI*>(p->GetAI());if(!ai)return true;auto*move=ai->GetAIMovement();
        uint32 now=getMSTime();
        auto finish=[&](char const*reason,bool complete){
            r.active=false;r.reason=reason;r.retryAt=complete?0:now+15000;
            if(!move->IsBFGJumpInProgress())move->ClearMovement();
            m_tasks.erase(p->GetGUIDLow());m_planAt=0;Trace(p,reason,r.failures);
        };
        if(!move->IsBFGJumpInProgress()){
            // Legitimate roots/casts/falls must not consume the stuck timer or
            // trigger a movement reset while the character cannot move.
            if(p->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||
                p->HasAuraType(SPELL_AURA_MOD_ROOT)||p->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR)){
                r.since=r.progressAt=now;r.reason="egress_wait_cc_or_landing";return true;
            }
            std::list<Player*>nearby;p->GetPlayerListInGrid(nearby,40);Player*attacker=nullptr;float closest=1e20f;
            for(Player*q:nearby)if(Visible(p,q)&&p->IsValidAttackTarget(q)&&std::abs(q->GetPositionZ()-p->GetPositionZ())<8&&
                (q->GetVictim()==p||(p->IsInCombat()&&(q==p->GetSelectedUnit()||p->GetDistance(q)<12)))){
                float d=p->GetDistance(q);if(d<closest){closest=d;attacker=q;}
            }
            if(attacker){
                if(!r.fighting){move->ClearMovement();ai->Dismount();Trace(p,"egress_self_defence");}
                r.fighting=true;r.since=r.progressAt=now;r.reason="egress_self_defence";
                if(p->GetSelectedUnit()!=attacker){p->AttackStop();p->SetSelection(attacker->GetGUID());}
                return false;
            }
            if(r.fighting){
                r.fighting=false;p->AttackStop();p->SetSelection(ObjectGuid::Empty);move->ClearMovement();
                r.cursor=FelsoriaGilneasRecovery::WalkingProgress(r.path,r.cursor,Location(p));
                if(r.cursor<r.path.size()&&!r.path[r.cursor].jump){
                    std::vector<FelsoriaGilneasRecovery::Step>joined;
                    if(JoinExitPath(p,r.path,r.cursor,joined)){r.path=std::move(joined);r.cursor=1;}
                }
                r.since=r.progressAt=now;Trace(p,"egress_resume_after_combat");
            }
        }
        if(r.grave>=3&&r.grave<5&&r.path.empty()){
            auto settled=move->SettleBFGStartRecoveryFloor();
            if(settled!=FelsoriaGilneasRecovery::MoveResult::Finished){
                r.reason=move->GetBFGMovementReason();
                if(!r.reportedAt||now-r.reportedAt>=3000){r.reportedAt=now?now:1;Trace(p,"start_floor_recovery",r.grave);}
                if(settled==FelsoriaGilneasRecovery::MoveResult::Running){r.progressAt=now;r.progress=Location(p);}
                else if(now-r.since>90000)finish("egress_retry_later",false);
                return true;
            }
        }
        if(!r.recorded&&!move->IsBFGJumpInProgress()&&FelsoriaGilneasRecovery::OnRoad(Location(p),unsigned(m_TeamID))){finish("egress_rejoined_road",true);return true;}
        if(FelsoriaGilneas::Distance(Location(p),r.progress)>.8f){r.progress=Location(p);r.progressAt=now;}
        if(now-r.since>90000){finish("egress_retry_later",false);return true;}
        auto&plan=m_exits[ExitKey(r.grave,p->GetPhases())];
        if(r.path.empty()){
            if(r.recorded){
                auto recorded=FelsoriaGilneasExits::Path(r.grave);
                if(JoinExitPath(p,recorded,0,r.path)){
                    r.cursor=1;r.progressAt=now;r.reason="egress_recorded_following";Trace(p,r.reason,unsigned(r.path.size()));
                }else{
                    r.reason="egress_recorded_start_join";
                    if(now-r.progressAt>6000){r.progressAt=now;++r.failures;Trace(p,r.reason,r.failures);}
                    if(r.failures<3)return true;
                    r.recorded=false;r.failures=0;plan=ExitPlan();Trace(p,"egress_recorded_fallback");
                }
            }
        }
        if(r.path.empty()){
            AdvanceExitPlan(p,plan);
            if(plan.path.empty()){r.reason=plan.failedAt?"egress_no_safe_exit":"egress_planning";return true;}
            Point current=Location(p);std::vector<FelsoriaGilneasRecovery::Step>path;
            path.push_back({current,false});auto first=plan.path.front().point;
            if(FelsoriaGilneas::Distance(current,first)>.5f){
                std::vector<G3D::Vector3>join;
                if(move->CanBFGRecoveryWalk(current,first))path.push_back({first,false});
                else if(move->BuildBFGGroundLeg(Vector(current),Vector(first),join)){
                    for(auto const&v:join)path.push_back({{v.x,v.y,v.z},false});
                }else if(move->CanBFGRecoveryJump(current,first))path.push_back({first,true});
                else {r.reason="egress_wait_start_join";return true;}
            }
            for(unsigned i=1;i<plan.path.size();++i)path.push_back(plan.path[i]);
            r.path=std::move(path);r.cursor=1;r.progressAt=now;r.reason="egress_following";Trace(p,r.reason,unsigned(r.path.size()));
        }
        auto result=move->MoveBFGEgressPath(r.path,r.cursor);r.reason=move->GetBFGMovementReason();
        if(!r.reportedAt||now-r.reportedAt>=8000){r.reportedAt=now?now:1;Trace(p,"egress_progress",r.cursor);}
        if(result==FelsoriaGilneasRecovery::MoveResult::Finished){finish("egress_complete",true);return true;}
        if(result==FelsoriaGilneasRecovery::MoveResult::Rejected||now-r.progressAt>6000){
            Trace(p,"egress_replan",r.cursor);++r.failures;r.path.clear();r.cursor=0;r.progressAt=now;
            move->ResetBFGAfterResurrection();
            if(r.failures>=3){
                plan=ExitPlan();
                if(r.recorded){r.recorded=false;r.failures=0;Trace(p,"egress_recorded_fallback");}
                else finish("egress_retry_later",false);
            }
        }
        return true;
    }
    void SyncRoster(){
        for(Player*p:TeamPlayers(true)){
            m_PlayerGUIDs.emplace(p->GetGUIDLow(),false);
            if(p->isAlive()&&p->GetMapId()==761&&p->IsBeingTeleportedNear()&&!p->IsBeingTeleportedFar()&&p->m_mover==p){
                p->GetSession()->HandleBotMoveTeleportAck();Trace(p,"revive_near_port_ack");
            }
            auto life=m_life.find(p->GetGUIDLow());
            if(life!=m_life.end()&&!life->second&&p->isAlive())BeginRecovery(p,true);
            m_life[p->GetGUIDLow()]=p->isAlive();
            EnsureStartPlacement(p);
            if(dynamic_cast<BotBGAI*>(p->GetAI())&&p->IsSettingFinish()&&!p->IsBeingTeleported())p->IsAIEnabled=true;
        }
    }
    void Audit(){
        if(!m_pBattleground)return;
        unsigned phase=unsigned(m_pBattleground->GetStatus());uint32 now=getMSTime();
        if(phase!=m_auditPhase){m_auditPhase=phase;m_auditAt=0;m_auditCount=0;}
        // Bounded snapshots per phase, fifteen seconds apart, in a separate file.
        // This never changes the general logger level or floods the console.
        unsigned window=0;auto remaining=m_pBattleground->GetStartDelayTime().count();
        if(m_pBattleground->GetStatus()==STATUS_WAIT_JOIN&&remaining>0){
            if(remaining<=20000)window|=1;if(remaining<=10000)window|=2;if(remaining<=5000)window|=4;if(remaining<=1000)window|=8;
        }
        bool critical=(window&~m_prepAuditMask)!=0;m_prepAuditMask|=window;
        if(!critical&&(m_auditCount>=(m_pBattleground->GetStatus()==STATUS_IN_PROGRESS?10u:4u)||(m_auditAt&&now-m_auditAt<15000)))return;
        m_auditAt=now?now:1;if(!critical)++m_auditCount;
        auto bots=TeamPlayers(true);unsigned active=0,arrived=0;
        for(Player*p:bots){
            if(dynamic_cast<BotBGAI*>(p->GetAI()))++active;
            auto ready=m_ready.find(p->GetGUIDLow());
            if(ready!=m_ready.end()&&FelsoriaGilneas::Distance(Location(p),ready->second)<1.1f)++arrived;
        }
        std::ostringstream buffer;
        buffer<<"V2.6 ms="<<now<<" instance="<<m_pBattleground->GetInstanceID()<<" team="<<unsigned(m_TeamID)
            <<" phase="<<phase<<" enabled="<<unsigned(Enabled())<<" bots="<<bots.size()<<" bg_ai="<<active
            <<" registered="<<m_PlayerGUIDs.size()<<" ready_chosen="<<m_ready.size()<<" ready_arrived="<<arrived
            <<" urgent="<<unsigned(m_strategy.urgent)<<" primary="<<m_strategy.primary<<" owned="<<m_strategy.owned<<" enemy_owned="<<m_strategy.enemyOwned
            <<" quota="<<m_strategy.quota[0]<<','<<m_strategy.quota[1]<<','<<m_strategy.quota[2]
            <<" score="<<m_pBattleground->GetTeamScore(m_TeamID)<<" enemy_score="<<m_pBattleground->GetTeamScore(TeamId(1-unsigned(m_TeamID)))
            <<" orders="<<m_tasks.size()<<" remaining_ms="<<m_pBattleground->GetStartDelayTime().count()<<'\n';
        for(Player*p:bots){
            auto*ai=dynamic_cast<BotBGAI*>(p->GetAI());auto task=m_tasks.find(p->GetGUIDLow());
            buffer<<" guid="<<p->GetGUIDLow()<<" ai_enabled="<<unsigned(p->IsAIEnabled)<<" alive="<<unsigned(p->isAlive())
                <<" casting="<<unsigned(p->HasUnitState(UNIT_STATE_CASTING))<<" root="<<unsigned(p->HasAuraType(SPELL_AURA_MOD_ROOT))
                <<" teleport="<<unsigned(p->IsBeingTeleported())<<" speed="<<p->GetSpeed(MOVE_RUN)
                <<" node="<<(task==m_tasks.end()?3u:task->second.goal)
                <<" path_points="<<(task==m_tasks.end()?0u:unsigned(task->second.path.size()))
                <<" ai_tick_age="<<(m_aiTicks.count(p->GetGUIDLow())?int64(now-m_aiTicks[p->GetGUIDLow()]):-1)
                <<" ai_gate="<<(m_aiGate.count(p->GetGUIDLow())?int(m_aiGate[p->GetGUIDLow()]):-1)
                <<" order_tick_age="<<(m_orderTicks.count(p->GetGUIDLow())?int64(now-m_orderTicks[p->GetGUIDLow()]):-1)
                <<" start="<<(m_startPlacement.count(p->GetGUIDLow())?m_startPlacement[p->GetGUIDLow()].reason:"not_seen")
                <<" buffs_done="<<unsigned(ai&&ai->GilneasPreparationDone())<<" mounted="<<unsigned(p->IsMounted())
                <<" mount_reason="<<(m_mountAudit.count(p->GetGUIDLow())?m_mountAudit[p->GetGUIDLow()].reason:"not_called")
                <<" mount_spell="<<(m_mountAudit.count(p->GetGUIDLow())?m_mountAudit[p->GetGUIDLow()].spell:0u)
                <<" mount_result="<<(m_mountAudit.count(p->GetGUIDLow())?m_mountAudit[p->GetGUIDLow()].result:0u)
                <<" capture="<<(task==m_tasks.end()?"none":task->second.capReason)
                <<" cap_ms="<<(task==m_tasks.end()||!task->second.capAt?0u:now-task->second.capAt)
                <<" node_owner="<<(task==m_tasks.end()?9u:unsigned(BG()->GetBFGNodeTeam(uint8(task->second.goal))))
                <<" node_status="<<(task==m_tasks.end()?9u:unsigned(BG()->GetBFGNodeStatus(uint8(task->second.goal))))
                <<" node_timer="<<(task==m_tasks.end()?0u:BG()->GetBFGNodeRemaining(uint8(task->second.goal)))
                <<" movement="<<(ai?ai->GetAIMovement()->GetBFGMovementReason():"no_bg_ai")
                <<" recovery="<<(m_recovery.count(p->GetGUIDLow())?m_recovery[p->GetGUIDLow()].reason:"none")
                <<" recovery_cursor="<<(m_recovery.count(p->GetGUIDLow())?m_recovery[p->GetGUIDLow()].cursor:0u)
                <<" recovery_steps="<<(m_recovery.count(p->GetGUIDLow())?unsigned(m_recovery[p->GetGUIDLow()].path.size()):0u)
                <<" battle="<<(task==m_tasks.end()?3u:task->second.battle)
                <<" x="<<p->GetPositionX()<<" y="<<p->GetPositionY()<<" z="<<p->GetPositionZ()<<'\n';
        }
        std::string name="Felsoria-Gilneas-Movement-V2.6.log";
        std::string directory=sConfigMgr->GetStringDefault("LogsDir","");
        if(!directory.empty()&&directory.back()!='/'&&directory.back()!='\\')directory+='/';
        static std::mutex fileMutex;std::lock_guard<std::mutex>lock(fileMutex);
        std::string path=directory+name;std::ofstream file(path,std::ios::app|std::ios::ate);
        if(!file){path=name;file.clear();file.open(path,std::ios::app|std::ios::ate);}
        if(file&&file.tellp()>std::streamoff(1024*1024)){file.close();file.open(path,std::ios::trunc);}
        if(file)file<<buffer.str();
    }
    void Cancel(Player*p,Task&t){
        if(!t.capAt)return;
        if(t.goal<3&&m_cappers[t.goal]==p->GetGUIDLow()){m_cappers[t.goal]=0;m_capUntil[t.goal]=0;}
        if(p->GetUInt32Value(UNIT_FIELD_EMOTE_STATE)==EMOTE_STATE_USE_STANDING)p->SetUInt32Value(UNIT_FIELD_EMOTE_STATE,EMOTE_STATE_NONE);
        t.capAt=0;t.capTick=0;
    }
    bool Capture(Player*p,Task&t){
        auto*bg=BG();if(!bg||t.goal>=3)return false;
        auto reject=[&](char const*reason){Cancel(p,t);t.capReason=reason;return false;};
        GameObject*flag=bg->GetBFGFlag(uint8(t.goal));uint32 now=getMSTime();
        if(!flag||!flag->isSpawned())return reject("flag_missing_or_hidden");
        if(bg->GetBFGNodeTeam(uint8(t.goal))==m_TeamID)return reject("already_ours");
        if(!p->IsWithinDistInMap(flag,5.0f))return reject("flag_out_of_range");
        if(!p->InSamePhase(flag)||!p->InSamePhaseId(flag))return reject("flag_phase");
        if(p->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR))return reject("falling");
        if(p->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||p->HasAuraType(SPELL_AURA_MOD_ROOT))return reject("cast_or_cc");
        p->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);p->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
        if(p->IsMounted())p->RemoveAurasByType(SPELL_AURA_MOUNTED);
        if(p->GetVehicleBase()||!p->CanUseBattlegroundObject())return reject("cannot_use_flag");
        if(t.capAt){
            if(now-t.capTick>2500||p->GetHealth()<t.health||p->GetDistance(t.capOrigin)>.8f){
                Cancel(p,t);t.retryAt=now;t.capReason="channel_interrupted";return false;
            }
            t.capTick=now;t.health=p->GetHealth();m_capUntil[t.goal]=now+3000;
            if(now-t.capAt<8000){t.capReason="channeling";return true;}
            Cancel(p,t);t.retryAt=now;
            bool accepted=bg->CompleteBFGBotCapture(p,uint8(t.goal));
            t.capReason=accepted?"native_accepted":"native_rejected";
            if(Debug())TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_BFG_CAPTURE instance=%u bot=%s node=%u changed=%u",
                p->GetInstanceId(),p->GetName(),t.goal,unsigned(accepted));
            return true;
        }
        if(t.retryAt&&now-t.retryAt<2000){t.capReason="capture_retry";return false;}
        if(m_cappers[t.goal]&&m_cappers[t.goal]!=p->GetGUIDLow()&&int32(m_capUntil[t.goal]-now)>0){t.capReason="covered_by_capper";return false;}
        auto*ai=dynamic_cast<BotBGAI*>(p->GetAI());if(!ai)return reject("no_bg_ai");
        ai->Dismount();ai->GetAIMovement()->ClearMovement();p->AttackStop();p->SetSelection(ObjectGuid::Empty);p->SetFacingToObject(flag);
        p->SetUInt32Value(UNIT_FIELD_EMOTE_STATE,EMOTE_STATE_USE_STANDING);
        t.capAt=now?now:1;t.capTick=now;t.health=p->GetHealth();t.capOrigin=p->GetPosition();t.path.clear();t.capReason="channeling";
        m_cappers[t.goal]=p->GetGUIDLow();m_capUntil[t.goal]=now+3000;return true;
    }
    bool LocalOpponent(Player*p)const{
        std::list<Player*>nearby;p->GetPlayerListInGrid(nearby,45);
        for(Player*q:nearby)if(Visible(p,q)&&p->IsValidAttackTarget(q)&&
            (q==p->GetSelectedUnit()||q->GetVictim()==p||p->GetVictim()==q||p->GetDistance(q)<15))return true;
        return false;
    }
    void Plan(){
        auto*bg=BG();if(!bg)return;uint32 now=getMSTime();unsigned assigned[3]={};
        std::vector<Player*>bots;
        for(Player*p:TeamPlayers(true))
            if(p->isAlive()&&!p->IsBeingTeleported()&&dynamic_cast<BotBGAI*>(p->GetAI())&&
                (!m_recovery.count(p->GetGUIDLow())||!m_recovery[p->GetGUIDLow()].active))bots.push_back(p);
        std::sort(bots.begin(),bots.end(),[](Player*a,Player*b){return a->GetGUIDLow()<b->GetGUIDLow();});
        for(auto it=m_tasks.begin();it!=m_tasks.end();){
            if(std::none_of(bots.begin(),bots.end(),[&](Player*p){return p->GetGUIDLow()==it->first;}))it=m_tasks.erase(it);else ++it;
        }
        FelsoriaGilneasStrategy::State state;state.home=m_TeamID==TEAM_ALLIANCE?0u:2u;
        state.score=bg->GetTeamScore(m_TeamID);state.enemyScore=bg->GetTeamScore(TeamId(1-unsigned(m_TeamID)));state.maxScore=bg->GetMaxScore();
        for(unsigned count=1;count<=3;++count)state.secondsPerPoint[count]=float(BgBFGTickIntervals[count].count())/1000.0f/float(BgBFGTickPoints[count]);
        auto teammates=TeamPlayers(false);std::set<uint64>observed;std::vector<Player*>enemies;
        for(Player*scout:teammates)if(scout->isAlive()){
            std::list<Player*>nearby;scout->GetPlayerListInGrid(nearby,55);
            for(Player*q:nearby)if(Visible(scout,q)&&scout->IsValidAttackTarget(q)&&observed.insert(q->GetGUIDLow()).second)enemies.push_back(q);
        }
        for(unsigned node=0;node<3;++node){
            auto&view=state.nodes[node];Point flag=FlagPoint(node);
            view.ours=bg->GetBFGNodeTeam(uint8(node))==m_TeamID;
            view.captured=bg->GetBFGNodeStatus(uint8(node))==NODE_STATUS_CAPTURE;
            view.neutral=bg->GetBFGNodeStatus(uint8(node))==NODE_STATUS_NEUTRAL;
            for(Player*q:enemies)if(FelsoriaGilneasCombat::Flat(Location(q),flag)<45&&std::abs(q->GetPositionZ()-flag.z)<8)++view.enemies;
            for(Player*q:teammates)if(!q->IsPlayerBot()&&q->isAlive()&&FelsoriaGilneasCombat::Flat(Location(q),flag)<28&&std::abs(q->GetPositionZ()-flag.z)<6)++view.humans;
            for(Player*p:bots)view.travel+=FelsoriaGilneas::Distance(Location(p),flag);
            if(!bots.empty())view.travel/=float(bots.size());
        }
        m_strategy=FelsoriaGilneasStrategy::Allocate(state,unsigned(bots.size()),m_strategy.primary);
        unsigned signature=unsigned(m_strategy.urgent)*31u+m_strategy.primary;
        for(unsigned node=0;node<3;++node)signature=signature*37u+m_strategy.quota[node]*5u+unsigned(state.nodes[node].ours);
        if(signature!=m_strategySignature){m_strategySignature=signature;if(!bots.empty())Trace(bots.front(),"strategy_rebalance",m_strategy.primary);}
        std::set<uint64>retained;
        // A real fight or flag channel finishes. Combat flags without an actual
        // nearby opponent cannot lock five idle bots into their old defence.
        for(Player*p:bots){auto it=m_tasks.find(p->GetGUIDLow());if(it==m_tasks.end())continue;Task&t=it->second;
            if(t.goal<3&&(t.capAt||(t.battle<3&&t.combat&&p->IsInCombat()&&LocalOpponent(p)))){++assigned[t.goal];retained.insert(p->GetGUIDLow());}
        }
        for(Player*p:bots){if(retained.count(p->GetGUIDLow()))continue;
            auto it=m_tasks.find(p->GetGUIDLow());if(it==m_tasks.end())continue;Task&t=it->second;
            if(t.goal<3&&assigned[t.goal]<m_strategy.quota[t.goal]&&now-t.assigned<18000){++assigned[t.goal];retained.insert(p->GetGUIDLow());}
        }
        // Fill each remaining strategic slot with a nearby free bot. Excess
        // passive defence is reassigned immediately, even inside the old 18s hold.
        for(unsigned pass=0;pass<3;++pass){
            unsigned node=pass;
            if(m_strategy.primary<3){node=pass==0?m_strategy.primary:pass<=m_strategy.primary?pass-1:pass;}
            while(assigned[node]<m_strategy.quota[node]){
                Player*chosen=nullptr;float best=1e20f;Point flag=FlagPoint(node);
                for(Player*p:bots)if(!retained.count(p->GetGUIDLow())){
                    float score=FelsoriaGilneas::Distance(Location(p),flag);
                    auto it=m_tasks.find(p->GetGUIDLow());if(it!=m_tasks.end()&&it->second.goal==node)score-=12;
                    if(score<best){best=score;chosen=p;}
                }
                if(!chosen)break;
                Task&t=m_tasks[chosen->GetGUIDLow()];
                if(t.goal!=node){Cancel(chosen,t);t=Task();t.goal=node;Trace(chosen,"strategy_assignment",node);}
                t.assigned=now;++assigned[node];retained.insert(chosen->GetGUIDLow());
            }
        }
        // Locked combat can exceed one quota. Keep all remaining free bots in
        // the attack squad rather than abandoning the planner without an order.
        for(Player*p:bots)if(!retained.count(p->GetGUIDLow())){
            unsigned node=m_strategy.primary<3?m_strategy.primary:unsigned(std::min_element(assigned,assigned+3)-assigned);
            Task&t=m_tasks[p->GetGUIDLow()];if(t.goal!=node){Cancel(p,t);t=Task();t.goal=node;}
            t.assigned=now;++assigned[node];
        }
    }
    void Risks(Player*p,std::vector<float>&risk){
        risk.assign(FelsoriaGilneas::PointCount,0.0f);
        // Shared observations from actual teammates, with normal visibility.
        std::set<uint64>seen;std::vector<Point>enemies;
        for(Player*scout:TeamPlayers(false)){
            if(!scout->IsInWorld()||!scout->isAlive()||scout->GetMap()!=p->GetMap())continue;
            std::list<Player*>nearbyPlayers;scout->GetPlayerListInGrid(nearbyPlayers,55.0f);
            for(Player*enemy:nearbyPlayers)if(Visible(scout,enemy)&&scout->IsValidAttackTarget(enemy)&&seen.insert(enemy->GetGUIDLow()).second)enemies.push_back(Location(enemy));
        }
        for(unsigned i=0;i<risk.size();++i)for(auto const&e:enemies){
            float d=FelsoriaGilneas::Distance(FelsoriaGilneas::Points[i],e);
            if(d<45)risk[i]+=2.5f*(1.0f-d/45.0f);
        }
    }
    bool BuildTravel(Player*p,Task&t){
        using namespace FelsoriaGilneas;
        auto*ai=dynamic_cast<BotBGAI*>(p->GetAI());if(!ai||t.goal>=3)return false;auto*move=ai->GetAIMovement();
        if(t.blockedSince&&getMSTime()-t.blockedSince>20000){t.blocked.clear();t.blockedSince=0;}
        std::vector<std::pair<float,unsigned>>candidates;
        Point pos=Location(p);
        for(unsigned i=0;i<PointCount;++i){float d=Distance(pos,Points[i]);
            if(!FelsoriaGilneasWater::Interior(i)&&d<140.0f&&Distance(Points[i],Starts[1-unsigned(m_TeamID)])>=40.0f)candidates.emplace_back(d,i);}
        std::sort(candidates.begin(),candidates.end());
        std::vector<float>risk;Risks(p,risk);float weight[9];
        for(unsigned r=0;r<9;++r){
            unsigned users=0;for(auto const&entry:m_tasks)if(entry.first!=p->GetGUIDLow()&&entry.second.route==r)++users;
            weight[r]=1.0f+float(users)*.12f+float(Noise(p->GetGUIDLow()+r*101)%100)*.001f;
        }
        float best=1e30f;std::vector<G3D::Vector3>chosen;unsigned route=9;
        for(unsigned c=0;c<candidates.size()&&c<16;++c){
            unsigned first=candidates[c].second;std::vector<G3D::Vector3>entry;
            if(!move->BuildBFGGroundLeg(p->GetVector3(),Vector(Points[first]),entry))continue;
            float length=0;for(unsigned i=1;i<entry.size();++i)length+=(entry[i]-entry[i-1]).length();
            if(length>std::max(18.0f,candidates[c].first*2.2f+5.0f))continue;
            // A rejected short junction must trigger another graph search,
            // rather than forcing the rest of the bot's recorded flag route.
            for(unsigned attempt=0;attempt<4;++attempt){
                std::vector<unsigned>nodes;float cost=0;
                if(!FindPath(first,Goals[t.goal],unsigned(m_TeamID),risk,weight,t.blocked,nodes,&cost))break;
                if(cost+length>=best)break;
                std::vector<G3D::Vector3>path=entry;
                bool valid=true;unsigned firstRoute=9;float routeLength[9]={};
                auto append=[&](G3D::Vector3 const&v){if(path.empty()||(path.back()-v).squaredLength()>=.0625f)path.push_back(v);};
                for(unsigned i=1;i<nodes.size();++i){
                    unsigned er=EdgeRoute(nodes[i-1],nodes[i]);
                    if(er<9){routeLength[er]+=Distance(Points[nodes[i-1]],Points[nodes[i]]);append(Vector(Points[nodes[i]]));}
                    else {
                        std::vector<G3D::Vector3>join;
                        if(!move->BuildBFGGroundLeg(Vector(Points[nodes[i-1]]),Vector(Points[nodes[i]]),join)){
                            t.blocked.insert({nodes[i-1],nodes[i]});if(!t.blockedSince)t.blockedSince=getMSTime();valid=false;break;}
                        float joinLength=0;for(unsigned k=1;k<join.size();++k)joinLength+=(join[k]-join[k-1]).length();
                        if(joinLength>10.0f){t.blocked.insert({nodes[i-1],nodes[i]});if(!t.blockedSince)t.blockedSince=getMSTime();valid=false;break;}
                        for(auto const&v:join)append(v);
                    }
                }
                if(!valid)continue;
                if(path.size()<2)break;
                float longest=0;for(unsigned r=0;r<9;++r)if(routeLength[r]>longest){longest=routeLength[r];firstRoute=r;}
                best=cost+length;chosen=std::move(path);route=firstRoute;break;
            }
        }
        if(chosen.empty())return false;
        std::vector<Point>center;for(auto const&v:chosen)center.push_back({v.x,v.y,v.z});
        auto probe=[&](Point const&previous,Point const&original,Point const&next,Point&candidate){
            float floor=p->GetMap()->GetHeight(p->GetPhases(),candidate.x,candidate.y,original.z+3.0f,true,6.0f);
            if(!std::isfinite(floor)||floor<=-99999.0f||std::abs(floor-original.z)>1.3f)return false;
            candidate.z=floor+.03f;
            return move->CanBFGRecoveryWalk(original,candidate)&&move->CanBFGRecoveryWalk(previous,candidate)&&move->CanBFGRecoveryWalk(candidate,next);
        };
        auto spread=FelsoriaGilneasTravel::Spread(center,p->GetGUIDLow()+p->GetInstanceId()*131u,probe);
        chosen.clear();for(auto const&v:spread)chosen.push_back(Vector(v));
        t.path=std::move(chosen);t.route=route;t.progress=p->GetPosition();t.progressAt=getMSTime();
        Trace(p,"route_goal_ready",t.goal);
        if(Debug())TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_BFG_ROUTE instance=%u bot=%s node=%u route=%u points=%u",
            p->GetInstanceId(),p->GetName(),t.goal,route,unsigned(t.path.size()));
        return true;
    }
public:
    bool HasCombatAnchor(Player*p)const{
        if(!p||!Enabled()||m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS)return false;
        auto it=m_tasks.find(p->GetGUIDLow());return it!=m_tasks.end()&&it->second.battle<3;
    }
    bool IsCombatTarget(Player*p,Unit*target)const{
        if(!p||!target)return false;
        if(!Enabled())return true;
        if(HasCombatAnchor(p)){
            auto const&t=m_tasks.find(p->GetGUIDLow())->second;
            return FelsoriaGilneasCombat::TargetAllowed(UnitLocation(target),FlagPoint(t.battle));
        }
        auto recovery=m_recovery.find(p->GetGUIDLow());
        if(recovery!=m_recovery.end()&&recovery->second.active&&recovery->second.fighting)
            return target==p->GetSelectedUnit();
        auto fight=m_transitCombat.find(p->GetGUIDLow());
        return fight!=m_transitCombat.end()&&FelsoriaGilneasCombat::TransitTarget(fight->second,Location(p),UnitLocation(target),
            getMSTime(),target->GetVictim()==p,target==p->GetSelectedUnit());
    }
    bool BoundCombatDestination(Player*p,float&x,float&y,float&z)const{
        if(!Enabled())return true;
        if(!HasCombatAnchor(p)){
            auto fight=p?m_transitCombat.find(p->GetGUIDLow()):m_transitCombat.end();
            if(fight==m_transitCombat.end()||!FelsoriaGilneasCombat::TransitActive(fight->second,Location(p),getMSTime()))return false;
            Point bound=FelsoriaGilneasCombat::BoundTransit({x,y,z},fight->second.origin);
            float floor=p->GetMap()->GetHeight(p->GetPhases(),bound.x,bound.y,fight->second.origin.z+5.0f,true,12.0f);
            if(!std::isfinite(floor)||floor<=-99999.0f||std::abs(floor-fight->second.origin.z)>5)return false;
            x=bound.x;y=bound.y;z=floor+.05f;return true;
        }
        auto const&t=m_tasks.find(p->GetGUIDLow())->second;Point flag=FlagPoint(t.battle);
        Point bound=FelsoriaGilneasCombat::Bound({x,y,z},flag);
        float floor=p->GetMap()->GetHeight(p->GetPhases(),bound.x,bound.y,flag.z+5.0f,true,12.0f);
        if(!std::isfinite(floor)||floor<=-99999.0f||std::abs(floor-flag.z)>5)return false;
        x=bound.x;y=bound.y;z=floor+.05f;return true;
    }
    bool CombatReposition(Player*p,Unit*target){
        if(!HasCombatAnchor(p))return false;
        Task&t=m_tasks.find(p->GetGUIDLow())->second;Point flag=FlagPoint(t.battle),current=Location(p);
        if(FelsoriaGilneasCombat::Inside(current,flag))return false;
        auto*ai=dynamic_cast<BotBGAI*>(p->GetAI());if(!ai)return false;auto*move=ai->GetAIMovement();
        if(move->IsBFGJumpInProgress())return true;
        if(p->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||p->HasAuraType(SPELL_AURA_MOD_ROOT))return false;
        uint32 now=getMSTime();if(t.repositionAt&&now-t.repositionAt<500)return true;
        bool first=!t.repositionAt;t.repositionAt=now?now:1;
        float angle=std::atan2(current.y-flag.y,current.x-flag.x);
        if(target&&FelsoriaGilneasCombat::TargetAllowed(UnitLocation(target),flag)){
            Point enemy=UnitLocation(target);angle=std::atan2(enemy.y-flag.y,enemy.x-flag.x);
        }
        for(float turn:{0.0f,.6f,-.6f,1.2f,-1.2f,3.14159265f}){
            float x=flag.x+21.0f*std::cos(angle+turn),y=flag.y+21.0f*std::sin(angle+turn),z=flag.z;
            if(!BoundCombatDestination(p,x,y,z))continue;
            std::vector<G3D::Vector3>path;if(!move->BuildBFGGroundLeg(p->GetVector3(),G3D::Vector3(x,y,z),path))continue;
            float limit=std::max(30.0f,FelsoriaGilneasCombat::Flat(current,flag)+2.0f);bool valid=true;
            for(auto const&v:path)if(FelsoriaGilneasCombat::Flat({v.x,v.y,v.z},flag)>limit||std::abs(v.z-flag.z)>7){valid=false;break;}
            if(!valid)continue;
            ai->Dismount();move->MoveBFGRecordedPath(path);if(first)Trace(p,"combat_return_to_flag",t.battle);return true;
        }
        if(first)Trace(p,"combat_return_path_rejected",t.battle);return true;
    }
    void ObserveMount(Player*p,char const*reason,unsigned spell,unsigned result){if(p)m_mountAudit[p->GetGUIDLow()]={reason,spell,result};}
    bool WaitingTick(Player*p){if(!Enabled()||!p||m_pBattleground->GetStatus()!=STATUS_WAIT_JOIN)return false;ReadyBot(p);return true;}
    bool IsCapturing(Player*p)const{if(!p)return false;auto it=m_tasks.find(p->GetGUIDLow());return it!=m_tasks.end()&&it->second.capAt!=0;}
    void ObserveAI(Player*p,bool gate){if(p){m_aiTicks[p->GetGUIDLow()]=getMSTime();m_aiGate[p->GetGUIDLow()]=gate;}}
    void DetachBattleground(){m_pBattleground=nullptr;m_tasks.clear();m_transitCombat.clear();m_PlayerGUIDs.clear();m_ready.clear();m_readyPaths.clear();m_startPlacement.clear();m_recovery.clear();m_exits.clear();m_life.clear();}
    CommandBFG(Battleground*bg,TeamId team):CommandBG(bg,team){}
    void Initialize()override{CommandBG::Initialize();m_tasks.clear();m_transitCombat.clear();m_mountAudit.clear();m_ready.clear();m_readyPaths.clear();m_readyRetry.clear();m_startPlacement.clear();m_planAt=0;m_strategySignature=~0u;m_strategy=FelsoriaGilneasStrategy::Allocation();m_auditAt=0;m_prepAuditMask=0;m_auditPhase=~0u;m_auditCount=0;m_aiTicks.clear();m_orderTicks.clear();m_aiGate.clear();
        m_recovery.clear();m_exits.clear();m_life.clear();m_traceCount.clear();
        for(unsigned i=0;i<3;++i){m_cappers[i]=0;m_capUntil[i]=0;}}
    void Update(uint32 diff)override{
        if(Enabled())SyncRoster();
        CommandBG::Update(diff);
        if(Enabled()){
            if(m_pBattleground->GetStatus()==STATUS_WAIT_JOIN)
                for(Player*p:TeamPlayers(true))ReadyBot(p);
            if(m_pBattleground->GetStatus()==STATUS_IN_PROGRESS){
                if(m_planAt>diff)m_planAt-=diff;else{m_planAt=2000;Plan();}
            }
        }
        Audit();
    }
    void OnPlayerDead(uint64 guid)override{CommandBG::OnPlayerDead(guid);m_life[guid]=false;m_recovery.erase(guid);m_transitCombat.erase(guid);
        auto it=m_tasks.find(guid);if(it!=m_tasks.end()){if(Player*p=FindBot(guid))Cancel(p,it->second);m_tasks.erase(it);}}
    void OnPlayerRevive(Player*p)override{if(p){m_life[p->GetGUIDLow()]=true;BeginRecovery(p,true);}m_planAt=0;}
    Creature const*GetMatchGraveyardNPC(Player const*p)override{return p&&m_pBattleground?m_pBattleground->GetClosestGraveCreature(p):nullptr;}
    void ReadyBot(Player*p){
        if(!Enabled()||!p||!p->isAlive()||p->GetBGTeamId()!=m_TeamID||m_pBattleground->GetStatus()!=STATUS_WAIT_JOIN)return;
        auto*ai=dynamic_cast<BotBGAI*>(p->GetAI());if(!ai)return;auto*move=ai->GetAIMovement();
        if(!EnsureStartPlacement(p))return;
        auto it=m_ready.find(p->GetGUIDLow());
        if(it==m_ready.end()){
            uint32 now=getMSTime();auto&retry=m_readyRetry[p->GetGUIDLow()];if(retry&&now-retry<1500)return;retry=now?now:1;
            std::vector<G3D::Vector3>path;
            for(unsigned attempt=0;attempt<64;++attempt){
                Point goal=FelsoriaGilneas::ReadyPoint(unsigned(m_TeamID),unsigned(m_ready.size())+attempt,m_pBattleground->GetInstanceID()*131u);
                bool crowded=false;for(auto const&v:m_ready)if(FelsoriaGilneas::Distance(goal,v.second)<1.6f){crowded=true;break;}
                if(crowded||!move->BuildBFGGroundLeg(p->GetVector3(),Vector(goal),path))continue;
                float length=0;for(unsigned i=1;i<path.size();++i)length+=(path[i]-path[i-1]).length();
                if(length>18.0f)continue;
                m_ready[p->GetGUIDLow()]=goal;m_readyPaths[p->GetGUIDLow()]=path;it=m_ready.find(p->GetGUIDLow());break;
            }
            if(it==m_ready.end())return;
        }
        if(FelsoriaGilneasPreparation::BuffWindow(true,m_pBattleground->GetStartDelayTime().count())&&
            FelsoriaGilneas::Distance(Location(p),FelsoriaGilneas::Starts[m_TeamID])<12.0f){
            ai->PrepareGilneasStart();return;
        }
        if(FelsoriaGilneas::Distance(Location(p),it->second)<1.0f){
            p->SetFacingTo(FelsoriaGilneas::StartFacing[m_TeamID]);ai->PrepareGilneasStart();return;
        }
        if(p->HasUnitState(UNIT_STATE_CASTING)||p->HasAuraType(SPELL_AURA_MOD_ROOT))return;
        auto&path=m_readyPaths[p->GetGUIDLow()];
        if(path.empty()||!move->MoveBFGRecordedPath(path)){
            if(move->BuildBFGGroundLeg(p->GetVector3(),Vector(it->second),path))move->MoveBFGRecordedPath(path);
        }
    }
    bool UpdateBot(Player*p){
        if(!Enabled()||!p||!p->IsInWorld()||!p->isAlive()||p->IsBeingTeleported()||p->GetBGTeamId()!=m_TeamID||
            p->GetBattleground()!=m_pBattleground||m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS)return false;
        if(!EnsureStartPlacement(p))return true;
        auto*ai=dynamic_cast<BotBGAI*>(p->GetAI());if(!ai)return false;uint32 now=getMSTime();m_orderTicks[p->GetGUIDLow()]=now;
        auto pendingExit=m_recovery.find(p->GetGUIDLow());
        if((pendingExit==m_recovery.end()||!pendingExit->second.active)&&BG()->GetBFGGraveyardIndex(p)<3)BeginRecovery(p,false);
        if(RecoveryTick(p))return true;
        auto recovery=m_recovery.find(p->GetGUIDLow());
        if(recovery!=m_recovery.end()&&recovery->second.active&&recovery->second.fighting)return false;
        auto it=m_tasks.find(p->GetGUIDLow());if(it==m_tasks.end()){Plan();it=m_tasks.find(p->GetGUIDLow());}if(it==m_tasks.end())return true;
        Task&t=it->second;auto*move=ai->GetAIMovement();
        if(move->IsBFGJumpInProgress()){
            if(!move->ContinueBFGWaterCrossing()&&std::string(move->GetBFGMovementReason()).find("water_reject")==0){
                for(unsigned i=FelsoriaGilneasWater::First;i<FelsoriaGilneasWater::Last;++i){t.blocked.insert({i,i+1});t.blocked.insert({i+1,i});}
                t.blockedSince=now;t.path.clear();
            }
            return true;
        }
        if(p->HasUnitState(UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||p->HasAuraType(SPELL_AURA_MOD_ROOT)){
            Cancel(p,t);return true;}
        auto*bg=BG();Point target=FelsoriaGilneas::Points[FelsoriaGilneas::Goals[t.goal]];
        float distance=FelsoriaGilneas::Distance(Location(p),target);
        std::list<Player*>nearbyPlayers;p->GetPlayerListInGrid(nearbyPlayers,45.0f);
        unsigned previousBattle=t.battle;bool keepBattle=false;
        if(t.battle<3){
            Point flag=FlagPoint(t.battle);
            for(Player*q:nearbyPlayers)if(Visible(p,q)&&p->IsValidAttackTarget(q)&&
                FelsoriaGilneasCombat::TargetAllowed(Location(q),flag)){keepBattle=true;break;}
            if(keepBattle)t.battleAt=now;
            else if(now-t.battleAt>6000)t.battle=3;
        }
        if(t.battle>=3){
            float closest=1e20f;
            for(Player*q:nearbyPlayers)if(Visible(p,q)&&p->IsValidAttackTarget(q))for(unsigned node=0;node<3;++node){
                Point flag=FlagPoint(node);
                if(!FelsoriaGilneasCombat::BaseBattle(Location(p),Location(q),flag))continue;
                float score=FelsoriaGilneasCombat::Flat(Location(p),flag)+FelsoriaGilneasCombat::Flat(Location(q),flag);
                if(score<closest){closest=score;t.battle=node;t.battleAt=now;}
            }
        }
        if(t.battle!=previousBattle){t.repositionAt=0;Trace(p,t.battle<3?"base_combat_begin":"base_combat_end",t.battle);}
        if(t.battle<3&&t.goal!=t.battle){Cancel(p,t);t.goal=t.battle;t.assigned=now;t.path.clear();}
        target=t.battle<3?FlagPoint(t.battle):FelsoriaGilneas::Points[FelsoriaGilneas::Goals[t.goal]];
        distance=FelsoriaGilneas::Distance(Location(p),target);
        if(t.battle>=3){
            bool threatened=false;
            for(Player*q:nearbyPlayers)if(Visible(p,q)&&p->IsValidAttackTarget(q)&&q->GetVictim()==p&&
                p->GetDistance(q)<=18&&std::abs(q->GetPositionZ()-p->GetPositionZ())<=6){threatened=true;break;}
            auto&fight=m_transitCombat[p->GetGUIDLow()];bool wasActive=fight.active;
            FelsoriaGilneasCombat::ObserveTransit(fight,Location(p),now,threatened);
            if(wasActive&&!fight.active){
                p->AttackStop();p->SetSelection(ObjectGuid::Empty);move->ClearMovement();t.path.clear();t.combat=false;
                Trace(p,"transit_resume_objective",t.goal);
            }
        }else m_transitCombat.erase(p->GetGUIDLow());
        Player*enemy=nullptr;float best=-1e20f;bool close=false;unsigned alliesFighting=0;
        for(Player*q:nearbyPlayers){if(!Visible(p,q))continue;
            if(p->IsValidAssistTarget(q)&&q->IsInCombat()&&FelsoriaGilneas::Distance(Location(q),target)<35)++alliesFighting;
            if(!p->IsValidAttackTarget(q)||!IsCombatTarget(p,q))continue;
            bool attacker=q->GetVictim()==p;
            if(p->GetDistance(q)<12)close=true;
            // TransitTarget permits only bounded direct self-defence. Passing
            // squads do not join a fight simply because an enemy is in combat.
            float score=100-p->GetDistance(q)+(attacker?35:0)+(q->GetSpecializationRole()==ROLES_HEALER?20:0);
            if(q->GetUInt32Value(UNIT_FIELD_EMOTE_STATE)==EMOTE_STATE_USE_STANDING)score+=90;
            if(t.battle<3)score+=std::max(0.0f,28.0f-FelsoriaGilneasCombat::Flat(Location(q),target));
            if(q==p->GetSelectedUnit())score+=15;if(score>best){best=score;enemy=q;}
        }
        if(!close&&(!enemy||alliesFighting>=2)&&p->GetHealthPct()>45&&Capture(p,t))return true;
        if(enemy){
            Cancel(p,t);if(!t.combat&&t.battle>=3)Trace(p,"field_combat_begin");t.combat=true;
            if(p->GetSelectedUnit()!=enemy){move->ClearMovement();p->AttackStop();p->SetSelection(enemy->GetGUID());}
            if(CombatReposition(p,enemy))return true;
            return false;
        }
        if(p->GetSelectedUnit()){p->AttackStop();p->SetSelection(ObjectGuid::Empty);t.combat=true;}
        if(Capture(p,t))return true;
        if(t.battle<3){
            t.combat=true;if(CombatReposition(p,nullptr))return true;
            if(p->movespline&&!p->movespline->Finalized())move->ClearMovement();
            return true;
        }
        if(p->HasUnitState(UNIT_STATE_CASTING))return true;
        if(t.combat){t.path.clear();t.combat=false;}
        bool ours=bg->GetBFGNodeTeam(uint8(t.goal))==m_TeamID;
        if(distance< (ours?14.0f:2.0f)){
            if(!ours){std::vector<G3D::Vector3>path;GameObject*flag=bg->GetBFGFlag(uint8(t.goal));
                if(flag&&move->BuildBFGGroundLeg(p->GetVector3(),flag->GetVector3(),path))move->MoveBFGRecordedPath(path);}
            else if(p->movespline&&!p->movespline->Finalized()){move->ClearMovement();t.path.clear();}
            return true;
        }
        if(!p->isInCombat()&&!p->IsMounted()&&ai->TryUpMount())return true;
        if(!t.progressAt||p->GetDistance(t.progress)>.8f){t.progress=p->GetPosition();t.progressAt=now;}
        if(now-t.progressAt>6000){t.path.clear();t.progressAt=now;move->ClearMovement();}
        if(t.path.empty()){
            if(t.attempt&&now-t.attempt<1500)return true;t.attempt=now;
            if(!BuildTravel(p,t)){
                if(bg->GetBFGGraveyardIndex(p)<5){BeginRecovery(p,false);return true;}
                if(Debug()&&(!t.warning||now-t.warning>15000)){t.warning=now;
                    TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_BFG_NO_ROUTE instance=%u bot=%s node=%u x=%.2f y=%.2f z=%.2f",
                        p->GetInstanceId(),p->GetName(),t.goal,p->GetPositionX(),p->GetPositionY(),p->GetPositionZ());}
                return true;
            }
        }
        if(!move->MoveBFGRecordedPath(t.path)){
            if(std::string(move->GetBFGMovementReason()).find("water_reject")==0){
                for(unsigned i=FelsoriaGilneasWater::First;i<FelsoriaGilneasWater::Last;++i){t.blocked.insert({i,i+1});t.blocked.insert({i+1,i});}
                t.blockedSince=now;
            }
            t.path.clear();
        }
        return true;
    }
};
#endif
