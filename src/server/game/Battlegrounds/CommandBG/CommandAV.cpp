#include "FelsoriaRegional.h"
#include "CommandAV.h"
#include "BotAI.h"
#include "Config.h"
#include "GameObject.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "MoveSpline.h"
#include "Spell.h"
#include "FelsoriaAVPolicy.h"
#include "FelsoriaAVSafety.h"
#include "FelsoriaAVRecordedData.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace {
using namespace FelsoriaAV;
Point Pos(Player const*p){return {p->GetPositionX(),p->GetPositionY(),p->GetPositionZ()};}
G3D::Vector3 Vec(Point p){return {p.x,p.y,p.z};}
float Dist(Player const*p,Point q){return Gap(Pos(p),q);}
uint64 Key(unsigned a,unsigned b){return (uint64(a)<<32)|b;}
float PathLength(std::vector<G3D::Vector3>const&p){float n=0;for(unsigned i=1;i<p.size();++i)n+=(p[i]-p[i-1]).length();return n;}
bool Visible(Player*p,Unit*u){return u&&u!=p&&u->isAlive()&&u->IsInWorld()&&u->GetMap()==p->GetMap()&&
 p->InSamePhase(u)&&p->InSamePhaseId(u)&&p->canSeeOrDetect(u)&&p->IsWithinLOSInMap(u);}
bool Debug(){return sConfigMgr->GetBoolDefault("Felsoria.AlteracValley.Debug",false);}
bool Corridor(std::vector<G3D::Vector3>const&path,Point a,Point b,float width=4.0f){
 float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,den=dx*dx+dy*dy+dz*dz;
 if(PathLength(path)>std::max(12.0f,Gap(a,b)*2.5f+4.0f))return false;
 for(auto const&p:path){float t=den>.01f?((p.x-a.x)*dx+(p.y-a.y)*dy+(p.z-a.z)*dz)/den:0;
  t=std::max(0.0f,std::min(1.0f,t));if(Gap({p.x,p.y,p.z},{a.x+t*dx,a.y+t*dy,a.z+t*dz})>width)return false;}
 return true;
}
bool GroundPieces(BotBGAIMovement*move,G3D::Vector3 const&start,G3D::Vector3 const&goal,float limit,std::vector<G3D::Vector3>&out){
 out.clear();
 if(!FelsoriaAVSafety::Finite({start.x,start.y,start.z})||!FelsoriaAVSafety::Finite({goal.x,goal.y,goal.z}))return false;
 auto delta=goal-start;float length=delta.length();
 if(length<=.25f||length>limit)return false;
 unsigned steps=unsigned(std::ceil(length/4.0f));
 std::vector<G3D::Vector3>checked{start},part;
 for(unsigned i=1;i<=steps;++i){
  auto next=start+delta*(float(i)/steps);
  if(!move->BuildAVGroundLeg(checked.back(),next,part)||part.empty()){out.clear();return false;}
  for(unsigned j=1;j<part.size();++j)
   if((checked.back()-part[j]).squaredLength()>=.0625f)checked.push_back(part[j]);
  if(checked.size()>4096){out.clear();return false;}
 }
 out.swap(checked);return out.size()>1;
}
bool RecordedLeg(BotBGAIMovement*move,G3D::Vector3 const&start,G3D::Vector3 const&goal,std::vector<G3D::Vector3>&out){
 if(move->BuildAVGroundLeg(start,goal,out))return true;
 // FELSORIA_AV_RECORDED_GAP_V1_7_1: the Alliance recording has a 28.3-yard
 // gap at (522.74,-413.77). Detour may report NORMAL with a bad endpoint,
 // while the local fallback only accepts 12 yards. Split only this recorded
 // graph edge into short, independently floor/collision-checked pieces.
 // Arbitrary route-entry joins retain their original mesh requirements.
 if((goal-start).length()<=12.0f)return false;
 return GroundPieces(move,start,goal,36.0f,out);
}
bool CaveEntry(Battleground*bg,TeamId team,G3D::Vector3 const&start){
 if(!bg||bg->GetStatus()!=STATUS_IN_PROGRESS||team>=TEAM_NEUTRAL||
    !FelsoriaAVSafety::Finite({start.x,start.y,start.z})||
    (start-Vec(Points[GateStarts[team]])).squaredLength()>115.0f*115.0f)return false;
 auto*spawn=bg->GetTeamStartPosition(team);if(!spawn)return false;
 auto const&gate=BG_AV_DoorPositons[team];
 return FelsoriaAVSafety::InsideStartingCave(
  {spawn->GetPositionX(),spawn->GetPositionY(),spawn->GetPositionZ()},
  {gate[0],gate[1],gate[2]},{start.x,start.y,start.z});
}
bool EntryLeg(BotBGAIMovement*move,Battleground*bg,TeamId team,unsigned vertex,
 G3D::Vector3 const&start,std::vector<G3D::Vector3>&out){
 auto goal=Vec(Points[vertex]);
 if(move->BuildAVGroundLeg(start,goal,out))return true;
 // FELSORIA_AV_CAVE_ENTRY_V1_7_2: random preparation positions can be
 // 30-74 yards behind the first recording. This is a separate connection
 // from the recorded graph edges fixed in V1.7.1. Only the own-cave anchor
 // may use this bounded fallback, after the match has actually started.
 if(team>=TEAM_NEUTRAL||vertex!=GateStarts[team]||!CaveEntry(bg,team,start))return false;
 if(!GroundPieces(move,start,goal,115.0f,out))return false;
 auto*spawn=bg->GetTeamStartPosition(team);auto const&gate=BG_AV_DoorPositons[team];
 for(auto const&p:out)if(!FelsoriaAVSafety::InsideStartingCave(
  {spawn->GetPositionX(),spawn->GetPositionY(),spawn->GetPositionZ()},
  {gate[0],gate[1],gate[2]},{p.x,p.y,p.z})){out.clear();return false;}
 return true;
}
}
CommandAV::CommandAV(Battleground*bg,TeamId team):CommandBG(bg,team){}
CommandAV::~CommandAV(){}
bool CommandAV::Valid(Player*p){return p&&p->isAlive()&&p->IsInWorld()&&!p->IsBeingTeleported()&&p->GetBattleground()==m_pBattleground&&
 p->GetBGTeamId()==m_TeamID&&p->GetMap()==m_pBattleground->FindBgMap()&&GetBotBGAI(p->GetGUIDLow());}
void CommandAV::Initialize(){CommandBG::Initialize();m_tasks.clear();m_blocked.clear();m_planTimer=0;m_startedAt=0;
 m_contacts.clear();m_joinCache.clear();m_roster.clear();m_phase=0;
 for(auto&wing:m_wings)wing=Mission();
 for(unsigned goal=0;goal<21;++goal){m_staging[goal]=Goals[goal];m_rallyAt[goal]=0;}
 for(unsigned goal=15;goal<19;++goal){std::vector<unsigned>route;
  if(FindRoute(GateStarts[m_TeamID],Goals[goal],PointCount,RouteOffsets(m_TeamID),RouteEdges(m_TeamID),[](unsigned a,unsigned b){return SnowfallApproachPoint(a)||SnowfallApproachPoint(b);},route))
   for(auto it=route.rbegin();it!=route.rend();++it)if(Gap(Points[*it],Points[Goals[goal]])>=55){m_staging[goal]=*it;break;}
 }
 m_roleLogAt=0;m_lastArmy=0;m_lastDefenders=0;m_lastRaiders=0;
 TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_START_MOVEMENT_V1 instance=%u team=%u",m_pBattleground->GetInstanceID(),unsigned(m_TeamID));
 TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_TACTICS_V1_10_2 instance=%u team=%u",m_pBattleground->GetInstanceID(),unsigned(m_TeamID));
 for(unsigned i=0;i<15;++i){m_cappers[i]=0;m_capUntil[i]=0;}}
void CommandAV::StartGame(){CommandBG::StartGame();m_planTimer=0;m_startedAt=getMSTime()?getMSTime():1;}
void CommandAV::OnPlayerDead(uint64 guid){CommandBG::OnPlayerDead(guid);auto it=m_tasks.find(guid);
 if(it!=m_tasks.end()){if(Player*p=GetBGPlayer(guid))Cancel(p,it->second);m_tasks.erase(it);}m_planTimer=0;}
void CommandAV::OnPlayerRevive(Player*p){
 if(!p||p->GetBattleground()!=m_pBattleground||p->GetBGTeamId()!=m_TeamID)return;
 m_tasks.erase(p->GetGUIDLow());m_planTimer=0;
}
Creature const*CommandAV::GetMatchGraveyardNPC(Player const*p){return p&&m_pBattleground?m_pBattleground->GetClosestGraveCreature(p):nullptr;}
bool CommandAV::CanUpMount(Player*p){if(!p||!m_pBattleground)return false;if(m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS)return true;
 auto it=m_tasks.find(p->GetGUIDLow());return it==m_tasks.end()||(!it->second.castAt&&it->second.flagNode>=15&&Dist(p,Points[Goals[it->second.objective]])>5.0f);}
Creature*CommandAV::ObjectiveCreature(unsigned objective)const{
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);if(!bg)return nullptr;
 if(objective>=19&&objective<=20)return nullptr;
 unsigned entry=objective==15?11949:objective==16?11947:objective==17?11948:objective==18?11946:0;
 if(!entry)return nullptr;
 for(unsigned i=0;i<AV_STATICCPLACE_MAX;++i){unsigned info=unsigned(BG_AV_StaticCreaturePos[i][4]);
  if(info<sizeof(BG_AV_StaticCreatureInfo)/sizeof(BG_AV_StaticCreatureInfo[0])&&BG_AV_StaticCreatureInfo[info][0]==entry)
   if(Creature*c=bg->GetBGCreature(AV_CPLACE_MAX+i))if(c->GetEntry()==entry)return c;}
 // Some leaders (notably Drek'Thar) occupy dynamic native creature slots.
 for(unsigned i=0;i<AV_CPLACE_MAX;++i)if(Creature*c=bg->GetBGCreature(i))if(c->GetEntry()==entry)return c;
 return nullptr;
}
bool CommandAV::ObjectiveAvailable(unsigned objective){
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);if(!bg||objective>=ObjectiveCount)return false;
 uint32 own=m_TeamID==TEAM_ALLIANCE?ALLIANCE:HORDE;
 if(objective<15)return bg->m_Nodes[objective].State!=POINT_DESTROYED;
 if(objective==15||objective==16)return bg->m_CaptainAlive[objective==15?TEAM_ALLIANCE:TEAM_HORDE]&&
  (objective==15?m_TeamID==TEAM_HORDE:m_TeamID==TEAM_ALLIANCE);
 if(objective==19||objective==20)return false;
 unsigned finalGY=m_TeamID==TEAM_ALLIANCE?6:0,firstTower=m_TeamID==TEAM_ALLIANCE?11:7,destroyed=0;
 for(unsigned i=firstTower;i<firstTower+4;++i)if(bg->m_Nodes[i].State==POINT_DESTROYED)++destroyed;
 return (objective==17?m_TeamID==TEAM_HORDE:m_TeamID==TEAM_ALLIANCE)&&GeneralReady(
  (bg->m_Nodes[finalGY].Owner==own&&bg->m_Nodes[finalGY].State==POINT_CONTROLED)||FinishingBoss(),destroyed,
  bg->m_CaptainAlive[m_TeamID==TEAM_ALLIANCE?TEAM_HORDE:TEAM_ALLIANCE]);
}
void CommandAV::Update(uint32 diff){
 CommandBG::Update(diff);if(!m_pBattleground||m_pBattleground->GetMapId()!=30)return;
 if(m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS){
  for(auto&entry:m_tasks)if(Player*p=GetBGPlayer(entry.first))Cancel(p,entry.second);
  m_tasks.clear();m_planTimer=0;return;}
 if(m_planTimer>diff){m_planTimer-=diff;return;}m_planTimer=2000;Plan();
}
unsigned CommandAV::Destination(Task const&t)const{return t.waiting?t.staging:Goals[t.objective];}
float CommandAV::TravelDistance(Player*p,unsigned objective){
 uint32 now=getMSTime();auto&c=m_joinCache[p->GetGUIDLow()];Point position=Pos(p);
 if(!c.at||now-c.at>=1900||Gap(position,c.position)>4.0f){
  c.at=now?now:1;c.position=position;c.gap[0]=c.gap[1]=1e9f;
  for(unsigned i=0;i<PointCount;++i){if(RouteOffsets(m_TeamID)[i]==RouteOffsets(m_TeamID)[i+1])continue;
   float d=Gap(position,Points[i]);if(d<c.gap[1]){c.gap[1]=d;c.vertex[1]=i;}
   if(!SnowfallApproachPoint(i)&&d<c.gap[0]){c.gap[0]=d;c.vertex[0]=i;}
  }
 }
 unsigned variant=objective==3?1:0;return c.gap[variant]+TeamCosts[m_TeamID][objective][c.vertex[variant]];
}
bool CommandAV::FinishingBoss(){
 unsigned objective=m_TeamID==TEAM_ALLIANCE?18:17;Creature*boss=ObjectiveCreature(objective);
 if(!boss||!boss->isAlive())return false;
 // Health is consulted only after a friendly observer can see the boss.
 for(auto const&slot:m_PlayerGUIDs)if(Player*p=GetBGPlayer(slot.first))
  if(p->isAlive()&&p->IsInWorld()&&p->GetBGTeamId()==m_TeamID&&p->GetBattleground()==m_pBattleground&&
     p->GetDistance(boss)<65&&Visible(p,boss)&&p->isInCombat())return boss->GetHealthPct()<=15;
 return false;
}
bool CommandAV::GroupReady(Player*p,unsigned objective){
 if(objective<15)return true;
 if(objective>=19||!ObjectiveAvailable(objective))return false;
 Creature*boss=ObjectiveCreature(objective);
 // Once friends are actually fighting the leader, reinforcements join them.
 // A casualty or a healer assigned elsewhere cannot send a live fight back
 // to staging. After a wipe the native creature leaves combat normally.
 if(boss&&boss->isAlive()&&boss->isInCombat()){
  Player*victim=boss->GetVictim()?boss->GetVictim()->GetCharmerOrOwnerPlayerOrPlayerItself():nullptr;
  if(victim&&victim->isAlive()&&victim->GetBattleground()==m_pBattleground&&victim->GetBGTeamId()==m_TeamID)return true;
  for(auto const&slot:m_PlayerGUIDs)if(Player*ally=GetBGPlayer(slot.first))
   if(Valid(ally)&&ally->isInCombat()&&ally->GetDistance(boss)<65&&Visible(ally,boss))return true;
 }
 unsigned army=unsigned(m_tasks.size());unsigned required=objective>=17?std::min(12u,std::max(6u,army/4)):std::min(10u,std::max(4u,army/4));
 unsigned nearby=0,tanks=0,healers=0,allTanks=0,allHealers=0,assigned=0;
 for(auto const&slot:m_PlayerGUIDs)if(Player*ally=GetBGPlayer(slot.first))if(Valid(ally)){
  auto task=m_tasks.find(ally->GetGUIDLow());if(task==m_tasks.end()||task->second.objective!=objective)continue;
  ++assigned;
  allTanks+=ally->GetSpecializationRole()==ROLES_TANK;allHealers+=ally->GetSpecializationRole()==ROLES_HEALER;
  // Own assigned allies around the same rally point count across a doorway.
  // Requiring mutual LOS let the first players entering a room un-ready the
  // rest, and could leave two groups waiting on opposite sides of its wall.
  if(Dist(ally,Points[Goals[objective]])>90)continue;
  ++nearby;tanks+=ally->GetSpecializationRole()==ROLES_TANK;healers+=ally->GetSpecializationRole()==ROLES_HEALER;
 }
 required=std::min(required,assigned);
 if(FinishingBoss())return true;
 if(!required||nearby<required){m_rallyAt[objective]=0;return false;}
 if((!allTanks||tanks)&&(!allHealers||healers)){m_rallyAt[objective]=0;return true;}
 // Preserve the support rendezvous, but don't wait forever for a distant
 // assigned support bot. The quorum must remain locally assembled for 30s.
 uint32 now=getMSTime();if(!m_rallyAt[objective])m_rallyAt[objective]=now?now:1;
 return now-m_rallyAt[objective]>=30000;
}
bool CommandAV::CanAssist(Player*p,Unit*u){
 if(!p||!u||!u->isAlive()||!u->IsInWorld()||u->GetMap()!=p->GetMap()||!p->InSamePhase(u)||!p->InSamePhaseId(u))return false;
 if(p==u)return true;
 auto ownTask=m_tasks.find(p->GetGUIDLow());
 if(ownTask!=m_tasks.end()&&ownTask->second.committed&&!AtMission(p,ownTask->second)){
  Player*friendPlayer=u->GetCharmerOrOwnerPlayerOrPlayerItself();
  auto friendTask=friendPlayer?m_tasks.find(friendPlayer->GetGUIDLow()):m_tasks.end();
  if(friendTask==m_tasks.end()||
     (friendTask->second.squad!=ownTask->second.squad&&friendTask->second.objective!=ownTask->second.objective))return false;
 }
 Player*ally=u->GetCharmerOrOwnerPlayerOrPlayerItself();
 if(!ally||ally->GetBattleground()!=m_pBattleground||ally->GetBGTeamId()!=m_TeamID||!p->IsWithinLOSInMap(u)||p->GetDistance(u)>40)return false;
 auto a=m_tasks.find(p->GetGUIDLow()),b=m_tasks.find(ally->GetGUIDLow());
 if(a!=m_tasks.end()&&a->second.flagNode<15&&m_cappers[a->second.flagNode]==ally->GetGUIDLow())return true;
 if(a!=m_tasks.end()&&b!=m_tasks.end()&&a->second.flagNode<15&&
    a->second.flagNode==b->second.flagNode&&p->GetDistance(ally)<30)return true;
 if(a!=m_tasks.end()&&b!=m_tasks.end()&&
    (a->second.squad==b->second.squad||(a->second.objective==b->second.objective&&p->GetDistance(ally)<30)))return true;
 return p->GetDistance(ally)<12&&ally->GetHealthPct()<35;
}
bool CommandAV::SelfThreat(Player*p,Unit*u){
 if(!p||!u)return false;
 if(u->GetVictim()&&u->GetVictim()->GetCharmerOrOwnerPlayerOrPlayerItself()==p)return true;
 if(auto*attackers=p->getAttackers())for(Unit*a:*attackers)if(a==u)return true;
 return false;
}
bool CommandAV::CapturingEnemy(Player*enemy){
 if(!enemy||enemy->GetBattleground()!=m_pBattleground||enemy->GetBGTeamId()==m_TeamID)return false;
 bool channel=enemy->GetUInt32Value(UNIT_FIELD_EMOTE_STATE)==EMOTE_STATE_USE_STANDING;
 if(Spell*cast=enemy->GetCurrentSpell(CURRENT_GENERIC_SPELL))
  if(auto*info=cast->GetSpellInfo())channel=channel||info->HasEffect(SPELL_EFFECT_OPEN_LOCK);
 if(!channel)return false;
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);if(!bg)return false;
 for(unsigned i=0;i<15;++i)if(bg->m_Nodes[i].Owner==(m_TeamID==TEAM_ALLIANCE?ALLIANCE:HORDE)&&Dist(enemy,Points[Goals[i]])<8)return true;
 return false;
}
void CommandAV::Observe(Player*p,Task&t){
 if(t.fightHealth&&p->GetHealth()<t.fightHealth)t.lastDamageAt=getMSTime();t.fightHealth=p->GetHealth();
 uint32 now=getMSTime();if(t.sightAt&&now-t.sightAt<1500)return;t.sightAt=now?now:1;
 std::list<Player*>nearby;p->GetPlayerListInGrid(nearby,80.0f);
 for(Player*enemy:nearby)if(enemy->GetBGTeamId()!=m_TeamID&&enemy->GetBattleground()==m_pBattleground&&Visible(p,enemy)&&p->GetDistance(enemy)<80)
  m_contacts[enemy->GetGUIDLow()]={Pos(enemy),now};
}
unsigned CommandAV::EnemyCount(unsigned objective,uint32 now)const{
 unsigned count=0;for(auto const&contact:m_contacts)
  if(now-contact.second.seen<15000&&Gap(contact.second.position,Points[Goals[objective]])<75)++count;
 return count;
}
void CommandAV::SetTask(Player*p,unsigned objective,TacticalRole role,unsigned squad,bool garrison,bool committed){
 auto it=m_tasks.find(p->GetGUIDLow());
 bool same=it!=m_tasks.end()&&it->second.objective==objective&&it->second.role==role&&it->second.squad==squad&&it->second.committed==committed;
 if(!same){
  if(it!=m_tasks.end())Cancel(p,it->second);
  if(auto*ai=GetBotBGAI(p->GetGUIDLow()))ai->GetAIMovement()->ClearMovement();
  Task next;next.objective=objective;next.role=role;next.squad=squad;next.committed=committed;next.assigned=getMSTime();next.lastAdvanceAt=next.assigned;
  if(it!=m_tasks.end()){
   next.avoidObjective=it->second.avoidObjective;next.avoidUntil=it->second.avoidUntil;
   next.ignoredTarget=it->second.ignoredTarget;next.ignoreUntil=it->second.ignoreUntil;
   next.lastDamageAt=it->second.lastDamageAt;next.fightHealth=it->second.fightHealth;
   if(FlagAllowed(next,it->second.flagNode))next.flagNode=it->second.flagNode;
  }
  next.bestRemaining=TravelDistance(p,objective);m_tasks[p->GetGUIDLow()]=next;
  if (p->isAlive() && p->IsInWorld())
  {
      FelsoriaRegional::FullTextFacts facts;
      facts.cooldown=120;
      char const* key=nullptr;
      if ((role==Guard || garrison) && objective<7)
          key="felsoria_bg_av_objective_1";
      else if ((role==Guard || garrison) && objective<15)
          key="felsoria_bg_av_objective_2";
      else if (objective>=17 && objective<19 && EnemyCount(objective,getMSTime()))
          key="felsoria_bg_av_objective_4";
      else if (role==Push)
          key="felsoria_bg_av_objective_3";
      if (key)
          sRegionalBotChatMgr.FullEvent(p,key,3,facts);
  }
 }
 Task&t=m_tasks[p->GetGUIDLow()];t.roleAssigned=true;t.garrison=garrison;t.committed=committed;
 t.defender=role==Guard||role==Reserve||garrison;t.raider=role==Raid&&!garrison;
}
void CommandAV::BuildRoster(){
 struct Candidate{Player*p;Slot slot;bool picked=false;};std::vector<Candidate>all;
 for(auto const&e:m_PlayerGUIDs)if(Player*p=GetBGPlayer(e.first))
  if(p->GetBattleground()==m_pBattleground&&p->GetBGTeamId()==m_TeamID&&GetBotBGAI(e.first))all.push_back({p});
 std::sort(all.begin(),all.end(),[](Candidate const&a,Candidate const&b){return a.p->GetGUIDLow()<b.p->GetGUIDLow();});
 bool changed=all.size()!=m_roster.size();for(auto const&c:all){auto previous=m_roster.find(c.p->GetGUIDLow());
  changed=changed||previous==m_roster.end()||previous->second.specRole!=c.p->GetSpecializationRole();}
 if(!changed)return;auto old=m_roster;unsigned army=unsigned(all.size());
 unsigned quota[4]={0,RaiderBudget(army),GuardBudget(army),ReserveBudget(army)},used[4]={};
 quota[Push]=army-quota[Raid]-quota[Guard]-quota[Reserve];
 auto pick=[&](TacticalRole role,unsigned wing,int spec)->bool{
  Candidate*best=nullptr;int value=-100000;
  for(auto&c:all)if(!c.picked&&(spec<0||int(c.p->GetSpecializationRole())==spec)){
   auto previous=old.find(c.p->GetGUIDLow());int score=0;
   if(previous!=old.end()&&previous->second.role==role)score+=1000;
   if(previous!=old.end()&&previous->second.wing==wing)score+=100;
   if(role==Guard&&c.p->GetSpecializationRole()==ROLES_TANK)score+=60;
   if(c.p->GetSpecializationRole()==ROLES_DPS)score+=20;
   if(score>value){best=&c;value=score;}
  }
  if(!best)return false;best->picked=true;best->slot.role=role;best->slot.wing=wing;++used[role];return true;
 };
 unsigned healers=0,tanks=0;for(auto const&c:all){healers+=c.p->GetSpecializationRole()==ROLES_HEALER;tanks+=c.p->GetSpecializationRole()==ROLES_TANK;}
 unsigned wingSlots=0;for(unsigned wing=0;wing<2;++wing)wingSlots+=quota[Raid]/2+(wing<quota[Raid]%2)>=3;
 for(unsigned i=0;i<std::min(quota[Push],MainHealerBudget(healers,wingSlots,quota[Reserve]!=0));++i)pick(Push,0,ROLES_HEALER);
 for(unsigned i=0;i<std::min(2u,tanks)&&used[Push]<quota[Push];++i)pick(Push,0,ROLES_TANK);
 for(unsigned wing=0;wing<2;++wing){unsigned count=quota[Raid]/2+(wing<quota[Raid]%2),before=used[Raid];
  if(count>=3)pick(Raid,wing,ROLES_HEALER);
  if(used[Raid]-before<count)pick(Raid,wing,ROLES_TANK);
  while(used[Raid]-before<count)pick(Raid,wing,-1);
 }
 if(quota[Reserve])pick(Reserve,0,ROLES_HEALER);
 while(used[Push]<quota[Push]&&pick(Push,0,ROLES_DPS)){}
 for(TacticalRole role:{Guard,Reserve,Push})while(used[role]<quota[role])pick(role,0,-1);
 m_roster.clear();for(auto&c:all){c.slot.specRole=c.p->GetSpecializationRole();m_roster[c.p->GetGUIDLow()]=c.slot;}
}
unsigned CommandAV::BurnedTowers()const{
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);if(!bg)return 0;unsigned count=0;
 for(unsigned i=7;i<15;++i)if(EnemyTower(i,unsigned(m_TeamID)))count+=bg->m_Nodes[i].State==POINT_DESTROYED;
 return count;
}
bool CommandAV::AllTowersClaimed()const{
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);if(!bg)return false;
 unsigned own=m_TeamID==TEAM_ALLIANCE?ALLIANCE:HORDE;
 for(unsigned i=7;i<15;++i)if(EnemyTower(i,unsigned(m_TeamID)))
  if(bg->m_Nodes[i].State!=POINT_DESTROYED&&bg->m_Nodes[i].Owner!=own)return false;
 return true;
}
unsigned CommandAV::WingTarget(unsigned wing)const{
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);if(!bg)return 21;
 unsigned lanes[2][2]={{EnemyTowerOrder(unsigned(m_TeamID),0),EnemyTowerOrder(unsigned(m_TeamID),2)},
                       {EnemyTowerOrder(unsigned(m_TeamID),1),EnemyTowerOrder(unsigned(m_TeamID),3)}};
 unsigned own=m_TeamID==TEAM_ALLIANCE?ALLIANCE:HORDE;
 for(unsigned pass=0;pass<2;++pass)for(unsigned node:lanes[(wing+pass)%2])
  if(bg->m_Nodes[node].State!=POINT_DESTROYED&&bg->m_Nodes[node].Owner!=own)return node;
 return 21;
}
unsigned CommandAV::GraveyardTarget()const{
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);if(!bg)return 21;
 unsigned own=m_TeamID==TEAM_ALLIANCE?ALLIANCE:HORDE;
 // Capture every enemy graveyard along the faction's advance. Ownership is
 // read afresh: a recaptured earlier graveyard can never be skipped forever.
 unsigned chain[3]={m_TeamID==TEAM_ALLIANCE?4u:2u,m_TeamID==TEAM_ALLIANCE?5u:1u,m_TeamID==TEAM_ALLIANCE?6u:0u};
 for(unsigned node:chain)if(bg->m_Nodes[node].State!=POINT_DESTROYED&&bg->m_Nodes[node].Owner!=own)return node;
 return 21;
}
bool CommandAV::MissionAvailable(Task const&t){
 if(ObjectiveAvailable(t.objective))return true;
 unsigned general=m_TeamID==TEAM_ALLIANCE?18:17;
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);
 return bg&&t.objective==general&&!bg->m_CaptainAlive[1-unsigned(m_TeamID)]&&AllTowersClaimed();
}
bool CommandAV::FlagAllowed(Task const&t,unsigned node)const{
 return node<15&&(!(t.committed||t.role==Raid)||node==t.objective);
}
bool CommandAV::AtMission(Player*p,Task const&t){
 if(t.objective<15){auto const&goal=Points[Goals[t.objective]];
  return Dist(p,goal)<35&&std::abs(p->GetPositionZ()-goal.z)<6;
 }
 if(Creature*boss=ObjectiveCreature(t.objective))return p->GetDistance(boss)<65&&Visible(p,boss);
 return false;
}
bool CommandAV::AdvanceToObjective(Player*p){
 if(!m_pBattleground||m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS||!Valid(p))return false;
 auto it=m_tasks.find(p->GetGUIDLow());if(it==m_tasks.end())return false;Task&t=it->second;
 if(!t.committed||t.garrison||t.castAt||!MissionAvailable(t)||AtMission(p,t)){
  t.engagedAt=0;t.advanceUntil=0;return false;
 }
 uint32 now=getMSTime();Observe(p,t);
 if(p->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||p->HasAuraType(SPELL_AURA_MOD_ROOT))return false;
 if(t.advanceUntil&&int32(t.advanceUntil-now)<=0){t.advanceUntil=0;t.engagedAt=0;}
 bool threatened=false;
 if(Unit*u=p->GetSelectedUnit())threatened=Visible(p,u)&&p->IsValidAttackTarget(u);
 if(auto*attackers=p->getAttackers())for(Unit*u:*attackers)if(Visible(p,u)&&SelfThreat(p,u))threatened=true;
 if(!t.advanceUntil){
  if(!threatened){t.engagedAt=0;return false;}
  if(!t.engagedAt){t.engagedAt=now?now:1;t.engageOrigin=p->GetPosition();return false;}
  if(now-t.engagedAt<6000)return false;
  t.advanceUntil=now+6000;p->AttackStop();p->SetSelection(ObjectGuid::Empty);
  if(auto*ai=GetBotBGAI(p->GetGUIDLow()))ai->GetAIMovement()->ReleaseCombatMovement();
  t.chunk=false;t.route.clear();t.entry=false;t.chunkPath.clear();t.combat=false;t.combatAt=0;
 }
 t.activity="advance_under_fire";t.tickAt=now?now:1;
 p->AttackStop();p->SetSelection(ObjectGuid::Empty);
 bool waiting=t.objective>=15&&!GroupReady(p,t.objective);
 t.waiting=waiting;t.staging=m_staging[t.objective];Travel(p,t);return true;
}
bool CommandAV::CanCombatMove(Player*p,float x,float y,float z){
 if(!m_pBattleground||m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS)return true;
 auto it=m_tasks.find(p->GetGUIDLow());if(it==m_tasks.end())return true;Task const&t=it->second;
 if(t.castAt)return false;if(!t.committed||t.garrison)return true;
 if(!FelsoriaAVSafety::Finite({x,y,z}))return false;
 if(AtMission(p,t))return Gap({x,y,z},Points[Goals[t.objective]])<(t.objective<15?35.0f:65.0f);
 uint32 now=getMSTime();if(t.advanceUntil&&int32(t.advanceUntil-now)>0)return false;
 if(t.engagedAt&&Gap({x,y,z},{t.engageOrigin.GetPositionX(),t.engageOrigin.GetPositionY(),t.engageOrigin.GetPositionZ()})>20)return false;
 return true;
}
void CommandAV::Plan(){
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);if(!bg||bg->GetStatus()!=STATUS_IN_PROGRESS)return;
 BuildRoster();uint32 now=getMSTime();if(!m_startedAt)m_startedAt=now?now:1;unsigned own=m_TeamID==TEAM_ALLIANCE?ALLIANCE:HORDE;
 struct Member{Player*p;TacticalRole role;unsigned wing=0,squad=0;int goal=-1;bool garrison=false,committed=false;};
 std::vector<Member>members;
 for(auto const&e:m_roster)if(Player*p=GetBGPlayer(e.first))if(Valid(p)){
  Member m;m.p=p;m.role=e.second.role;m.wing=e.second.wing;
  m.squad=m.role==Raid?100+m.wing:m.role==Push?400:m.role==Guard?200:300;
  members.push_back(m);Observe(p,m_tasks[e.first]);
 }
 for(auto it=m_tasks.begin();it!=m_tasks.end();)if(std::none_of(members.begin(),members.end(),[&](Member const&m){return m.p->GetGUIDLow()==it->first;})){
  if(Player*p=GetBGPlayer(it->first))Cancel(p,it->second);m_joinCache.erase(it->first);it=m_tasks.erase(it);
 }else ++it;
 for(auto it=m_contacts.begin();it!=m_contacts.end();)if(now-it->second.seen>=15000)it=m_contacts.erase(it);else ++it;
 for(auto it=m_blocked.begin();it!=m_blocked.end();)if(int32(now-it->second)>=0)it=m_blocked.erase(it);else ++it;
 unsigned army=unsigned(members.size());if(!army)return;
 unsigned captain=m_TeamID==TEAM_ALLIANCE?16:15,general=m_TeamID==TEAM_ALLIANCE?18:17;
 bool captainAlive=ObjectiveAvailable(captain);unsigned burned=BurnedTowers(),phase=burned==4&&!captainAlive?2:captainAlive?0:1;
 unsigned home=m_TeamID==TEAM_ALLIANCE?0:6;
 unsigned friendlyFront=home;for(unsigned i=0;i<7;++i)if(bg->m_Nodes[i].Owner==own&&bg->m_Nodes[i].State==POINT_CONTROLED)
  if((m_TeamID==TEAM_ALLIANCE&&i>friendlyFront)||(m_TeamID==TEAM_HORDE&&i<friendlyFront))friendlyFront=i;
 unsigned assigned[21]={};
 auto assign=[&](Member&m,unsigned goal,bool garrison,bool committed,unsigned squad){m.goal=int(goal);m.garrison=garrison;m.committed=committed;m.squad=squad;++assigned[goal];};
 auto take=[&](unsigned goal,unsigned count,unsigned mask,bool garrison,bool committed,unsigned squad,int spec)->unsigned{
  unsigned added=0;while(added<count){Member*best=nullptr;float value=1e9f;
   for(auto&m:members)if(m.goal<0&&(mask&(1u<<unsigned(m.role)))&&(spec<0||int(m.p->GetSpecializationRole())==spec)){
    float d=TravelDistance(m.p,goal);if(d>=1e8f)continue;
    auto old=m_tasks.find(m.p->GetGUIDLow());float score=d;
    if(old!=m_tasks.end()&&old->second.objective==goal)score-=150;
    if(old!=m_tasks.end()&&old->second.garrison&&garrison&&old->second.objective==goal)score-=500;
    if(spec<0){score+=m.p->GetSpecializationRole()==ROLES_HEALER?1000:0;score+=m.p->GetSpecializationRole()==ROLES_TANK?500:0;}
    if(score<value){best=&m;value=score;}
   }
   if(!best)break;assign(*best,goal,garrison,committed,squad);++added;
  }return added;
 };
 auto finish=[&](){
  unsigned counts[4]={},garrisons=0,bosses=0,towers=0;
  for(auto&m:members){if(m.goal<0)assign(m,friendlyFront,false,false,m.squad);
   SetTask(m.p,unsigned(m.goal),m.role,m.squad,m.garrison,m.committed);
   ++counts[m.role];garrisons+=m.garrison;bosses+=unsigned(m.goal)==general;towers+=m.goal>=7&&m.goal<15;
  }
  bool report=Debug()&&(!m_roleLogAt||now-m_roleLogAt>=15000||m_lastArmy!=army||m_phase!=phase);
  m_phase=phase;
  if(report){m_roleLogAt=now?now:1;m_lastArmy=army;
   unsigned specs[4]={},captainSpecs[3]={},wingHealers[2]={},gy=0,bossHealers=0;
   for(auto const&m:members){unsigned role=m.p->GetSpecializationRole();++specs[std::min(3u,role)];gy+=m.goal>=0&&m.goal<7;
    if(m.goal==int(captain)&&role<3)++captainSpecs[role];
    for(unsigned wing=0;wing<2;++wing)wingHealers[wing]+=m.squad==100+wing&&role==ROLES_HEALER;
    bossHealers+=m.goal==int(general)&&role==ROLES_HEALER;
   }
   TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_BALANCE instance=%u team=%u elapsed_ms=%u captainAlive=%u burned=%u tanks=%u healers=%u dps=%u unknown=%u captainTanks=%u captainHealers=%u captainDps=%u wing0Healers=%u wing1Healers=%u graveTasks=%u bossHealers=%u",
    bg->GetInstanceID(),unsigned(m_TeamID),now-m_startedAt,unsigned(captainAlive),burned,specs[ROLES_TANK],specs[ROLES_HEALER],specs[ROLES_DPS],specs[3],
    captainSpecs[ROLES_TANK],captainSpecs[ROLES_HEALER],captainSpecs[ROLES_DPS],wingHealers[0],wingHealers[1],gy,bossHealers);
   TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_TACTICS_ROLES instance=%u team=%u phase=%u push=%u raid=%u guard=%u reserve=%u garrison=%u burned=%u towerTasks=%u bossTasks=%u observed=%u graveTarget=%u mines=disabled",
    bg->GetInstanceID(),unsigned(m_TeamID),phase,counts[Push],counts[Raid],counts[Guard],counts[Reserve],garrisons,burned,towers,bosses,unsigned(m_contacts.size()),burned==4?21u:GraveyardTarget());
   for(auto const&m:members){auto const&t=m_tasks[m.p->GetGUIDLow()];Unit*target=m.p->GetSelectedUnit();Creature*c=target?target->ToCreature():nullptr;
    TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_ACTIVITY instance=%u bot=%s role=%u squad=%u objective=%u flagNode=%u capper=%u committed=%u specRole=%u action=%s tickAge=%u waiting=%u casting=%u targetEntry=%u distance=%.1f x=%.3f y=%.3f z=%.3f",
     bg->GetInstanceID(),m.p->GetName(),unsigned(t.role),t.squad,t.objective,t.flagNode,t.flagNode<15?unsigned(m_cappers[t.flagNode]):0,unsigned(t.committed),unsigned(m.p->GetSpecializationRole()),t.activity,
     t.tickAt?now-t.tickAt:now,unsigned(t.waiting),unsigned(m.p->HasUnitState(UNIT_STATE_CASTING)),c?c->GetEntry():0,
     Dist(m.p,Points[Goals[t.objective]]),m.p->GetPositionX(),m.p->GetPositionY(),m.p->GetPositionZ());
   }
  }
 };
 // Once all four enemy towers have actually burned, every living bot
 // finishes the enemy captain if necessary and then attacks the general.
 // No graveyard capture/guard, tower garrison, or rescue task survives this.
 if(burned==4){
  for(auto&m:members)assign(m,captainAlive?captain:general,false,true,600);
  finish();return;
 }
 for(unsigned wing=0;wing<2;++wing){unsigned target=WingTarget(wing);auto&mission=m_wings[wing];
  if(mission.objective!=target){mission=Mission();mission.objective=target;mission.progressAt=now?now:1;}
  if(target>=15)continue;float best=1e9f;unsigned milestones=0;
  for(auto const&m:members)if(m.role==Raid&&m.wing==wing){
   best=std::min(best,TravelDistance(m.p,target));
   if(Dist(m.p,Points[Goals[target]])<40)milestones|=1;
   auto*flag=const_cast<GameObject*>(bg->GetNodeObjectByEnemyType(m.p,BG_AV_Nodes(target)));
   if(BannerInteraction(m.p,flag))milestones|=2;
   auto old=m_tasks.find(m.p->GetGUIDLow());if(old!=m_tasks.end()&&old->second.castAt&&CaptureNode(old->second)==target)milestones|=4;
  }
  if(best+8<mission.best||(milestones&~mission.milestones)){mission.best=std::min(best,mission.best);mission.milestones|=milestones;mission.progressAt=now?now:1;}
  if(Debug()&&now-mission.progressAt>=60000&&(!mission.logAt||now-mission.logAt>=15000)){
   mission.logAt=now?now:1;TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_TOWER_STALLED instance=%u team=%u wing=%u objective=%u stalled_ms=%u remaining=%.1f milestones=%u",
    bg->GetInstanceID(),unsigned(m_TeamID),wing,target,now-mission.progressAt,best,mission.milestones);
  }
 }
 unsigned raiders=0;for(auto const&m:members)raiders+=m.role==Raid;
 bool active=WingTarget(0)<15||WingTarget(1)<15;
 unsigned garrisonCap=active?raiders-std::min(4u,raiders):raiders,held=0;
 std::vector<unsigned>pendingTowers;
 for(unsigned i=0;i<4;++i){unsigned node=EnemyTowerOrder(unsigned(m_TeamID),i);
  if(bg->m_Nodes[node].Owner==own&&bg->m_Nodes[node].State==POINT_ASSAULTED)pendingTowers.push_back(node);
 }
 std::stable_sort(pendingTowers.begin(),pendingTowers.end(),[&](unsigned a,unsigned b){
  unsigned ea=EnemyCount(a,now),eb=EnemyCount(b,now);return ea!=eb?ea>eb:bg->m_Nodes[a].Timer<bg->m_Nodes[b].Timer;
 });
 // Round allocation gives every held tower its first/second guard before
 // any tower consumes third/fourth guards from the shared mobile budget.
 for(unsigned round=0;round<4&&held<garrisonCap;++round)for(unsigned node:pendingTowers){
  if(held>=garrisonCap||assigned[node]>round||assigned[node]>=GarrisonSize(EnemyCount(node,now)))continue;
  Member*best=nullptr;float value=1e9f;
  for(auto&m:members)if(m.goal<0&&m.role==Raid){float distance=TravelDistance(m.p,node);auto old=m_tasks.find(m.p->GetGUIDLow());
   bool alreadyHolding=old!=m_tasks.end()&&old->second.garrison&&old->second.objective==node&&Dist(m.p,Points[Goals[node]])<60;
   if(!alreadyHolding&&!CanRescue(distance,m.p->GetSpeed(MOVE_RUN),bg->m_Nodes[node].Timer))continue;
   float score=distance+(m.p->GetSpecializationRole()==ROLES_HEALER?1000.0f:0)+(m.p->GetSpecializationRole()==ROLES_TANK?500.0f:0);
   if(alreadyHolding)score-=1000;if(score<value){best=&m;value=score;}
  }
  if(best){assign(*best,node,true,false,700+node);++held;}
 }
 auto holdingTower=[&]()->unsigned{unsigned goal=21;float score=-1e9f;
  for(unsigned i=0;i<4;++i){unsigned n=EnemyTowerOrder(unsigned(m_TeamID),i);if(bg->m_Nodes[n].State!=POINT_DESTROYED&&bg->m_Nodes[n].Owner==own){
   float value=EnemyCount(n,now)*40.0f-assigned[n]*30.0f;if(value>score){score=value;goal=n;}
  }}return goal;
 };
 for(auto&m:members)if(m.role==Raid&&m.goal<0){unsigned goal=WingTarget(m.wing);
  if(goal<15)assign(m,goal,false,true,100+m.wing);
  else if(!captainAlive&&AllTowersClaimed())assign(m,general,false,true,600);
  else{goal=holdingTower();assign(m,goal<15?goal:captain,goal<15,goal>=15,goal<15?700+goal:400);}
 }
 // Home rescue only consumes the guard/reserve pool. Tower squads retain
 // their mission, even while fighting or waiting for a usable path.
 struct Rescue{float priority,distance;unsigned node,order;};std::vector<Rescue>rescue;
 for(unsigned n=0;n<15;++n)if(bg->m_Nodes[n].State==POINT_ASSAULTED&&bg->m_Nodes[n].TotalOwner==own&&bg->m_Nodes[n].Owner!=own){
  float distance=1e9f;for(auto const&m:members)if(m.goal<0&&(m.role==Guard||m.role==Reserve)){
   float d=TravelDistance(m.p,n);if(CanRescue(d,m.p->GetSpeed(MOVE_RUN),bg->m_Nodes[n].Timer))distance=std::min(distance,d);
  }
  rescue.push_back({(n>=7?1200.0f:n==friendlyFront?1100.0f:800.0f)-std::min(240000u,bg->m_Nodes[n].Timer)/1000.0f,distance,n,DefenceOrder(unsigned(m_TeamID),n)});
 }
 std::sort(rescue.begin(),rescue.end(),[](Rescue const&a,Rescue const&b){
  if(a.priority!=b.priority)return a.priority>b.priority;if(a.distance!=b.distance)return a.distance<b.distance;return a.order<b.order;
 });
 for(auto const&request:rescue){unsigned node=request.node,needed=GarrisonSize(EnemyCount(node,now));
  while(assigned[node]<needed){Member*best=nullptr;float value=1e9f;
   for(auto&m:members)if(m.goal<0&&(m.role==Guard||m.role==Reserve)){float distance=TravelDistance(m.p,node);
    if(CanRescue(distance,m.p->GetSpeed(MOVE_RUN),bg->m_Nodes[node].Timer)&&distance<value){best=&m;value=distance;}}
   if(!best)break;assign(*best,node,false,true,800+node);
  }
 }
 unsigned guardIndex=0;for(auto&m:members)if(m.role==Guard&&m.goal<0){unsigned node=(guardIndex++%2)?friendlyFront:home;assign(m,node,false,false,200+node);}
 for(auto&m:members)if(m.role==Push&&m.goal<0&&captainAlive)assign(m,captain,false,true,400);
 if(!captainAlive){
  unsigned grave=GraveyardTarget();
  if(grave<7){unsigned needed=std::min(6u,std::max(2u,army*15/100));
   if(assigned[grave]<needed)take(grave,1,1u<<Push,false,true,430,ROLES_HEALER);
   if(assigned[grave]<needed)take(grave,needed-assigned[grave],1u<<Push,false,true,430,-1);
  }
  for(auto&m:members)if(m.role==Push&&m.goal<0){unsigned a=WingTarget(0),b=WingTarget(1),goal=21;
   if(a<15&&b<15&&a!=b){
    float va=float(assigned[a])*100+TravelDistance(m.p,a)/20,vb=float(assigned[b])*100+TravelDistance(m.p,b)/20;
    goal=va<=vb?a:b;
   }else goal=a<15?a:b;
   if(goal<15)assign(m,goal,false,true,500+goal);
   else if(AllTowersClaimed())assign(m,general,false,true,600);
   else{goal=holdingTower();assign(m,goal<15?goal:friendlyFront,goal<15,false,goal<15?700+goal:400);}
  }
 }
 for(unsigned wing=0;wing<2;++wing){auto const&mission=m_wings[wing];if(mission.objective<15&&now-mission.progressAt>=60000)
  take(mission.objective,2,1u<<Reserve,false,true,100+wing,-1);
 }
 // Keep a small escort on newly assaulted graveyards while the mobile
 // capture party advances. Prefer the farthest forward pending graveyard;
 // these escorts come from reserve, never from either persistent tower wing.
 if(!captainAlive)for(unsigned step=0;step<3;++step){unsigned node=m_TeamID==TEAM_ALLIANCE?6-step:step;
  if(bg->m_Nodes[node].Owner==own&&bg->m_Nodes[node].State==POINT_ASSAULTED)
   take(node,2,1u<<Reserve,false,true,440+node,-1);
 }
 if(bg->m_Nodes[3].Owner!=own&&bg->m_Nodes[3].State!=POINT_DESTROYED&&EnemyCount(3,now)<3)
  take(3,std::min(2u,std::max(1u,army/20)),1u<<Reserve,false,false,310,-1);
 for(auto&m:members)if(m.goal<0){unsigned target=WingTarget(m.wing);
  if(m.role==Reserve&&target<15&&now-m_wings[m.wing].progressAt>=60000)assign(m,target,false,true,100+m.wing);
  else assign(m,friendlyFront,false,false,300);}
 // Balance the final pair using main-force support, preserving the two
 // persistent raid wings. An active capper is never moved for balancing.
 unsigned pair[2]={m_TeamID==TEAM_ALLIANCE?13u:7u,m_TeamID==TEAM_ALLIANCE?14u:8u};
 if(AttackNode(bg->m_Nodes[pair[0]].Owner,bg->m_Nodes[pair[0]].State,own)&&bg->m_Nodes[pair[0]].State!=POINT_DESTROYED&&
    AttackNode(bg->m_Nodes[pair[1]].Owner,bg->m_Nodes[pair[1]].State,own)&&bg->m_Nodes[pair[1]].State!=POINT_DESTROYED){
  unsigned totals[2]={};for(auto const&m:members)if(!m.garrison)for(unsigned side=0;side<2;++side)totals[side]+=m.goal==int(pair[side]);
  while(std::abs(int(totals[0])-int(totals[1]))>1){unsigned from=totals[0]>totals[1]?0:1,to=1-from;Member*best=nullptr;float value=1e9f;
   for(auto&m:members)if(m.role==Push&&!m.garrison&&m.goal==int(pair[from])){auto old=m_tasks.find(m.p->GetGUIDLow());if(old!=m_tasks.end()&&old->second.castAt)continue;
    float d=TravelDistance(m.p,pair[to]);if(d<value){best=&m;value=d;}}
   if(!best)break;--assigned[pair[from]];++assigned[pair[to]];best->goal=int(pair[to]);best->squad=500+pair[to];--totals[from];++totals[to];
  }
 }
 finish();
}

bool CommandAV::IsCombatTarget(Player*p,Unit*u){
 if(!p||!u||!m_pBattleground||!Visible(p,u)||!p->IsValidAttackTarget(u))return false;
 auto it=m_tasks.find(p->GetGUIDLow());Task const*t=it==m_tasks.end()?nullptr:&it->second;
 Player*owner=u->GetCharmerOrOwnerPlayerOrPlayerItself();Creature*c=u->ToCreature();
 if(owner&&(owner==p||owner->GetBattleground()!=m_pBattleground||owner->GetBGTeamId()==m_TeamID))return false;
 if(c&&(c->isSpiritService()||c->isTrainingDummy()||c->IsInEvadeMode()))return false;
 float distance=p->GetDistance(u);bool self=SelfThreat(p,u);uint32 now=getMSTime();
 if(c&&(c->GetEntry()==11948||c->GetEntry()==11946)&&!ObjectiveAvailable(m_TeamID==TEAM_ALLIANCE?18:17))return false;
 if(t&&t->committed&&!AtMission(p,*t)&&t->advanceUntil&&int32(t->advanceUntil-now)>0)return false;
 // A nearby capturable banner overrides incidental PvP, including an old
 // selection behind the squad. The capper owns the channel; escorts fight
 // only at the banner and never chase a retreating enemy away from it.
 if(t&&t->flagNode<15){
  auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);
  auto*flag=bg?bg->GetNodeObjectByEnemyType(p,BG_AV_Nodes(t->flagNode)):nullptr;
  if(flag&&flag->isSpawned()&&p->GetDistance(flag->GetPosition())<=24){
   if(t->castAt)return false;
   bool atBanner=u->GetDistance(flag->GetPosition())<=20&&std::abs(u->GetPositionZ()-flag->GetPositionZ())<=6;
   bool flagPursuitExpired=t->combatAt&&PursuitExpired(now-t->combatAt,p->GetDistance(t->combatOrigin));
   if(self&&distance<=35&&(!flagPursuitExpired||p->IsWithinMeleeRange(u)||(t->lastDamageAt&&now-t->lastDamageAt<3000)))return true;
   Player*flagVictim=u->GetVictim()?u->GetVictim()->GetCharmerOrOwnerPlayerOrPlayerItself():nullptr;
   if(flagVictim&&m_cappers[t->flagNode]==flagVictim->GetGUIDLow()&&distance<=35)return true;
   if(!atBanner||distance>30)return false;
   if(owner)return true;
   if(!c||c->GetCreatureType()==CREATURE_TYPE_CRITTER||c->GetCreatureType()==CREATURE_TYPE_BEAST||!c->IsHostileTo(p))return false;
   if(c->GetEntry()==11948||c->GetEntry()==11946||c->GetEntry()==11949||c->GetEntry()==11947)return false;
   for(auto const&info:BG_AV_CreatureInfo)if(info[0]==c->GetEntry())return true;
   for(auto const&info:BG_AV_StaticCreatureInfo)if(info[0]==c->GetEntry())return true;
   return false;
  }
 }
 Point objective=t?Points[Goals[t->objective]]:Pos(p);
 bool local=t&&Dist(p,objective)<55&&Gap({u->GetPositionX(),u->GetPositionY(),u->GetPositionZ()},objective)<50;
 bool expired=t&&t->combatAt&&!local&&PursuitExpired(now-t->combatAt,p->GetDistance(t->combatOrigin));
 // Self defence takes precedence in EVERY role, including a guard travelling
 // away from its base. Recent incoming damage permits a ranged counterattack;
 // a distant melee attacker retaining its victim pointer cannot kite forever.
 if(self&&distance<=45&&(!expired||p->IsWithinMeleeRange(u)||(t&&t->lastDamageAt&&now-t->lastDamageAt<3000)))return true;
 if(!t)return false;
 // An assigned, live PvE objective is not an incidental eight-second chase.
 if(c&&t->objective>=15&&u==ObjectiveCreature(t->objective))return ObjectiveAvailable(t->objective)&&GroupReady(p,t->objective);
 if(Player*enemy=u->ToPlayer())if(distance<=35&&CapturingEnemy(enemy)&&(!t->committed||AtMission(p,*t)))return true;
 if(t->ignoredTarget==u->GetGUID()&&int32(t->ignoreUntil-now)>0)return false;
 if(expired)return false;
 Player*victim=u->GetVictim()?u->GetVictim()->GetCharmerOrOwnerPlayerOrPlayerItself():nullptr;
 if(victim&&CanAssist(p,victim)&&distance<=35)return true;
 if(t->garrison||t->role==Guard||t->role==Reserve){if(!local)return false;}
 else if(t->raider){if(!local)return false;}
 if(owner){
  if(distance>35)return false;
  if(local)return true;
  if(t->role!=Push)return false;
  // A main-force enemy is relevant when it blocks the validated next route
  // chunk, or stands directly in the immediate forward approach.
  Point enemy={u->GetPositionX(),u->GetPositionY(),u->GetPositionZ()};
  for(unsigned i=1;i<t->chunkPath.size();++i){auto a=t->chunkPath[i-1],b=t->chunkPath[i];
   float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,den=dx*dx+dy*dy+dz*dz;
   float k=den>.01f?((enemy.x-a.x)*dx+(enemy.y-a.y)*dy+(enemy.z-a.z)*dz)/den:0;k=std::max(0.0f,std::min(1.0f,k));
   if(Gap(enemy,{a.x+k*dx,a.y+k*dy,a.z+k*dz})<10&&distance<28)return true;
  }
  Point here=Pos(p);float dx=objective.x-here.x,dy=objective.y-here.y,den=dx*dx+dy*dy;
  float ex=enemy.x-here.x,ey=enemy.y-here.y,k=den>.01f?(ex*dx+ey*dy)/den:0;
  return distance<25&&k>=0&&std::abs(enemy.z-here.z)<8&&std::abs(ex*dy-ey*dx)/std::sqrt(std::max(den,.01f))<10;
 }
 if(!c||c->GetCreatureType()==CREATURE_TYPE_CRITTER||c->GetCreatureType()==CREATURE_TYPE_BEAST||!c->IsHostileTo(p))return false;
 // Captains/generals never become incidental guard targets for another task.
 if(c->GetEntry()==11948||c->GetEntry()==11946||c->GetEntry()==11949||c->GetEntry()==11947)return false;
 bool guard=false;for(auto const&info:BG_AV_CreatureInfo)if(info[0]==c->GetEntry()){guard=true;break;}
 if(!guard)for(auto const&info:BG_AV_StaticCreatureInfo)if(info[0]==c->GetEntry()){guard=true;break;}
 return guard&&local;
}

void CommandAV::Cancel(Player*p,Task&t){
 if(t.castAt&&p&&p->GetUInt32Value(UNIT_FIELD_EMOTE_STATE)==EMOTE_STATE_USE_STANDING)p->SetUInt32Value(UNIT_FIELD_EMOTE_STATE,EMOTE_STATE_NONE);
 unsigned node=CaptureNode(t);
 if(node<15&&p&&m_cappers[node]==p->GetGUIDLow()){m_cappers[node]=0;m_capUntil[node]=0;}
 t.castAt=0;t.castTick=0;t.flag.Clear();
}
unsigned CommandAV::CaptureNode(Task const&t)const{return t.flagNode<15?t.flagNode:t.objective;}
bool CommandAV::BannerInteraction(Player*p,GameObject*flag){
 if(!p||!flag||!flag->isSpawned()||!p->InSamePhase(flag)||!p->InSamePhaseId(flag)||
    p->GetDistance(flag->GetPosition())>5.0f||std::abs(p->GetPositionZ()-flag->GetPositionZ())>3.0f)return false;
 // Use the same native object/range gate as a player's GO interaction. The
 // additional banner LOS check rejected close attempts in the live log.
 // Do not add a unit-target LOS requirement to this gameobject interaction.
 return p->GetGameObjectIfCanInteractWith(flag->GetGUID(),flag->GetGoType())==flag;
}
GameObject*CommandAV::NearbyFlag(Player*p,Task&t){
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);if(!bg)return nullptr;
 auto resolve=[&](unsigned node,float radius)->GameObject*{
  if(node>=15||!FlagAllowed(t,node)||!Capturable(bg->m_Nodes[node].Owner,bg->m_Nodes[node].State,p->GetBGTeam()))return nullptr;
  auto*flag=const_cast<GameObject*>(bg->GetNodeObjectByEnemyType(p,BG_AV_Nodes(node)));
  return flag&&flag->isSpawned()&&p->InSamePhase(flag)&&p->InSamePhaseId(flag)&&
   p->GetDistance(flag->GetPosition())<=radius&&std::abs(p->GetPositionZ()-flag->GetPositionZ())<=3.0f?flag:nullptr;
 };
 if(auto*flag=resolve(t.flagNode,24))return flag;
 if(t.castAt)Cancel(p,t);
 t.flagNode=15;t.coverAt=0;
 unsigned node=t.objective;GameObject*best=resolve(node,18);float distance=best?p->GetDistance(best->GetPosition()):12.0f;
 if(!best)for(unsigned i=0;i<15;++i)if(auto*flag=resolve(i,12)){
  float d=p->GetDistance(flag->GetPosition());if(!best||d<distance){best=flag;node=i;distance=d;}
 }
 if(best)t.flagNode=node;
 return best;
}
uint64 CommandAV::SelectCapper(Player*p,unsigned node,GameObject*flag){
 if(node>=15||!flag||!flag->isSpawned())return 0;
 uint32 now=getMSTime();
 auto handoff=[&](uint64 chosen,uint32 duration){
  if(m_cappers[node]&&m_cappers[node]!=chosen){auto old=m_tasks.find(m_cappers[node]);
   if(old!=m_tasks.end()&&old->second.castAt)if(Player*previous=GetBGPlayer(old->first)){
    CaptureBlocked(previous,old->second,flag,"capper_handoff");Cancel(previous,old->second);old->second.retry=now+1500;
   }
  }
  m_cappers[node]=chosen;m_capUntil[node]=chosen?now+duration:0;return chosen;
 };
 // A real player already opening this banner also receives an escort.
 std::list<Player*>nearby;p->GetPlayerListInGrid(nearby,20.0f);
 for(Player*ally:nearby)if(ally&&ally->isAlive()&&ally->GetBattleground()==m_pBattleground&&ally->GetBGTeamId()==m_TeamID&&
    BannerInteraction(ally,flag)&&p->IsWithinLOSInMap(ally))
  if(Spell*spell=ally->GetCurrentSpell(CURRENT_GENERIC_SPELL))if(auto*info=spell->GetSpellInfo())if(info->HasEffect(SPELL_EFFECT_OPEN_LOCK)){
   return handoff(ally->GetGUIDLow(),2000);
  }
 auto eligible=[&](Player*ally,Task const&task){return Valid(ally)&&FlagAllowed(task,node)&&
  ally->GetDistance(flag->GetPosition())<=18&&std::abs(ally->GetPositionZ()-flag->GetPositionZ())<=3&&
  (task.flagNode==node||task.objective==node||ally->GetDistance(flag->GetPosition())<=12)&&
  ally->InSamePhase(flag)&&ally->InSamePhaseId(flag)&&ally->CanUseBattlegroundObject()&&ally->GetHealthPct()>=30&&
  !ally->GetVehicleBase()&&!ally->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)&&
  !ally->HasAuraType(SPELL_AURA_MOD_ROOT)&&!CaptureThreat(ally,task)&&
  !(task.retry&&int32(task.retry-now)>0);};
 if(m_cappers[node]&&int32(m_capUntil[node]-now)>0){
  Player*current=GetBGPlayer(m_cappers[node]);auto old=m_tasks.find(m_cappers[node]);
  if(current&&old!=m_tasks.end()&&eligible(current,old->second)&&
     (!old->second.castAt||old->second.flag==flag->GetGUID()))return m_cappers[node];
 }
 uint64 chosen=0;float best=1e9f;
 for(auto const&slot:m_PlayerGUIDs){auto it=m_tasks.find(slot.first);if(it==m_tasks.end())continue;
  Player*ally=GetBGPlayer(slot.first);auto const&task=it->second;
  if(!eligible(ally,task)||(task.castAt&&CaptureNode(task)!=node))continue;
  // Keep healers free to cover when an unpressured damage dealer is present.
  float score=ally->GetDistance(flag->GetPosition())+(ally->GetSpecializationRole()==ROLES_HEALER?20.0f:0.0f);
  if(score<best||(score==best&&ally->GetGUIDLow()<chosen)){chosen=ally->GetGUIDLow();best=score;}
 }
 return handoff(chosen,12000);
}
bool CommandAV::CoverFlag(Player*p,Task&t){
 if(t.flagNode>=15)return false;
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);auto*ai=GetBotBGAI(p->GetGUIDLow());if(!bg||!ai)return false;
 auto*flag=bg->GetNodeObjectByEnemyType(p,BG_AV_Nodes(t.flagNode));if(!flag||!flag->isSpawned())return false;
 auto*move=ai->GetAIMovement();uint32 now=getMSTime();
 if(t.combat||!t.coverAt){move->ClearMovement();t.route.clear();t.chunk=false;t.entry=false;t.chunkPath.clear();t.combat=false;t.combatAt=0;t.combatTarget.Clear();}
 t.activity="flag_cover";ai->Dismount();
 if(t.coverAt&&now-t.coverAt<6000)return true;
 t.coverAt=now?now:1;
 if(p->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||p->HasAuraType(SPELL_AURA_MOD_ROOT))return true;
 G3D::Vector3 start=p->GetVector3();
 if(p->movespline&&!p->movespline->Finalized()){auto v=p->movespline->ComputePosition();start={v.x,v.y,v.z};}
 for(unsigned i=0;i<16;++i){
  unsigned side=(p->GetGUIDLow()+i)%8;float angle=float(side)*.7853981634f,radius=i<8?6.0f:9.0f;
  G3D::Vector3 goal(flag->GetPositionX()+radius*std::cos(angle),flag->GetPositionY()+radius*std::sin(angle),flag->GetPositionZ());
  if((goal-start).squaredLength()<2.25f)return true;
  std::vector<G3D::Vector3>path;
  if(!move->BuildAVGroundLeg(start,goal,path)||path.size()<2||path.size()>64||PathLength(path)>25)continue;
  auto const&end=path.back();if(std::abs(end.z-flag->GetPositionZ())>3||(end-goal).squaredLength()>4)continue;
  if(!move->MoveAVRecordedChunk(path))continue;
  t.coverGoal=Position(end.x,end.y,end.z,0);return true;
 }
 // An unsafe tower-edge position is rejected by the unchanged native mover.
 // Hold the current safe spot and keep scanning/healing instead of all capping.
 return true;
}
bool CommandAV::Capture(Player*p,Task&t){
 unsigned nodeId=CaptureNode(t);if(nodeId>=15)return false;
 if(!FlagAllowed(t,nodeId)){Cancel(p,t);t.flagNode=15;return false;}auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);if(!bg)return false;
 auto const&node=bg->m_Nodes[nodeId];
 GameObject*flag=const_cast<GameObject*>(bg->GetNodeObjectByEnemyType(p,BG_AV_Nodes(nodeId)));
 uint32 now=getMSTime();
 if(flag&&!t.castAt&&BannerInteraction(p,flag)){
  p->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);p->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
 }
 bool available=flag&&flag->isSpawned()&&BannerInteraction(p,flag)&&
  p->InSamePhase(flag)&&p->InSamePhaseId(flag)&&p->CanUseBattlegroundObject()&&!p->GetVehicleBase()&&
  !p->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)&&!p->HasAuraType(SPELL_AURA_MOD_ROOT);
 bool same=flag&&flag->GetGUID()==t.flag;
 uint64 capper=SelectCapper(p,nodeId,flag);
 if(t.castAt&&capper!=p->GetGUIDLow()){CaptureBlocked(p,t,flag,"capper_handoff");Cancel(p,t);t.retry=now+1500;return false;}
 bool threatened=CaptureThreat(p,t);
 if(t.castAt){
  if(!ChannelValid(available,same,threatened,p->GetHealth()<t.health,p->GetDistance(t.castOrigin),now-t.castTick)){
   CaptureBlocked(p,t,flag,!available?"interaction_invalid":!same?"banner_changed":threatened?"active_attacker":p->GetHealth()<t.health?"damage":"moved_or_tick_gap");
   Cancel(p,t);t.retry=now+1500;return false;}
  t.castTick=now;t.health=p->GetHealth();m_capUntil[nodeId]=now+12000;
  t.activity="capture";if(now-t.castAt<8000)return true;
  // Re-resolve the currently spawned banner and BG ownership immediately before
  // the native AV event. No triggered spell or direct node-state assignment.
  if(!Capturable(node.Owner,node.State,p->GetBGTeam())){Cancel(p,t);return false;}
  unsigned before=node.Owner,state=node.State;
  Cancel(p,t);t.retry=now+1500;bool remove=false;bg->EventPlayerClickedOnFlag(p,flag,remove);m_planTimer=0;
  bool changed=before!=node.Owner||state!=node.State;
  if(Debug()||!changed)TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_CAPTURE_RESULT bot=%s node=%u changed=%u owner=%u state=%u entry=%u",p->GetName(),nodeId,unsigned(changed),node.Owner,unsigned(node.State),flag->GetEntry());
  return true;
 }
 if(!available||threatened||p->GetHealthPct()<30||(t.retry&&int32(t.retry-now)>0)){
  if(flag&&p->GetDistance(flag->GetPosition())<18)
   CaptureBlocked(p,t,flag,!flag?"missing_banner":!flag->isSpawned()?"banner_despawned":!BannerInteraction(p,flag)?"native_interaction_range_or_height":!p->CanUseBattlegroundObject()?"native_use_rejected":!available?"cc_cast_or_phase":threatened?"active_attacker":p->GetHealthPct()<30?"low_health":"retry");
  return false;
 }
 if(capper!=p->GetGUIDLow())return false;
 auto*ai=GetBotBGAI(p->GetGUIDLow());if(!ai)return false;
 ai->Dismount();ai->GetAIMovement()->ClearMovement();p->StopMoving();p->AttackStop();p->SetSelection(ObjectGuid::Empty);
 t.approachFlag.Clear();t.approachAt=0;t.activity="capture";
 p->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);p->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
 p->SetFacingToObject(flag);p->SetUInt32Value(UNIT_FIELD_EMOTE_STATE,EMOTE_STATE_USE_STANDING);
 t.castAt=now?now:1;t.castTick=now;t.castOrigin=p->GetPosition();t.health=p->GetHealth();t.flag=flag->GetGUID();
 m_cappers[nodeId]=p->GetGUIDLow();m_capUntil[nodeId]=now+12000;t.chunk=false;t.route.clear();
 if(Debug())TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_CAPTURE_START bot=%s node=%u",p->GetName(),nodeId);
 return true;
}
void CommandAV::CaptureBlocked(Player*p,Task&t,GameObject*flag,char const*reason){
 unsigned nodeId=CaptureNode(t);uint32 now=getMSTime();if(!Debug()||(t.captureLogAt&&now-t.captureLogAt<10000))return;
 t.captureLogAt=now?now:1;
 auto*ai=GetBotBGAI(p->GetGUIDLow());
 TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_CAPTURE_BLOCKED instance=%u bot=%s node=%u reason=%s distance=%.2f los=%u nativeInteract=%u capper=%u canUse=%u movement=%s x=%.3f y=%.3f z=%.3f",
  m_pBattleground->GetInstanceID(),p->GetName(),nodeId,reason,flag?p->GetDistance(flag):-1.0f,
  unsigned(flag&&p->IsWithinLOSInMap(flag)),unsigned(BannerInteraction(p,flag)),nodeId<15?unsigned(m_cappers[nodeId]):0,unsigned(p->CanUseBattlegroundObject()),
  ai?ai->GetAIMovement()->GetAVMovementRejectReason():"no_ai",p->GetPositionX(),p->GetPositionY(),p->GetPositionZ());
}
bool CommandAV::ApproachFlag(Player*p,Task&t){
 unsigned nodeId=CaptureNode(t);if(nodeId>=15||t.castAt||m_cappers[nodeId]!=p->GetGUIDLow())return false;
 auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);auto*ai=GetBotBGAI(p->GetGUIDLow());if(!bg||!ai)return false;
 auto*flag=bg->GetNodeObjectByEnemyType(p,BG_AV_Nodes(nodeId));
 if(!flag||!flag->isSpawned()||!p->InSamePhase(flag)||!p->InSamePhaseId(flag)||
    p->GetDistance(flag->GetPosition())>18||std::abs(p->GetPositionZ()-flag->GetPositionZ())>3)return false;
 // Already in interaction range: leave an available capper stationary.
 if(BannerInteraction(p,const_cast<GameObject*>(flag)))return false;
 if(p->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||p->HasAuraType(SPELL_AURA_MOD_ROOT))return false;
 uint32 now=getMSTime();auto*move=ai->GetAIMovement();
 if(t.approachFlag==flag->GetGUID()&&p->movespline&&!p->movespline->Finalized()){
  if(p->GetDistance(t.progress)>.6f){t.progress=p->GetPosition();t.movedAt=now;}
  if(now-t.movedAt<4000){t.activity="flag_approach";return true;}
 }
 if(t.approachAt&&now-t.approachAt<750)return true;
 t.approachAt=now?now:1;
 G3D::Vector3 current=p->GetVector3();
 if(p->movespline&&!p->movespline->Finalized()){auto v=p->movespline->ComputePosition();current={v.x,v.y,v.z};}
 // A recording is a route endpoint, not proof of banner LOS. Walk around the
 // actual live banner when necessary; all candidate paths retain the native
 // floor, wall, path-length and spline validation. No direct teleport/Z fix.
 for(unsigned i=0;i<8;++i){
  unsigned side=(t.approachSide+i+p->GetGUIDLow())%8;float angle=float(side)*.7853981634f;
  G3D::Vector3 goal(flag->GetPositionX()+2.25f*std::cos(angle),flag->GetPositionY()+2.25f*std::sin(angle),flag->GetPositionZ());
  if((goal-current).squaredLength()<1.44f)continue;
  std::vector<G3D::Vector3>path;
  if(!move->BuildAVGroundLeg(current,goal,path)||path.size()<2||path.size()>64||PathLength(path)>35)continue;
  ai->Dismount();if(!move->MoveAVRecordedChunk(path))continue;
  t.approachSide=(t.approachSide+i+1)%8;t.approachFlag=flag->GetGUID();t.progress=p->GetPosition();t.movedAt=now;
  t.route.clear();t.chunk=false;t.entry=false;t.chunkPath.clear();t.activity="flag_approach";return true;
 }
 CaptureBlocked(p,t,const_cast<GameObject*>(flag),"no_ground_banner_approach");return false;
}
bool CommandAV::CaptureThreat(Player*p,Task const&t){
 uint32 now=getMSTime();if((t.lastDamageAt&&now-t.lastDamageAt<2000)||(t.fightHealth&&p->GetHealth()<t.fightHealth))return true;
 if(auto*attackers=p->getAttackers())for(Unit*u:*attackers)
  if(Visible(p,u)&&u->isInCombat()&&p->GetDistance(u)<=45&&SelfThreat(p,u))return true;
 return false;
}
bool CommandAV::UpdateCapture(Player*p){
 if(!m_pBattleground||m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS||!Valid(p))return false;
 auto it=m_tasks.find(p->GetGUIDLow());if(it==m_tasks.end()){Plan();it=m_tasks.find(p->GetGUIDLow());}if(it==m_tasks.end())return false;
 Task&t=it->second;t.tickAt=getMSTime()?getMSTime():1;Observe(p,t);
 if(t.castAt&&Capture(p,t))return true;
 GameObject*flag=NearbyFlag(p,t);if(!flag)return false;
 unsigned node=CaptureNode(t);
 // Commit the local flag task before nominating its capper. Waiting until
 // five yards left a lone stealthed bot ineligible to approach from nine.
 if(!t.castAt){p->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);p->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);}
 uint64 capper=SelectCapper(p,node,flag);
 if(capper==p->GetGUIDLow()){
  if(Capture(p,t))return true;
  if(!CaptureThreat(p,t)&&ApproachFlag(p,t))return true;
 }
 t.activity="flag_cover";return false;
}
void CommandAV::RouteFailure(Player*p,Task&t,char const*reason){
 uint32 now=getMSTime();
 if(!t.logAt||now-t.logAt>=15000){
  t.logAt=now?now:1;
  auto*ai=GetBotBGAI(p->GetGUIDLow());
  char const*detail=ai?ai->GetAIMovement()->GetAVMovementRejectReason():"no_ai";
  TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_MOVEMENT_REJECT instance=%u bot=%s objective=%u reason=%s detail=%s x=%.3f y=%.3f z=%.3f",p->GetInstanceId(),p->GetName(),t.objective,reason,detail,p->GetPositionX(),p->GetPositionY(),p->GetPositionZ());
 }
}
G3D::Vector3 CommandAV::RoutePoint(Player*p,Task const&t,unsigned cursor){
 unsigned vertex=t.route[cursor];Point point=Points[vertex];
 // Finish objective approaches on the recorded point, especially tower stairs.
 if(cursor+1==t.route.size()||Gap(point,Points[Destination(t)])<20.0f)return Vec(point);
 Point before=Points[t.route[cursor?cursor-1:cursor]],after=Points[t.route[cursor+1]];
 float dx=after.x-before.x,dy=after.y-before.y,length=std::sqrt(dx*dx+dy*dy);
 if(length<.1f)return Vec(point);float nx=-dy/length,ny=dx/length;
 unsigned seed=FelsoriaAVSafety::ScatterSeed(p->GetGUIDLow()^(p->GetInstanceId()*0x9e3779b9u));
 float phase=float(seed&0xffffu)*(.0000958738f);
 float side=1.35f*(.65f*std::sin(point.x*.035f+point.y*.027f+phase)+.35f*std::sin(point.x*.017f-point.y*.031f+phase*1.7f));
 if(t.raider&&Dist(p,Points[Destination(t)])>45.0f){
  std::list<Player*>nearby;p->GetPlayerListInGrid(nearby,70.0f);
  unsigned fighters=0,enemies=0;float weight=0,projection=0;
  for(Player*other:nearby){if(other==p||!other->isAlive()||!other->isInCombat()||other->GetMap()!=p->GetMap()||!p->InSamePhase(other)||!p->InSamePhaseId(other))continue;
   float ox=other->GetPositionX()-point.x,oy=other->GetPositionY()-point.y,dist=std::sqrt(ox*ox+oy*oy);
   if(dist>25.0f||std::abs(other->GetPositionZ()-point.z)>6.0f)continue;
   float w=1.0f-dist/25.0f;weight+=w;projection+=(ox*nx+oy*ny)*w;++fighters;
   if(other->GetBGTeamId()!=m_TeamID)++enemies;
  }
  if(fighters>=3&&enemies){float direction=std::abs(projection)>.1f?(projection>0?-1.0f:1.0f):(seed&1u?1.0f:-1.0f);
   side+=direction*std::min(5.0f,weight*2.0f);
  }
 }
 return {point.x+nx*side,point.y+ny*side,point.z};
}
void CommandAV::Travel(Player*p,Task&t){
 auto*ai=GetBotBGAI(p->GetGUIDLow());if(!ai)return;auto*move=ai->GetAIMovement();uint32 now=getMSTime();
 if(p->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||p->HasAuraType(SPELL_AURA_MOD_ROOT)||
    p->GetVehicleBase()||p->GetTransport()||p->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR))return;
 if(t.combat){t.route.clear();t.chunk=false;t.entry=false;t.chunkPath.clear();t.combat=false;t.combatAt=0;t.combatTarget.Clear();}
 G3D::Vector3 current=p->GetVector3();
 if(p->movespline&&!p->movespline->Finalized()){
  auto v=p->movespline->ComputePosition();current={v.x,v.y,v.z};
 }
 std::vector<G3D::Vector3>path,leg;bool continuing=false,speedChanged=false;
 if(t.chunk){
  if(p->GetDistance(t.progress)>.6f){t.progress=p->GetPosition();t.movedAt=now;}
  bool ended=!p->movespline||p->movespline->Finalized();
  speedChanged=std::abs(t.speed-p->GetSpeed(MOVE_RUN))>=.1f;
  bool hasMore=!t.chunkEntry||t.chunkEnd+1<t.route.size();
  bool extend=!ended&&hasMore&&p->movespline->Duration()-p->movespline->GetTimePassed()<=750;
  if(!ended&&now-t.movedAt<4000){
   if(!extend&&!speedChanged)return;
   // Preserve every unconsumed bend when extending a running spline. Replacing
   // it before its last half second avoids a stop between 65-yard chunks.
   float best=1e20f;unsigned segment=t.chunkCursor;
   for(unsigned i=t.chunkCursor;i+1<t.chunkPath.size();++i){
    auto delta=t.chunkPath[i+1]-t.chunkPath[i];float sq=delta.squaredLength();
    float fraction=sq>.00001f?std::max(0.0f,std::min(1.0f,(current-t.chunkPath[i]).dot(delta)/sq)):0;
    float distance=(current-(t.chunkPath[i]+delta*fraction)).squaredLength();
    if(distance<best){best=distance;segment=i;}
   }
   if(best<=6.25f){
    path.push_back(current);
    for(unsigned i=segment+1;i<t.chunkPath.size();++i)
     if((path.back()-t.chunkPath[i]).squaredLength()>=.0625f)path.push_back(t.chunkPath[i]);
    t.chunkCursor=segment;t.cursor=t.chunkEnd;t.entry=t.chunkEntry;continuing=true;
   }
  }
  if(!continuing){
   if(ended&&p->GetDistance(t.chunkGoal)<2.0f){
    t.cursor=t.chunkEnd;if(t.chunkEntry)t.entry=true;t.failures=0;
   }else{
    if(!ended||now-t.movedAt>=4000){
     if(!t.route.empty()){
      if(!t.entry)t.badJoins[t.route.front()]=now+15000;
      else if(t.cursor+1<t.route.size())m_blocked[Key(t.route[t.cursor],t.route[t.cursor+1])]=now+15000;
     }
     ++t.failures;RouteFailure(p,t,"no_spline_progress_replan");
    }
    move->ClearMovement();t.route.clear();t.entry=false;
   }
   t.chunk=false;t.chunkPath.clear();
  }
 }
 if(!continuing&&t.retry&&int32(t.retry-now)>0)return;
 if(t.route.empty()){
  std::vector<std::pair<float,unsigned>>candidates;
  bool cave=CaveEntry(m_pBattleground,m_TeamID,current);
  float radius=cave?115.0f:t.failures>=2?80.0f:45.0f;
  for(unsigned i=0;i<PointCount;++i){float d=Dist(p,Points[i]);auto bad=t.badJoins.find(i);
   if(bad!=t.badJoins.end()&&int32(bad->second-now)>0)continue;
   if(d<radius&&TeamCosts[m_TeamID][t.objective][i]<1e8f)candidates.push_back({d,i});
  }
  std::sort(candidates.begin(),candidates.end());
  // Join the known own-cave anchor before attempting farther points outside
  // the door. All other candidates keep their native-path requirement.
  if(cave)std::stable_sort(candidates.begin(),candidates.end(),[&](std::pair<float,unsigned>const&a,std::pair<float,unsigned>const&b){
   return (a.second==GateStarts[m_TeamID])>(b.second==GateStarts[m_TeamID]);
  });
  auto blocked=[&](unsigned a,unsigned b){
   if(t.objective!=3&&(SnowfallApproachPoint(a)||SnowfallApproachPoint(b)))return true;
   auto it=m_blocked.find(Key(a,b));return it!=m_blocked.end()&&int32(it->second-now)>0;
  };
  for(unsigned c=0;c<candidates.size()&&c<(t.failures>=2?48u:24u);++c){unsigned v=candidates[c].second;
   if(!EntryLeg(move,m_pBattleground,m_TeamID,v,current,leg)||PathLength(leg)>std::max(12.0f,candidates[c].first*2.5f+5)){
    if(cave&&v==GateStarts[m_TeamID])RouteFailure(p,t,"cave_entry_ground");
    // Advance past rejected nearby recordings on the next attempt instead
    // of spending every retry on the same first 24 points at a cave seam.
    t.badJoins[v]=now+5000;continue;
   }
   if(FindRoute(v,Destination(t),PointCount,RouteOffsets(m_TeamID),RouteEdges(m_TeamID),blocked,t.route))break;
  }
  if(t.route.empty()){++t.failures;t.retry=now+750;RouteFailure(p,t,"no_ground_join_or_route");return;}
  t.cursor=0;t.entry=false;
 }
 if(t.cursor>=t.route.size()){t.route.clear();t.entry=false;return;}
 unsigned first=t.route[t.cursor],end=t.cursor;
 if(path.empty())path.push_back(current);
 bool entryComplete=t.entry,extended=false;
 float travelled=PathLength(path);
 if(!t.entry){
  if(!EntryLeg(move,m_pBattleground,m_TeamID,first,path.back(),leg)){
   if(continuing)return;
   t.badJoins[first]=now+10000;++t.failures;t.route.clear();t.retry=now+500;RouteFailure(p,t,"ground_join");return;
  }
  unsigned used=1;
  while(used<leg.size()&&path.size()<64){
   float step=(leg[used]-path.back()).length();
   if(travelled+step>65.0f&&path.size()>1)break;
   if(step>=.25f){path.push_back(leg[used]);travelled+=step;extended=true;}++used;
  }
  entryComplete=used==leg.size();
 }
 bool failed=false;
 if(entryComplete)for(unsigned i=t.cursor+1;i<t.route.size();++i){unsigned a=t.route[i-1],b=t.route[i];
  auto lane=RoutePoint(p,t,i);float width=t.raider?8.0f:4.0f;
  bool valid=RecordedLeg(move,path.back(),lane,leg)&&Corridor(leg,Points[a],Points[b],width);
  if(!valid)valid=RecordedLeg(move,path.back(),Vec(Points[b]),leg)&&Corridor(leg,Points[a],Points[b],width);
  if(!valid){
   failed=true;m_blocked[Key(a,b)]=now+30000;
   if(travelled<.1f){t.route.clear();t.entry=false;t.chunk=false;++t.failures;t.retry=now+500;RouteFailure(p,t,"ground_edge_or_corridor");return;}
   break;
  }
  unsigned used=1;
  while(used<leg.size()&&path.size()<64){
   float step=(leg[used]-path.back()).length();
   if(travelled+step>65.0f&&path.size()>1)break;
   if(step>=.25f){path.push_back(leg[used]);travelled+=step;extended=true;}++used;
  }
  if(used!=leg.size())break;
  end=i;if(travelled>=60.0f||path.size()>=60)break;
 }
 // A failed extension leaves the existing safe spline running to its endpoint.
 if(continuing&&!extended&&!speedChanged)return;
 if(path.size()<2||travelled<.1f){
  if(!failed&&entryComplete)t.entry=true;
  return;
 }
 if(!move->MoveAVRecordedChunk(path)){t.retry=now+500;RouteFailure(p,t,"spline_launch");return;}
 auto const&last=path.back();t.chunkGoal=Position(last.x,last.y,last.z,0);
 t.chunk=true;t.chunkEntry=entryComplete;t.chunkEnd=end;t.chunkCursor=0;t.chunkPath=path;
 t.progress=p->GetPosition();t.movedAt=now;t.speed=p->GetSpeed(MOVE_RUN);t.retry=0;
}
bool CommandAV::UpdateBot(Player*p){
 if(!m_pBattleground||m_pBattleground->GetMapId()!=30||m_pBattleground->GetStatus()!=STATUS_IN_PROGRESS||!Valid(p))return false;
 auto it=m_tasks.find(p->GetGUIDLow());if(it==m_tasks.end()){Plan();it=m_tasks.find(p->GetGUIDLow());}if(it==m_tasks.end())return true;
 Task&t=it->second;auto*ai=GetBotBGAI(p->GetGUIDLow());uint32 now=getMSTime();
 t.tickAt=now?now:1;
 Observe(p,t);
 if(UpdateCapture(p))return true;
 if(AdvanceToObjective(p))return true;
 bool leaderFight=t.objective>=15&&ObjectiveAvailable(t.objective)&&p->GetSelectedUnit()==ObjectiveCreature(t.objective);
 if(!leaderFight&&t.combatAt&&Dist(p,Points[Goals[t.objective]])>=55&&PursuitExpired(now-t.combatAt,p->GetDistance(t.combatOrigin))){
  Unit*selected=p->GetSelectedUnit();
  bool pressed=selected&&SelfThreat(p,selected)&&(p->IsWithinMeleeRange(selected)||(t.lastDamageAt&&now-t.lastDamageAt<3000));
  if(!pressed){t.ignoredTarget=t.combatTarget;t.ignoreUntil=now+12000;t.combatTarget.Clear();t.combatAt=0;
   if(selected){p->AttackStop();p->SetSelection(ObjectGuid::Empty);ai->GetAIMovement()->ReleaseCombatMovement();}
  }
 }
 // A native-valid channel owns the capper's tick; escorts handle unrelated
 // enemies. Real damage/combat still cancels it through Capture().
 Unit*enemy=nullptr;float best=-1e9f;std::list<Player*>players;p->GetPlayerListInGrid(players,45.0f);
 auto consider=[&](Unit*u){if(!IsCombatTarget(p,u))return;float d=p->GetDistance(u);
  float score=60-d+(u==p->GetSelectedUnit()?15.0f:0);
  if(t.objective>=15&&u==ObjectiveCreature(t.objective))score+=900;
  if(SelfThreat(p,u))score+=p->GetHealthPct()<35?4000.0f:1000.0f;
  Player*victim=u->GetVictim()?u->GetVictim()->GetCharmerOrOwnerPlayerOrPlayerItself():nullptr;
  if(victim&&CanAssist(p,victim))score+=victim->GetHealthPct()<35?600.0f:250.0f;
  if(t.flagNode<15&&victim&&m_cappers[t.flagNode]==victim->GetGUIDLow())score+=2000;
  if(Player*other=u->ToPlayer()){
   if(CapturingEnemy(other))score+=800;
   if(other->GetSpecializationRole()==ROLES_HEALER)score+=100;
   if(other->GetHealthPct()<20)score+=80;
  }
  if(score>best){best=score;enemy=u;}};
 if(auto*attackers=p->getAttackers())for(Unit*u:*attackers)consider(u);
 consider(p->GetSelectedUnit());for(Player*other:players)consider(other);
 std::list<Creature*>creatures;Trinity::AllWorldObjectsInRange check(p,30.0f);
 Trinity::CreatureListSearcher<Trinity::AllWorldObjectsInRange>search(p,creatures,check);p->VisitNearbyGridObject(30.0f,search);
 for(Creature*c:creatures)consider(c);
 if(t.objective>=15)if(Creature*c=ObjectiveCreature(t.objective))if(p->GetDistance(c)<=60)consider(c);
 if(enemy){Cancel(p,t);
  if(!t.combatAt){t.combatAt=now?now:1;t.combatOrigin=p->GetPosition();}
  t.combatTarget=enemy->GetGUID();t.combat=true;t.activity="combat";t.approachFlag.Clear();
  ai->Dismount();
  if(p->GetSelectedUnit()!=enemy){ai->GetAIMovement()->ClearMovement();p->AttackStop();p->SetSelection(enemy->GetGUID());}
  return false;
 }
 if(p->GetSelectedUnit()){p->AttackStop();p->SetSelection(ObjectGuid::Empty);t.combat=true;}
 // Combat flags can outlive a distant attacker. Keep moving toward the task
 // without falling through to generic searches for attackable wildlife.
 if(p->isInCombat())Cancel(p,t);
 if(t.flagNode<15&&CoverFlag(p,t))return true;
 if(!MissionAvailable(t)){m_planTimer=0;t.assigned=0;Plan();return true;}
 bool waiting=t.objective>=15&&!GroupReady(p,t.objective);
 if(waiting!=t.waiting){ai->GetAIMovement()->ClearMovement();t.route.clear();t.chunk=false;t.entry=false;t.chunkPath.clear();}
 t.waiting=waiting;t.staging=m_staging[t.objective];
 float remaining=Dist(p,Points[Destination(t)]);
 if(t.objective>=15&&!waiting&&remaining<40){Creature*target=ObjectiveCreature(t.objective);
  if(target&&IsCombatTarget(p,target)){
   ai->Dismount();ai->GetAIMovement()->ClearMovement();p->SetSelection(target->GetGUID());t.combat=true;
   t.combatAt=now?now:1;t.combatOrigin=p->GetPosition();t.combatTarget=target->GetGUID();return false;
  }
  // A recorded endpoint can be outside the captain's room or its doorway.
  // Reach the actual NPC through the existing native ground/LOS validator;
  // never idle at the endpoint merely because the leader is behind a wall.
  if(target&&target->isAlive()&&remaining<=3.0f&&p->GetDistance(target)<65){
   ai->Dismount();t.chunk=false;t.route.clear();t.entry=false;t.chunkPath.clear();
   ai->GetAIMovement()->MovementTo(target->GetPositionX(),target->GetPositionY(),target->GetPositionZ());return true;
  }
 }
 bool guarding=false;
 if(t.objective<15){auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);guarding=t.defender&&bg->m_Nodes[t.objective].Owner==p->GetBGTeam()&&remaining<18.0f;}
 if(ApproachFlag(p,t))return true;
 if(remaining>3.0f&&!guarding){if(!p->isInCombat()&&!p->IsMounted()&&ai->TryUpMount()){
   t.chunk=false;t.chunkPath.clear();t.route.clear();t.entry=false;return true;
  }t.activity=waiting?"rally":"travel";Travel(p,t);return true;}
 // Several defenders use different nearby recorded samples, rather than
 // repeatedly assigning random off-road coordinates or walking off a tower.
 if(t.objective<15){auto*bg=dynamic_cast<BattlegroundAV*>(m_pBattleground);auto const&node=bg->m_Nodes[t.objective];
  if(node.Owner!=p->GetBGTeam()){
   t.activity="flag_wait";
  }else if(t.defender){
   if(!t.guardAt||now-t.guardAt>uint32(6000+p->GetGUIDLow()%5000)){
    t.guardAt=now;t.guardPoint=Goals[t.objective];unsigned bestPoint=0;
    for(unsigned i=0;i<PointCount;++i){float d=Gap(Points[i],Points[Goals[t.objective]]);
     if(d<4||d>12||std::abs(Points[i].z-Points[Goals[t.objective]].z)>2||TeamCosts[m_TeamID][t.objective][i]>20)continue;
     unsigned score=unsigned((p->GetGUIDLow()*13+now/8000+i*17)%997);
     if(score>bestPoint){bestPoint=score;t.guardPoint=i;}
    }
   }
   auto const&point=Points[t.guardPoint];ai->GetAIMovement()->MovementTo(point.x,point.y,point.z);
  }
 }
 if(waiting)t.activity="rally";else if(guarding)t.activity="guard";
 return true;
}
