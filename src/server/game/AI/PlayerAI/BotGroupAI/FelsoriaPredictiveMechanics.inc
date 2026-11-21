// FELSORIA_PREDICTIVE_MECHANICS_V1 — read-only detection, ordinary movement/casts.
#include "FelsoriaMechanicsGeometry.h"
#include "AreaTrigger.h"
#include "MoveSpline.h"
#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "Spell.h"
#include "SpellInfo.h"
#include <map>
#include <tuple>
#include <memory>

namespace FelsoriaMechanicRuntime {
using namespace FelsoriaMechanics;
struct Objects {uint32 tick=0;std::vector<ObjectGuid> areas,creatures;};
using Key=std::tuple<uint32,uint32,ObjectGuid,int,int>;
// Map update threads never share mutable cache state. No stored WorldObject pointers.
static thread_local std::map<Key,Objects> objects;
inline Objects const& nearby(Player* bot,uint32 now){
 Key key(bot->GetMapId(),bot->GetInstanceId(),bot->GetGroup()->GetGUID(),
         int(std::floor(bot->GetPositionX()/25)),int(std::floor(bot->GetPositionY()/25)));
 auto& cache=objects[key];
 if(!cache.tick || uint32(now-cache.tick)>=100){
  cache.tick=now;cache.areas.clear();cache.creatures.clear();
  std::list<AreaTrigger*> ats;std::list<Creature*> mobs;
  Trinity::AllWorldObjectsInRange check(bot,160.0f);
  Trinity::AreaTriggerListSearcher<Trinity::AllWorldObjectsInRange> atSearch(bot,ats,check);
  Trinity::CreatureListSearcher<Trinity::AllWorldObjectsInRange> mobSearch(bot,mobs,check);
  bot->VisitNearbyObject(160.0f,atSearch);bot->VisitNearbyGridObject(160.0f,mobSearch);
  for(auto at:ats)if(at && at->IsInWorld())cache.areas.push_back(at->GetGUID());
  for(auto c:mobs)if(c && c->IsInWorld() && c->isAlive())cache.creatures.push_back(c->GetGUID());
  if(objects.size()>64)for(auto it=objects.begin();it!=objects.end();){
   if(uint32(now-it->second.tick)>30000)it=objects.erase(it);else ++it;
  }
 }
 return cache;
}
inline Vec point(Position const& p){return {p.GetPositionX(),p.GetPositionY(),p.GetPositionZ()};}
inline bool harmful(SpellInfo const* s,unsigned depth=0){
 if(!s)return false;
 if(s->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE)||s->HasEffect(SPELL_EFFECT_INSTAKILL)||
    s->HasEffect(SPELL_EFFECT_ENVIRONMENTAL_DAMAGE)||s->HasEffect(SPELL_EFFECT_HEALTH_LEECH)||
    s->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL)||s->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE)||
    s->HasEffect(SPELL_EFFECT_WEAPON_PERCENT_DAMAGE)||s->HasEffect(SPELL_EFFECT_NORMALIZED_WEAPON_DMG)||
    s->HasEffect(SPELL_EFFECT_POWER_BURN)||s->HasAura(SPELL_AURA_PERIODIC_LEECH)||
    s->HasAura(SPELL_AURA_PERIODIC_WEAPON_PERCENT_DAMAGE)||s->HasEffect(SPELL_EFFECT_KNOCK_BACK)||
    s->HasEffect(SPELL_EFFECT_KNOCK_BACK_DEST)||s->HasAura(SPELL_AURA_PERIODIC_DAMAGE)||
    s->HasAura(SPELL_AURA_PERIODIC_DAMAGE_PERCENT)||s->HasAura(SPELL_AURA_MOD_STUN)||
    s->HasAura(SPELL_AURA_MOD_ROOT)||s->HasAura(SPELL_AURA_MOD_FEAR)||
    s->HasAura(SPELL_AURA_MOD_CONFUSE))return true;
 if(depth<2)for(uint8 i=0;i<MAX_SPELL_EFFECTS;++i){
  auto e=s->GetEffect(i);if(e && e->TriggerSpell && e->TriggerSpell!=s->Id &&
    harmful(sSpellMgr->GetSpellInfo(e->TriggerSpell),depth+1))return true;
 }
 return false;
}
inline bool damaging(SpellInfo const* s,unsigned depth=0){
 if(!s)return false;
 if(s->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE)||s->HasEffect(SPELL_EFFECT_INSTAKILL)||
    s->HasEffect(SPELL_EFFECT_ENVIRONMENTAL_DAMAGE)||s->HasEffect(SPELL_EFFECT_HEALTH_LEECH)||
    s->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL)||s->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE)||
    s->HasEffect(SPELL_EFFECT_WEAPON_PERCENT_DAMAGE)||s->HasEffect(SPELL_EFFECT_NORMALIZED_WEAPON_DMG)||
    s->HasEffect(SPELL_EFFECT_POWER_BURN)||s->HasAura(SPELL_AURA_PERIODIC_LEECH)||
    s->HasAura(SPELL_AURA_PERIODIC_WEAPON_PERCENT_DAMAGE)||s->HasAura(SPELL_AURA_PERIODIC_DAMAGE)||
    s->HasAura(SPELL_AURA_PERIODIC_DAMAGE_PERCENT))return true;
 if(depth<2)for(uint8 i=0;i<MAX_SPELL_EFFECTS;++i){
  auto e=s->GetEffect(i);if(e && e->TriggerSpell && e->TriggerSpell!=s->Id &&
   damaging(sSpellMgr->GetSpellInfo(e->TriggerSpell),depth+1))return true;
 }
 return false;
}
inline Shape shape(AreaTrigger* at){
 auto info=at->GetAreaTriggerInfo();Shape s;s.radius=at->GetRadius();
 s.height=info.Polygon.Height>0?info.Polygon.Height:6.0f;
 if(info.polygon){
  auto const& v=info.Polygon.VerticesTarget.empty()?info.Polygon.Vertices:info.Polygon.VerticesTarget;
  for(size_t i=0;i<v.size();++i){
   // Native active-vertex mask; unset mask is treated conservatively as all vertices.
   if(at->ActivePointPolygon.size()==v.size() && !at->ActivePointPolygon[i])continue;
   s.polygon.push_back({v[i].Pos.m_positionX,v[i].Pos.m_positionY});
  }
  if(s.polygon.size()<3)s.polygon.clear();
 }else if(info.hasAreaTriggerBox && info.AllowBoxCheck){
  auto a=info.box.low(),b=info.box.high();
  s.polygon={{a.x,a.y},{b.x,a.y},{b.x,b.y},{a.x,b.y}};
  s.height=std::max(std::fabs(a.z),std::fabs(b.z));
 }
 return s;
}
inline Hazard area(AreaTrigger* at){
 Hazard h;h.shape=shape(at);h.active=std::max(0.0f,float(at->GetBotActivationDelay())/1000-0.2f);
 h.expires=at->GetDuration()<0?100000.0f:float(at->GetDuration())/1000;
 h.moving=at->isMoving();auto info=at->GetAreaTriggerInfo();
 for(unsigned i=0;i<=15;++i){
  Position pos=at->GetBotFuturePosition(i*100);
  // Native boxes are axis aligned; native polygons rotate around the trigger.
  h.frames.push_back({point(pos),info.polygon?pos.GetOrientation():0.0f});
 }
 return h;
}
inline bool castIs(Creature* c,uint32 id){
 if(c->HasAura(id))return true;
 for(uint8 type:{uint8(CURRENT_GENERIC_SPELL),uint8(CURRENT_CHANNELED_SPELL)})
  if(auto s=c->GetCurrentSpell(type))if(s->getState()!=SPELL_STATE_FINISHED && s->GetSpellInfo()->Id==id)return true;
 return false;
}
inline bool safeAreaId(uint32 map,uint32 spell,uint32 entry){
 if(map==1456 && spell==193064)return true;
 if(map!=1477)return false;
 return spell==203955 || spell==197968 || spell==197971 || spell==197972 ||
        spell==197975 || spell==197977 || spell==193781 || entry==9758;
}
inline bool globalMechanic(uint32 map,uint32 spell){
 return (map==1456 && (spell==193597 || spell==193611)) ||
        (map==1477 && (spell==192304 || spell==200901 || spell==193826 || spell==193827 || spell==197961));
}
// V12 shared detector: ordinary chase/follow cannot re-enter a zone just escaped.
inline bool hostileArea(Player* bot,AreaTrigger* at,Hazard& h){
 uint32 id=at->GetSpellId(),map=bot->GetMapId();
 if(!at->IsInWorld() || !bot->InSamePhase(at) || globalMechanic(map,id) ||
    safeAreaId(map,id,at->GetRealEntry()))return false;
 Unit* caster=at->GetCaster();if(!caster || !caster->IsHostileTo(bot))return false;
 // FELSORIA_CORDANA_FULL_V2: the wall visual follows a moving ghost, not its own AT spline.
 Creature* ghost=caster->ToCreature();
 bool cordanaWall=map==1493 && id==213628 && ghost && ghost->GetEntry()==100336;
 bool harm=cordanaWall || damaging(at->GetSpellInfo());auto info=at->GetAreaTriggerInfo();
 for(auto const& action:info.actions){
  if(action.actionType!=AT_ACTION_TYPE_CAST_SPELL && action.actionType!=AT_ACTION_TYPE_CAST_SPELL_NOT_TRIGGER &&
     action.actionType!=AT_ACTION_TYPE_OWNER_CAST_SPELL && action.actionType!=AT_ACTION_TYPE_SHARE_DAMAGE)continue;
  if(action.targetFlags&AT_TARGET_FLAG_FRIENDLY)continue;
  harm=harm || action.actionType==AT_ACTION_TYPE_SHARE_DAMAGE || damaging(sSpellMgr->GetSpellInfo(action.spellId));
 }
 if(!harm)return false;
 h=area(at);
 if(cordanaWall && ghost->movespline && ghost->movespline->Initialized() && !ghost->movespline->Finalized()){
  h.moving=true;h.frames.clear();
  float dx=at->GetPositionX()-ghost->GetPositionX(),dy=at->GetPositionY()-ghost->GetPositionY();
  float dz=at->GetPositionZ()-ghost->GetPositionZ();
  for(unsigned i=0;i<=15;++i){
   Movement::MoveSpline future(*ghost->movespline);future.updateState(i*100);
   Movement::Location loc=future.ComputePosition();Position pos;
   pos.Relocate(loc.x+dx,loc.y+dy,loc.z+dz,loc.orientation);
   h.frames.push_back({point(pos),info.polygon?pos.GetOrientation():0.0f});
  }
 }
 return h.shape.radius>0 || !h.shape.polygon.empty();
}
inline void appendCastDangers(Player* bot,std::vector<Creature*> const& creatures,std::vector<Hazard>& dangers){
 for(auto c:creatures){
  if(!c->isInCombat() || !c->IsHostileTo(bot))continue;
  for(uint8 type:{uint8(CURRENT_GENERIC_SPELL),uint8(CURRENT_CHANNELED_SPELL)}){
   Spell* cast=c->GetCurrentSpell(type);if(!cast || cast->getState()==SPELL_STATE_FINISHED)continue;
   auto spell=cast->GetSpellInfo();if(!spell || globalMechanic(bot->GetMapId(),spell->Id))continue;
   for(uint8 i=0;i<MAX_SPELL_EFFECTS;++i){
    auto e=spell->GetEffect(i,bot->GetMap()->GetDifficultyID());
    if(!e || !e->IsTargetingArea())continue;
    // A heal/buff effect of a mixed spell is not a damaging footprint.
    bool damage=e->Effect==SPELL_EFFECT_SCHOOL_DAMAGE || e->Effect==SPELL_EFFECT_ENVIRONMENTAL_DAMAGE ||
      e->Effect==SPELL_EFFECT_HEALTH_LEECH || e->Effect==SPELL_EFFECT_INSTAKILL ||
      e->ApplyAuraName==SPELL_AURA_PERIODIC_DAMAGE || e->ApplyAuraName==SPELL_AURA_PERIODIC_DAMAGE_PERCENT;
    uint32 a=e->TargetA.GetTarget(),b=e->TargetB.GetTarget();
    bool dest=a==TARGET_UNIT_DEST_AREA_ENEMY || b==TARGET_UNIT_DEST_AREA_ENEMY;
    if(!damage || !dest || !cast->m_targets.HasDst())continue;
    float r=e->CalcRadius(c,cast);if(r<=0 || r>30)continue;
    Position center=*cast->m_targets.GetDstPos();Hazard h;h.shape.radius=r;
    h.active=type==CURRENT_CHANNELED_SPELL?0:std::max(0.0f,float(cast->GetFelsoriaRemainingCastTime())/1000-0.2f);
    h.expires=std::max(h.active+0.5f,float(cast->GetFelsoriaRemainingCastTime())/1000);
    for(unsigned k=0;k<=15;++k)h.frames.push_back({point(center),0});
    dangers.push_back(std::move(h));break;
   }
  }
 }
}
}

// FELSORIA_PARTY_MOVEMENT_COMBAT_V12
bool BotGroupAI::AllowsDungeonRoute(std::vector<FelsoriaMechanics::Vec> const& route)
{
 using namespace FelsoriaMechanics;using namespace FelsoriaMechanicRuntime;
 if(!me->GetMap() || !me->GetMap()->IsDungeon() || me->InBattleground() || me->InArena())return true;
 if(m_MechanicOwned || !m_Teleporting.CanMovement() || me->IsBeingTeleported() ||
    me->IsChangeMap() || !me->IsInWorld() || !me->isAlive() || route.size()<2)return false;
 for(auto const& p:route)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;
 // During combat a route cannot abandon the human; distant bots may move inward.
 if(me->isInCombat() && m_MasterPlayer && m_MasterPlayer->IsInWorld() && m_MasterPlayer->GetMap()==me->GetMap()){
  float limit=(IsTankBotAI() || IsMeleeBotAI())?30.0f:40.0f;
  Vec here=point(me->GetPosition()),leader=point(m_MasterPlayer->GetPosition());
  if(!withinPartyLeash(here,route.back(),leader,limit))return false;
  float corridor=std::max(limit,distance(here,leader)+0.25f);
  for(auto const& p:route)if(distance(p,leader)>corridor)return false;
 }
 if(!me->GetGroup())return true;
 uint32 now=getMSTime();auto const& cache=nearby(me,now);
 std::vector<Hazard> dangers;std::vector<Creature*> creatures;
 for(auto guid:cache.areas)if(auto at=ObjectAccessor::GetAreaTrigger(*me,guid)){
  Hazard h;if(hostileArea(me,at,h))dangers.push_back(std::move(h));
 }
 for(auto guid:cache.creatures)if(auto c=ObjectAccessor::GetCreature(*me,guid))
  if(c->IsInWorld() && c->isAlive() && me->InSamePhase(c))creatures.push_back(c);
 appendCastDangers(me,creatures,dangers);
 bool noEscape=m_MechanicBlockedUntil && int32(now-m_MechanicBlockedUntil)<0;
 return pathSafe(route,std::max(0.1f,me->GetSpeed(MOVE_RUN)),dangers,noEscape);
}

bool BotGroupAI::ProcessPredictiveMechanics()
{
 using namespace FelsoriaMechanics;using namespace FelsoriaMechanicRuntime;
 auto release=[&](){
  if(m_MechanicMoving){m_Movement->ClearMovement();me->StopMoving();}
  m_MechanicOwned=false;m_MechanicMoving=false;m_RdfAvoidMoving=false;
 };
 if(!me->IsInWorld() || !me->isAlive() || !me->GetGroup() || !me->GetMap() ||
    !me->GetMap()->IsDungeon() || me->InBattleground() || me->InArena()){
  if(me->IsInWorld())release();else {m_MechanicOwned=false;m_MechanicMoving=false;}
  return false;
 }
 if(me->IsBeingTeleported() || me->IsChangeMap() || !m_Teleporting.CanMovement() ||
    me->GetVehicleBase() || me->IsFlying() || m_StopFollow || m_ForceFlee ||
    m_CruxMovement.HasCruxMovement() || !me->IsSettingFinish()){
  // Transfer/lift/explicit command owns movement; never clear that movement here.
  m_MechanicOwned=false;m_MechanicMoving=false;m_RdfAvoidMoving=false;return false;
 }
 if(m_MechanicMap!=me->GetMapId() || m_MechanicInstance!=me->GetInstanceId()){
  m_MechanicTick=0;m_MechanicOwned=false;m_MechanicMoving=false;
  m_MechanicMap=me->GetMapId();m_MechanicInstance=me->GetInstanceId();
 }
 uint32 now=getMSTime();
 if(m_MechanicTick && !RdfRotationPolicy::elapsed(now,m_MechanicTick,100))return m_MechanicOwned;
 m_MechanicTick=now;
 if(m_MechanicBlockedUntil && int32(now-m_MechanicBlockedUntil)<0)return false;
 m_MechanicBlockedUntil=0;
 auto const& cache=nearby(me,now);uint32 map=me->GetMapId();
 std::vector<AreaTrigger*> areas;std::vector<Creature*> creatures;
 for(auto guid:cache.areas)if(auto at=ObjectAccessor::GetAreaTrigger(*me,guid))
  if(at->IsInWorld() && me->InSamePhase(at) && me->GetDistance(at)<120)areas.push_back(at);
 for(auto guid:cache.creatures)if(auto c=ObjectAccessor::GetCreature(*me,guid))
  if(c->isAlive() && c->IsInWorld() && me->InSamePhase(c) && me->GetDistance(c)<120)creatures.push_back(c);
 std::vector<Hazard> dangers;uint32 request=0; // 193597 sand, 203955 eye, matching rune, 193781 Aegis.
 bool avoidDunes=false,ragnarok=false;
 for(auto c:creatures){
  if(map==1456 && c->GetEntry()==91789 && c->isInCombat()){
   if(castIs(c,193597))request=193597;
   if(castIs(c,193611))avoidDunes=true;
  }
  if(map==1477 && c->GetEntry()==95676 && c->isInCombat()){
   uint32 auras[]={197963,197964,197965,197966,197967};
   uint32 triggers[]={197968,197971,197972,197975,197977};
   for(unsigned i=0;i<5;++i)if(me->HasAura(auras[i]))request=triggers[i];
  }
  if(map==1477 && c->GetEntry()==95675 && c->isInCombat() && castIs(c,193826))ragnarok=true;
 }
 std::vector<Hazard> shelters;
 for(auto at:areas){
  uint32 id=at->GetSpellId();auto info=at->GetAreaTriggerInfo();
  bool friendlyShield=map==1477 && (id==193781 || at->GetRealEntry()==9758 || at->GetCustomEntry()==9758);
  if(friendlyShield){
   Unit* owner=at->GetCaster();Player* player=owner?owner->GetCharmerOrOwnerPlayerOrPlayerItself():nullptr;
   if(!player || !me->GetGroup()->IsMember(player->GetGUID()))continue;
  }
  if(safeAreaId(map,id,at->GetRealEntry()) || friendlyShield){
   if(map==1477 && id==203955 && (!request || request==203955))request=203955;
   if(ragnarok && friendlyShield && !request)request=193781;
   if((request==193597 && id==193064) || id==request || (request==193781 && friendlyShield))
    shelters.push_back(area(at));
   if(avoidDunes && id==193064){Hazard h=area(at);h.active=0;dangers.push_back(h);}
   continue;
  }
  Hazard h;if(hostileArea(me,at,h))dangers.push_back(std::move(h));
 }
 if(ragnarok && !request)request=193781; // Missing real shield is a blocked mechanic, not a made-up shelter.
 appendCastDangers(me,creatures,dangers);
 Vec here=point(me->GetPosition());bool threatened=false;
 for(auto const& h:dangers){
  // Visual footprint triggers an immediate move even before activation.
  if(h.contains(here,0)){threatened=true;break;}
  for(unsigned i=0;i<15;++i)if(h.crossing(here,here,float(i)/10,float(i+1)/10)){threatened=true;break;}
 }
 if(!threatened && !request && me->movespline && !me->movespline->Finalized()){
  auto end=me->movespline->FinalDestination();std::vector<Vec> route{here,{end.x,end.y,end.z}};
  if(!pathSafe(route,std::max(0.1f,me->GetSpeed(MOVE_RUN)),dangers))threatened=true;
 }
 if(!threatened && !request){release();return false;}
 m_MechanicOwned=true;
 auto satisfies=[&](Vec p,float t){
  if(!request)return true;
  for(auto const& h:shelters)if(t<=h.expires && h.shape.contains(local(p,h.pose(t)),-0.75f))return true;
  return false;
 };
 if(IsNotMovement()){
  m_MechanicMoving=false;
  if(RdfRotationPolicy::elapsed(now,m_MechanicLog,3000)){
   m_MechanicLog=now;TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_MECHANIC_BLOCKED account=%u map=%u reason=movement_disabled",me->GetSession()->GetAccountId(),map);
  }
  return true;
 }
 // Safe inside the correct shelter: hold position but allow the stationary rotation.
 bool safeHere=!threatened;
 if(safeHere && satisfies(here,0) && satisfies(here,0.5f)){
  if(m_MechanicMoving || !me->IsStopped()){m_Movement->ClearMovement();me->StopMoving();}
  m_MechanicMoving=false;m_RdfFormationMoving=false;return true;
 }
 // Plan with actual unmounted speed: mounting speed cannot shorten an escape deadline.
 me->RemoveAurasByType(SPELL_AURA_MOUNTED);
 std::vector<Position> candidates;
 // Enumerate real shelter interiors. No hardcoded teleport/Z correction.
 for(auto const& h:shelters){
  Pose p=h.pose(0);
  for(float radius:{0.0f,1.5f,3.0f,5.0f})for(unsigned i=0;i<(radius==0?1u:12u);++i){
   float angle=float(i)*6.28318530718f/12;
   Vec q{p.pos.x+radius*std::cos(angle),p.pos.y+radius*std::sin(angle),p.pos.z};
   if(!h.shape.contains(local(q,p),-0.75f))continue;
   Position dest;dest.Relocate(q.x,q.y,q.z,me->GetOrientation());candidates.push_back(dest);
  }
 }
 // Expanded rings let a bot leave a large footprint, not just its centre.
 if(!request)for(float r:{3.0f,5.0f,8.0f,12.0f,16.0f,20.0f,24.0f,32.0f,40.0f})for(unsigned i=0;i<16;++i){
  Position p;me->GetFirstCollisionPosition(p,r,float(i)*6.28318530718f/16);candidates.push_back(p);
 }
 // Stable tie-breaking gives bots different first choices without random oscillation.
 unsigned slot=me->GetGUID().GetCounter()%16;
 std::stable_sort(candidates.begin(),candidates.end(),[&](Position const& a,Position const& b){
  float da=me->GetDistance(a)-0.1f*std::cos(std::atan2(a.GetPositionY()-here.y,a.GetPositionX()-here.x)-float(slot)*0.392699f);
  float db=me->GetDistance(b)-0.1f*std::cos(std::atan2(b.GetPositionY()-here.y,b.GetPositionX()-here.x)-float(slot)*0.392699f);
  return da<db;
 });
 // FELSORIA_DODGE_NAVIGATION_V7: keep ordinary escapes connected to this fight.
 // Healers retain sight to a fighting ally; attackers retain sight to their enemy.
 Unit* combatAnchor=nullptr;
 auto validAnchor=[&](Unit* unit){
  return unit && unit!=me && unit->isAlive() && unit->IsInWorld() &&
   unit->GetMap()==me->GetMap() && me->InSamePhase(unit) && unit->isInCombat();
 };
 if(IsHealerBotAI()){
  if(validAnchor(m_MasterPlayer))combatAnchor=m_MasterPlayer;
  for(GroupReference* ref=me->GetGroup()->GetFirstMember();ref;ref=ref->next())
   if(Player* member=ref->getSource())if(validAnchor(member) &&
     (!combatAnchor || me->GetDistance(member)<me->GetDistance(combatAnchor)))combatAnchor=member;
 }else{
  Unit* selected=me->GetSelectedUnit();
  if(validAnchor(selected) && selected->IsHostileTo(me))combatAnchor=selected;
  if(!combatAnchor)for(auto creature:creatures){
   if(!validAnchor(creature) || !creature->IsHostileTo(me))continue;
   Unit* victim=creature->getVictim();
   Player* owner=victim?victim->GetCharmerOrOwnerPlayerOrPlayerItself():nullptr;
   if(!owner || !me->GetGroup()->IsMember(owner->GetGUID()))continue;
   if(!combatAnchor || me->GetDistance(creature)<me->GetDistance(combatAnchor))combatAnchor=creature;
  }
 }
 if(!combatAnchor && m_MasterPlayer && m_MasterPlayer->IsInWorld() &&
    m_MasterPlayer->GetMap()==me->GetMap() && me->InSamePhase(m_MasterPlayer) &&
    m_MasterPlayer->isAlive())combatAnchor=m_MasterPlayer;
 auto floorAt=[&](Vec p){return me->GetHeight(p.x,p.y,p.z+2.0f,true,6.0f);};
 auto sight=[&](Vec a,Vec b){
  return me->GetMap()->isInLineOfSight(a.x,a.y,a.z+0.75f,
      b.x,b.y,b.z+0.75f,me->GetPhases());
 };
 auto combatPosition=[&](Vec end){
  if(request)return true; // Real mechanic shelters can require breaking boss LOS.
  if(!combatAnchor)return false; // No grounded combat/follow reference: do not roam.
  Vec anchor=point(combatAnchor->GetPosition());
  // The endpoint must remain visible to the fight, while allowing a wide AoE exit.
  float limit=(IsTankBotAI() || IsMeleeBotAI())?30.0f:40.0f;
  if(m_MasterPlayer && m_MasterPlayer->GetMap()==me->GetMap() &&
     !withinPartyLeash(here,end,point(m_MasterPlayer->GetPosition()),limit))return false;
  return distance(end,anchor)<=limit && sight(end,anchor);
 };
 std::unique_ptr<PathGenerator> best;Position bestDest;
 float speed=std::max(0.1f,me->GetSpeed(MOVE_RUN));unsigned attempts=0;
 unsigned rejectGround=0,rejectHazard=0,rejectCombat=0,rejectPath=0;
 auto tryPoint=[&](Position const& p)->bool{
  if(std::fabs(p.GetPositionZ()-me->GetPositionZ())>4.5f)return false;
  // Do not kite into new trash/bosses. Existing encounter enemies may stay in combat.
  for(auto c:creatures)if(!c->isInCombat() && c->IsHostileTo(me) && c->GetReactState()==REACT_AGGRESSIVE){
   float dx=p.GetPositionX()-c->GetPositionX(),dy=p.GetPositionY()-c->GetPositionY();
   float r=c->GetAttackDistance(me)+2;if(dx*dx+dy*dy<r*r && std::fabs(p.GetPositionZ()-c->GetPositionZ())<6)return false;
  }
  if(!combatPosition(point(p))){++rejectCombat;return false;}
  float estimate=distance(here,point(p))/speed;
  if(!satisfies(point(p),estimate))return false;
  for(auto const& h:dangers)if(!h.moving && h.hit(point(p),std::max(estimate,h.active))){++rejectHazard;return false;}
  if(++attempts>20)return false;
  std::unique_ptr<PathGenerator> path(new PathGenerator(me));
  if(!path->CalculatePath(p.GetPositionX(),p.GetPositionY(),p.GetPositionZ(),false,false,false) || path->GetPathType()!=PATHFIND_NORMAL){++rejectPath;return false;}
  if(PathApproachesBlockedBoss(*path))return false;
  auto const& pts=path->GetPath();if(pts.size()<2)return false;
  auto const& end=pts.back();
  float dx=end.x-p.GetPositionX(),dy=end.y-p.GetPositionY();
  if(dx*dx+dy*dy>0.25f || std::fabs(end.z-p.GetPositionZ())>1.5f)return false;
  std::vector<Vec> route;route.push_back(here);float len=0;
  for(auto const& q:pts){Vec v{q.x,q.y,q.z};len+=distance(route.back(),v);route.push_back(v);}
  // No room-sized detour for a short dodge. This grounded route is also
  // available in reverse after the threat disappears.
  float const direct=distance(here,point(p));
  float const pathLimit=request ? 75.0f : std::min(55.0f,direct*1.5f+3.0f);
  if(!walkableEscape(route,floorAt,sight,pathLimit)){++rejectGround;return false;}
  if(!combatPosition(route.back())){++rejectCombat;return false;}
  float arrival=len/speed;
  if(!satisfies(route.back(),arrival) || !satisfies(route.back(),arrival+0.5f) || !pathSafe(route,speed,dangers)){++rejectHazard;return false;}
  for(auto c:creatures)if(!c->isInCombat() && c->IsHostileTo(me) && c->GetReactState()==REACT_AGGRESSIVE){
   float r=c->GetAttackDistance(me)+2;
   for(size_t i=1;i<route.size();++i)if(std::fabs(route[i].z-c->GetPositionZ())<6 &&
     segmentDistance2({c->GetPositionX(),c->GetPositionY()},route[i-1],route[i])<r*r)return false;
  }
  bestDest=p;best=std::move(path);return true;
 };
 // First revalidate the current escape destination; prevent retarget jitter.
 if(m_MechanicMoving)tryPoint(m_MechanicDestination);
 size_t visited=0;
 if(!best && !candidates.empty())for(size_t i=0;i<candidates.size();++i){
  if(attempts>=20)break;
  ++visited;
  auto const& p=candidates[(i+m_MechanicSearchOffset)%candidates.size()];if(tryPoint(p))break;
 }
 if(best)m_MechanicSearchOffset=0;
 else if(!candidates.empty())m_MechanicSearchOffset=unsigned((m_MechanicSearchOffset+visited)%candidates.size());
 if(!best){
  if(m_MechanicMoving){m_Movement->ClearMovement();me->StopMoving();}
  m_MechanicMoving=false;
  if(RdfRotationPolicy::elapsed(now,m_MechanicLog,3000)){
   m_MechanicLog=now;TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_MECHANIC_BLOCKED account=%u map=%u request=%u hazards=%u reason=no_safe_path path=%u ground_or_wall=%u combat=%u hazard=%u attempts=%u",me->GetSession()->GetAccountId(),map,request,uint32(dangers.size()),rejectPath,rejectGround,rejectCombat,rejectHazard,attempts);
  }
  if(request)return true; // Real shelters retain their requirement.
  release();m_MechanicBlockedUntil=now+500;
  return false; // No escape must not indefinitely disable ordinary combat.
 }
 auto end=best->GetPath().back();
 bool keep=false;
 if(m_MechanicMoving && me->movespline && !me->movespline->Finalized()){
  auto old=me->movespline->FinalDestination();float dx=old.x-end.x,dy=old.y-end.y,dz=old.z-end.z;
  keep=dx*dx+dy*dy+dz*dz<0.16f;
  float moved=me->GetDistance(m_MechanicProgressPosition);
  if(moved>0.2f){m_MechanicProgressPosition=me->GetPosition();m_MechanicProgressTime=now;}
  if(RdfRotationPolicy::elapsed(now,m_MechanicProgressTime,1500))keep=false;
 }
 if(!keep){
  me->InterruptSpell(CURRENT_GENERIC_SPELL,false);me->InterruptSpell(CURRENT_CHANNELED_SPELL,false);
  me->RemoveAurasByType(SPELL_AURA_MOUNTED);me->SetStandState(UNIT_STAND_STATE_STAND);
  m_Movement->ClearMovement();me->GetMotionMaster()->Clear();
  Movement::MoveSplineInit init(*me);init.MovebyPath(best->GetPath());init.SetWalk(false);
  m_MechanicMoving=init.Launch()>0;m_MechanicDestination=bestDest;
  if (m_MechanicMoving && !request)
      for (auto const& danger : dangers)
          if (danger.contains(point(me->GetPosition()),0))
          {
              sRegionalBotChatMgr.FullEvent(me,"felsoria_instance_ground_danger",4);
              break;
          }
  m_MechanicProgressPosition=me->GetPosition();m_MechanicProgressTime=now;
  if(RdfRotationPolicy::elapsed(now,m_MechanicLog,1000)){
   m_MechanicLog=now;TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_MECHANIC_MOVE account=%u map=%u request=%u hazards=%u x=%.2f y=%.2f z=%.2f",me->GetSession()->GetAccountId(),map,request,uint32(dangers.size()),double(end.x),double(end.y),double(end.z));
  }
 }
 m_RdfFormationMoving=false;m_RdfAvoidMoving=m_MechanicMoving;return true;
}

// FELSORIA_DODGE_LEASH_V10: no cause whitelist; separation is the trigger.
// Existing teleport ACKs retain control until the headless transfer completes.
bool BotGroupAI::ProcessDungeonLeash(bool bridgeRescue)
{
 using namespace FelsoriaMechanics;using namespace FelsoriaMechanicRuntime;
 Player* leader=m_MasterPlayer;
 auto reset=[&](){m_DungeonLeashClock.reset();};
 if(!leader || !leader->GetSession() || leader->GetSession()->IsBotSession() ||
    !me->GetSession() || !me->GetSession()->IsBotSession() || !me->GetGroup() ||
    leader->GetGroup()!=me->GetGroup() || !me->GetMap() ||
    !me->GetGroup()->IsMember(leader->GetGUID()) || !me->IsInWorld() || !me->isAlive() ||
    !leader->IsInWorld() || !leader->isAlive() || !leader->GetMap() ||
    !leader->GetMap()->IsDungeon() || me->InBattleground() || me->InArena()){
  reset();return false;
 }
 // Cross-map entry still belongs to native LFG; never select an instance by coordinates.
 if(leader->GetMap()!=me->GetMap()){
  if(bridgeRescue)return false;
  reset();return TryTeleportToMaster();
 }
 if(!me->InSamePhase(leader) || me->IsBeingTeleported() || me->IsChangeMap() ||
    leader->IsBeingTeleported() || leader->IsChangeMap() || !m_Teleporting.CanMovement() ||
    me->GetTransport() || me->GetVehicleBase() || me->isInFlight() ||
    leader->GetTransport() || leader->GetVehicleBase() || leader->isInFlight()){
  reset();return false;
 }
 uint32 now=getMSTime();
 if(m_DungeonLeashLeader!=leader->GetGUID() || m_DungeonLeashMap!=me->GetMapId() ||
    m_DungeonLeashInstance!=me->GetInstanceId()){
  reset();m_DungeonLeashAttempted=false;
  m_DungeonLeashLeader=leader->GetGUID();m_DungeonLeashMap=me->GetMapId();
  m_DungeonLeashInstance=me->GetInstanceId();m_DungeonLeashTick=0;
 }
 if(!bridgeRescue && m_DungeonLeashTick && uint32(now-m_DungeonLeashTick)<250)return false;
 m_DungeonLeashTick=now;
 bool liveCast=false;
 for(uint8 slot:{uint8(CURRENT_GENERIC_SPELL),uint8(CURRENT_CHANNELED_SPELL),uint8(CURRENT_AUTOREPEAT_SPELL)})
  if(auto cast=me->GetCurrentSpell(slot))if(cast->getState()!=SPELL_STATE_FINISHED)liveCast=true;
 float separation=me->GetDistance(leader);
 bool stalled=!liveCast && !m_MechanicMoving && !IsNotMovement() &&
  (!me->isInCombat() || !me->IsWithinLOSInMap(leader) ||
   std::fabs(me->GetPositionZ()-leader->GetPositionZ())>6.0f);
 auto reason=m_DungeonLeashClock.observe(point(me->GetPosition()),separation,stalled,now);
 if(!bridgeRescue && reason==RegroupReason::None)return false;
 // Distance recovery also works during combat/dodge; transient falling destinations wait.
 if(leader->IsFlying() || leader->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR))return false;
 if(m_DungeonLeashAttempted && uint32(now-m_DungeonLeashAttempt)<5000)return false;
 m_DungeonLeashAttempted=true;m_DungeonLeashAttempt=now;
 float ground=leader->GetHeight(leader->GetPositionX(),leader->GetPositionY(),
                               leader->GetPositionZ()+2.0f,true,10.0f);
 bool const observedLanding=bridgeRescue && bridgeHeightMissing(ground) &&
     VaultBridgeObservedLanding(leader->GetPosition(),now);
 if((!std::isfinite(ground) || std::fabs(ground-leader->GetPositionZ())>1.5f) && !observedLanding)return false;
 if(observedLanding)TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_VAULT_OBSERVED_LANDING_V11 account=%u reason=missing_height",me->GetSession()->GetAccountId());
 auto const& cache=nearby(leader,now);
 std::vector<Hazard> hazards;
 std::vector<Creature*> idleEnemies;
 for(auto guid:cache.areas)if(auto at=ObjectAccessor::GetAreaTrigger(*leader,guid)){
  if(!at->IsInWorld() || !leader->InSamePhase(at))continue;
  Unit* caster=at->GetCaster();if(!caster || !caster->IsHostileTo(leader))continue;
  uint32 id=at->GetSpellId();if(safeAreaId(leader->GetMapId(),id,at->GetRealEntry()))continue;
  bool harm=harmful(at->GetSpellInfo());
  auto info=at->GetAreaTriggerInfo();
  for(auto const& action:info.actions){
   if(action.targetFlags&AT_TARGET_FLAG_FRIENDLY)continue;
   if(action.actionType==AT_ACTION_TYPE_CAST_SPELL || action.actionType==AT_ACTION_TYPE_CAST_SPELL_NOT_TRIGGER ||
      action.actionType==AT_ACTION_TYPE_OWNER_CAST_SPELL || action.actionType==AT_ACTION_TYPE_SHARE_DAMAGE)
    harm=harm||harmful(sSpellMgr->GetSpellInfo(action.spellId));
  }
  if(harm)hazards.push_back(area(at));
 }
 for(auto guid:cache.creatures)if(auto c=ObjectAccessor::GetCreature(*leader,guid)){
  if(!c->IsInWorld() || !c->isAlive() || !leader->InSamePhase(c) || !c->IsHostileTo(leader))continue;
  if(!c->isInCombat()){
   if(c->GetReactState()==REACT_AGGRESSIVE)idleEnemies.push_back(c);
   continue;
  }
  for(uint8 type:{uint8(CURRENT_GENERIC_SPELL),uint8(CURRENT_CHANNELED_SPELL)}){
   Spell* cast=c->GetCurrentSpell(type);if(!cast || cast->getState()==SPELL_STATE_FINISHED)continue;
   auto spell=cast->GetSpellInfo();if(!harmful(spell))continue;
   // Do not bypass special shelter mechanics by teleporting during their cast.
   if(globalMechanic(leader->GetMapId(),spell->Id))return false;
   for(uint8 i=0;i<MAX_SPELL_EFFECTS;++i){
    auto e=spell->GetEffect(i,leader->GetMap()->GetDifficultyID());
    if(!e || !e->IsTargetingArea())continue;
    uint32 a=e->TargetA.GetTarget(),b=e->TargetB.GetTarget();
    bool dest=a==TARGET_UNIT_DEST_AREA_ENEMY || b==TARGET_UNIT_DEST_AREA_ENEMY;
    bool src=a==TARGET_UNIT_SRC_AREA_ENEMY || b==TARGET_UNIT_SRC_AREA_ENEMY;
    bool cone=a==TARGET_UNIT_CONE_ENEMY_24 || a==TARGET_UNIT_CONE_ENEMY_54 || a==TARGET_UNIT_CONE_ENEMY_104 || a==TARGET_UNIT_CONE_ENEMY_110 ||
              b==TARGET_UNIT_CONE_ENEMY_24 || b==TARGET_UNIT_CONE_ENEMY_54 || b==TARGET_UNIT_CONE_ENEMY_104 || b==TARGET_UNIT_CONE_ENEMY_110;
    if(!dest && !src && !cone)continue;
    float radius=e->CalcRadius(c,cast);if(radius<=0 || radius>80)continue;
    Position center=c->GetPosition();
    if(dest){if(!cast->m_targets.HasDst())continue;center=*cast->m_targets.GetDstPos();}
    Hazard h;h.shape.radius=radius;h.active=0;h.expires=100000;
    if(cone){
     float degrees=spell->TargetRestrictions.ConeAngle?spell->TargetRestrictions.ConeAngle:180.0f;
     if(degrees<0)center.SetOrientation(center.GetOrientation()+3.14159265f);
     float half=std::min(360.0f,std::fabs(degrees))*0.00872664626f;
     h.shape.polygon.push_back({0,0});
     for(unsigned k=0;k<=24;++k){float angle=-half+2*half*k/24;h.shape.polygon.push_back({radius*std::cos(angle),radius*std::sin(angle)});}
    }
    h.frames.push_back({point(center),cone?center.GetOrientation():0});hazards.push_back(std::move(h));
   }
  }
 }
 auto safeLanding=[&](Position const& p){
  Vec end=point(p);
  for(auto const& h:hazards){
   // Check visual footprints and moving hazards through the ACK horizon.
   if(h.contains(end,0))return false;
   for(unsigned i=0;i<15;++i)if(h.crossing(end,end,float(i)/10,float(i+1)/10))return false;
  }
  for(auto c:idleEnemies){
   float r=c->GetAttackDistance(me)+2.0f;
   if(c->GetDistance(leader)>=r && distance(end,point(c->GetPosition()))<r)return false;
  }
  return true;
 };
 unsigned slot=unsigned(me->GetGUID().GetCounter()%12);
 for(unsigned i=0;i<=12;++i){
  Position dest;
  if(i==12)dest=leader->GetPosition(); // Exact grounded player position is the fallback.
  else leader->GetFirstCollisionPosition(dest,2.0f,float((slot+i)%12)*6.28318530718f/12.0f);
  if(bridgeRescue && !VaultBridgeLandingAllowed(dest))continue;
  if(std::fabs(dest.GetPositionZ()-leader->GetPositionZ())>1.5f || !safeLanding(dest))continue;
  float floor=leader->GetHeight(dest.GetPositionX(),dest.GetPositionY(),dest.GetPositionZ()+2.0f,true,10.0f);
  if((!std::isfinite(floor) || std::fabs(floor-dest.GetPositionZ())>1.5f) &&
     !(bridgeRescue && bridgeHeightMissing(floor) && VaultBridgeObservedLanding(dest,now)))continue;
  if(i<12){
   PathGenerator path(leader);
   if(!path.CalculatePath(dest.GetPositionX(),dest.GetPositionY(),dest.GetPositionZ(),false,false,false) || path.GetPathType()!=PATHFIND_NORMAL)continue;
   auto const& pts=path.GetPath();if(pts.size()<2)continue;
   if(distance({pts.back().x,pts.back().y,pts.back().z},point(dest))>1.0f)continue;
   std::vector<Vec> route;
   for(auto const& q:pts)route.push_back({q.x,q.y,q.z});
   auto floorAt=[&](Vec p){return leader->GetHeight(p.x,p.y,p.z+2.0f,true,6.0f);};
   auto sight=[&](Vec a,Vec b){return leader->GetMap()->isInLineOfSight(a.x,a.y,a.z+0.75f,b.x,b.y,b.z+0.75f,leader->GetPhases());};
   if(!walkableEscape(route,floorAt,sight,6.0f))continue;
  }
  me->InterruptSpell(CURRENT_GENERIC_SPELL,false);me->InterruptSpell(CURRENT_CHANNELED_SPELL,false);
  me->RemoveAurasByType(SPELL_AURA_MOUNTED);
  m_Movement->ClearMovement();m_CruxMovement.ClearMovement();me->GetMotionMaster()->Clear();
  dest.SetOrientation(leader->GetOrientation());
  m_Teleporting.SetTeleport(dest,true);
  if(m_Teleporting.CanMovement())continue;
  m_DungeonLeashDestination=dest;
  reset();m_MechanicOwned=false;m_MechanicMoving=false;m_MechanicTick=0;m_MechanicSearchOffset=0;
  m_RdfAvoidMoving=false;m_RdfFormationMoving=false;m_RdfFormationTick=0;m_RdfPositionTick=0;
  m_RdfTravelArmed=false;m_RdfTravelStable=0;m_VaultBridgeLink=-1;m_VaultBridgeStage=-1;
  m_VaultBridgeIssued=false;m_VaultBridgeRetry=0;m_VaultBridgeFallbackLink=-1;
  m_VaultBridgeFallbackStart=0;m_VaultBridgeProgressTime=0;
  m_RdfFormationRecoveryAttempt=0;m_RdfRepositionRequested=false;
  m_TankLeadHandled=false;m_TankLeadTick=0;
  m_RdfFormationProgressTime=0;m_RdfPathProgressTime=0;m_RdfStillSince=0;
  TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_DUNGEON_REGROUP_V10 account=%u map=%u instance=%u reason=%s distance=%.2f combat=%u",
      me->GetSession()->GetAccountId(),me->GetMapId(),me->GetInstanceId(),
      bridgeRescue?"vault_bridge":(reason==RegroupReason::Distant?"distance":"stalled"),double(separation),uint32(me->isInCombat()));
  return true;
 }
 TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_DUNGEON_REGROUP_BLOCKED_V10 account=%u reason=no_safe_landing",me->GetSession()->GetAccountId());
 return false;
}

bool BotGroupAI::ProcessMountFollow()
{
 if(m_MechanicOwned || !me->IsInWorld() || !me->isAlive() || !me->GetMap() || !me->GetGroup() ||
    !m_MasterPlayer || !m_MasterPlayer->IsInWorld() || m_MasterPlayer->GetMap()!=me->GetMap() ||
    !me->InSamePhase(m_MasterPlayer) || !me->GetGroup()->IsMember(m_MasterPlayer->GetGUID()) ||
    !m_MasterPlayer->IsMounted() || me->IsMounted() || m_StopFollow || m_ForceFlee ||
    !me->IsSettingFinish() || me->IsBeingTeleported() || me->IsChangeMap() ||
    !m_Teleporting.CanMovement() || me->GetVehicleBase() || m_CruxMovement.HasCruxMovement())return false;
 if(me->isInCombat())return false;
 for(GroupReference* r=me->GetGroup()->GetFirstMember();r;r=r->next())if(Player* p=r->getSource())
  if(p->IsInWorld() && p->GetMap()==me->GetMap() && me->InSamePhase(p) && p->isInCombat())return false;
 if(Spell* cast=me->GetCurrentSpell(CURRENT_GENERIC_SPELL))
  if(cast->GetSpellInfo()->HasAura(SPELL_AURA_MOUNTED))return true;
 if(me->HasUnitState(UNIT_STATE_CASTING) || m_CombatCastIssued || IsNotMovement())return false;
 uint32 now=getMSTime();if(m_MountFollowAttempt && !RdfRotationPolicy::elapsed(now,m_MountFollowAttempt,5000))return false;
 m_MountFollowAttempt=now;
 PlayerBotSetting::RemoveForbiddenMount(me);
 if(!m_UseMountID || !me->HasSpell(m_UseMountID))m_UseMountID=PlayerBotSetting::MountForPlayer(me);
 if(!m_UseMountID || !me->HasSpell(m_UseMountID) ||
    !me->GetMap()->IsOutdoors(me->GetPositionX(),me->GetPositionY(),me->GetPositionZ()))return false;
 // A stale selected enemy must not suppress mounting in a quiet party.
 if(me->GetSelectedUnit() && !me->GetSelectedUnit()->isInCombat())me->SetSelection(ObjectGuid::Empty);
 m_Movement->ClearMovement();me->GetMotionMaster()->Clear();me->StopMoving();
 bool ok=TryCastSpell(m_UseMountID,me)==SPELL_CAST_OK;
 if(ok)TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_MOUNT_FOLLOW account=%u spell=%u",me->GetSession()->GetAccountId(),m_UseMountID);
 return ok;
}

bool BotGroupAI::ShouldBlockBossPull(Unit* target)
{
 Creature* boss=target?target->ToCreature():nullptr;
 if(!boss || !me->IsInWorld() || !me->GetGroup() || !me->GetMap() || !me->GetMap()->IsDungeon() ||
    !boss->IsInWorld() || !boss->isAlive() || boss->GetMap()!=me->GetMap() ||
    !me->InSamePhase(boss) || !boss->IsDungeonBoss() || boss->isInCombat())return false;
 uint32 now=getMSTime();
 if(m_BossPullCheck && m_BossPullGuid==boss->GetGUID() && !RdfRotationPolicy::elapsed(now,m_BossPullCheck,1000))return m_BossPullBlocked;
 m_BossPullCheck=now;m_BossPullGuid=boss->GetGUID();m_BossPullBlocked=false;
 NearCreatureVec trash;SearchCreatureListFromRange(boss,trash,70.0f,false);
 FelsoriaMechanics::Vec from{me->GetPositionX(),me->GetPositionY()},to{boss->GetPositionX(),boss->GetPositionY()};
 for(auto c:trash){
  if(!c->IsInWorld() || !c->isAlive() || c==boss || c->IsDungeonBoss() || !me->InSamePhase(c) || !c->IsHostileTo(me) ||
     !me->IsValidAttackTarget(c) || std::fabs(c->GetPositionZ()-boss->GetPositionZ())>6)continue;
  bool nearbyTrash=c->GetDistance(boss)<35.0f && boss->IsWithinLOSInMap(c);
  float width=std::min(12.0f,c->GetAttackDistance(me)+2);
  bool corridor=me->IsWithinLOSInMap(c) && FelsoriaMechanics::segmentDistance2(
      {c->GetPositionX(),c->GetPositionY()},from,to)<width*width;
  if(!nearbyTrash && !corridor)continue;
  m_BossPullBlocked=true;
  if(RdfRotationPolicy::elapsed(now,m_BossPullLog,5000)){
   m_BossPullLog=now;TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_BOSS_PULL_BLOCK account=%u boss=%u trash=%u reason=near_or_approach",me->GetSession()->GetAccountId(),boss->GetEntry(),c->GetEntry());
  }
  break;
 }
 return m_BossPullBlocked;
}

bool BotGroupAI::ProcessBossPullSafety()
{
 if(!me->IsInWorld() || !me->isAlive() || !me->GetGroup() || !me->GetMap() || !me->GetMap()->IsDungeon() ||
    me->IsBeingTeleported() || me->IsChangeMap() || !m_Teleporting.CanMovement() || me->GetVehicleBase())return false;
 Unit* selected=me->GetSelectedUnit();
 if(ShouldBlockBossPull(selected)){me->AttackStop();me->SetSelection(ObjectGuid::Empty);}
 if(ShouldBlockBossPull(me->getVictim()))me->AttackStop();
 for(uint8 type:{uint8(CURRENT_GENERIC_SPELL),uint8(CURRENT_CHANNELED_SPELL)})
  if(Spell* cast=me->GetCurrentSpell(type))
   if(!cast->GetSpellInfo()->IsPositive() && ShouldBlockBossPull(cast->m_targets.GetUnitTarget()))me->InterruptSpell(static_cast<CurrentSpellTypes>(type),false);
 if(Pet* pet=me->GetPet())if(ShouldBlockBossPull(pet->getVictim())){pet->AttackStop();pet->GetMotionMaster()->Clear();}
 // The player may select the boss; selection alone never authorizes an early bot pull.
 if(me->isInCombat() || !m_MasterPlayer || !m_MasterPlayer->IsInWorld() ||
    m_MasterPlayer->GetMap()!=me->GetMap() || !me->InSamePhase(m_MasterPlayer) || m_MasterPlayer->isInCombat())return false;
 for(GroupReference* r=me->GetGroup()->GetFirstMember();r;r=r->next())if(Player* p=r->getSource())
  if(p->IsInWorld() && p->GetMap()==me->GetMap() && me->InSamePhase(p) && p->isInCombat())return false;
 uint32 now=getMSTime();
 auto const& cache=FelsoriaMechanicRuntime::nearby(me,now);
 for(auto guid:cache.creatures)if(auto boss=ObjectAccessor::GetCreature(*me,guid)){
  if(!ShouldBlockBossPull(boss))continue;
  float r=boss->GetAttackDistance(me)+2;
  if(m_MasterPlayer->GetDistance(boss)<r && me->GetDistance(boss)<r+12){
   m_Movement->ClearMovement();me->StopMoving();m_RdfFormationMoving=false;
   return true;
  }
 }
 return false;
}

bool BotGroupAI::PathApproachesBlockedBoss(PathGenerator const& path)
{
 if(!me->GetMap() || !me->GetMap()->IsDungeon() || !me->GetGroup())return false;
 auto const& cache=FelsoriaMechanicRuntime::nearby(me,getMSTime());
 for(auto guid:cache.creatures)if(auto boss=ObjectAccessor::GetCreature(*me,guid)){
  if(!ShouldBlockBossPull(boss))continue;
  float r=boss->GetAttackDistance(me)+2;
  auto const& points=path.GetPath();
  for(size_t i=1;i<points.size();++i){auto a=points[i-1],b=points[i];
   if(std::fabs(b.z-boss->GetPositionZ())<6 && FelsoriaMechanics::segmentDistance2(
       {boss->GetPositionX(),boss->GetPositionY()},{a.x,a.y},{b.x,b.y})<r*r)return true;
  }
 }
 return false;
}

bool BotGroupAI::BlocksBossPullSpell(SpellInfo const* spell,Unit* caster,Unit* target,Position const* destination)
{
 if(!spell || !caster || (spell->IsPositive() && !FelsoriaMechanicRuntime::harmful(spell)))return false;
 if(ShouldBlockBossPull(target))return true;
 if(!me->GetMap() || !me->GetMap()->IsDungeon() || !me->GetGroup())return false;
 auto const& cache=FelsoriaMechanicRuntime::nearby(me,getMSTime());
 for(uint8 i=0;i<MAX_SPELL_EFFECTS;++i){
  auto e=spell->GetEffect(i,me->GetMap()->GetDifficultyID());if(!e || !e->IsTargetingArea())continue;
  float radius=e->CalcRadius(caster);if(radius<=0)continue;
  uint32 a=e->TargetA.GetTarget(),b=e->TargetB.GetTarget();
  Position center=caster->GetPosition();
  if(a==TARGET_UNIT_DEST_AREA_ENEMY || b==TARGET_UNIT_DEST_AREA_ENEMY){
   if(destination)center=*destination;else if(target)center=target->GetPosition();else continue;
  }
  for(auto guid:cache.creatures)if(auto boss=ObjectAccessor::GetCreature(*me,guid))
   if(boss->GetDistance(center)<radius+2 && ShouldBlockBossPull(boss))return true;
 }
 return false;
}
