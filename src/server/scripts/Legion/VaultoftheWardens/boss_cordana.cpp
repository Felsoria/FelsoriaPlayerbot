/*
    Dungeon : Vault of the Wardens 100-110
    Encounter: Cordana
    TO-DO: Resizing AT, cast from invis for npc
*/
#include <set>
#include "FelsoriaCordanaPolicy.h"
#include "AreaTrigger.h"
#include "AreaTriggerAI.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SmartAI.h"
#include "vault_of_the_wardens.h"

enum Says
{
    SAY_AGGRO               = 0,
    SAY_SHADOWSTEP          = 1,
    SAY_SHADOWSTEP_EMOTE    = 2,
    SAY_SHADOWSTEP_REMOVE   = 3,
    SAY_GLAIVE              = 4,
    SAY_KILLED              = 5,
    SAY_TURN_KICK           = 6,
    SAY_DEATH               = 7,
    SAY_EVADE               = 8,
    SAY_KILLED_WARDEN       = 9,
    SAY_EMOTE_CREEPING      = 10,
    SAY_CREEPING            = 12,
    SAY_MYTHIC_VENGEANCE    = 13,
    SAY_MYTHIC_DOWNFALL     = 14,
    SAY_KILLED_PRISON       = 15,
    SAY_TURN_KICK_WARNING   = 16,
};

enum Spells
{
    SPELL_KNOCKDOWN_KICK        = 197251,
    SPELL_TURN_KICK             = 197250,
    SPELL_DEEPENING_SHADOWS     = 213583,  // 213397
    SPELL_DEEPENING_SHADOWS_TWO = 213576,  // two targets 
    SPELL_DEEPENING_SHADOWS_RAND = 197551, 
    SPELL_DETONATING_MOONGLAIVE = 197513,
    SPELL_SHADOWSTEP            = 203416,
    SPELL_SHADOWSTEP_2          = 204635,
    SPELL_VENGEANCE             = 204984,   //! To-Do invis for npc and cast from invis with visual
    SPELL_VENGEANCE_SHIELD      = 205004,
    
    // while invis
    SPELL_INVISE_BY_SHADOWSTEP  = 204461,
    SPELL_INVISE_BY_SHADOWSTEP_1  = 192750,
    SPELL_FEL_GLAIVE            = 197333,
    SPELL_STEAL_LIGHT           = 206387,
    
    // heroic and mythic 
    SPELL_CREEPING_DOOM_SHIELD  = 197422, 
    SPELL_CREEPING_DOOM_SECOND  = 213685,
    SPELL_CREEPING_DOOM_DMG     = 197507,

    //Other
    SPELL_ELUNES_LIGHT          = 201359,
    SPELL_ELUNES_LIGHT_VIS_1    = 197897,
    SPELL_ELUNES_LIGHT_VIS_2    = 204294,
    SPELL_ELUNES_LIGHT_CLICK    = 197890,
    SPELL_ELUNES_LIGHT_OVERRIDE = 192656,
    SPELL_ELUNES_LIGHT_SUMMON   = 204491,
    SPELL_ELUNES_LIGHT_DMG      = 192752,
    
    
    SPELL_INTRO                 = 198309,    
};

enum eEvents
{
    EVENT_KNOCKDOWN_KICK        = 1,
    EVENT_KNOCKDOWN_KICK_TRIGGER,
    EVENT_DEEPENING_SHADOWS,
    EVENT_DETONATING_MOONGLAIVE,
    EVENT_SHADOWSTEP,
    EVENT_VENGEANCE,
    EVENT_CREEPING_FIRST,
    EVENT_CREEPING_SECOND
};

Position const centrPos = {4049.907f, -297.4254f, -281.4167f};

std::map<uint8, std::pair<Position, Position> > paths =
{
    {0, {{4074.16f, -325.45f, -281.11f}, {4028.46f, -325.07f, -281.11f}}},
    {1, {{4022.30f, -321.86f, -281.11f}, {4022.86f, -273.61f, -281.11f}}},
    {2, {{4027.00f, -269.02f, -281.11f}, {4074.33f, -269.34f, -281.11f}}}
};

Position const vengeance_position[4]
{
    {4027.47f, -273.93f, -281.11f, 0.0f},
    {4074.04f, -274.42f, -281.11f, 0.0f},
    {4074.04f, -320.78f, -281.11f, 0.0f},
    {4027.47f, -320.78f, -281.11f, 0.0f},
};

std::map<uint8, std::pair<Position, Position> > creeping_walls =
{
    {0, {{4020.11f, -267.21f, -281.11f, 6.26f}, {4020.11f, -327.91f, -281.11f, 6.26f}}},
    {1, {{4081.06f, -327.91f, -281.11f, 1.55f}, {4020.11f, -327.91f, -281.11f, 1.55f}}},
    {2, {{4081.06f, -267.21f, -281.11f, 3.17f}, {4081.06f, -327.91f, -281.11f, 3.17f}}},
    {3, {{4081.06f, -267.21f, -281.11f, 4.71f}, {4020.11f, -267.21f, -281.11f, 4.71f}}}
};

// FELSORIA_VOTW_LIGHT_V2: retain SmartAI combat, enforce illumination server-side.
namespace FelsoriaWardensLight
{
    static constexpr float RevealRadius = 10.0f;

    bool IsRevealed(Creature* creature)
    {
        if (!creature || creature->GetMapId() != 1493)
            return true;
        std::list<Player*> players;
        creature->GetPlayerListInGrid(players, RevealRadius);
        for (Player* player : players)
            if (player && player->IsInWorld() && creature->InSamePhase(player) &&
                player->isAlive() && !player->isGameMaster() &&
                player->HasAura(SPELL_ELUNES_LIGHT_OVERRIDE) &&
                creature->IsWithinDistInMap(player, RevealRadius))
                return true;
        // Thrown/dropped light reveals enemies before it is picked up.
        return creature->FindNearestCreature(NPC_ELUNES, RevealRadius) != nullptr;
    }

    void UpdateShadow(Creature* creature)
    {
        if (!creature->isAlive() || creature->GetMapId() != 1493)
            return;
        if (IsRevealed(creature))
        {
            creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            creature->RemoveAurasDueToSpell(192750);
        }
        else
        {
            creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            if (!creature->HasAura(192750))
                creature->CastSpell(creature, 192750, true);
        }
    }
}

// Preserve the existing SmartAI spell rotation and events for both spider entries.
class npc_felsoria_wardens_shadow_trash : public CreatureScript
{
public:
    npc_felsoria_wardens_shadow_trash() : CreatureScript("npc_felsoria_wardens_shadow_trash") {}
    struct AI : public SmartAI
    {
        AI(Creature* creature) : SmartAI(creature) {}
        uint32 lightTimer = 0;
        void Reset() override
        {
            SmartAI::Reset();
            lightTimer = 0;
            FelsoriaWardensLight::UpdateShadow(me);
        }
        void UpdateAI(uint32 diff) override
        {
            SmartAI::UpdateAI(diff);
            if (lightTimer <= diff)
            {
                lightTimer = 250;
                FelsoriaWardensLight::UpdateShadow(me);
            }
            else
                lightTimer -= diff;
        }
        void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType type) override
        {
            if (me->GetMapId() == 1493 && !FelsoriaWardensLight::IsRevealed(me))
            {
                damage = 0; // Covers AoE and DoTs, including spells ignoring selectable flags.
                return;
            }
            SmartAI::DamageTaken(attacker, damage, type);
        }
    };
    CreatureAI* GetAI(Creature* creature) const override { return new AI(creature); }
};

// Keep the dropped light's SmartAI visuals/click events; also allow walking pickup.
class npc_felsoria_wardens_dropped_light : public CreatureScript
{
public:
    npc_felsoria_wardens_dropped_light() : CreatureScript("npc_felsoria_wardens_dropped_light") {}
    struct AI : public SmartAI
    {
        AI(Creature* creature) : SmartAI(creature) {}
        bool consumed = false;
        uint32 pickupTimer = 0;
        void UpdateAI(uint32 diff) override
        {
            SmartAI::UpdateAI(diff);
            if (me->GetMapId() != 1493 || consumed)
                return;
            if (pickupTimer > diff)
            {
                pickupTimer -= diff;
                return;
            }
            pickupTimer = 200;
            std::list<Player*> players;
            me->GetPlayerListInGrid(players, 1.5f);
            for (Player* player : players)
                if (player && player->isAlive() && !player->isGameMaster() &&
                    !player->HasAura(SPELL_ELUNES_LIGHT_OVERRIDE) &&
                    me->IsWithinDistInMap(player, 1.5f))
                {
                    player->CastSpell(player, SPELL_ELUNES_LIGHT_OVERRIDE, true);
                    if (player->HasAura(SPELL_ELUNES_LIGHT_OVERRIDE))
                    {
                        consumed = true;
                        me->DespawnOrUnsummon();
                        break;
                    }
                }
        }
    };
    CreatureAI* GetAI(Creature* creature) const override { return new AI(creature); }
};

//95888
class boss_cordana : public CreatureScript
{
public:
    boss_cordana() : CreatureScript("boss_cordana") {}

    // FELSORIA_CORDANA_FULL_V2: encounter state is independent of speech throttles.
    struct boss_cordanaAI : public BossAI
    {
        boss_cordanaAI(Creature* creature) : BossAI(creature, DATA_CORDANA)
        {
            SetCanSeeEvenInPassiveMode(true);
        }
        bool intro=false,encounterActive=false,vengeancePhase=false,creeping_phase=false;
        bool knockdownReactionPending=false,shadowstepActive=false,glaiveAnnounced=false;
        bool firstCreepingActive=false,firstCreepingPending=false,firstWavesIssued=false;
        bool achievement=true,creepingStarted=false;
        uint8 vengeance_health=50,creeping_health=0;
        uint32 dialogueCooldown[17]={},killSpeechCooldown=0,encounterSerial=0;
        uint32 shadowStarted=0,firstCreepingStarted=0,lastCreepingCast=0,lightdetect=0;
        unsigned pendingFirstWaves=0;
        ObjectGuid shadowSummon;
        std::set<ObjectGuid> vengeanceSummons,creepingSummons;

        bool IsMythic() const
        {
            return GetDifficultyID()==DIFFICULTY_MYTHIC_DUNGEON || GetDifficultyID()==DIFFICULTY_MYTHIC_KEYSTONE;
        }
        void TalkGuarded(uint8 textId,uint32 cooldown=2000)
        {
            if(textId>=17 || dialogueCooldown[textId])return;
            dialogueCooldown[textId]=cooldown;Talk(textId);
        }
        void Reset() override
        {
            // Disable callbacks BEFORE aura/summon cleanup can invoke them.
            encounterActive=false;shadowstepActive=false;firstCreepingActive=false;
            firstCreepingPending=false;firstWavesIssued=false;glaiveAnnounced=false;
            knockdownReactionPending=false;creepingStarted=false;++encounterSerial;
            vengeancePhase=false;creeping_phase=GetDifficultyID()==DIFFICULTY_NORMAL;
            vengeance_health=creeping_phase?50:25;creeping_health=creeping_phase?0:40;
            pendingFirstWaves=0;vengeanceSummons.clear();creepingSummons.clear();shadowSummon.Clear();
            achievement=true;lightdetect=0;killSpeechCooldown=0;
            for(uint32& cooldown:dialogueCooldown)cooldown=0;
            me->KillAllDelayedEvents();
            _Reset();
            me->SetWalk(false);SetFlyMode(false);me->SetReactState(REACT_DEFENSIVE);
            me->RemoveAurasDueToSpell(SPELL_FEL_GLAIVE);
            me->RemoveAurasDueToSpell(SPELL_INVISE_BY_SHADOWSTEP);
            me->RemoveAurasDueToSpell(SPELL_INVISE_BY_SHADOWSTEP_1);
            me->RemoveAurasDueToSpell(SPELL_CREEPING_DOOM_SHIELD);
            me->RemoveAurasDueToSpell(SPELL_VENGEANCE_SHIELD);
            if(instance){
                instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_ELUNES_LIGHT_OVERRIDE);
                instance->SetData(DATA_CORDANA_ACTIONS,1);
                instance->SetData(DATA_CORDANA_ACTIONS,2);
            }
            // Intro is an approach scene, not repeated on every combat Reset.
        }
        void EnterCombat(Unit* /*who*/) override
        {
            Reset();_EnterCombat();
            if(instance && instance->GetBossState(DATA_CORDANA)!=IN_PROGRESS)return;
            encounterActive=true;TalkGuarded(SAY_AGGRO);
            uint32 serial=encounterSerial;
            me->AddDelayedCombat(1000,[this,serial](){
                if(encounterActive && serial==encounterSerial && me->isAlive())
                    me->CastSpell(me,SPELL_ELUNES_LIGHT,true);
            });
            ScheduleEvents();events.RescheduleEvent(EVENT_SHADOWSTEP,20000);
            me->RemoveFlag(UNIT_FIELD_FLAGS,UNIT_FLAG_NOT_SELECTABLE|UNIT_FLAG_IMMUNE_TO_PC|UNIT_FLAG_NOT_ATTACKABLE_1);
        }
        void EnterEvadeMode() override
        {
            bool speak=encounterActive && me->isAlive();
            encounterActive=false;shadowstepActive=false;firstCreepingActive=false;
            firstCreepingPending=false;me->KillAllDelayedEvents();me->GetMotionMaster()->Clear();
            if(speak)TalkGuarded(SAY_EVADE);
            BossAI::EnterEvadeMode();
        }
        void JustDied(Unit* /*killer*/) override
        {
            encounterActive=false;shadowstepActive=false;firstCreepingActive=false;
            firstCreepingPending=false;me->KillAllDelayedEvents();
            TalkGuarded(SAY_DEATH);_JustDied();
            me->SummonCreature(103860,4064.80f,-281.94f,-281.42f,3.9f);
            if(instance){
                instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_ELUNES_LIGHT_OVERRIDE);
                if(achievement && IsMythic())instance->DoUpdateAchievementCriteria(CRITERIA_TYPE_BE_SPELL_TARGET,215485);
            }
            DoCast(me,SPELL_ELUNES_LIGHT_SUMMON,true);
        }
        void DoAction(int32 const action) override
        {
            if(encounterActive && action==ACTION_1)achievement=false;
        }
        void KilledUnit(Unit* victim) override
        {
            if(!encounterActive || !victim || !victim->IsPlayer() || killSpeechCooldown)return;
            killSpeechCooldown=10000;
            uint8 variants[]={SAY_KILLED,SAY_KILLED_WARDEN,SAY_KILLED_PRISON};
            TalkGuarded(variants[urand(0,2)]);
        }
        void ReleaseVengeance(Creature* summon)
        {
            if(summon->GetEntry()!=100351 || !vengeanceSummons.erase(summon->GetGUID()))return;
            if(encounterActive && vengeanceSummons.empty())me->RemoveAurasDueToSpell(SPELL_VENGEANCE_SHIELD);
        }
        void ReleaseShadow(Creature* summon)
        {
            if(summon->GetEntry()!=104293 || shadowSummon!=summon->GetGUID())return;
            shadowSummon.Clear();
            if(encounterActive && me->isAlive())me->CastSpell(summon->GetPosition(),SPELL_ELUNES_LIGHT_SUMMON,true);
        }
        void JustSummoned(Creature* summon) override
        {
            if(!encounterActive && (summon->GetEntry()==100336 || summon->GetEntry()==100351 || summon->GetEntry()==104293)){
                summon->DespawnOrUnsummon();return;
            }
            if(summon->GetEntry()==104293 && !shadowSummon.IsEmpty()){
                summon->DespawnOrUnsummon();return;
            }
            summons.Summon(summon);
            if(summon->GetEntry()==103860)return; // Outro is peaceful SmartAI, never root/aggro it.
            if(encounterActive)DoZoneInCombat(summon,150.0f);
            if(summon->GetEntry()!=100336)summon->SetControlled(true,UNIT_STATE_ROOT);
            if(summon->GetEntry()==100336){creepingSummons.insert(summon->GetGUID());return;}
            if(summon->GetEntry()==104293){
                shadowSummon=summon->GetGUID();bool stolen=false;
                std::list<Player*> players;me->GetPlayerListInGrid(players,100.0f);
                for(Player* player:players)if(player && player->IsInWorld() && player->isAlive() &&
                    me->InSamePhase(player) && player->HasAura(SPELL_ELUNES_LIGHT_OVERRIDE)){
                    player->RemoveAurasDueToSpell(SPELL_ELUNES_LIGHT_OVERRIDE);
                    summon->CastSpell(player,SPELL_STEAL_LIGHT,true);stolen=true;
                }
                if(stolen)summon->CastSpell(summon,206567,true);
                if(instance){instance->SetData(DATA_CORDANA_ACTIONS,1);instance->SetData(DATA_CORDANA_ACTIONS,2);}
            }else if(summon->GetEntry()==100351){
                vengeanceSummons.insert(summon->GetGUID());me->AddAura(SPELL_VENGEANCE_SHIELD,me);
            }
        }
        void SummonedCreatureDies(Creature* summon,Unit* /*killer*/) override
        {
            ReleaseShadow(summon);ReleaseVengeance(summon);
            creepingSummons.erase(summon->GetGUID());
        }
        void SummonedCreatureDespawn(Creature* summon) override
        {
            ReleaseShadow(summon);ReleaseVengeance(summon);
            creepingSummons.erase(summon->GetGUID());BossAI::SummonedCreatureDespawn(summon);
        }
        void MoveInLineOfSight(Unit* who) override
        {
            if(!who || !who->IsPlayer() || who->ToPlayer()->isGameMaster())return;
            if(who->GetPositionX()>=4093.63f && who->GetPositionX()<=4168.71f && who->HasAura(SPELL_ELUNES_LIGHT_OVERRIDE))
                who->RemoveAurasDueToSpell(SPELL_ELUNES_LIGHT_OVERRIDE);
            if(!encounterActive && me->GetDistance(who)<80.0f && !intro){
                intro=true;me->GetMotionMaster()->MovePoint(1,centrPos);me->SetHomePosition(centrPos);DoCast(SPELL_INTRO);
            }
        }
        void MovementInform(uint32 type,uint32 id) override
        {
            if(type!=POINT_MOTION_TYPE)return;
            if(id==1 && !encounterActive)
                me->RemoveFlag(UNIT_FIELD_FLAGS,UNIT_FLAG_NOT_SELECTABLE|UNIT_FLAG_IMMUNE_TO_PC|UNIT_FLAG_NOT_ATTACKABLE_1);
            else if(id==2 && shadowstepActive && encounterActive)StartMove(urand(0,2));
            else if(id==3 && firstCreepingActive && encounterActive && !firstWavesIssued){
                firstWavesIssued=true;SummonCreepings(1);
            }
        }
        void SpellHitTarget(Unit* target,SpellInfo const* spell) override
        {
            if(encounterActive && target && spell->Id==SPELL_KNOCKDOWN_KICK && knockdownReactionPending){
                knockdownReactionPending=false;events.RescheduleEvent(EVENT_KNOCKDOWN_KICK_TRIGGER,500);
                TalkGuarded(urand(0,1)?SAY_TURN_KICK:SAY_TURN_KICK_WARNING);
            }
        }
        void FinishShadowstep()
        {
            if(!shadowstepActive)return;
            shadowstepActive=false;
            me->RemoveAurasDueToSpell(SPELL_FEL_GLAIVE);
            me->RemoveAurasDueToSpell(SPELL_INVISE_BY_SHADOWSTEP);
            me->RemoveAurasDueToSpell(SPELL_INVISE_BY_SHADOWSTEP_1);
            me->GetMotionMaster()->Clear();me->SetWalk(false);me->SetReactState(REACT_DEFENSIVE);
            if(encounterActive && me->isAlive()){
                TalkGuarded(SAY_SHADOWSTEP_REMOVE);ScheduleEvents();
            }
        }
        void OnApplyOrRemoveAura(uint32 spellId,AuraRemoveMode /*mode*/,bool apply) override
        {
            if(spellId==SPELL_FEL_GLAIVE){
                if(apply && encounterActive && shadowstepActive && !glaiveAnnounced){
                    glaiveAnnounced=true;uint32 serial=encounterSerial;
                    // Give the Shadowstep line time to finish before the glaive voice.
                    me->AddDelayedCombat(4000,[this,serial](){
                        if(encounterActive && serial==encounterSerial && shadowstepActive && me->isAlive())TalkGuarded(SAY_GLAIVE);
                    });
                }else if(!apply)FinishShadowstep();
            }else if(spellId==SPELL_CREEPING_DOOM_SHIELD && firstCreepingActive && encounterActive){
                if(apply){
                    if(!firstWavesIssued){
                        TalkGuarded(SAY_CREEPING);SetFlyMode(true);
                        me->GetMotionMaster()->MovePoint(3,centrPos.GetPositionX(),centrPos.GetPositionY(),centrPos.GetPositionZ()+5.0f);
                    }
                }else{
                    // An expiring visual cannot make Cordana vulnerable while her first walls remain.
                    if(!FirstCreepingReady(getMSTime())){
                        uint32 serial=encounterSerial;
                        // Wait until removal has finished; never re-add an aura from inside its removal callback.
                        me->AddDelayedEvent(1,[this,serial](){
                            if(encounterActive && firstCreepingActive && serial==encounterSerial &&
                               !me->HasAura(SPELL_CREEPING_DOOM_SHIELD))me->AddAura(SPELL_CREEPING_DOOM_SHIELD,me);
                        });
                    }
                    else FinishFirstCreeping();
                }
            }
        }
        void SpellFinishCast(SpellInfo const* spell) override
        {
            if(!encounterActive || !me->isAlive())return;
            if(spell->Id==SPELL_CREEPING_DOOM_SECOND){
                if(IsMythic())TalkGuarded(urand(0,1)?SAY_MYTHIC_VENGEANCE:SAY_MYTHIC_DOWNFALL);
                SummonCreepings(2);
            }
        }
        void QueueVengeance()
        {
            if(!vengeancePhase && !firstCreepingActive && !firstCreepingPending && me->HealthBelowPct(vengeance_health)){
                vengeancePhase=true;events.RescheduleEvent(EVENT_VENGEANCE,1);
            }
        }
        void DamageTaken(Unit* /*attacker*/,uint32& damage,DamageEffectType /*type*/) override
        {
            if(!encounterActive)return;
            if(firstCreepingActive || firstCreepingPending){damage=0;return;}
            if(!creeping_phase && FelsoriaCordana::threshold(me->GetHealth(),me->GetMaxHealth(),damage,creeping_health)){
                creeping_phase=true;firstCreepingPending=true;events.RescheduleEvent(EVENT_CREEPING_FIRST,1);return;
            }
            if(!vengeancePhase && FelsoriaCordana::threshold(me->GetHealth(),me->GetMaxHealth(),damage,vengeance_health)){
                vengeancePhase=true;events.RescheduleEvent(EVENT_VENGEANCE,1);
            }
        }
        void ScheduleEvents()
        {
            if(!encounterActive || shadowstepActive || firstCreepingActive)return;
            events.RescheduleEvent(EVENT_KNOCKDOWN_KICK,8300);
            events.RescheduleEvent(EVENT_DEEPENING_SHADOWS,11000);
            events.RescheduleEvent(EVENT_DETONATING_MOONGLAIVE,11000);
            if(vengeancePhase)events.RescheduleEvent(EVENT_VENGEANCE,35000);
            if(creepingStarted)events.RescheduleEvent(EVENT_CREEPING_SECOND,
                FelsoriaCordana::until(getMSTime(),lastCreepingCast,FelsoriaCordana::CreepingCooldown));
        }
        bool FirstCreepingReady(uint32 now) const
        {
            return firstWavesIssued && !pendingFirstWaves && creepingSummons.empty() &&
                FelsoriaCordana::elapsed(now,firstCreepingStarted,FelsoriaCordana::FirstPhaseMinimum);
        }
        void FinishFirstCreeping()
        {
            if(!firstCreepingActive)return;
            firstCreepingActive=false;pendingFirstWaves=0;
            me->RemoveAurasDueToSpell(SPELL_CREEPING_DOOM_SHIELD);SetFlyMode(false);
            me->GetMotionMaster()->MovePoint(0,centrPos);me->SetReactState(REACT_DEFENSIVE);
            if(encounterActive){ScheduleEvents();QueueVengeance();}
        }
        void CheckAchievementLight()
        {
            if(!firstCreepingActive || !achievement || !IsMythic())return;
            for(auto const& ref:me->GetMap()->GetPlayers())if(Player* player=ref.getSource())
                if(player->IsInWorld() && player->isAlive() && !player->isGameMaster() &&
                   me->InSamePhase(player) && player->HasAura(SPELL_ELUNES_LIGHT_OVERRIDE)){
                    achievement=false;return;
                }
        }
        void UpdateAI(uint32 diff) override
        {
            for(uint32& cooldown:dialogueCooldown)cooldown=cooldown>diff?cooldown-diff:0;
            killSpeechCooldown=killSpeechCooldown>diff?killSpeechCooldown-diff:0;
            if(!encounterActive || !me->isAlive())return;
            uint32 now=getMSTime();
            if(!me->isInCombat()){EnterEvadeMode();return;}
            if(shadowstepActive){
                if(FelsoriaWardensLight::IsRevealed(me) || FelsoriaCordana::elapsed(now,shadowStarted,60000) ||
                   (!me->HasAura(SPELL_FEL_GLAIVE) && FelsoriaCordana::elapsed(now,shadowStarted,3000)))FinishShadowstep();
            }
            if(firstCreepingActive){
                if(lightdetect<=diff){lightdetect=100;CheckAchievementLight();}else lightdetect-=diff;
                if(!firstWavesIssued && FelsoriaCordana::elapsed(now,firstCreepingStarted,4000)){
                    firstWavesIssued=true;SummonCreepings(1);
                }
                if(FirstCreepingReady(now))FinishFirstCreeping();
                else if(FelsoriaCordana::elapsed(now,firstCreepingStarted,FelsoriaCordana::FirstPhaseFailsafe)){
                    pendingFirstWaves=0;summons.DespawnEntry(100336);creepingSummons.clear();FinishFirstCreeping();
                    TC_LOG_ERROR(LOG_FILTER_GENERAL,"FELSORIA_CORDANA_V2 first_phase_timeout entry=%u",me->GetEntry());
                }
            }
            if(!shadowstepActive && !firstCreepingActive && !UpdateVictim())return;
            events.Update(diff);
            if(CheckHomeDistToEvade(diff,38.0f,4049.90f,-297.42f,-281.59f))return;
            if(me->HasUnitState(UNIT_STATE_CASTING))return;
            if(uint32 eventId=events.ExecuteEvent())switch(eventId){
                case EVENT_KNOCKDOWN_KICK:
                    knockdownReactionPending=true;DoCastVictim(SPELL_KNOCKDOWN_KICK);
                    events.RescheduleEvent(EVENT_KNOCKDOWN_KICK,16000);break;
                case EVENT_KNOCKDOWN_KICK_TRIGGER:
                    if(Unit* victim=me->getVictim())DoCast(victim,SPELL_TURN_KICK);
                    break;
                case EVENT_DEEPENING_SHADOWS:
                    DoCast(GetDifficultyID()==DIFFICULTY_NORMAL?SPELL_DEEPENING_SHADOWS:SPELL_DEEPENING_SHADOWS_RAND);
                    events.RescheduleEvent(EVENT_DEEPENING_SHADOWS,30500);break;
                case EVENT_DETONATING_MOONGLAIVE:
                    DoCast(SPELL_DETONATING_MOONGLAIVE);events.RescheduleEvent(EVENT_DETONATING_MOONGLAIVE,9000);break;
                case EVENT_SHADOWSTEP:
                    shadowstepActive=true;glaiveAnnounced=false;shadowStarted=now;
                    TalkGuarded(SAY_SHADOWSTEP);TalkGuarded(SAY_SHADOWSTEP_EMOTE);
                    me->StopAttack();me->SetReactState(REACT_PASSIVE);events.Reset();
                    DoCast(me,SPELL_SHADOWSTEP,true);DoCast(me,SPELL_SHADOWSTEP_2,true);
                    DoCast(me,SPELL_FEL_GLAIVE,true);StartMove(urand(0,2));break;
                case EVENT_VENGEANCE:
                    me->CastSpell(vengeance_position[urand(0,3)],SPELL_VENGEANCE,false);
                    events.RescheduleEvent(EVENT_VENGEANCE,35000);break;
                case EVENT_CREEPING_FIRST:
                    if(GetDifficultyID()==DIFFICULTY_NORMAL)break;
                    // A threshold reached by DoTs during Shadowstep owns movement now.
                    shadowstepActive=false;knockdownReactionPending=false;
                    me->RemoveAurasDueToSpell(SPELL_FEL_GLAIVE);
                    me->RemoveAurasDueToSpell(SPELL_INVISE_BY_SHADOWSTEP);
                    me->RemoveAurasDueToSpell(SPELL_INVISE_BY_SHADOWSTEP_1);
                    me->GetMotionMaster()->Clear();me->SetWalk(false);
                    firstCreepingPending=false;firstCreepingActive=true;firstWavesIssued=false;
                    firstCreepingStarted=lastCreepingCast=now;creepingStarted=true;
                    me->StopAttack();me->SetReactState(REACT_PASSIVE);events.Reset();
                    DoCast(me,SPELL_CREEPING_DOOM_SHIELD,true);CheckAchievementLight();break;
                case EVENT_CREEPING_SECOND:
                    if(GetDifficultyID()==DIFFICULTY_NORMAL)break;
                    lastCreepingCast=now;DoCast(SPELL_CREEPING_DOOM_SECOND);
                    events.RescheduleEvent(EVENT_CREEPING_SECOND,FelsoriaCordana::CreepingCooldown);break;
            }
            if(!shadowstepActive && !firstCreepingActive)DoMeleeAttackIfReady();
        }
        void StartMove(uint8 point)
        {
            if(!shadowstepActive || !encounterActive || !me->isInCombat())return;
            Position from=paths[point].first,to=paths[point].second;
            if(urand(0,1))std::swap(from,to); // Never mutate the shared path table across instances.
            me->SetWalk(true);me->NearTeleportTo(from);
            uint32 serial=encounterSerial;
            me->AddDelayedEvent(100,[this,to,serial](){
                if(shadowstepActive && encounterActive && serial==encounterSerial && me->isInCombat())
                    me->GetMotionMaster()->MovePoint(2,to);
            });
        }
        void SummonCreepings(uint8 wave)
        {
            if(!encounterActive || !me->isAlive())return;
            auto plan=FelsoriaCordana::waves(GetDifficultyID()==DIFFICULTY_NORMAL,IsMythic(),wave==2);
            if(!plan.count)return;
            TalkGuarded(SAY_EMOTE_CREEPING);
            if(wave==1)pendingFirstWaves=plan.count;
            unsigned side=urand(0,3);uint32 serial=encounterSerial;
            for(unsigned i=0;i<plan.count;++i){
                unsigned wallSide=side,gap=urand(6,18);
                me->AddDelayedEvent(i*plan.interval+1,[this,wallSide,gap,serial,wave](){
                    if(!encounterActive || serial!=encounterSerial || !me->isAlive() ||
                       (wave==1 && !firstCreepingActive))return;
                    for(auto const& p:FelsoriaCordana::wall(wallSide,gap))
                        me->SummonCreature(100336,p.x,p.y,p.z,p.yaw,TEMPSUMMON_TIMED_DESPAWN,18000);
                    if(wave==1 && pendingFirstWaves)--pendingFirstWaves;
                });
                side=FelsoriaCordana::nextWall(side,urand(0,1)!=0);
            }
        }
    };
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new boss_cordanaAI (creature);
    }
};

//100525
class npc_cordana_glowing_sentry : public CreatureScript
{
public:
    npc_cordana_glowing_sentry() : CreatureScript("npc_cordana_glowing_sentry") { }

    struct npc_cordana_glowing_sentryAI : public ScriptedAI
    {
        npc_cordana_glowing_sentryAI(Creature* creature) : ScriptedAI(creature) {}

        void Reset() override
        {
            me->SetFlag(UNIT_FIELD_NPC_FLAGS, UNIT_NPC_FLAG_SPELLCLICK);
            InstanceScript* instance = me->GetInstanceScript();
            if (me->GetDistance(4029.81f, -297.39f, -281.31f) > 80.0f ||
                !instance || instance->GetBossState(DATA_CORDANA) != IN_PROGRESS)
            {
                DoCast(me, SPELL_ELUNES_LIGHT_VIS_1, true);
                DoCast(me, SPELL_ELUNES_LIGHT_VIS_2, true);
            }
        }

        void IsSummonedBy(Unit* summoner) override
        {
            if (me->GetDistance(summoner) > 80.0f)
            {
                DoCast(me, SPELL_ELUNES_LIGHT_VIS_1, true);
                DoCast(me, SPELL_ELUNES_LIGHT_VIS_2, true);
            }
        }
        
        void SpellHit(Unit* caster, SpellInfo const* spell) override
        {                           
            if (spell->Id == SPELL_ELUNES_LIGHT)
            {
                DoCast(me, SPELL_ELUNES_LIGHT_VIS_1, true);
                DoCast(me, SPELL_ELUNES_LIGHT_VIS_2, true);
                
                 //! hack =C
                caster->RemoveAurasDueToSpell(SPELL_ELUNES_LIGHT_VIS_1);
                caster->RemoveAurasDueToSpell(SPELL_ELUNES_LIGHT_VIS_2);
            }
        }
        
        void OnSpellClick(Unit* clicker) override
        {
            if (!clicker || !clicker->IsPlayer() || !clicker->isAlive() ||
                clicker->HasAura(SPELL_ELUNES_LIGHT_OVERRIDE))
                return;
            if (me->HasAura(SPELL_ELUNES_LIGHT_VIS_1) && me->HasAura(SPELL_ELUNES_LIGHT_VIS_2))
            {
                DoCast(clicker, SPELL_ELUNES_LIGHT_CLICK, true);
                clicker->CastSpell(clicker, SPELL_ELUNES_LIGHT_OVERRIDE, true);
                // Keep the light available if its carrier aura could not be applied.
                if (!clicker->HasAura(SPELL_ELUNES_LIGHT_OVERRIDE))
                    return;
                me->RemoveAurasDueToSpell(SPELL_ELUNES_LIGHT_VIS_1);
                me->RemoveAurasDueToSpell(SPELL_ELUNES_LIGHT_VIS_2);
            }
        }

        void UpdateAI(uint32 diff) override {}
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_cordana_glowing_sentryAI(creature);
    }
};

//100364
class npc_cordana_spirit_of_vengeance : public CreatureScript
{
public:
    npc_cordana_spirit_of_vengeance() : CreatureScript("npc_cordana_spirit_of_vengeance") {}

    struct npc_cordana_spirit_of_vengeanceAI : public ScriptedAI
    {
        npc_cordana_spirit_of_vengeanceAI(Creature* creature) : ScriptedAI(creature)
        {
            instance = me->GetInstanceScript();
            doorSpirit = me->GetDistance(4234.50f, -297.59f, -281.39f) < 5.0f;
        }

        InstanceScript* instance;
        EventMap events;
        bool doorSpirit = false;

        void Reset()
        {
            if (doorSpirit)
                instance->SetBossState(DATA_CORDANA_SPIRIT, NOT_STARTED);
            events.Reset();
            FelsoriaWardensLight::UpdateShadow(me);
        }

        void JustDied(Unit* /*killer*/) override
        {
            if (doorSpirit)
                instance->SetBossState(DATA_CORDANA_SPIRIT, DONE);
        }

        void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*type*/) override
        {
            if (!FelsoriaWardensLight::IsRevealed(me))
                damage = 0;
        }

        void UpdateAI(uint32 diff) override
        {
            FelsoriaWardensLight::UpdateShadow(me);
            if (!UpdateVictim())
                return;

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_1:
                        break;
                }
            }
            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_cordana_spirit_of_vengeanceAI(creature);
    }
};

// 100336
class npc_cordana_creeping : public CreatureScript
{
public:
    npc_cordana_creeping() : CreatureScript("npc_cordana_creeping") {}

    struct npc_cordana_creepingAI : public ScriptedAI
    {
        npc_cordana_creepingAI(Creature* creature) : ScriptedAI(creature) {}


        void Reset() override
        {
            me->KillAllDelayedEvents();
            me->RemoveAllAreaObjects();me->RemoveAurasDueToSpell(197415);
            me->SetReactState(REACT_PASSIVE);me->SetWalk(false);
            DoCast(me, 213628, true);
            me->AddDelayedEvent(500, [this]()
            {
                Unit* owner=me->GetAnyOwner();
                if (!owner || !owner->isAlive() || !owner->isInCombat()) { me->DespawnOrUnsummon();return; }
                // A wall is a straight moving hazard; navmesh must not divert each ghost around players.
                float yaw=me->GetOrientation();
                me->GetMotionMaster()->MovePoint(1,
                    me->GetPositionX()+std::cos(yaw)*FelsoriaCordana::GhostTravel,
                    me->GetPositionY()+std::sin(yaw)*FelsoriaCordana::GhostTravel,
                    me->GetPositionZ(),false,FelsoriaCordana::GhostSpeed);
            });
            me->AddDelayedEvent(600, [this]()
            {
                Unit* owner=me->GetAnyOwner();
                if (owner && owner->isAlive() && owner->isInCombat()) DoCast(me, 197415, true);
            });
        }

        void SpellHitTarget(Unit* target, const SpellInfo* spell) override
        {
            if (target && target->IsPlayer() && (spell->Id == 197506 || spell->Id == SPELL_CREEPING_DOOM_DMG))
            {
                if (auto owner = me->GetAnyOwner())
                    if (owner && owner->IsAIEnabled)
                        if (auto cordana = owner->ToCreature())
                            cordana->AI()->DoAction(ACTION_1);
            }
        }
        
         void MovementInform(uint32 type, uint32 id) override
        {
            if (type != POINT_MOTION_TYPE || id != 1)
                return;
            
            me->DespawnOrUnsummon();
        }

        void UpdateAI(uint32 /*diff*/) {}
    };

    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_cordana_creepingAI(creature);
    }
};

// custom ENTRY 11568
class areatrigger_cordana_deepening_shadows : public AreaTriggerScript
{
    public:
        areatrigger_cordana_deepening_shadows() : AreaTriggerScript("areatrigger_cordana_deepening_shadows") { }

    struct areatrigger_cordana_deepening_shadowsAI : AreaTriggerAI
    {
        areatrigger_cordana_deepening_shadowsAI(AreaTrigger* areatrigger) : AreaTriggerAI(areatrigger) {}
        
        void ActionOnUpdate(GuidList& affectedPlayers) override
        {
            bool can_gain = true;
            for (GuidList::iterator itr = affectedPlayers.begin(), next; itr != affectedPlayers.end(); ++itr)
            {
                Unit* unit = ObjectAccessor::GetUnit(*at, *itr);
                if (!unit)
                    continue;
                
                if (unit->HasAura(SPELL_ELUNES_LIGHT_OVERRIDE))
                {
                    can_gain = false;
                    break;
                }
            }
            if (at->GetRadius() <= 50.0f || !can_gain)
                at->SetSphereScale(can_gain ? 0.006f : -0.025, 100);
            
            if (at->GetRadius() <= 1.7f)
                at->Despawn();
        }

    };

    AreaTriggerAI* GetAI(AreaTrigger* areatrigger) const override
    {
        return new areatrigger_cordana_deepening_shadowsAI(areatrigger);
    }
};

//1755
class eventobject_votw_spirit_trig : public EventObjectScript
{
public:
    eventobject_votw_spirit_trig() : EventObjectScript("eventobject_votw_spirit_trig") {}

    bool IsTriggerMeets(Player* player, EventObject* eo) override
    {
        bool meeted = player->FindNearestCreature(108996, 200.f);
        if ((player->HasAura(204485)) && !meeted)
        {
            player->SummonCreature(108996, 4398.75f, -532.89f, 126.f, 2.38f, TEMPSUMMON_TIMED_DESPAWN, 300000);
            return true;
        }

        return false;
    }
};


void AddSC_boss_cordana()
{
    new npc_felsoria_wardens_shadow_trash();
    new npc_felsoria_wardens_dropped_light();
    new boss_cordana();
    new npc_cordana_glowing_sentry();
    new npc_cordana_spirit_of_vengeance();
    new npc_cordana_creeping();
    new areatrigger_cordana_deepening_shadows();
    new eventobject_votw_spirit_trig();
}