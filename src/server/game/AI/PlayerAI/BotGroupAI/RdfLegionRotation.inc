// RDF_LEGION_ROTATIONS_V1
#include "RdfRotationPolicy.h"
#include "../FelsoriaCombatPolicy.h"
#include "SpellAuras.h"
#include "SpellTargetInfo.h"
#include <cstring>
#include <cmath>
#include "PathGenerator.h"

bool BotGroupAI::UseRdfLegionRotation() const
{
    return me->GetGroup() && me->GetGroup()->isLFGGroup() &&
        RdfRotationPolicy::supported(me->GetSpecializationId()) &&
        sConfigMgr->GetIntDefault("PlayerBot.RDF.LegionRotations", 1) != 0;
}

bool BotGroupAI::ProcessRdfLegionRotation(Unit* enemy, bool partyCombat)
{
    if (!UseRdfLegionRotation() && !UsePartyLegionRotation(enemy, partyCombat)) return false;
    if (!me->IsInWorld() || !me->isAlive() || me->IsBeingTeleported() || me->IsChangeMap()) return true;
    unsigned spec=me->GetSpecializationId();
    bool healer=RdfRotationPolicy::healer(spec), tank=RdfRotationPolicy::tank(spec), melee=RdfRotationPolicy::melee(spec);
    uint32 now=getMSTime();
    bool audit=sConfigMgr->GetIntDefault("PlayerBot.CombatSpellAudit",0)!=0;
    auto waitReason=[&](char const* reason) {
        if(RdfRotationPolicy::elapsed(now,m_RdfCombatGateLog,5000)) {
            m_RdfCombatGateLog=now;
            TC_LOG_INFO(LOG_FILTER_LFG,"RDF_COMBAT_WAIT account=%u spec=%u reason=%s target=%u mana=%.1f",me->GetSession()->GetAccountId(),spec,reason,enemy?enemy->GetEntry():0,double(me->GetPowerPct(POWER_MANA)));
        }
    };
    // A flag with no live cast is stale. Never cancel a legitimate channel on a fixed timeout.
    if(me->HasUnitState(UNIT_STATE_CASTING)) {
        Spell* generic=me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        Spell* channel=me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if((!generic || generic->getState()==SPELL_STATE_FINISHED) &&
           (!channel || channel->getState()==SPELL_STATE_FINISHED)) {
            me->ClearUnitState(UNIT_STATE_CASTING);
            waitReason("orphan_cast_flag_cleared");
        }
    }
    // Learned class spells only; no old NPC IDs, learning, or forced casts.
    if (m_RdfRotSpec!=spec || RdfRotationPolicy::elapsed(now,m_RdfRotCatalogTime,30000)) {
        if(m_RdfRotSpec!=spec){m_RdfRotRetry.Clear();m_RdfDefensiveGate.Clear();m_RdfGroupGate.Clear();m_RdfRotLastSpell=0;m_RdfRotUnavailable.clear();}
        m_RdfRotSpec=spec; m_RdfRotCatalogTime=now; m_RdfRotCatalog.clear();m_RdfRotCandidates.clear();
        unsigned family=0;
        switch(me->getClass()) {
            case CLASS_WARRIOR:family=SPELLFAMILY_WARRIOR;break;
            case CLASS_PALADIN:family=SPELLFAMILY_PALADIN;break;
            case CLASS_HUNTER:family=SPELLFAMILY_HUNTER;break;
            case CLASS_ROGUE:family=SPELLFAMILY_ROGUE;break;
            case CLASS_PRIEST:family=SPELLFAMILY_PRIEST;break;
            case CLASS_DEATH_KNIGHT:family=SPELLFAMILY_DEATHKNIGHT;break;
            case CLASS_SHAMAN:family=SPELLFAMILY_SHAMAN;break;
            case CLASS_MAGE:family=SPELLFAMILY_MAGE;break;
            case CLASS_WARLOCK:family=SPELLFAMILY_WARLOCK;break;
            case CLASS_MONK:family=SPELLFAMILY_MONK;break;
            case CLASS_DRUID:family=SPELLFAMILY_DRUID;break;
            case CLASS_DEMON_HUNTER:family=SPELLFAMILY_DEMON_HUNTER;break;
        }
        for(auto const& e:me->GetSpellMap()) {
            if(e.second.state==PLAYERSPELL_REMOVED || e.second.disabled || !e.second.active) continue;
            SpellInfo const* si=sSpellMgr->GetSpellInfo(e.first);
            if(!si || si->IsPassive() || !si->SpellName || si->ClassOptions.SpellClassSet!=family) continue;
            m_RdfRotCandidates[si->SpellName].push_back(e.first);
            auto it=m_RdfRotCatalog.find(si->SpellName);
            if(it==m_RdfRotCatalog.end()) m_RdfRotCatalog[si->SpellName]=e.first;
            else {
                SpellInfo const* old=sSpellMgr->GetSpellInfo(it->second);
                if(old && si->IsRankOf(old) && si->GetRank()>old->GetRank())it->second=e.first;
            }
        }
        if(audit) TC_LOG_INFO(LOG_FILTER_LFG,"RDF_ROTATION_PROFILE account=%u spec=%u spells=%u",
            me->GetSession()->GetAccountId(),spec,uint32(m_RdfRotCatalog.size()));
    }
    auto ID=[&](char const* name)->uint32 {
        auto it=m_RdfRotCatalog.find(name);uint32 id=it==m_RdfRotCatalog.end()?0:it->second;
        if(!id && m_RdfRotUnavailable.insert(name).second)
            TC_LOG_INFO(LOG_FILTER_LFG,"RDF_ROTATION_UNAVAILABLE account=%u spec=%u name=%s reason=missing_or_ambiguous",me->GetSession()->GetAccountId(),spec,name);
        return id;
    };
    auto A=[&](Unit* u,uint32 id)->bool {return u && u->HasAura(id);};
    auto stacks=[&](Unit* u,uint32 id)->unsigned {Aura* a=u?u->GetAura(id,u==me?ObjectGuid::Empty:me->GetGUID()):nullptr;return a?a->GetStackAmount():0;};
    auto missing=[&](char const* n,Unit* u,int32 ms)->bool {
        uint32 id=ID(n);if(!id || !u)return false;
        Aura* a=u->GetAura(id,me->GetGUID());return !a || (a->GetDuration()>=0 && a->GetDuration()<=ms);
    };
    auto P=[&](Powers p)->float {return me->GetPowerPct(p);};
    auto R=[&](Powers p)->int32 {return me->GetPower(p);};
    auto C=[&](char const* name,Unit* target)->bool {
        if(target && ShouldBlockBossPull(target))return false;
        if(!target || m_CombatCastIssued || me->HasUnitState(UNIT_STATE_CASTING))return false;
        uint32 primary=ID(name);if(!primary)return false;
        auto tryId=[&](uint32 id)->bool {
        bool petPreparation = std::strncmp(name, "Call Pet", 8) == 0 || std::strcmp(name, "Revive Pet") == 0 ||
            std::strcmp(name, "Summon Imp") == 0 || std::strcmp(name, "Summon Felguard") == 0 || std::strcmp(name, "Raise Dead") == 0;
        uint32 retryDelay = petPreparation ? 15000 : 1200;
        if(!m_RdfRotRetry.Ready(id,now))return false;
        SpellInfo const* si=sSpellMgr->GetSpellInfo(id);
        if(!si || !me->HasSpell(id) || me->HasSpellCooldown(id))return false;
        if(BotUtility::CombatGlobalCooldown(me,si))return false;
        if(!target->IsInWorld() || target->GetMap()!=me->GetMap() || !me->InSamePhase(target))return false;
        if(target!=me && !si->IsPositive() && !me->IsValidAttackTarget(target))return false;
        float range=me->GetSpellMaxRangeForTarget(target,si);
        float minRange=me->GetSpellMinRangeForTarget(target,si);
        if(target!=me && (!me->IsWithinLOSInMap(target) ||
            (range>0 && me->GetDistance(target)>range) || me->GetDistance(target)<minRange)) {
            if(!m_RdfRepositionRequested) {
                m_RdfRepositionRequested=true;
                if(!(melee && target->IsFriendlyTo(me)))
                    ProcessRdfCombatPosition(target,minRange,range>0?range:30.0f);
            }
            waitReason("spell_range_or_los");
            return false;
        }
        if(spec==269 && id==m_RdfRotLastSpell)return false;
        if(BotUtility::PrepareBotCast(me,si,target,m_Movement)!=SPELL_CAST_OK)return false;
        Dismount();
        TriggerCastData data;data.triggerFlags=TriggerCastFlags::TRIGGERED_NONE;
        Spell* spell=new Spell(me,si,data);spell->m_CastItem=nullptr;
        SpellCastTargets targets;targets.SetUnitTarget(target);targets.SetDst(*target);
        SpellCastResult result=spell->prepare(&targets);
        if(result!=SPELL_CAST_OK){
            m_RdfRotRetry.Record(id,now,petPreparation?15000:BotUtility::CombatRetryDelay(result));
            if(RdfRotationPolicy::elapsed(now,m_RdfRotLog,5000)){
                m_RdfRotLog=now;TC_LOG_INFO(LOG_FILTER_LFG,"RDF_ROTATION_FAIL account=%u spec=%u spell=%u result=%u name=%s target=%s",me->GetSession()->GetAccountId(),spec,id,uint32(result),name,target->GetName());
            }
            return false;
        }
        if (retryDelay == 15000) m_RdfRotRetry.Record(id,now,15000);
        m_CombatCastIssued=true;m_RdfRotLastSpell=id;
        if(audit || RdfRotationPolicy::elapsed(now,m_RdfRotCastLog,10000)) {
            m_RdfRotCastLog=now;
            TC_LOG_INFO(LOG_FILTER_LFG,"RDF_ROTATION_CAST account=%u spec=%u spell=%u name=%s target=%s hp=%.1f",me->GetSession()->GetAccountId(),spec,id,name,target->GetName(),double(target->GetHealthPct()));
        }
        return true;
        };
        if(tryId(primary))return true;
        // Learned same-name variants are checked by the native spell validator.
        auto variants=m_RdfRotCandidates.find(name);
        if(variants!=m_RdfRotCandidates.end())
            for(uint32 id:variants->second)if(id!=primary && tryId(id))return true;
        return false;
    };
    auto DEF=[&](char const* n)->bool{
        if(!m_RdfDefensiveGate.Ready(now,me->GetHealthPct()))return false;
        if(C(n,me)){m_RdfDefensiveGate.Record(now,me->GetHealthPct());return true;}return false;
    };
    Player* low=me;Player* groupTank=nullptr;
    float score=RdfRotationPolicy::triage(me->GetHealthPct(),tank);
    unsigned injured=0,critical=0;float minimumHP=me->GetHealthPct();
    for(GroupReference* ref=me->GetGroup()->GetFirstMember();ref;ref=ref->next()){
        Player* p=ref->getSource();
        if(!p || !p->IsInWorld() || !p->isAlive() || p->GetMap()!=me->GetMap() || !me->InSamePhase(p) || me->GetDistance(p)>60)continue;
        bool t=RdfRotationPolicy::tank(p->GetSpecializationId());if(t)groupTank=p;
        float hp=p->GetHealthPct();if(hp<minimumHP)minimumHP=hp;
        float next=RdfRotationPolicy::triage(hp,t);
        if(next<score){score=next;low=p;}
        if(me->GetDistance(p)<=40){if(hp<70)++injured;if(hp<35)++critical;}
    }
    float hp=me->GetHealthPct(),lh=low->GetHealthPct(),mana=P(POWER_MANA);
    unsigned cp=me->GetComboPoints();
    m_RdfRepositionRequested=false;
    bool combat=partyCombat||me->isInCombat(),emergency=RdfRotationPolicy::groupEmergency(injured,critical);
    if(combat && m_RdfFormationMoving){m_Movement->ClearMovement();m_RdfFormationMoving=false;}
    // FELSORIA_GUARDIAN_PACK_V1: do not restrict recovery to the selected mob.
    unsigned guardianNearby=0,guardianLostNearby=0;
    bool guardianAreaSafe=true;
    if(spec==104 && combat){
        NearCreatureVec pack;SearchCreatureListFromRange(me,pack,30.0f,false);
        Unit* lost=nullptr;float bestPriority=-100000.0f;
        for(Creature* creature:pack){
            if(!creature || !creature->IsInWorld() || !creature->isAlive() ||
               creature->GetMap()!=me->GetMap() || !me->InSamePhase(creature) ||
               !me->IsValidAttackTarget(creature) || !me->canSeeOrDetect(creature) ||
               IsInvincible(creature))continue;
            float distance=me->GetDistance(creature);
            bool close=distance<=8.0f && std::fabs(me->GetPositionZ()-creature->GetPositionZ())<4.0f &&
                me->IsWithinLOSInMap(creature);
            // Do not use pack AoE next to CC, unpulled aggressive trash or a blocked boss.
            if(close && (TargetIsControl(creature) || ShouldBlockBossPull(creature) ||
               (!creature->isInCombat() && creature->GetReactState()==REACT_AGGRESSIVE)))guardianAreaSafe=false;
            if(!creature->isInCombat() || TargetIsControl(creature) || ShouldBlockBossPull(creature))continue;
            Unit* victim=creature->getVictim();
            Player* owner=victim?victim->GetCharmerOrOwnerPlayerOrPlayerItself():nullptr;
            if(!owner || !me->GetGroup()->IsMember(owner->GetGUID()))continue;
            if(close)++guardianNearby;
            // A pet owned by this tank is also a lost target; the bear should tank it.
            bool lostAggro=victim!=me && !RdfRotationPolicy::tank(owner->GetSpecializationId());
            if(owner==me && victim!=me)lostAggro=true;
            if(!lostAggro)continue;
            if(close)++guardianLostNearby;
            if(distance>30.0f || !me->IsWithinLOSInMap(creature))continue;
            float priority=(RdfRotationPolicy::healer(owner->GetSpecializationId())?300.0f:0.0f)+
                (100.0f-owner->GetHealthPct())*3.0f-distance;
            // Prefer the current lost target on ties, avoiding needless retarget jitter.
            if(creature==enemy)priority+=10.0f;
            if(!lost || priority>bestPriority){lost=creature;bestPriority=priority;}
        }
        if(lost)enemy=lost;
    }
    if(enemy && (!enemy->IsInWorld() || !enemy->isAlive() || enemy->GetMap()!=me->GetMap() || !me->IsValidAttackTarget(enemy) || IsInvincible(enemy) || TargetIsControl(enemy)))enemy=nullptr;
    bool boss=enemy && enemy->ToCreature() && (enemy->ToCreature()->IsDungeonBoss() || enemy->ToCreature()->isWorldBoss());
    unsigned adds=enemy?1:0;
    if(enemy){NearCreatureVec v;SearchCreatureListFromRange(enemy,v,8,false);for(Creature* c:v)if(c!=enemy && c->isInCombat() && !TargetIsControl(c))++adds;}
    if(combat)m_UseFood.UpdateBotFood(BOTAI_UPDATE_TICK,m_UseMountID);
    if(m_ForceFlee){waitReason("commanded_flee");me->AttackStop();ProcessFollowToMaster();return true;}
    if(m_CruxMovement.HasCruxMovement()){waitReason("priority_movement");m_CruxMovement.UpdateCruxMovement(m_Movement);return true;}
    if(enemy && hp>20 && BotUtility::TryCombatInterrupt(me,enemy,m_Movement)) {
        m_CombatCastIssued=true;return true;
    }
    if(me->HasUnitState(UNIT_STATE_CASTING)){waitReason("active_cast_or_channel");return true;}
    if(!combat) {
        bool quiet=true;
        for(GroupReference* r=me->GetGroup()->GetFirstMember();r;r=r->next()) {
            Player* p=r->getSource();
            if(p && p->isAlive() && p->IsInWorld() && p->GetMap()==me->GetMap() && p->isInCombat()){quiet=false;break;}
        }
        if(quiet && ProcessRevivePlayer())return true;
    }
    if(combat)m_UsePotion.TryUsePotion();
    if(combat && hp<20 && me->getClass()==CLASS_MAGE && DEF("Ice Block"))return true;
    if(combat && hp<45){switch(me->getClass()){
        case CLASS_WARRIOR:if(DEF(tank?"Shield Wall":(spec==71?"Die by the Sword":"Enraged Regeneration")))return true;break;
        case CLASS_PALADIN:if(DEF(tank?"Ardent Defender":(spec==70?"Shield of Vengeance":"Divine Protection")))return true;break;
        case CLASS_HUNTER:if(C("Exhilaration",me))return true;break;
        case CLASS_ROGUE:if(DEF("Feint"))return true;break;
        case CLASS_PRIEST:if(DEF(spec==258?"Dispersion":"Desperate Prayer"))return true;break;
        case CLASS_DEATH_KNIGHT:if(DEF("Icebound Fortitude"))return true;break;
        case CLASS_SHAMAN:if(DEF("Astral Shift"))return true;break;
        case CLASS_MAGE:if(DEF(spec==62?"Prismatic Barrier":(spec==63?"Blazing Barrier":"Ice Barrier")))return true;break;
        case CLASS_WARLOCK:if(DEF("Unending Resolve"))return true;break;
        case CLASS_MONK:if(DEF(tank?"Fortifying Brew":"Diffuse Magic"))return true;break;
        case CLASS_DRUID:if(DEF("Barkskin"))return true;break;
        case CLASS_DEMON_HUNTER:if(DEF(tank?"Metamorphosis":"Blur"))return true;break;
    }}
    // FELSORIA_DUNGEON_COMBAT_V1: dispel actual harmful party auras, not random clean targets.
    // Stabilize critically injured members before spending an action on utility.
    if(healer && combat && minimumHP>=35.0f) {
        char const* cleanse=spec==65?"Cleanse":((spec==256||spec==257)?"Purify":
            (spec==264?"Purify Spirit":(spec==270?"Detox":"Nature's Cure")));
        SpellInfo const* dispel=sSpellMgr->GetSpellInfo(ID(cleanse));
        uint32 mask=0;
        if(dispel)
            for(uint8 i=0;i<MAX_SPELL_EFFECTS;++i) {
                SpellEffectInfo const* effect=dispel->GetEffect(i,me->GetMap()->GetDifficultyID());
                if(effect && effect->Effect==SPELL_EFFECT_DISPEL)
                    mask|=SpellInfo::GetDispelMask(DispelType(effect->MiscValue));
            }
        if(mask)
            for(GroupReference* ref=me->GetGroup()->GetFirstMember();ref;ref=ref->next()) {
                Player* p=ref->getSource();
                if(!p || !p->isAlive() || !p->IsInWorld() || p->GetMap()!=me->GetMap() ||
                   !me->InSamePhase(p) || me->GetDistance(p)>40.0f || !me->IsWithinLOSInMap(p))continue;
                DispelChargesList auras;p->GetDispellableAuraList(me,mask,auras);
                bool harmful=false;
                for(auto const& entry:auras) {
                    AuraApplication* app=entry.first->GetApplicationOfTarget(p->GetGUID());
                    if(app && !app->IsPositive() && dispel->CanDispelAura(entry.first->GetSpellInfo())){harmful=true;break;}
                }
                if(harmful && C(cleanse,p)) {
                    TC_LOG_INFO(LOG_FILTER_LFG,"RDF_HEALER_DISPEL account=%u spec=%u target=%s",me->GetSession()->GetAccountId(),spec,p->GetName());
                    return true;
                }
            }
    }
    auto GROUPCD=[&](char const* n,Unit* t)->bool {
        if(!m_RdfGroupGate.Ready(now,minimumHP,critical>=2))return false;
        if(C(n,t)){m_RdfGroupGate.Record(now,minimumHP);return true;}return false;
    };
    // Low party HP takes priority over every damage action, even before self combat starts.
    if(healer && minimumHP<95){
        // C() approaches using the actual healing spell range and LOS.
        switch(spec){
    case 65:
        if((lh<18) && C("Lay on Hands",low))return true;
        if((lh<30) && C("Blessing of Sacrifice",low))return true;
        if((emergency) && GROUPCD("Avenging Wrath",me))return true;
        if((lh<85) && C("Holy Shock",low))return true;
        if((injured>=3) && C("Light of Dawn",low))return true;
        if((lh<55) && C("Flash of Light",low))return true;
        if((groupTank && missing("Beacon of Light",groupTank,0)) && C("Beacon of Light",groupTank))return true;
        if((lh<95) && C("Holy Light",low))return true;
        break;
    case 256:
        if((lh<30) && C("Pain Suppression",low))return true;
        if((emergency) && GROUPCD("Power Word: Barrier",low))return true;
        if((lh<45) && C("Shadow Mend",low))return true;
        if((injured>=3) && C("Power Word: Radiance",low))return true;
        if((lh<90 && missing("Power Word: Shield",low,0)) && C("Power Word: Shield",low))return true;
        if((lh<80) && C("Penance",low))return true;
        if((lh<90) && C("Shadow Mend",low))return true;
        break;
    case 257:
        if((lh<25) && C("Guardian Spirit",low))return true;
        if((emergency) && GROUPCD("Divine Hymn",me))return true;
        if((lh<55) && C("Holy Word: Serenity",low))return true;
        if((injured>=3) && C("Holy Word: Sanctify",low))return true;
        if((lh<45) && C("Flash Heal",low))return true;
        if((injured>=3) && C("Prayer of Healing",low))return true;
        if((groupTank && missing("Prayer of Mending",groupTank,0)) && C("Prayer of Mending",groupTank))return true;
        if((lh<90 && missing("Renew",low,3000)) && C("Renew",low))return true;
        if((lh<95) && C("Heal",low))return true;
        break;
    case 264:
        if((emergency) && GROUPCD("Healing Tide Totem",me))return true;
        if((critical>=2) && GROUPCD("Spirit Link Totem",low))return true;
        if((lh<40) && C("Healing Surge",low))return true;
        if((lh<90 && missing("Riptide",low,3000)) && C("Riptide",low))return true;
        if((injured>=3) && C("Healing Rain",low))return true;
        if((injured>=3) && C("Chain Heal",low))return true;
        if((lh<95) && C("Healing Wave",low))return true;
        break;
    case 270:
        if((emergency) && GROUPCD("Revival",me))return true;
        if((lh<30) && C("Life Cocoon",low))return true;
        if((lh<50 && missing("Enveloping Mist",low,1000)) && C("Enveloping Mist",low))return true;
        if((lh<55) && C("Vivify",low))return true;
        if((lh<90 && missing("Renewing Mist",low,3000)) && C("Renewing Mist",low))return true;
        if((injured>=3) && C("Essence Font",me))return true;
        if((lh<80) && C("Vivify",low))return true;
        if((lh<95) && C("Effuse",low))return true;
        break;
    case 105:
        if((lh<30) && C("Ironbark",low))return true;
        if((emergency) && GROUPCD("Tranquility",me))return true;
        if((lh<45) && C("Swiftmend",low))return true;
        if((lh<40) && C("Regrowth",low))return true;
        if((injured>=3) && C("Wild Growth",low))return true;
        if((groupTank && missing("Lifebloom",groupTank,3000)) && C("Lifebloom",groupTank))return true;
        if((lh<90 && missing("Rejuvenation",low,3000)) && C("Rejuvenation",low))return true;
        if((lh<75) && C("Regrowth",low))return true;
        if((lh<95) && C("Healing Touch",low))return true;
        break;
        }
        if(!RdfRotationPolicy::mayDamage(true,minimumHP)){waitReason("healing_required_no_cast");return true;}
    }
    // Preparation is conditional. No mana gem, first aid or random buff loop.
    if(spec==104 && !A(me,5487) && C("Bear Form",me))return true;
    if(spec==103 && !A(me,768) && C("Cat Form",me))return true;
    if(spec==102 && !A(me,24858) && C("Moonkin Form",me))return true;
    if(spec==258 && !A(me,232698) && !A(me,194249) && C("Shadowform",me))return true;
    if((spec==253||spec==255) && !me->GetPet() && C("Call Pet 1",me))return true;
    if((spec==253||spec==255) && me->GetPet() && !me->GetPet()->isAlive() && C("Revive Pet",me))return true;
    if(me->getClass()==CLASS_WARLOCK && !me->GetPet() && C(spec==266?"Summon Felguard":"Summon Imp",me))return true;
    if(spec==252 && !me->GetPet() && C("Raise Dead",me))return true;
    if(enemy && me->GetPet() && me->GetPet()->isAlive() && me->GetPet()->getVictim()!=enemy)me->GetPet()->AI()->AttackStart(enemy);
    if(!combat){me->AttackStop();if(m_UseFood.UpdateBotFood(BOTAI_UPDATE_TICK,m_UseMountID))return true;if(!ProcessRdfFormation())ProcessFollowToMaster();return true;}
    if(!enemy){waitReason("no_engaged_target");ProcessFollowToMaster();return true;}
    if(me->GetSelectedUnit()!=enemy)me->SetSelection(enemy->GetGUID());
    // Taunt only enemies already attacking another party member or their pet.
    if(tank && enemy->getVictim()) {
        Player* victim=enemy->getVictim()->GetCharmerOrOwnerPlayerOrPlayerItself();
        if(victim && victim!=me && !RdfRotationPolicy::tank(victim->GetSpecializationId()) && me->GetGroup()->IsMember(victim->GetGUID())) {
            char const* taunt=nullptr;
            switch(spec){case 73:taunt="Taunt";break;case 66:taunt="Hand of Reckoning";break;
                case 250:taunt="Dark Command";break;case 268:taunt="Provoke";break;
                case 104:taunt="Growl";break;case 581:taunt="Torment";break;}
            if(taunt && C(taunt,enemy)) {
                TC_LOG_INFO(LOG_FILTER_LFG,"RDF_TANK_TAUNT account=%u spec=%u entry=%u victim=%s",me->GetSession()->GetAccountId(),spec,enemy->GetEntry(),victim->GetName());
                return true;
            }
        }
    }
    // Local pack threat must still work when the recovered selected mob is ranged.
    if(spec==104 && guardianNearby>=2 && guardianAreaSafe){
        bool cast=C("Thrash",me);
        if(!cast && guardianLostNearby>0)cast=C("Swipe",me);
        if(cast){
            if(audit || RdfRotationPolicy::elapsed(now,m_RdfTankPullTime,3000)){
                m_RdfTankPullTime=now;
                TC_LOG_INFO(LOG_FILTER_LFG,"RDF_GUARDIAN_PACK account=%u nearby=%u lost=%u",me->GetSession()->GetAccountId(),guardianNearby,guardianLostNearby);
            }
            return true;
        }
    }
    if(hp<=20 && BotUtility::TryCombatInterrupt(me,enemy,m_Movement)) {
        m_CombatCastIssued=true;return true;
    }
    // Keep ranged threat usable while approaching; validators enforce learned spells and range.
    if(tank && enemy->getVictim()!=me && !me->IsWithinMeleeRange(enemy) &&
       (!m_RdfTankPullTime || RdfRotationPolicy::elapsed(now,m_RdfTankPullTime,4000))) {
        char const* pull=nullptr;
        switch(spec){case 73:pull="Heroic Throw";break;case 66:pull="Avenger's Shield";break;
            case 250:pull="Death's Caress";break;case 104:pull="Moonfire";break;case 581:pull="Throw Glaive";break;}
        if(pull && C(pull,enemy)){m_RdfTankPullTime=now;return true;}
    }
    // Ranged/healing spells position inside C(), with their own native range.
    if(melee && !me->IsWithinMeleeRange(enemy)) {
        ProcessRdfCombatPosition(enemy,0.0f,4.0f);
        waitReason("melee_approach");return true;
    }
    me->SetInFront(enemy);me->SetFacingToObject(enemy);
    if(me->getVictim()!=enemy)me->Attack(enemy,melee);
    if(melee)DoMeleeAttackIfReady();else if(me->getClass()==CLASS_HUNTER)DoRangedAttackIfReady();
    float eh=enemy->GetHealthPct();
    // Recover a nearby engaged pack with class AoE after the single-target taunt.
    if(tank && hp>=35.0f && me->IsWithinMeleeRange(enemy)) {
        NearCreatureVec pack;SearchCreatureListFromRange(me,pack,8.0f,false);
        unsigned engagedNearby=0,lostNearby=0;
        for(Creature* c:pack) {
            if(!c || !c->isInCombat() || !me->InSamePhase(c) || TargetIsControl(c) || IsInvincible(c))continue;
            Unit* victim=c->getVictim();
            Player* owner=victim?victim->GetCharmerOrOwnerPlayerOrPlayerItself():nullptr;
            if(!owner || !me->GetGroup()->IsMember(owner->GetGUID()))continue;
            ++engagedNearby;
            if(owner!=me && !RdfRotationPolicy::tank(owner->GetSpecializationId()))++lostNearby;
        }
        if(engagedNearby>=2 && lostNearby>0) {
            bool cast=false;
            switch(spec){case 73:cast=C("Thunder Clap",me);break;
                case 66:cast=C("Avenger's Shield",enemy);if(!cast && missing("Consecration",me,0))cast=C("Consecration",me);break;
                case 250:cast=C("Blood Boil",me);break;case 268:cast=C("Keg Smash",enemy);break;
                case 104:if(guardianAreaSafe){cast=C("Thrash",me);if(!cast)cast=C("Swipe",me);}break;case 581:cast=C("Immolation Aura",me);break;}
            if(cast) {
                TC_LOG_INFO(LOG_FILTER_LFG,"RDF_TANK_PACK_RECOVERY account=%u spec=%u engaged=%u lost=%u",me->GetSession()->GetAccountId(),spec,engagedNearby,lostNearby);
                return true;
            }
        }
    }
    switch(spec){
    case 71:
        if((boss) && C("Battle Cry",me))return true;
        if((missing("Colossus Smash",enemy,0)) && C("Colossus Smash",enemy))return true;
        if((adds>=3) && C("Bladestorm",me))return true;
        if((missing("Rend",enemy,4500)) && C("Rend",enemy))return true;
        if((eh<20) && C("Execute",enemy))return true;
        if((true) && C("Mortal Strike",enemy))return true;
        if((true) && C("Overpower",enemy))return true;
        if((adds>=3) && C("Whirlwind",me))return true;
        if((true) && C("Slam",enemy))return true;
        break;
    case 72:
        if((boss) && C("Battle Cry",me))return true;
        if((adds>=3 && !A(me,85739)) && C("Whirlwind",me))return true;
        if((!A(me,184362) || P(POWER_RAGE)>90) && C("Rampage",enemy))return true;
        if((eh<20) && C("Execute",enemy))return true;
        if((!A(me,184362)) && C("Bloodthirst",enemy))return true;
        if((true) && C("Raging Blow",enemy))return true;
        if((true) && C("Bloodthirst",enemy))return true;
        if((true) && C("Furious Slash",enemy))return true;
        break;
    case 73:
        if((hp<30) && DEF("Last Stand"))return true;
        if(((hp<80 || boss) && missing("Shield Block",me,500)) && C("Shield Block",me))return true;
        if((hp<75 && missing("Ignore Pain",me,0)) && C("Ignore Pain",me))return true;
        if((hp<65) && C("Demoralizing Shout",me))return true;
        if((true) && C("Shield Slam",enemy))return true;
        if((true) && C("Thunder Clap",me))return true;
        if((true) && C("Revenge",enemy))return true;
        if((true) && C("Devastate",enemy))return true;
        break;
    case 65:
        if((true) && C("Judgment",enemy))return true;
        if((true) && C("Holy Shock",enemy))return true;
        if((true) && C("Crusader Strike",enemy))return true;
        break;
    case 66:
        if((hp<55) && C("Light of the Protector",me))return true;
        if((hp<30) && DEF("Guardian of Ancient Kings"))return true;
        if(((hp<80 || boss) && !A(me,132403)) && C("Shield of the Righteous",enemy))return true;
        if((true) && C("Avenger's Shield",enemy))return true;
        if((missing("Consecration",me,0)) && C("Consecration",me))return true;
        if((true) && C("Judgment",enemy))return true;
        if((true) && C("Blessed Hammer",me))return true;
        if((true) && C("Hammer of the Righteous",enemy))return true;
        break;
    case 70:
        if((boss) && C("Crusade",me))return true;
        if((boss) && C("Avenging Wrath",me))return true;
        if((adds>=3 && R(POWER_HOLY_POWER)>=5) && C("Divine Storm",enemy))return true;
        if((R(POWER_HOLY_POWER)>=5) && C("Templar's Verdict",enemy))return true;
        if((true) && C("Judgment",enemy))return true;
        if((adds>=3 && R(POWER_HOLY_POWER)>=3) && C("Divine Storm",enemy))return true;
        if((R(POWER_HOLY_POWER)>=3) && C("Templar's Verdict",enemy))return true;
        if((true) && C("Blade of Justice",enemy))return true;
        if((true) && C("Wake of Ashes",enemy))return true;
        if((true) && C("Crusader Strike",enemy))return true;
        break;
    case 253:
        if((boss) && C("Aspect of the Wild",me))return true;
        if((true) && C("Bestial Wrath",me))return true;
        if((adds>=3 && !A(me,118455)) && C("Multi-Shot",enemy))return true;
        if((true) && C("Kill Command",enemy))return true;
        if((true) && C("Dire Frenzy",enemy))return true;
        if((true) && C("Dire Beast",enemy))return true;
        if((boss) && C("Titan's Thunder",me))return true;
        if((P(POWER_FOCUS)>60) && C("Cobra Shot",enemy))return true;
        break;
    case 254:
        if((boss) && C("Trueshot",me))return true;
        if((true) && C("Windburst",enemy))return true;
        if((A(enemy,185365)) && C("Marked Shot",enemy))return true;
        if((A(enemy,187131)) && C("Aimed Shot",enemy))return true;
        if((adds>=3) && C("Multi-Shot",enemy))return true;
        if((P(POWER_FOCUS)>85) && C("Aimed Shot",enemy))return true;
        if((true) && C("Arcane Shot",enemy))return true;
        break;
    case 255:
        if((boss) && C("Aspect of the Eagle",me))return true;
        if((missing("Lacerate",enemy,3000)) && C("Lacerate",enemy))return true;
        if((stacks(me,190931)>=5) && C("Fury of the Eagle",enemy))return true;
        if((true) && C("Mongoose Bite",enemy))return true;
        if((true) && C("Explosive Trap",enemy))return true;
        if((true) && C("Flanking Strike",enemy))return true;
        if((adds>=3) && C("Carve",enemy))return true;
        if((P(POWER_FOCUS)>60) && C("Raptor Strike",enemy))return true;
        break;
    case 259:
        if((boss) && C("Vendetta",enemy))return true;
        if((missing("Garrote",enemy,4500)) && C("Garrote",enemy))return true;
        if((cp>=4 && missing("Rupture",enemy,6000)) && C("Rupture",enemy))return true;
        if((cp>=4) && C("Envenom",enemy))return true;
        if((boss) && C("Kingsbane",enemy))return true;
        if((adds>=3) && C("Fan of Knives",me))return true;
        if((true) && C("Mutilate",enemy))return true;
        break;
    case 260:
        if((boss) && C("Adrenaline Rush",me))return true;
        if((cp>=5 && !A(me,193356) && !A(me,199600) && !A(me,193358) && !A(me,193357) && !A(me,199603) && !A(me,193359)) && C("Roll the Bones",me))return true;
        if((cp>=5) && C("Run Through",enemy))return true;
        if((adds>=2 && missing("Blade Flurry",me,0)) && C("Blade Flurry",me))return true;
        if((A(me,195627)) && C("Pistol Shot",enemy))return true;
        if((true) && C("Saber Slash",enemy))return true;
        break;
    case 261:
        if((boss) && C("Shadow Blades",me))return true;
        if((cp>=5 && missing("Nightblade",enemy,4500)) && C("Nightblade",enemy))return true;
        if((cp>=5) && C("Eviscerate",enemy))return true;
        if((cp<=2 && !A(me,185422)) && C("Shadow Dance",me))return true;
        if((A(me,185422) || A(me,1784)) && C("Shadowstrike",enemy))return true;
        if((adds>=3) && C("Shuriken Storm",me))return true;
        if((true) && C("Gloomblade",enemy))return true;
        if((true) && C("Backstab",enemy))return true;
        break;
    case 256:
        if((missing("Purge the Wicked",enemy,3000)) && C("Purge the Wicked",enemy))return true;
        if((!ID("Purge the Wicked") && missing("Shadow Word: Pain",enemy,3000)) && C("Shadow Word: Pain",enemy))return true;
        if((true) && C("Penance",enemy))return true;
        if((boss) && C("Mindbender",enemy))return true;
        if((true) && C("Smite",enemy))return true;
        break;
    case 257:
        if((missing("Holy Fire",enemy,0)) && C("Holy Fire",enemy))return true;
        if((true) && C("Holy Word: Chastise",enemy))return true;
        if((true) && C("Smite",enemy))return true;
        break;
    case 258:
        if((A(me,194249)) && C("Void Bolt",enemy))return true;
        if((!A(me,194249)) && C("Void Eruption",enemy))return true;
        if((missing("Vampiric Touch",enemy,4500)) && C("Vampiric Touch",enemy))return true;
        if((missing("Shadow Word: Pain",enemy,4500)) && C("Shadow Word: Pain",enemy))return true;
        if((eh<20) && C("Shadow Word: Death",enemy))return true;
        if((true) && C("Mind Blast",enemy))return true;
        if((boss) && C("Shadowfiend",enemy))return true;
        if((adds>=3) && C("Mind Sear",enemy))return true;
        if((true) && C("Mind Flay",enemy))return true;
        break;
    case 250:
        if((hp<70 || P(POWER_RUNIC_POWER)>85) && C("Death Strike",enemy))return true;
        if((hp<45) && DEF("Vampiric Blood"))return true;
        if((stacks(me,195181)<6) && C("Marrowrend",enemy))return true;
        if((true) && C("Blood Boil",me))return true;
        if((adds>=3) && C("Death and Decay",enemy))return true;
        if((true) && C("Heart Strike",enemy))return true;
        break;
    case 251:
        if((boss) && C("Pillar of Frost",me))return true;
        if((!A(enemy,55095) || A(me,59052)) && C("Howling Blast",enemy))return true;
        if((boss) && C("Sindragosa's Fury",enemy))return true;
        if((P(POWER_RUNIC_POWER)>80) && C("Frost Strike",enemy))return true;
        if((true) && C("Obliterate",enemy))return true;
        if((true) && C("Frost Strike",enemy))return true;
        break;
    case 252:
        if((!A(enemy,191587)) && C("Outbreak",enemy))return true;
        if((boss) && C("Dark Transformation",me->GetPet()))return true;
        if((boss) && C("Summon Gargoyle",enemy))return true;
        if((adds>=3) && C("Death and Decay",enemy))return true;
        if((stacks(enemy,194310)>=4) && C("Clawing Shadows",enemy))return true;
        if((stacks(enemy,194310)>=4) && C("Scourge Strike",enemy))return true;
        if((A(me,81340) || P(POWER_RUNIC_POWER)>80) && C("Death Coil",enemy))return true;
        if((stacks(enemy,194310)<4) && C("Festering Strike",enemy))return true;
        if((true) && C("Death Coil",enemy))return true;
        break;
    case 262:
        if((!A(enemy,188389)) && C("Flame Shock",enemy))return true;
        if((boss) && C("Fire Elemental",me))return true;
        if((adds>=3 && R(POWER_MAELSTROM)>=60) && C("Earthquake",enemy))return true;
        if((R(POWER_MAELSTROM)>=90) && C("Earth Shock",enemy))return true;
        if((true) && C("Lava Burst",enemy))return true;
        if((adds>=3) && C("Chain Lightning",enemy))return true;
        if((true) && C("Lightning Bolt",enemy))return true;
        break;
    case 263:
        if((boss) && C("Feral Spirit",me))return true;
        if((!A(me,194084)) && C("Flametongue",enemy))return true;
        if((true) && C("Stormstrike",enemy))return true;
        if((adds>=3) && C("Crash Lightning",enemy))return true;
        if((R(POWER_MAELSTROM)>80) && C("Lava Lash",enemy))return true;
        if((true) && C("Boulderfist",enemy))return true;
        if((true) && C("Rockbiter",enemy))return true;
        break;
    case 264:
        if((!A(enemy,188838)) && C("Flame Shock",enemy))return true;
        if((true) && C("Lava Burst",enemy))return true;
        if((adds>=3) && C("Chain Lightning",enemy))return true;
        if((true) && C("Lightning Bolt",enemy))return true;
        break;
    case 62:
        if((mana<15) && C("Evocation",me))return true;
        if((boss && mana>65) && C("Arcane Power",me))return true;
        if((stacks(me,79683)>=2) && C("Arcane Missiles",enemy))return true;
        if((R(POWER_ARCANE_CHARGES)>0 && (mana<20 || (R(POWER_ARCANE_CHARGES)>=4 && mana<55 && !A(me,12042)))) && C("Arcane Barrage",enemy))return true;
        // Count close hostiles around the caster, not around a distant boss.
        { NearCreatureVec close;SearchCreatureListFromRange(me,close,8.0f,false);
          unsigned nearby=0;for(Creature* c:close)if(c->isInCombat() && !TargetIsControl(c))++nearby;
          if(nearby>=3 && C("Arcane Explosion",me))return true; }
        if((A(me,79683)) && C("Arcane Missiles",enemy))return true;
        if((true) && C("Arcane Blast",enemy))return true;
        if(C("Arcane Barrage",enemy))return true;
        break;
    case 63:
        if((boss) && C("Combustion",me))return true;
        if((adds>=3 && A(me,48108)) && C("Flamestrike",enemy))return true;
        if((A(me,48108)) && C("Pyroblast",enemy))return true;
        if((A(me,48107)) && C("Fire Blast",enemy))return true;
        if((true) && C("Phoenix's Flames",enemy))return true;
        if((true) && C("Fireball",enemy))return true;
        break;
    case 64:
        if((boss) && C("Icy Veins",me))return true;
        if((true) && C("Frozen Orb",enemy))return true;
        if((A(me,190446)) && C("Flurry",enemy))return true;
        if((A(me,44544) || A(enemy,228358)) && C("Ice Lance",enemy))return true;
        if((adds>=3) && C("Blizzard",enemy))return true;
        if((true) && C("Frostbolt",enemy))return true;
        break;
    case 265:
        if((missing("Agony",enemy,4500)) && C("Agony",enemy))return true;
        if((missing("Corruption",enemy,4000)) && C("Corruption",enemy))return true;
        if((missing("Siphon Life",enemy,3000)) && C("Siphon Life",enemy))return true;
        if((boss) && C("Summon Doomguard",enemy))return true;
        if((adds>=3 && missing("Seed of Corruption",enemy,0)) && C("Seed of Corruption",enemy))return true;
        if((P(POWER_SOUL_SHARDS)>=80) && C("Unstable Affliction",enemy))return true;
        if((!A(enemy,233490) && P(POWER_SOUL_SHARDS)>=40) && C("Unstable Affliction",enemy))return true;
        if((true) && C("Drain Soul",enemy))return true;
        if(C("Drain Life",enemy))return true;
        break;
    case 266:
        if((missing("Doom",enemy,12000)) && C("Doom",enemy))return true;
        if((m_RdfRotLastSpell==ID("Call Dreadstalkers") || m_RdfRotLastSpell==ID("Hand of Gul'dan")) && C("Demonic Empowerment",me))return true;
        if((true) && C("Call Dreadstalkers",enemy))return true;
        if((P(POWER_SOUL_SHARDS)>=80) && C("Hand of Gul'dan",enemy))return true;
        if((boss) && C("Summon Doomguard",enemy))return true;
        if((true) && C("Demonbolt",enemy))return true;
        if((true) && C("Shadow Bolt",enemy))return true;
        break;
    case 267:
        if((missing("Immolate",enemy,4500)) && C("Immolate",enemy))return true;
        if((boss) && C("Summon Doomguard",enemy))return true;
        if((adds>=3 && P(POWER_SOUL_SHARDS)>=80) && C("Rain of Fire",enemy))return true;
        if((P(POWER_SOUL_SHARDS)>=80) && C("Chaos Bolt",enemy))return true;
        if((true) && C("Conflagrate",enemy))return true;
        if((P(POWER_SOUL_SHARDS)>=40) && C("Chaos Bolt",enemy))return true;
        if((true) && C("Incinerate",enemy))return true;
        break;
    case 268:
        if((A(me,124273) || (A(me,124274) && hp<70)) && C("Purifying Brew",me))return true;
        if((missing("Ironskin Brew",me,2000)) && C("Ironskin Brew",me))return true;
        if((hp<55) && C("Expel Harm",me))return true;
        if((true) && C("Keg Smash",enemy))return true;
        if((true) && C("Breath of Fire",enemy))return true;
        if((true) && C("Blackout Strike",enemy))return true;
        if((P(POWER_ENERGY)>65) && C("Tiger Palm",enemy))return true;
        break;
    case 269:
        if((boss) && C("Touch of Death",enemy))return true;
        if((boss) && C("Serenity",me))return true;
        if((boss && !ID("Serenity")) && C("Storm, Earth, and Fire",me))return true;
        if((true) && C("Whirling Dragon Punch",enemy))return true;
        if((R(POWER_CHI)>=3) && C("Fists of Fury",enemy))return true;
        if((true) && C("Rising Sun Kick",enemy))return true;
        if((adds>=3) && C("Spinning Crane Kick",me))return true;
        if((R(POWER_CHI)<=3) && C("Tiger Palm",enemy))return true;
        if((true) && C("Blackout Kick",enemy))return true;
        break;
    case 270:
        if((true) && C("Rising Sun Kick",enemy))return true;
        if((stacks(me,202090)>=3) && C("Blackout Kick",enemy))return true;
        if((true) && C("Tiger Palm",enemy))return true;
        break;
    case 102:
        if((boss) && C("Celestial Alignment",me))return true;
        if((missing("Moonfire",enemy,4500)) && C("Moonfire",enemy))return true;
        if((missing("Sunfire",enemy,4500)) && C("Sunfire",enemy))return true;
        if((adds>=3 && R(POWER_LUNAR_POWER)>=60) && C("Starfall",enemy))return true;
        if((R(POWER_LUNAR_POWER)>=80) && C("Starsurge",enemy))return true;
        if((stacks(me,164547)>=2) && C("Lunar Strike",enemy))return true;
        if((stacks(me,164545)>=2) && C("Solar Wrath",enemy))return true;
        if((true) && C("Full Moon",enemy))return true;
        if((true) && C("Half Moon",enemy))return true;
        if((true) && C("New Moon",enemy))return true;
        if((R(POWER_LUNAR_POWER)>=40) && C("Starsurge",enemy))return true;
        if((adds>=3) && C("Lunar Strike",enemy))return true;
        if((true) && C("Solar Wrath",enemy))return true;
        break;
    case 103:
        if((P(POWER_ENERGY)<50) && C("Tiger's Fury",me))return true;
        if((boss) && C("Berserk",me))return true;
        if((missing("Rake",enemy,4500)) && C("Rake",enemy))return true;
        if((cp>=5 && missing("Rip",enemy,7000)) && C("Rip",enemy))return true;
        if((cp>=5) && C("Ferocious Bite",enemy))return true;
        if((adds>=3 && missing("Thrash",enemy,4500)) && C("Thrash",enemy))return true;
        if((adds>=3) && C("Swipe",enemy))return true;
        if((true) && C("Shred",enemy))return true;
        break;
    case 104:
        if((hp<55) && C("Frenzied Regeneration",me))return true;
        if(((hp<80 || boss) && missing("Ironfur",me,1500)) && C("Ironfur",me))return true;
        if((hp<35) && DEF("Survival Instincts"))return true;
        if((missing("Moonfire",enemy,3000)) && C("Moonfire",enemy))return true;
        if((true) && (guardianAreaSafe && C("Thrash",me)))return true;
        if((true) && C("Mangle",enemy))return true;
        if((P(POWER_RAGE)>85) && C("Maul",enemy))return true;
        if((true) && (guardianAreaSafe && C("Swipe",me)))return true;
        break;
    case 105:
        if((missing("Moonfire",enemy,3000)) && C("Moonfire",enemy))return true;
        if((missing("Sunfire",enemy,3000)) && C("Sunfire",enemy))return true;
        if((true) && C("Solar Wrath",enemy))return true;
        break;
    case 577:
        if((boss) && C("Metamorphosis",enemy))return true;
        if((adds>=2 || boss || R(POWER_FURY)>=50) && C("Eye Beam",enemy))return true;
        if((true) && C("Death Sweep",enemy))return true;
        if((true) && C("Blade Dance",enemy))return true;
        if((R(POWER_FURY)>=40) && C("Annihilation",enemy))return true;
        if((R(POWER_FURY)>=40) && C("Chaos Strike",enemy))return true;
        if((boss) && C("Fury of the Illidari",enemy))return true;
        if((true) && C("Demon's Bite",enemy))return true;
        if((true) && C("Throw Glaive",enemy))return true;
        break;
    case 581:
        if((hp<65) && C("Soul Cleave",enemy))return true;
        if(((hp<80 || boss) && missing("Demon Spikes",me,1000)) && C("Demon Spikes",me))return true;
        if((hp<50) && C("Fiery Brand",enemy))return true;
        if((true) && C("Immolation Aura",me))return true;
        if((true) && C("Sigil of Flame",enemy))return true;
        if((R(POWER_PAIN)>80) && C("Soul Cleave",enemy))return true;
        if((true) && C("Fracture",enemy))return true;
        if((true) && C("Shear",enemy))return true;
        break;
    }
    if(audit && RdfRotationPolicy::elapsed(now,m_RdfRotLog,10000)){
        m_RdfRotLog=now;TC_LOG_INFO(LOG_FILTER_LFG,"RDF_ROTATION_IDLE account=%u spec=%u target=%s hp=%.1f lowest=%.1f mana=%.1f",me->GetSession()->GetAccountId(),spec,enemy->GetName(),double(hp),double(minimumHP),double(mana));
    }
    return true;
}

// RDF_FORMATION_V1: stable role slots relative to the real player's facing.
bool BotGroupAI::ProcessRdfFormation()
{
    if(m_MechanicOwned)return true;
    // FELSORIA_TANK_LEAD_V1_GATE
    if(ProcessTankLeadFormation())return true;

    // FELSORIA_FORMATION_RECOVERY_V1
    auto resetProgress=[&]() {
        m_RdfFormationProgressTime=0;
        m_RdfFormationRecoveryAttempt=0;
    };
    if(!UseRdfLegionRotation() || !me->IsInWorld() || !me->isAlive() ||
        !me->GetMap()->IsDungeon()) {resetProgress();return false;}
    for(GroupReference* r=me->GetGroup()->GetFirstMember();r;r=r->next())
        if(Player* p=r->getSource())
            if(p->IsInWorld() && p->GetMap()==me->GetMap() && p->isInCombat()) {
                resetProgress();return false;
            }
    if(me->IsBeingTeleported() || me->IsChangeMap() || me->HasUnitState(UNIT_STATE_CASTING) ||
        me->GetTransport() || me->GetVehicleBase() || me->IsFlying() || m_StopFollow ||
        me->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING) ||
        me->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR) || IsNotMovement()) {
        resetProgress();return true;
    }
    Player* anchor=m_MasterPlayer;
    if(!anchor || !anchor->IsInWorld() || !anchor->isAlive() || anchor->GetMap()!=me->GetMap() ||
        !me->InSamePhase(anchor) || !anchor->GetSession() || anchor->GetSession()->IsBotSession()) {
        resetProgress();return false;
    }
    uint32 now=getMSTime();
    if(m_RdfFormationTick && !RdfRotationPolicy::elapsed(now,m_RdfFormationTick,500))return true;
    m_RdfFormationTick=now;
    if(m_RdfFormationProgressTime) {
        float px=me->GetPositionX()-m_RdfFormationProgressPosition.GetPositionX();
        float py=me->GetPositionY()-m_RdfFormationProgressPosition.GetPositionY();
        float pz=me->GetPositionZ()-m_RdfFormationProgressPosition.GetPositionZ();
        if(px*px+py*py+pz*pz>0.0625f) {
            m_RdfFormationProgressPosition=me->GetPosition();
            m_RdfFormationProgressTime=now;
        } else if(RdfRotationPolicy::elapsed(now,m_RdfFormationProgressTime,4000)) {
            ++m_RdfFormationRecoveryAttempt;
            m_RdfFormationProgressTime=now;
            if(m_RdfFormationMoving)m_Movement->ClearMovement();
            m_RdfFormationMoving=false;
            TC_LOG_INFO(LOG_FILTER_LFG,"RDF_FORMATION_RETRY account=%u attempt=%u bot=(%.3f,%.3f,%.3f) anchor=(%.3f,%.3f,%.3f)",
                me->GetSession()->GetAccountId(),m_RdfFormationRecoveryAttempt,
                me->GetPositionX(),me->GetPositionY(),me->GetPositionZ(),
                anchor->GetPositionX(),anchor->GetPositionY(),anchor->GetPositionZ());
        }
    }
    unsigned spec=me->GetSpecializationId(),slot=0;
    auto role=[](unsigned s)->unsigned{return RdfRotationPolicy::tank(s)?0:(RdfRotationPolicy::healer(s)?1:2);};
    for(GroupReference* r=me->GetGroup()->GetFirstMember();r;r=r->next()) {
        Player* p=r->getSource();
        if(p && p!=me && p->GetSession() && p->GetSession()->IsBotSession() &&
            role(p->GetSpecializationId())==role(spec) && p->GetGUID()<me->GetGUID())++slot;
    }
    RdfRotationPolicy::FormationOffset offset=RdfRotationPolicy::formation(spec,slot);
    float distance=std::sqrt(offset.forward*offset.forward+offset.side*offset.side);
    float angle=std::atan2(offset.side,offset.forward);
    float const scales[]={1.0f,0.75f,0.5f};
    float const turns[]={0.0f,0.5f,-0.5f,1.0f,-1.0f,1.5f,-1.5f};
    // Prefer the normal slot; try other reachable sides only when necessary.
    for(unsigned i=0;i<7;++i) {
        unsigned turn=(m_RdfFormationRecoveryAttempt+i)%7;
        for(float scale:scales) {
            Position candidate;
            anchor->GetFirstCollisionPosition(candidate,distance*scale,angle+turns[turn]);
            float dx=candidate.GetPositionX()-anchor->GetPositionX();
            float dy=candidate.GetPositionY()-anchor->GetPositionY();
            if(dx*dx+dy*dy<2.25f ||
                std::fabs(candidate.GetPositionZ()-anchor->GetPositionZ())>4.5f)continue;
            PathGenerator path(me);
            if(!path.CalculatePath(candidate.GetPositionX(),candidate.GetPositionY(),candidate.GetPositionZ(),false,false,false))continue;
            if(!(path.GetPathType()&PATHFIND_NORMAL) ||
                (path.GetPathType()&(PATHFIND_NOPATH|PATHFIND_NOT_USING_PATH|PATHFIND_INCOMPLETE|PATHFIND_SHORTCUT|PATHFIND_SHORT)))continue;
            auto const& points=path.GetPath();
            if(points.size()<2)continue;
            auto const& end=points.back();
            float ex=end.x-candidate.GetPositionX(),ey=end.y-candidate.GetPositionY();
            if(ex*ex+ey*ey>2.25f || std::fabs(end.z-candidate.GetPositionZ())>2.0f)continue;
            dx=me->GetPositionX()-candidate.GetPositionX();
            dy=me->GetPositionY()-candidate.GetPositionY();
            float d2=dx*dx+dy*dy;
            bool sameFloor=std::fabs(me->GetPositionZ()-candidate.GetPositionZ())<=1.5f;
            if(sameFloor && (d2<=1.0f || (!m_RdfFormationMoving && d2<3.24f))) {
                if(m_RdfFormationMoving)m_Movement->ClearMovement();
                m_RdfFormationMoving=false;resetProgress();return true;
            }
            // Launch the very path we checked, without calculating it a second time.
            if(!LaunchRdfValidatedPath(path))continue;
            m_RdfFormationMoving=true;
            if(!m_RdfFormationProgressTime) {
                m_RdfFormationProgressPosition=me->GetPosition();
                m_RdfFormationProgressTime=now;
            }
            return true;
        }
    }
    if(m_RdfFormationMoving)m_Movement->ClearMovement();
    m_RdfFormationMoving=false;
    if(RdfRotationPolicy::elapsed(now,m_RdfFormationLog,10000)) {
        m_RdfFormationLog=now;
        TC_LOG_INFO(LOG_FILTER_LFG,"RDF_FORMATION_BLOCKED account=%u spec=%u slot=%u bot=(%.3f,%.3f,%.3f) anchor=(%.3f,%.3f,%.3f)",
            me->GetSession()->GetAccountId(),spec,slot,
            me->GetPositionX(),me->GetPositionY(),me->GetPositionZ(),
            anchor->GetPositionX(),anchor->GetPositionY(),anchor->GetPositionZ());
    }
    return false;
}
