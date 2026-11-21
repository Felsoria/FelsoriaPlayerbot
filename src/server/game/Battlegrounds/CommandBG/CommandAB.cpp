#include "CommandAB.h"
#include "BotAI.h"
#include "Config.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include "FelsoriaArathiManualNavigation.h"
#include "FelsoriaArathiPolicy.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <utility>
namespace {
using namespace FelsoriaArathiManual;
float Distance(Player const*p,Point const&v){float x=p->GetPositionX()-v.x,y=p->GetPositionY()-v.y,z=p->GetPositionZ()-v.z;return std::sqrt(x*x+y*y+z*z);}
float Gap(Point const&a,Point const&b){float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return std::sqrt(x*x+y*y+z*z);}
bool Visible(Player*p,Player*other){return other&&other!=p&&other->IsInWorld()&&other->isAlive()&&
    other->GetMap()==p->GetMap()&&p->InSamePhase(other)&&p->InSamePhaseId(other)&&p->canSeeOrDetect(other)&&p->IsWithinLOSInMap(other);}
}
CommandAB::CommandAB(Battleground*bg,TeamId team):CommandBG(bg,team){}
CommandAB::~CommandAB(){}
bool CommandAB::Enabled()const{return m_pBattleground&&m_pBattleground->GetMapId()==529&&sConfigMgr->GetBoolDefault("Felsoria.ArathiBasin.Enable",true);}
void CommandAB::Initialize(){CommandBG::Initialize();m_tasks.clear();m_progress.clear();m_planTimer=0;
    for(unsigned i=0;i<5;++i){m_reports[i]=Report();m_cappers[i]=0;m_capUntil[i]=0;}
    TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_MANUAL_V2_1 instance=%u team=%u recordings=18 raw_points=1225",
        m_pBattleground->GetInstanceID(),uint32(m_TeamID));}
void CommandAB::OnPlayerDead(uint64 guid){CommandBG::OnPlayerDead(guid);
    auto task=m_tasks.find(guid);if(task!=m_tasks.end())if(Player*p=GetBGPlayer(guid))CancelCapture(p,task->second,"dead");
    m_tasks.erase(guid);m_progress.erase(guid);
    for(unsigned i=0;i<5;++i)if(m_cappers[i]==guid){m_cappers[i]=0;m_capUntil[i]=0;}}
void CommandAB::OnPlayerRevive(Player*p){if(p){m_tasks.erase(p->GetGUIDLow());m_progress.erase(p->GetGUIDLow());}m_planTimer=0;}
Creature const*CommandAB::GetMatchGraveyardNPC(Player const*p){return p&&m_pBattleground?m_pBattleground->GetClosestGraveCreature(p):nullptr;}
unsigned CommandAB::NearestAnchor(Player*p)const{
    unsigned result=0;float best=std::numeric_limits<float>::max();
    for(unsigned i=0;i<PointCount;++i){
        if(Cost[0][i]>=1e8f)continue;
        float score=Distance(p,Points[i]);if(score<best){best=score;result=i;}}
    return result;
}
void CommandAB::Observe(Player*p){
    std::list<Player*>nearby;p->GetPlayerListInGrid(nearby,55.0f);unsigned seen[5]={};uint32 now=getMSTime();
    for(Player*enemy:nearby){if(!Visible(p,enemy)||!p->IsValidAttackTarget(enemy))continue;
        for(unsigned i=0;i<5;++i)if(Distance(enemy,Points[Goals[i]])<70.0f)++seen[i];}
    for(unsigned i=0;i<5;++i){if(seen[i]){
        if(now-m_reports[i].time>2000)m_reports[i].count=0;
        m_reports[i].count=std::max(m_reports[i].count,seen[i]);m_reports[i].time=now;
    }else if(Distance(p,Points[Goals[i]])<25.0f&&now-m_reports[i].time>8000)m_reports[i]=Report();}
}
void CommandAB::Update(uint32 diff){
    CommandBG::Update(diff);if(!Enabled())return;
    if(m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS){
        for(auto&entry:m_tasks)if(Player*p=GetBGPlayer(entry.first))CancelCapture(p,entry.second,"match_ended");
        m_tasks.clear();return;}
    if(m_planTimer>diff){m_planTimer-=diff;return;}m_planTimer=2000;Plan();
}
void CommandAB::Plan(){
    auto*bg=dynamic_cast<BattlegroundArathiBasin*>(m_pBattleground);if(!bg)return;
    uint32 now=getMSTime();std::vector<Player*>bots;
    for(auto const&slot:m_PlayerGUIDs){Player*p=GetBGPlayer(slot.first);
        if(!p||!p->isAlive()||!p->IsInWorld()||p->IsBeingTeleported()||p->GetMap()!=bg->GetBgMap()||p->GetBGTeamId()!=m_TeamID){m_tasks.erase(slot.first);continue;}
        if(GetBotBGAI(slot.first)){bots.push_back(p);Observe(p);}}
    for(auto it=m_tasks.begin();it!=m_tasks.end();){
        if(std::none_of(bots.begin(),bots.end(),[&](Player*p){return p->GetGUIDLow()==it->first;}))it=m_tasks.erase(it);else ++it;}
    if(bots.empty())return;
    // Rotate allocation priority without changing an active order every tick.
    std::sort(bots.begin(),bots.end(),[now](Player*a,Player*b){return(a->GetGUIDLow()+now/30000)%100003<(b->GetGUIDLow()+now/30000)%100003;});
    unsigned assigned[5]={},owned=0;
    for(unsigned i=0;i<5;++i){auto n=bg->GetABNodeState(i);if(n&&n->TeamID==m_TeamID&&n->Status==NODE_STATUS_CAPTURE)++owned;}
    for(Player*p:bots){auto old=m_tasks.find(p->GetGUIDLow());
        if(old!=m_tasks.end()&&(old->second.castAt||old->second.exitRoute>=0||
            (old->second.opportunityUntil&&int32(old->second.opportunityUntil-now)>0&&
             bg->GetABNodeState(old->second.node)->TeamID!=m_TeamID))){
            ++assigned[old->second.node];continue;}
        unsigned start=NearestAnchor(p),chosen=0;float best=-1e20f;
        for(unsigned i=0;i<5;++i){auto n=bg->GetABNodeState(i);if(!n||!n->Point)continue;
            bool previous=old!=m_tasks.end()&&old->second.node==i;
            unsigned threats=now-m_reports[i].time<10000?m_reports[i].count:0;
            float travel=Distance(p,Points[start])+Cost[i][start];
            float score=FelsoriaArathiPolicy::Score(n->TeamID==m_TeamID,n->Status==NODE_STATUS_ASSAULT,n->Status==NODE_STATUS_NEUTRAL,
                owned,threats,assigned[i],unsigned(bots.size()),travel,previous,previous&&now-old->second.assigned<12000,p->GetSpecializationRole()==ROLES_HEALER);
            if(owned>=3 && n->TeamID==m_TeamID && bg->GetTeamScore(m_TeamID)>bg->GetTeamScore(m_TeamID==TEAM_ALLIANCE?TEAM_HORDE:TEAM_ALLIANCE))score+=20;
            score+=float((p->GetGUIDLow()*17+i*31)%19);if(score>best){best=score;chosen=i;}}
        ++assigned[chosen];auto n=bg->GetABNodeState(chosen);if(!n)continue;
        bool changed=old==m_tasks.end()||old->second.node!=chosen;Task&task=m_tasks[p->GetGUIDLow()];
        if(changed){bool exited=task.exitChecked;task=Task();task.exitChecked=exited;task.node=uint8(chosen);task.assigned=now;
            TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_TASK instance=%u team=%u bot=%s node=%u mode=%s",
                bg->GetInstanceID(),uint32(m_TeamID),p->GetName(),chosen,n->TeamID==m_TeamID?"defend":"assault");}
    }
    // Never starve an uncaptured node (including LM) when a full team has
    // several bots on another objective. Preserve active cappers/exit orders.
    for(unsigned node=0;node<5&&bots.size()>=5;++node){
        auto n=bg->GetABNodeState(node);if(!n||!n->Point||n->Status!=NODE_STATUS_NEUTRAL||assigned[node])continue;
        Player*chosen=nullptr;float closest=1e20f;
        for(Player*p:bots){auto const&t=m_tasks[p->GetGUIDLow()];
            if(assigned[t.node]<=1||t.castAt||t.exitRoute>=0||
                (t.opportunityUntil&&int32(t.opportunityUntil-now)>0))continue;
            unsigned start=NearestAnchor(p);float cost=Distance(p,Points[start])+Cost[node][start];
            if(p->GetSpecializationRole()==ROLES_HEALER)cost+=100;
            if(cost<closest){closest=cost;chosen=p;}
        }
        if(chosen){auto&t=m_tasks[chosen->GetGUIDLow()];--assigned[t.node];++assigned[node];
            bool exited=t.exitChecked;t=Task();t.exitChecked=exited;t.node=uint8(node);t.assigned=now;
            TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_NEUTRAL_COVER instance=%u bot=%s node=%u",
                bg->GetInstanceID(),chosen->GetName(),node);}
    }
    for(Player*p:bots)TrackProgress(p,m_tasks[p->GetGUIDLow()]);
}
void CommandAB::Travel(Player*p,Task&task,unsigned destination){
    BotBGAI*ai=GetBotBGAI(p->GetGUIDLow());if(!ai||destination>=10)return;
    auto*movement=ai->GetAIMovement();
    if(task.to!=int(destination)||task.from<0||task.combat){
        uint32 now=getMSTime();if(task.entryAt&&now-task.entryAt<1000)return;task.entryAt=now;
        // Choose an entry by a complete native ground connector, not distance
        // alone. Overlapping nearby samples may belong to another polygon.
        std::vector<std::pair<float,unsigned>>candidates;
        for(unsigned i=0;i<PointCount;++i){
            float d=Distance(p,Points[i]);if(Cost[destination][i]<1e8f&&d<45.0f)candidates.emplace_back(d,i);
        }
        std::sort(candidates.begin(),candidates.end());std::vector<G3D::Vector3>connector;
        task.from=-1;
        for(unsigned c=0;c<candidates.size()&&c<16;++c){
            unsigned i=candidates[c].second;auto const&v=Points[i];
            if(!movement->BuildABGroundPath(v.x,v.y,v.z,connector))continue;
            float length=0;for(unsigned k=1;k<connector.size();++k)length+=(connector[k]-connector[k-1]).length();
            if(length>std::max(12.0f,candidates[c].first*2.0f+4.0f))continue;
            task.from=int(i);break;
        }
        if(task.from<0)return;
        task.to=int(destination);task.leg=false;task.attached=true;task.combat=false;
        task.exitRoute=-1;task.exitCursor=0;
        if(!task.exitChecked){
            task.exitChecked=true;float closest=15.0f;
            for(unsigned ri=10;ri<18;++ri){
                if(ri==14||ri==15)continue;
                if((ri==10&&m_TeamID!=TEAM_ALLIANCE)||(ri==13&&m_TeamID!=TEAM_HORDE))continue;
                float d=Distance(p,Points[Routes[ri].first]);if(d<closest){closest=d;task.exitRoute=int(ri);}
            }
            unsigned lm=14+unsigned(p->GetGUIDLow()%2);
            if(Distance(p,Points[Routes[lm].first])<closest)task.exitRoute=int(lm);
            if(task.exitRoute>=0){
                auto const&v=Points[Routes[task.exitRoute].first];
                std::vector<G3D::Vector3>exitConnector;
                if(movement->BuildABGroundPath(v.x,v.y,v.z,exitConnector)){
                    connector=std::move(exitConnector);task.from=int(Routes[task.exitRoute].first);
                }
                else task.exitRoute=-1;
            }
        }
        task.legPath=connector;
        if(task.legPath.empty())task.legPath.push_back(p->GetVector3());
        unsigned vertex=unsigned(task.from),cursor=0;
        auto append=[&](Point const&v){G3D::Vector3 point(v.x,v.y,v.z);
            if((task.legPath.back()-point).squaredLength()>=0.0625f)task.legPath.push_back(point);};
        append(Points[vertex]);
        // Give the spline the complete recorded route to the objective. There
        // is no 18m stop/wait boundary, including at the graveyard exit end.
        for(unsigned hops=0;hops<PointCount*2;++hops){
            unsigned edge=NoEdge;
            if(task.exitRoute>=0&&cursor<Routes[task.exitRoute].count)
                edge=RouteEdges[Routes[task.exitRoute].offset+cursor++];
            else edge=NextEdge[destination][vertex];
            if(edge==NoEdge||edge>=EdgeCount)break;
            auto const&e=Edges[edge];if(e.from!=vertex)break;
            for(unsigned k=0;k<e.count;++k)append(Geometry[e.offset+k]);
            vertex=e.to;if(vertex==Goals[destination]&&
                (task.exitRoute<0||cursor>=Routes[task.exitRoute].count))break;
        }
        if(vertex!=Goals[destination]){task.from=-1;return;}
        task.legEnd=vertex;task.exitLegEnd=cursor;
        if(task.legPath.size()<2)return;
        if(task.exitRoute>=0)TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_EXIT instance=%u bot=%s route=%u",
            p->GetInstanceId(),p->GetName(),unsigned(task.exitRoute));
    }
    if(Distance(p,Points[Goals[destination]])<1.5f){task.leg=false;task.exitRoute=-1;return;}
    if(!movement->MoveABRecordedPath(task.legPath)){task.from=-1;task.leg=false;return;}
    task.leg=true;
}
void CommandAB::ConsiderNearbyBase(Player*p,Task&task){
    if(task.castAt||task.exitRoute>=0)return;
    auto*bg=dynamic_cast<BattlegroundArathiBasin*>(m_pBattleground);if(!bg)return;
    std::list<Player*>visible;p->GetPlayerListInGrid(visible,80.0f);unsigned chosen=task.node;float nearest=40.0f;
    for(unsigned i=0;i<5;++i){
        auto n=bg->GetABNodeState(i);if(!n||!n->Point||n->TeamID==m_TeamID||!n->Point->isSpawned())continue;
        float distance=p->GetDistance(n->Point);
        if(distance>=nearest||!p->InSamePhase(n->Point)||!p->InSamePhaseId(n->Point)||!p->IsWithinLOSInMap(n->Point))continue;
        bool defended=false;
        for(Player*enemy:visible)if(p->IsValidAttackTarget(enemy)&&enemy->GetDistance(n->Point)<35.0f&&Visible(p,enemy)){defended=true;break;}
        if(defended)continue;
        if(m_cappers[i]&&m_cappers[i]!=p->GetGUIDLow()&&int32(m_capUntil[i]-getMSTime())>0)continue;
        unsigned arriving=0;
        for(auto const&entry:m_tasks)if(entry.first!=p->GetGUIDLow()&&entry.second.node==i)
            if(Player*other=GetBGPlayer(entry.first))if(other->isAlive()&&other->GetDistance(n->Point)<50.0f)++arriving;
        if(arriving>=2)continue;
        nearest=distance;chosen=i;
    }
    if(chosen==task.node)return;
    task=Task();task.node=uint8(chosen);task.assigned=getMSTime();task.opportunityUntil=getMSTime()+15000;task.exitChecked=true;
    GetBotBGAI(p->GetGUIDLow())->GetAIMovement()->ClearMovement();p->StopMoving();
    TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_OPPORTUNITY instance=%u bot=%s node=%u",
        p->GetInstanceId(),p->GetName(),chosen);
}
void CommandAB::TrackProgress(Player*p,Task const&task){
    uint32 now=getMSTime();auto&progress=m_progress[p->GetGUIDLow()];
    if(!progress.movedAt||p->GetDistance(progress.position)>1.0f){progress.position=p->GetPosition();progress.movedAt=now;}
    if(now-progress.movedAt<7000||Distance(p,Points[Goals[task.node]])<30.0f||
        (progress.loggedAt&&now-progress.loggedAt<10000))return;
    progress.loggedAt=now;
    TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_MOVEMENT_STALL instance=%u team=%u bot=%s node=%u tick_age=%u from=%d x=%.3f y=%.3f z=%.3f casting=%u root=%u role=%u selected=%u combat=%u",
        p->GetInstanceId(),uint32(m_TeamID),p->GetName(),uint32(task.node),progress.tickAt?now-progress.tickAt:0xffffffffu,
        task.from,p->GetPositionX(),p->GetPositionY(),p->GetPositionZ(),uint32(p->HasUnitState(UNIT_STATE_CASTING)),uint32(p->HasAuraType(SPELL_AURA_MOD_ROOT)),
        uint32(p->GetSpecializationRole()),uint32(p->GetSelectedUnit()!=nullptr),uint32(p->isInCombat()));
}
void CommandAB::CancelCapture(Player*p,Task&task,char const*reason){
    if(!task.castAt)return;
    if(reason)TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_CAPTURE_CANCEL instance=%u bot=%s node=%u reason=%s",
        p->GetInstanceId(),p->GetName(),uint32(task.node),reason);
    if(m_cappers[task.node]==p->GetGUIDLow()){m_cappers[task.node]=0;m_capUntil[task.node]=0;}
    if(p->GetUInt32Value(UNIT_FIELD_EMOTE_STATE)==EMOTE_STATE_USE_STANDING)p->SetUInt32Value(UNIT_FIELD_EMOTE_STATE,EMOTE_STATE_NONE);
    task.castAt=0;task.castTick=0;
}
bool CommandAB::Capture(Player*p,Task&task){
    auto*bg=dynamic_cast<BattlegroundArathiBasin*>(m_pBattleground);if(!bg)return false;
    auto n=bg->GetABNodeState(task.node);
    if(!n||!n->Point||n->TeamID==m_TeamID){CancelCapture(p,task,"node_changed");return false;}
    GameObject*flag=n->Point;uint32 now=getMSTime();
    bool available=p->isAlive()&&flag->isSpawned()&&p->IsWithinDistInMap(flag,5.0f)&&p->IsWithinLOSInMap(flag)&&
        p->InSamePhase(flag)&&p->InSamePhaseId(flag)&&!p->GetVehicleBase()&&
        !p->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)&&
        !p->HasAuraType(SPELL_AURA_MOD_ROOT);
    if(!available){CancelCapture(p,task,"unavailable");return false;}
    p->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);p->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
    if(!p->CanUseBattlegroundObject()){CancelCapture(p,task,"cannot_use");return false;}
    if(task.castAt){
        if(now-task.castTick>1200||p->GetHealth()<task.castHealth||p->GetDistance(task.castOrigin)>0.8f){
            CancelCapture(p,task,"interrupted");task.retryAt=now;return false;
        }
        task.castTick=now;task.castHealth=p->GetHealth();m_capUntil[task.node]=now+1500;
        if(now-task.castAt<8000)return true;
        // Legion type-42 flags use GameObject::Use. Old open-lock spell 21651
        // returned SPELL_FAILED_BAD_TARGETS in the supplied instance-37 log.
        CancelCapture(p,task,nullptr);task.retryAt=now;
        flag->Use(p);
        auto state=bg->GetABNodeState(task.node);
        TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_CAPTURE_USE instance=%u bot=%s node=%u team=%u status=%u changed=%u",
            bg->GetInstanceID(),p->GetName(),uint32(task.node),state?uint32(state->TeamID):9,
            state?uint32(state->Status):9,uint32(state&&state->TeamID==m_TeamID));
        return true;
    }
    if(task.retryAt&&now-task.retryAt<2000)return false;
    if(m_cappers[task.node]&&m_cappers[task.node]!=p->GetGUIDLow()&&int32(m_capUntil[task.node]-now)>0)return false;
    BotBGAI*ai=GetBotBGAI(p->GetGUIDLow());if(!ai)return false;
    ai->Dismount();ai->GetAIMovement()->ClearMovement();p->StopMoving();p->AttackStop();p->SetSelection(ObjectGuid::Empty);
    p->SetFacingToObject(flag);p->SetUInt32Value(UNIT_FIELD_EMOTE_STATE,EMOTE_STATE_USE_STANDING);
    task.castAt=now?now:1;task.castTick=now;task.castHealth=p->GetHealth();task.castOrigin=p->GetPosition();
    m_cappers[task.node]=p->GetGUIDLow();m_capUntil[task.node]=now+1500;
    task.leg=false;task.from=-1;
    TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_CAPTURE_BEGIN instance=%u bot=%s node=%u duration=8000",
        bg->GetInstanceID(),p->GetName(),uint32(task.node));
    return true;
}
bool CommandAB::UpdateBot(Player*p){
    if(!Enabled()||!p||!p->isAlive()||p->IsBeingTeleported()||p->GetBGTeamId()!=m_TeamID||p->GetBattleground()!=m_pBattleground||m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS)return false;
    auto it=m_tasks.find(p->GetGUIDLow());if(it==m_tasks.end()){Plan();it=m_tasks.find(p->GetGUIDLow());}if(it==m_tasks.end())return true;
    m_progress[p->GetGUIDLow()].tickAt=getMSTime();
    Task&task=it->second;Observe(p);uint32 now=getMSTime();auto*ai=GetBotBGAI(p->GetGUIDLow());if(!ai)return false;
    if(task.exitRoute>=0&&Distance(p,Points[Routes[task.exitRoute].last])<8.0f)task.exitRoute=-1;
    std::list<Player*>nearby;p->GetPlayerListInGrid(nearby,45.0f);Player*selected=nullptr;float best=-1e20f;unsigned cover=0;bool closeThreat=false;
    for(Player*enemy:nearby){
        if(!Visible(p,enemy))continue;
        if(p->IsValidAssistTarget(enemy)&&enemy->IsInCombat()&&Distance(enemy,Points[Goals[task.node]])<30.0f)++cover;
        if(!p->IsValidAttackTarget(enemy))continue;
        if(p->GetDistance(enemy)<12.0f)closeThreat=true;
        bool attacker=enemy->GetVictim()==p;
        if(Distance(p,Points[Goals[task.node]])>90.0f&&p->GetDistance(enemy)>25.0f&&!attacker)continue;
        if(Distance(enemy,Points[Goals[task.node]])>80.0f&&!attacker)continue;
        if(enemy->HasAuraType(SPELL_AURA_MOD_CONFUSE)||enemy->HasAuraType(SPELL_AURA_MOD_FEAR)||enemy->HasAuraType(SPELL_AURA_MOD_FEAR_2))continue;
        float score=100.0f-p->GetDistance(enemy);
        if(enemy->GetSpecializationRole()==ROLES_HEALER)score+=24;
        if(enemy==p->GetSelectedUnit())score+=14;if(attacker)score+=30;
        if(Spell*cast=enemy->GetCurrentSpell(CURRENT_GENERIC_SPELL))if(cast->GetSpellInfo()->Id==21651)score+=100;
        if(enemy->GetUInt32Value(UNIT_FIELD_EMOTE_STATE)==EMOTE_STATE_USE_STANDING&&
            Distance(enemy,Points[Goals[task.node]])<8.0f)score+=100;
        if(Unit*victim=enemy->GetVictim())if(Player*ally=victim->ToPlayer())if(ally->GetBGTeamId()==m_TeamID&&ally->GetSpecializationRole()==ROLES_HEALER)score+=35;
        if(score>best){best=score;selected=enemy;}}
    // One healthy nearby bot may channel while its fighting teammates cover.
    // The use interaction still obeys damage/CC and the real flag range.
    if(!closeThreat && cover>=2 && p->GetHealthPct()>60.0f && p->GetSpecializationRole()!=ROLES_HEALER && Capture(p,task))return true;
    if(selected){CancelCapture(p,task,"enemy");task.combat=true;if(p->GetSelectedUnit()!=selected){p->AttackStop();ai->GetAIMovement()->ClearMovement();p->SetSelection(selected->GetGUID());}return false;}
    if(p->GetSelectedUnit()){p->AttackStop();p->SetSelection(ObjectGuid::Empty);task.combat=true;}
    ConsiderNearbyBase(p,task);
    if(Capture(p,task))return true;
    auto* ab=dynamic_cast<BattlegroundArathiBasin*>(m_pBattleground);
    auto node=ab ? ab->GetABNodeState(task.node) : nullptr;
    float arrive=node && node->TeamID!=m_TeamID ? 3.0f : 15.0f;
    if(Distance(p,Points[Goals[task.node]])>arrive){if(!p->isInCombat()&&!p->IsMounted()&&ai->TryUpMount())return true;Travel(p,task,task.node);return true;}
    // Normal object proximity activates a useful spawned buff; no forced aura.
    for(unsigned v=0;v<3;++v){GameObject*buff=m_pBattleground->GetBGObject(BG_AB_OBJECT_SPEEDBUFF_STABLES+task.node*3+v);
        bool useful=(v==1&&p->GetHealthPct()<65)||(v==0&&!p->IsMounted())||v==2;
        if(buff&&buff->isSpawned()&&useful&&p->GetDistance(buff)<28.0f&&now-task.patrolAt>7000){Travel(p,task,5+task.node);return true;}}
    if(!task.patrolAt||now-task.patrolAt>uint32(5000+(p->GetGUIDLow()%5000))){
        unsigned chosen=Goals[task.node];float score=-1.0f;
        for(unsigned i=0;i<PointCount;++i){
            float d=Gap(Points[i],Points[Goals[task.node]]);if(d<5.0f||d>14.0f||Cost[task.node][i]>=30.0f)continue;
            float value=float((p->GetGUIDLow()*13+now/8000+i*17)%997);
            if(value>score){score=value;chosen=i;}
        }
        task.patrol.Relocate(Points[chosen].x,Points[chosen].y,Points[chosen].z);task.patrolAt=now;
    }
    ai->GetAIMovement()->MovementTo(task.patrol.GetPositionX(),task.patrol.GetPositionY(),task.patrol.GetPositionZ());return true;
}
