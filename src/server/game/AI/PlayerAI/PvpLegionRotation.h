#ifndef FELSORIA_PVP_LEGION_ROTATION_H
#define FELSORIA_PVP_LEGION_ROTATION_H
// FELSORIA_PVP_LEGION_ROTATIONS_V1. Shared by field, BG/arena, duel and party AI.
#include "PvpRotationPolicy.h"
#include "FelsoriaCombatPolicy.h"
#include "BotAITool.h"
#include "Battleground.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Pet.h"
#include "CreatureAI.h"
#include "PlayerBotSetting.h"
#include "WorldSession.h"
#include "Timer.h"
#include <algorithm>
#include "Config.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <list>

class PvpLegionRotation
{
    std::map<std::string, std::vector<uint32>> catalog;
    FelsoriaCombatPolicy::RetryBook retry;
    FelsoriaCombatPolicy::CooldownGate defensiveGate, groupGate;
    std::set<std::string> unavailable;
    uint32 catalogTime=0, catalogSpec=0, lastSpell=0, logTime=0;
    uint32 controlTime=0, burstTime=0;
    bool catalogReady=false, controlUsed=false, burstStarted=false;

    static bool controlled(Unit* target)
    {
        return BotUtility::IsCombatControl(target);
    }
    static unsigned family(Player* p)
    {
        switch(p->getClass()) {
        case CLASS_WARRIOR:return SPELLFAMILY_WARRIOR;
        case CLASS_PALADIN:return SPELLFAMILY_PALADIN;
        case CLASS_HUNTER:return SPELLFAMILY_HUNTER;
        case CLASS_ROGUE:return SPELLFAMILY_ROGUE;
        case CLASS_PRIEST:return SPELLFAMILY_PRIEST;
        case CLASS_DEATH_KNIGHT:return SPELLFAMILY_DEATHKNIGHT;
        case CLASS_SHAMAN:return SPELLFAMILY_SHAMAN;
        case CLASS_MAGE:return SPELLFAMILY_MAGE;
        case CLASS_WARLOCK:return SPELLFAMILY_WARLOCK;
        case CLASS_MONK:return SPELLFAMILY_MONK;
        case CLASS_DRUID:return SPELLFAMILY_DRUID;
        case CLASS_DEMON_HUNTER:return SPELLFAMILY_DEMON_HUNTER;
        default:return 0;
        }
    }
    void refresh(Player* me, uint32 now)
    {
        unsigned spec=me->GetSpecializationId();
        if(catalogReady && catalogSpec==spec && !PvpRotationPolicy::elapsed(now,catalogTime,30000))return;
        if(catalogSpec!=spec){retry.Clear();defensiveGate.Clear();groupGate.Clear();lastSpell=0;burstStarted=false;controlUsed=false;}
        catalogReady=true;catalogSpec=spec;catalogTime=now;catalog.clear();unavailable.clear();
        unsigned f=family(me);
        for(auto const& e:me->GetSpellMap()) {
            if(e.second.state==PLAYERSPELL_REMOVED || e.second.disabled || !e.second.active)continue;
            SpellInfo const* si=sSpellMgr->GetSpellInfo(e.first);
            if(!si || si->IsPassive() || !si->SpellName || si->ClassOptions.SpellClassSet!=f)continue;
            catalog[si->SpellName].push_back(e.first);
        }
        // Prefer the highest learned rank; native CheckCast selects usable same-name variants.
        for(auto& e:catalog)std::sort(e.second.begin(),e.second.end(),[](uint32 a,uint32 b){
            SpellInfo const* sa=sSpellMgr->GetSpellInfo(a);SpellInfo const* sb=sSpellMgr->GetSpellInfo(b);
            return sa && sb && sa->GetRank()!=sb->GetRank()?sa->GetRank()>sb->GetRank():a<b;
        });
    }
    template<class Movement>
    bool execute(Player* me, Unit* enemy, Movement* movement,
        std::vector<Unit*> const& friends, std::vector<Unit*> const& enemies)
    {
        unsigned spec=me->GetSpecializationId();
        bool healer=PvpRotationPolicy::healer(spec), tank=PvpRotationPolicy::tank(spec), melee=PvpRotationPolicy::melee(spec);
        uint32 now=getMSTime();
        bool audit=sConfigMgr->GetIntDefault("PlayerBot.CombatSpellAudit",0)!=0;
        refresh(me,now);
        auto approach=[&](Unit* target,float range){
            if(!target || me->HasUnitState(UNIT_STATE_CASTING))return;
            if(me->GetDistance(target)>range || !me->IsWithinLOSInMap(target))movement->MovementTo(target->GetGUID());
        };
        auto ID=[&](char const* name)->uint32 {
            auto it=catalog.find(name);
            if(it!=catalog.end() && !it->second.empty())return it->second.front();
            if(audit && unavailable.insert(name).second)
                TC_LOG_INFO(LOG_FILTER_PLAYER,"PVP_ROTATION_UNAVAILABLE account=%u spec=%u name=%s",me->GetSession()->GetAccountId(),spec,name);
            return 0;
        };
        auto A=[&](Unit* u,uint32 id)->bool{return u && u->HasAura(id);};
        auto stacks=[&](Unit* u,uint32 id)->unsigned{
            Aura* a=u?u->GetAura(id,u==me?ObjectGuid::Empty:me->GetGUID()):nullptr;return a?a->GetStackAmount():0;
        };
        auto missing=[&](char const* name,Unit* u,int32 ms)->bool{
            auto it=catalog.find(name);if(!u || it==catalog.end())return false;
            for(uint32 id:it->second)if(Aura* a=u->GetAura(id,me->GetGUID()))
                if(a->GetDuration()<0 || a->GetDuration()>ms)return false;
            return true;
        };
        auto P=[&](Powers p)->float{return me->GetPowerPct(p);};
        auto R=[&](Powers p)->int32{return me->GetPower(p);};
        auto C=[&](char const* name,Unit* target)->bool{
            if(!target || me->HasUnitState(UNIT_STATE_CASTING))return false;
            if(!ID(name))return false;
            for(uint32 id:catalog[name]) {
                SpellInfo const* si=sSpellMgr->GetSpellInfo(id);
                if(!si || !me->HasSpell(id) || me->HasSpellCooldown(id) || BotUtility::CombatGlobalCooldown(me,si))continue;
                uint32 attemptDelay = std::string(name).find("Call Pet") == 0 || std::string(name) == "Revive Pet" ? 15000 : 1500;
                if(!retry.Ready(id,now))continue;
                if(!target->IsInWorld() || !target->isAlive() || target->GetMap()!=me->GetMap() || !me->InSamePhase(target))continue;
                bool friendly=target==me || me->IsValidAssistTarget(target);
                if(target!=me && ((!friendly && !me->IsValidAttackTarget(target)) || !me->canSeeOrDetect(target)))continue;
                if(!friendly && controlled(target))continue;
                bool controlSpell=std::string(name)=="Storm Bolt" || std::string(name)=="Hammer of Justice" ||
                    std::string(name)=="Kidney Shot" || std::string(name)=="Leg Sweep" ||
                    std::string(name)=="Lightning Surge Totem" || std::string(name)=="Chaos Nova" ||
                    std::string(name)=="Sigil of Misery" || std::string(name)=="Polymorph" ||
                    std::string(name)=="Hex" || std::string(name)=="Fear" || std::string(name)=="Blind" ||
                    std::string(name)=="Freezing Trap";
                if(!friendly && controlSpell && !BotUtility::CanUseBotControl(target,si))continue;
                float range=si->GetMaxRange(friendly,me), minRange=si->GetMinRange(friendly);
                if(target!=me && (!me->IsWithinLOSInMap(target) ||
                    (range>0 && me->GetDistance(target)>range) || me->GetDistance(target)<minRange))continue;
                if(spec==269 && id==lastSpell)continue; // Hit Combo: alternate abilities.
                if(BotUtility::PrepareBotCast(me,si,target,movement)!=SPELL_CAST_OK)continue;
                TriggerCastData data;data.triggerFlags=TriggerCastFlags::TRIGGERED_NONE;
                Spell* spell=new Spell(me,si,data);spell->m_CastItem=nullptr;
                SpellCastTargets targets;targets.SetUnitTarget(target);targets.SetDst(*target);
                SpellCastResult result=spell->prepare(&targets);
                if(result!=SPELL_CAST_OK){retry.Record(id,now,attemptDelay==15000?15000:BotUtility::CombatRetryDelay(result));
                    if(audit && PvpRotationPolicy::elapsed(now,logTime,5000)){
                        logTime=now;TC_LOG_INFO(LOG_FILTER_PLAYER,"PVP_ROTATION_FAIL account=%u spec=%u spell=%u result=%u",me->GetSession()->GetAccountId(),spec,id,uint32(result));
                    }
                    continue;
                }
                if (attemptDelay == 15000) retry.Record(id,now,15000);
                lastSpell=id;
                if(audit)TC_LOG_INFO(LOG_FILTER_PLAYER,"PVP_ROTATION_CAST account=%u spec=%u spell=%u name=%s target=%s",me->GetSession()->GetAccountId(),spec,id,name,target->GetName());
                return true;
            }
            return false;
        };
        auto DEF=[&](char const* name,Unit* target=nullptr)->bool{
            if(!defensiveGate.Ready(now,me->GetHealthPct()))return false;
            if(C(name,target?target:me)){defensiveGate.Record(now,me->GetHealthPct());return true;}return false;
        };
        Unit* low=me;float minimumHP=me->GetHealthPct();unsigned injured=0,critical=0;
        bool hasSelf=false;
        for(Unit* p:friends){
            if(p==me)hasSelf=true;
            if(!p || !p->IsInWorld() || !p->isAlive() || p->GetMap()!=me->GetMap() || !me->InSamePhase(p) ||
                (p!=me && !me->IsValidAssistTarget(p)) || me->GetDistance(p)>40 || !me->IsWithinLOSInMap(p))continue;
            float hp=p->GetHealthPct();if(hp<minimumHP){minimumHP=hp;low=p;}
            if(hp<70)++injured;if(hp<35)++critical;
        }
        if(!hasSelf){if(minimumHP<70)++injured;if(me->GetHealthPct()<35)++critical;}
        float hp=me->GetHealthPct(),lh=low->GetHealthPct(),mana=P(POWER_MANA),eh=enemy->GetHealthPct();
        unsigned cp=me->GetComboPoints();bool emergency=PvpRotationPolicy::groupEmergency(injured,critical);
        unsigned adds=0,closeEnemies=0;bool safeAoE=true;
        for(Unit* p:enemies){
            if(!p || !p->isAlive() || p->GetMap()!=me->GetMap() || !me->InSamePhase(p))continue;
            if(controlled(p) && (p->GetDistance(enemy)<12 || p->GetDistance(me)<12))safeAoE=false;
            if(!me->IsValidAttackTarget(p) || controlled(p))continue;
            if(p->GetDistance(enemy)<8)++adds;if(p->GetDistance(me)<8)++closeEnemies;
        }
        if(!adds)adds=1;
        if(hp<20 && me->getClass()==CLASS_MAGE && DEF("Ice Block"))return true;
        if(hp<45){switch(me->getClass()){
        case CLASS_WARRIOR:if(DEF(tank?"Shield Wall":(spec==71?"Die by the Sword":"Enraged Regeneration")))return true;break;
        case CLASS_PALADIN:if(DEF(tank?"Ardent Defender":(spec==70?"Shield of Vengeance":"Divine Protection")))return true;break;
        case CLASS_HUNTER:if(C("Exhilaration",me) || DEF("Aspect of the Turtle"))return true;break;
        case CLASS_ROGUE:if(C("Crimson Vial",me) || DEF("Evasion") || DEF("Cloak of Shadows"))return true;break;
        case CLASS_PRIEST:if(DEF(spec==258?"Dispersion":"Desperate Prayer"))return true;break;
        case CLASS_DEATH_KNIGHT:if(DEF("Icebound Fortitude") || DEF("Anti-Magic Shell"))return true;break;
        case CLASS_SHAMAN:if(DEF("Astral Shift"))return true;break;
        case CLASS_MAGE:if(DEF(spec==62?"Prismatic Barrier":(spec==63?"Blazing Barrier":"Ice Barrier")))return true;break;
        case CLASS_WARLOCK:if(DEF("Unending Resolve"))return true;break;
        case CLASS_MONK:if(DEF(tank?"Fortifying Brew":"Touch of Karma",tank?me:enemy) || DEF("Diffuse Magic"))return true;break;
        case CLASS_DRUID:if(DEF("Barkskin") || DEF("Survival Instincts"))return true;break;
        case CLASS_DEMON_HUNTER:if(DEF(tank?"Metamorphosis":"Blur"))return true;break;
        }}
        auto GROUPCD=[&](char const* name,Unit* target)->bool{
            if(!groupGate.Ready(now,minimumHP,critical>=2))return false;
            if(C(name,target)){groupGate.Record(now,minimumHP);return true;}return false;
        };
#include "PvpLegionHealing.inc"
        // Do not damage a feared/polymorphed opponent and break allied control.
        if(controlled(enemy)){me->AttackStop();if(me->GetPet())me->GetPet()->AttackStop();return true;}
        if(me->HasAuraType(SPELL_AURA_MOD_ROOT)){
            if(C("Blessing of Freedom",me) || C("Tiger's Lust",me) || C("Escape Artist",me))return true;
        }
        if(me->HasAuraType(SPELL_AURA_MOD_FEAR) || me->HasAuraType(SPELL_AURA_MOD_FEAR_2)){
            if(C("Berserker Rage",me) || C("Will of the Forsaken",me))return true;
        }
        if(spec==104 && !A(me,5487) && C("Bear Form",me))return true;
        if(spec==103 && !A(me,768) && C("Cat Form",me))return true;
        if(spec==102 && !A(me,24858) && C("Moonkin Form",me))return true;
        if(spec==258 && !A(me,232698) && !A(me,194249) && C("Shadowform",me))return true;
        if((spec==253||spec==255) && !me->GetPet() && C("Call Pet 1",me))return true;
        if((spec==253||spec==255) && me->GetPet() && !me->GetPet()->isAlive() && C("Revive Pet",me))return true;
        if(me->getClass()==CLASS_WARLOCK && !me->GetPet() && C(spec==266?"Summon Felguard":"Summon Felhunter",me))return true;
        if(spec==252 && !me->GetPet() && C("Raise Dead",me))return true;
        if(me->GetPet() && me->GetPet()->isAlive() && me->GetPet()->AI() && me->GetPet()->getVictim()!=enemy)me->GetPet()->AI()->AttackStart(enemy);
        // Shared native interrupt selection; available off-GCD and coordinated per target cast.
        bool interruptible=BotUtility::HasBotInterruptibleCast(enemy);
        if(BotUtility::TryCombatInterrupt(me,enemy,movement))return true;
        // Dispel crowd control only when removable; avoid Unstable Affliction backlash.
        auto dispellable=[&](Unit* u,bool positive,char const* dispelName)->bool{
            if(!u)return false;
            SpellInfo const* dispel=sSpellMgr->GetSpellInfo(ID(dispelName));if(!dispel)return false;
            if(!positive)for(auto const& a:u->GetAppliedAuras()){
                auto const& app=a.second;if(!app || app->IsPositive())continue;
                SpellInfo const* si=app->GetBase()->GetSpellInfo();
                if(si && si->SpellName && std::string(si->SpellName)=="Unstable Affliction")return false;
            }
            for(auto const& a:u->GetAppliedAuras()){
                auto const& app=a.second;if(!app || app->IsPositive()!=positive)continue;
                SpellInfo const* si=app->GetBase()->GetSpellInfo();if(!si || !(si->GetDispelMask() & SpellInfo::GetDispelMask(DISPEL_MAGIC)) || !dispel->CanDispelAura(si))continue;
                if(si->SpellName && std::string(si->SpellName)=="Unstable Affliction")return false;
                if(positive ? si->HasAura(SPELL_AURA_SCHOOL_ABSORB) :
                    (si->HasAura(SPELL_AURA_MOD_STUN) || si->HasAura(SPELL_AURA_MOD_ROOT) ||
                     si->HasAura(SPELL_AURA_MOD_FEAR) || si->HasAura(SPELL_AURA_MOD_CONFUSE)))return true;
            }
            return false;
        };
        if(healer){
            char const* cleanse=spec==65?"Cleanse":(spec==264?"Purify Spirit":(spec==270?"Detox":(spec==105?"Nature's Cure":"Purify")));
            for(Unit* p:friends)if(p && p->isAlive() && me->IsValidAssistTarget(p) && dispellable(p,false,cleanse) && C(cleanse,p))return true;
            if(dispellable(me,false,cleanse) && C(cleanse,me))return true;
        }
        char const* purge=me->getClass()==CLASS_SHAMAN?"Purge":(me->getClass()==CLASS_PRIEST?"Dispel Magic":(me->getClass()==CLASS_MAGE?"Spellsteal":nullptr));
        if(purge && dispellable(enemy,true,purge) && C(purge,enemy))return true;
        // Gap closers use native path/range validation, never teleport coordinates.
        if(melee && me->GetDistance(enemy)>7 && me->IsWithinLOSInMap(enemy)){
            switch(me->getClass()){
            case CLASS_WARRIOR:if(C("Charge",enemy))return true;break;
            case CLASS_ROGUE:if(C("Shadowstep",enemy) || C("Grappling Hook",enemy))return true;break;
            case CLASS_DEATH_KNIGHT:if(C("Death Grip",enemy))return true;break;
            case CLASS_HUNTER:if(spec==255 && C("Harpoon",enemy))return true;break;
            }
        }
        if(me->GetDistance(enemy)<=28 && me->IsWithinLOSInMap(enemy) &&
            (!burstStarted || PvpRotationPolicy::elapsed(now,burstTime,60000))){burstStarted=true;burstTime=now;}
        bool burst=PvpRotationPolicy::burstWindow(burstStarted,now,burstTime);
        if(burst && spec==71 && C("Sharpen Blade",me))return true;
        // Stuns initiate burst/stop dangerous casts, spaced to avoid dumping all CC.
        if((burst || interruptible) && (!controlUsed || PvpRotationPolicy::elapsed(now,controlTime,18000)) &&
            !enemy->HasAuraType(SPELL_AURA_MOD_STUN)){
            char const* stun=nullptr;
            switch(me->getClass()){
            case CLASS_WARRIOR:stun="Storm Bolt";break;
            case CLASS_PALADIN:stun="Hammer of Justice";break;
            case CLASS_ROGUE:if(cp>=4)stun="Kidney Shot";break;
            case CLASS_MONK:stun="Leg Sweep";break;
            case CLASS_SHAMAN:stun="Lightning Surge Totem";break;
            case CLASS_DEMON_HUNTER:stun=spec==577?"Chaos Nova":"Sigil of Misery";break;
            }
            if(stun && C(stun,enemy)){controlUsed=true;controlTime=now;return true;}
        }
        // Control an opposing healer without breaking it with our primary attacks.
        if(burst && (!controlUsed || PvpRotationPolicy::elapsed(now,controlTime,18000))){
            for(Unit* other:enemies){
                if(!other || other==enemy || !other->isAlive() || controlled(other) || !me->IsValidAttackTarget(other))continue;
                Player* p=other->ToPlayer();if(!p || !PvpRotationPolicy::healer(p->GetSpecializationId()))continue;
                char const* cc=nullptr;
                switch(me->getClass()){
                case CLASS_MAGE:cc="Polymorph";break;
                case CLASS_SHAMAN:cc="Hex";break;
                case CLASS_WARLOCK:cc="Fear";break;
                case CLASS_ROGUE:cc="Blind";break;
                case CLASS_HUNTER:cc="Freezing Trap";break;
                }
                if(cc && C(cc,other)){controlUsed=true;controlTime=now;return true;}
            }
        }
        if(melee && !me->IsWithinMeleeRange(enemy)){
            // Available ranged tools while closing, instead of idle running.
            if(me->getClass()==CLASS_DEMON_HUNTER && C("Throw Glaive",enemy))return true;
            if(me->getClass()==CLASS_DEATH_KNIGHT && C("Chains of Ice",enemy))return true;
            approach(enemy,3.0f);return true;
        }
#include "PvpLegionDamage.inc"
        approach(enemy,melee?3.0f:25.0f);
        if(audit && PvpRotationPolicy::elapsed(now,logTime,10000)){
            logTime=now;TC_LOG_INFO(LOG_FILTER_PLAYER,"PVP_ROTATION_WAIT account=%u spec=%u hp=%.1f mana=%.1f",me->GetSession()->GetAccountId(),spec,double(hp),double(mana));
        }
        return true; // Applicable PvP never falls through to the old random spell loop.
    }
public:
    // Native self-heals can run without an acquired enemy. No bandage in combat.
    template<class Movement>
    bool SelfHeal(Player* me, Movement* movement, bool instantOnly=false)
    {
        if (!me || !movement || !me->IsInWorld() || !me->isAlive() ||
            me->IsBeingTeleported() || me->IsChangeMap() || me->GetHealthPct() > 55.0f ||
            me->HasUnitState(UNIT_STATE_CASTING)) return false;
        uint32 now=getMSTime(); refresh(me,now);
        std::vector<char const*> names;
        switch (me->getClass())
        {
        case CLASS_PALADIN: names={"Word of Glory","Flash of Light","Holy Shock"}; break;
        case CLASS_PRIEST: names={"Desperate Prayer","Shadow Mend","Flash Heal","Heal"}; break;
        case CLASS_SHAMAN: names={"Healing Surge","Riptide","Healing Wave"}; break;
        case CLASS_DRUID: names={"Rejuvenation","Regrowth","Healing Touch"}; break;
        case CLASS_MONK: names={"Expel Harm","Vivify","Effuse"}; break;
        case CLASS_HUNTER: names={"Exhilaration"}; break;
        case CLASS_ROGUE: names={"Crimson Vial"}; break;
        case CLASS_WARRIOR: names={"Enraged Regeneration"}; break;
        case CLASS_DEATH_KNIGHT: names={"Death Pact"}; break;
        default: break;
        }
        for (char const* name : names)
        {
            auto found=catalog.find(name); if(found==catalog.end()) continue;
            for(uint32 id:found->second)
            {
                SpellInfo const* si=sSpellMgr->GetSpellInfo(id);
                if(!si || me->HasAura(id) || !me->HasSpell(id) || me->HasSpellCooldown(id) ||
                    BotUtility::CombatGlobalCooldown(me,si)) continue;
                bool stationary=si->CalcCastTime(me)>0 || si->IsChanneled();
                if(instantOnly && stationary) continue;
                if(!retry.Ready(id,now)) continue;
                if(BotUtility::PrepareBotCast(me,si,me,movement)!=SPELL_CAST_OK)continue;
                TriggerCastData data; data.triggerFlags=TriggerCastFlags::TRIGGERED_NONE;
                Spell* spell=new Spell(me,si,data);spell->m_CastItem=nullptr;
                SpellCastTargets targets;targets.SetUnitTarget(me);
                SpellCastResult result=spell->prepare(&targets); retry.Record(id,now,result==SPELL_CAST_OK?1500:BotUtility::CombatRetryDelay(result));
                if(result==SPELL_CAST_OK)return true;
            }
        }
        return false;
    }
    static std::vector<Unit*> Nearby(Player* me)
    {
        std::list<Player*> players;me->GetPlayerListInGrid(players,40.0f);
        std::vector<Unit*> units;
        for(Player* p:players)if(p && p->IsInWorld() && p->isAlive() && p->GetMap()==me->GetMap() &&
            me->InSamePhase(p) && me->canSeeOrDetect(p))units.push_back(p);
        return units;
    }
    static bool Applicable(Player* me,Unit* enemy,bool avNpc=false)
    {
        // Only the AV commander opts an approved native NPC into the shared
        // specialization rotation. Existing field/duel/party/PvP callers keep
        // their player/pet-only default and configuration switch.
        bool nativeAV=avNpc && me && enemy && enemy->ToCreature() &&
            me->GetMapId()==30 && me->GetBattleground() &&
            me->GetBattleground()->GetStatus()==STATUS_IN_PROGRESS;
        return me && enemy && enemy!=me && (enemy->GetCharmerOrOwnerPlayerOrPlayerItself() || nativeAV) &&
            PvpRotationPolicy::supported(me->GetSpecializationId()) && me->IsValidAttackTarget(enemy) &&
            sConfigMgr->GetIntDefault("PlayerBot.PvP.LegionRotations",1)!=0;
    }
    template<class Movement>
    bool Run(Player* me,Unit* enemy,Movement* movement,
        std::vector<Unit*> const& friends={},std::vector<Unit*> const& enemies={},bool avNpc=false)
    {
        if(!Applicable(me,enemy,avNpc))return false;
        if(!me->IsInWorld() || !me->isAlive() || me->IsBeingTeleported() || me->IsChangeMap() ||
            !enemy->IsInWorld() || !enemy->isAlive() || enemy->GetMap()!=me->GetMap() || !me->InSamePhase(enemy))return true;
        if(me->HasUnitState(UNIT_STATE_CASTING)){
            BotUtility::TryCombatInterrupt(me,enemy,movement); // A pet can act while its owner channels.
            return true;
        }
        if(me->IsMounted())PlayerBotSetting::ClearUnknowMount(me);
        bool melee=PvpRotationPolicy::melee(me->GetSpecializationId());
        me->SetSelection(enemy->GetGUID());me->SetInFront(enemy);
        if(!controlled(enemy)){
            me->Attack(enemy,melee);
            if(melee && me->IsWithinMeleeRange(enemy)){movement->ClearMovement();me->StopMoving();}
        }
        return execute(me,enemy,movement,friends,enemies);
    }
};
#endif
