#ifndef FELSORIA_COMMAND_AV_H
#define FELSORIA_COMMAND_AV_H
#include "CommandBG.h"
#include "BattlegroundAlteracValley.h"
#include "FelsoriaAVPolicy.h"
#include <map>
#include <vector>
class CommandAV : public CommandBG {
public:
 CommandAV(Battleground* bg,TeamId team);
 ~CommandAV();
 void Initialize() override;
 void StartGame() override;
 void Update(uint32 diff) override;
 void OnPlayerDead(uint64 guid) override;
 void OnPlayerRevive(Player* player) override;
 AIWaypoint* GetReadyPosition() override {return nullptr;}
 Creature const* GetMatchGraveyardNPC(Player const* player) override;
 bool CanDireFlee() override {return false;}
 bool CanUpMount(Player* player) override;
 bool UpdateBot(Player* player);
 bool UpdateCapture(Player* player);
 bool AdvanceToObjective(Player* player);
 bool CanCombatMove(Player* player,float x,float y,float z);
 bool IsCombatTarget(Player* player,Unit* target);
 bool CanAssist(Player* player,Unit* target);
private:
 struct Task {
  unsigned objective=3,cursor=0,chunkEnd=0,flagNode=15;
  uint32 assigned=0,retry=0,castAt=0,castTick=0,movedAt=0,logAt=0,guardAt=0;
  unsigned guardPoint=0;
  FelsoriaAV::TacticalRole role=FelsoriaAV::Push;
  unsigned squad=0,staging=0,avoidObjective=21;
  uint32 sightAt=0,combatAt=0,ignoreUntil=0,lastAdvanceAt=0,avoidUntil=0,lastDamageAt=0;
  uint32 tickAt=0,captureLogAt=0,approachAt=0;
  unsigned approachSide=0;
  char const* activity="assigned";
  bool roleAssigned=false,garrison=false,waiting=false;
  float bestRemaining=1e9f;
  ObjectGuid combatTarget,ignoredTarget,approachFlag;
  Position combatOrigin;
  uint64 health=0;
  uint64 fightHealth=0;
  bool active=false,combat=false,chunk=false,entry=false,chunkEntry=false,defender=false,raider=false;
  float speed=0;
  ObjectGuid flag;
  Position castOrigin,progress,chunkGoal,coverGoal;
  uint32 coverAt=0;
  bool committed=false;
  uint32 engagedAt=0,advanceUntil=0;
  Position engageOrigin;
  unsigned chunkCursor=0,failures=0;
  std::vector<unsigned>route;
  std::vector<G3D::Vector3>chunkPath;
  std::map<unsigned,uint32>badJoins;
 };
 std::map<uint64,Task>m_tasks;
 struct Slot {FelsoriaAV::TacticalRole role=FelsoriaAV::Push;unsigned wing=0,specRole=3;};
 std::map<uint64,Slot>m_roster;
 struct Mission {unsigned objective=21,milestones=0;float best=1e9f;uint32 progressAt=0,logAt=0;};
 Mission m_wings[2];
 unsigned m_phase=0;
 std::map<uint64,uint32>m_blocked;
 struct Contact {FelsoriaAV::Point position;uint32 seen=0;};
 std::map<uint64,Contact>m_contacts;
 struct JoinCache {FelsoriaAV::Point position;uint32 at=0;unsigned vertex[2]={};float gap[2]={};};
 std::map<uint64,JoinCache>m_joinCache;
 unsigned m_staging[21]={};
 uint32 m_rallyAt[21]={};
 uint64 m_cappers[15]={};
 uint32 m_capUntil[15]={};
 uint32 m_startedAt=0;
 uint32 m_planTimer=0;
 uint32 m_roleLogAt=0;
 unsigned m_lastArmy=0,m_lastDefenders=0,m_lastRaiders=0;
 bool Valid(Player* player);
 bool ObjectiveAvailable(unsigned objective);
 void Plan();
 void BuildRoster();
 unsigned BurnedTowers() const;
 unsigned WingTarget(unsigned wing) const;
 unsigned GraveyardTarget() const;
 bool AllTowersClaimed() const;
 bool MissionAvailable(Task const& task);
 bool FlagAllowed(Task const& task,unsigned node) const;
 bool AtMission(Player* player,Task const& task);
 void Cancel(Player* player,Task& task);
 bool Capture(Player* player,Task& task);
 unsigned CaptureNode(Task const& task) const;
 GameObject* NearbyFlag(Player* player,Task& task);
 bool BannerInteraction(Player* player,GameObject* flag);
 uint64 SelectCapper(Player* player,unsigned node,GameObject* flag);
 bool CoverFlag(Player* player,Task& task);
 bool ApproachFlag(Player* player,Task& task);
 void CaptureBlocked(Player* player,Task& task,GameObject* flag,char const* reason);
 void Travel(Player* player,Task& task);
 G3D::Vector3 RoutePoint(Player* player,Task const& task,unsigned cursor);
 void RouteFailure(Player* player,Task& task,char const* reason);
 Creature* ObjectiveCreature(unsigned objective) const;
 bool FinishingBoss();
 bool GroupReady(Player* player,unsigned objective);
 bool CaptureThreat(Player* player,Task const& task);
 bool SelfThreat(Player* player,Unit* target);
 bool CapturingEnemy(Player* enemy);
 void Observe(Player* player,Task& task);
 unsigned EnemyCount(unsigned objective,uint32 now) const;
 float TravelDistance(Player* player,unsigned objective);
 unsigned Destination(Task const& task) const;
 void SetTask(Player* player,unsigned objective,FelsoriaAV::TacticalRole role,unsigned squad,bool garrison,bool committed=false);
};
#endif
