/*
    Dungeon : Court of Stars 110
    Encounter: Patrol Captain Gerdo
    Mythic: 100%
*/

#include "Map.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "WaypointManager.h"
#include "Log.h"
#include <algorithm>
#include <cmath>
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "court_of_stars.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include <iterator>
#include <list>

enum Says
{
    SAY_AGGRO           = 0,
    SAY_ARCANE_LOCKDOWN = 1,
    SAY_SIGNAL          = 2,
    SAY_FLASK           = 3,
    SAY_DEATH           = 4,
};

enum Spells
{
    SPELL_RESONANT_SLASH            = 207261,
    SPELL_RESONANT_SLASH_FRONT      = 206574,
    SPELL_RESONANT_SLASH_BACK       = 206580,
    SPELL_ARCANE_LOCKDOWN           = 207278,
    SPELL_SIGNAL_BEACON             = 207806,
    SPELL_SUM_VIGILANT_NIGHTWATCH   = 210435,
    SPELL_FLASK_SOLEMN_NIGHT        = 207815,
    SPELL_STREETSWEEPER             = 219488,

    //Guards
    SPELL_HINDER                    = 215204,

    //Streetsweeper
    SPELL_STREETSWEEPER_VISUAL      = 219475,
    SPELL_STREETSWEEPER_VISUAL_BEAM = 219483,
    SPELL_STREETSWEEPER_AT          = 219477,

    //Beacon
    SPELL_WATCHPOST_VICINITY        = 212479, //?
    SPELL_WATCHPOST_VICINITY_AT     = 212480, //?
    SPELL_CAST_DISABLE_BEACON       = 210253,
    SPELL_BEACON_DISABLED           = 210257,
};

enum eEvents
{
    EVENT_RESONANT_SLASH            = 1,
    EVENT_ARCANE_LOCKDOWN           = 2,
    EVENT_SIGNAL_BEACON             = 3,
    EVENT_FLASK_SOLEMN_NIGHT        = 4,
    EVENT_FLASK_SOLEMN_NIGHT_END    = 5,
    EVENT_STREETSWEEPER             = 6
};

Position const flaskPos = {1308.29f, 3537.42f, 20.76f};

//104215
class boss_patrol_captain_gerdo : public CreatureScript
{
public:
    boss_patrol_captain_gerdo() : CreatureScript("boss_patrol_captain_gerdo") {}

    struct boss_patrol_captain_gerdoAI : public BossAI
    {
        boss_patrol_captain_gerdoAI(Creature* creature) : BossAI(creature, DATA_CAPTAIN_GERDO) {}

        bool flaskEvent = false;
        bool beaconEvent = false;
        bool flaskMoving = false;
        bool poisonedFlaskKill = false;
        EventMap flaskEvents;

        void ResumeAfterFlask()
        {
            flaskMoving = false;
            flaskEvents.Reset();
            me->SetReactState(REACT_AGGRESSIVE);
        }

        void Reset() override
        {
            _Reset();
            flaskEvent = false;
            beaconEvent = false;
            poisonedFlaskKill = false;
            flaskMoving = false;
            flaskEvents.Reset();
            me->SetReactState(REACT_AGGRESSIVE);
        }

        void EnterCombat(Unit* /*who*/) override
        //06:03
        {
            Talk(SAY_AGGRO);
            _EnterCombat();

            events.RescheduleEvent(EVENT_RESONANT_SLASH, 7000);   //06:10, 06:32, 06:44
            events.RescheduleEvent(EVENT_ARCANE_LOCKDOWN, 15000); //06:18, 06:52, 07:20
            events.RescheduleEvent(EVENT_STREETSWEEPER, 18000);
        }

        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DEATH);
            _JustDied();
            if (Creature* sayer = me->FindNearestCreature(104694, 250.0f, true))
                sayer->AI()->Talk(0);
        }

        void SpellFinishCast(SpellInfo const* spell) override
        {
            if (spell->Id == SPELL_SIGNAL_BEACON && instance)
                instance->SetData(DATA_BEACON_ACTIVATE, IN_PROGRESS);
            else if (spell->Id == SPELL_FLASK_SOLEMN_NIGHT)
            {
                if (flaskMoving && me->isInCombat() && instance && instance->GetData(DATA_FLASK_POISONED))
                    poisonedFlaskKill = true;
                else
                    ResumeAfterFlask();
            }
        }

        void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType dmgType) override
        {
            if (damage >= me->GetHealth())
                return;
            if (!beaconEvent && me->HealthBelowPctDamaged(75, damage))
            {
                beaconEvent = true;
                events.RescheduleEvent(EVENT_SIGNAL_BEACON, 1);
            }
            if (!flaskEvent && me->HealthBelowPctDamaged(25, damage))
            {
                flaskEvent = true;
                Talk(SAY_FLASK);
                events.RescheduleEvent(EVENT_FLASK_SOLEMN_NIGHT, 500);
            }
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type != POINT_MOTION_TYPE)
                return;

            if (id == 1 && flaskMoving)
                flaskEvents.RescheduleEvent(EVENT_FLASK_SOLEMN_NIGHT_END, 500);
        }

        void UpdateAI(uint32 diff) override
        {
            if (poisonedFlaskKill)
            {
                poisonedFlaskKill = false;
                flaskMoving = false;
                flaskEvents.Reset();
                if (me->isAlive() && me->isInCombat())
                    me->Kill(me);
                return;
            }
            if (flaskMoving)
            {
                if (!me->isInCombat())
                {
                    ResumeAfterFlask();
                    return;
                }
                flaskEvents.Update(diff);
                if (uint32 flaskId = flaskEvents.ExecuteEvent())
                {
                    if (flaskId == EVENT_FLASK_SOLEMN_NIGHT_END)
                    {
                        DoCast(me, SPELL_FLASK_SOLEMN_NIGHT);
                        flaskEvents.RescheduleEvent(EVENT_FLASK_SOLEMN_NIGHT, 4000);
                    }
                    else
                        ResumeAfterFlask();
                }
                return;
            }
            if (!UpdateVictim())
                return;

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_RESONANT_SLASH:
                    {
                        DoCast(SPELL_RESONANT_SLASH);
                        float angle = 0.0f;
                        Position pos;
                        me->GetNearPosition(pos, 4.0f, angle);
                        if (Creature* frontTrig = me->SummonCreature(NPC_GERDO_TRIG_FRONT, pos))
                        {
                            angle += 3.14f;
                            frontTrig->CastSpell(frontTrig, SPELL_RESONANT_SLASH_FRONT);
                        }
                        me->GetNearPosition(pos, 4.0f, angle);
                        if (Creature* backTrig = me->SummonCreature(NPC_GERDO_TRIG_BACK, pos))
                            backTrig->CastSpell(backTrig, SPELL_RESONANT_SLASH_BACK);
                        events.RescheduleEvent(EVENT_RESONANT_SLASH, 12000);
                        break;
                    }
                    case EVENT_ARCANE_LOCKDOWN:
                        DoCast(SPELL_ARCANE_LOCKDOWN);
                        Talk(SAY_ARCANE_LOCKDOWN);
                        events.RescheduleEvent(EVENT_ARCANE_LOCKDOWN, 28000);
                        break;
                    case EVENT_SIGNAL_BEACON:
                        DoCast(SPELL_SIGNAL_BEACON);
                        Talk(SAY_SIGNAL);
                        break;
                    case EVENT_FLASK_SOLEMN_NIGHT:
                        flaskMoving = true;
                        me->StopAttack(false, true);
                        me->GetMotionMaster()->MovePoint(1, flaskPos);
                        flaskEvents.RescheduleEvent(EVENT_FLASK_SOLEMN_NIGHT, 15000);
                        return;
                    case EVENT_STREETSWEEPER:
                        DoCast(SPELL_STREETSWEEPER);
                        events.RescheduleEvent(EVENT_STREETSWEEPER, 18000);
                        break;
                }
            }
            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new boss_patrol_captain_gerdoAI (creature);
    }
};

//104918
class npc_captain_gerdo_vigilant_duskwatch : public CreatureScript
{
public:
    npc_captain_gerdo_vigilant_duskwatch() : CreatureScript("npc_captain_gerdo_vigilant_duskwatch") {}

    struct npc_captain_gerdo_vigilant_duskwatchAI : public ScriptedAI
    {
        npc_captain_gerdo_vigilant_duskwatchAI(Creature* creature) : ScriptedAI(creature) {}

        EventMap events;

        void Reset() override
        {
            events.Reset();
        }

        void IsSummonedBy(Unit* summoner) override
        {
            DoZoneInCombat(me, 100.0f);
            events.RescheduleEvent(EVENT_1, 5000);
        }

        void UpdateAI(uint32 diff) override
        {
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
                        DoCastVictim(SPELL_HINDER);
                        events.RescheduleEvent(EVENT_1, 10000);
                        break;
                    default:
                        break;
                }
            }
            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_captain_gerdo_vigilant_duskwatchAI(creature);
    }
};

//105729
class npc_signal_lantern : public CreatureScript
{
public:
    npc_signal_lantern() : CreatureScript("npc_signal_lantern") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!player->HasAura(210295))
        {
            player->ADD_GOSSIP_ITEM_DB(19515, 0, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+1);
            player->SEND_GOSSIP_MENU(28836, creature->GetGUID());
        }
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        if (action != GOSSIP_ACTION_INFO_DEF+1 || player->HasAura(210295) || !player->isAlive())
            return false;
        player->AddAura(210295, player);
        player->CastSpell(player, 209524);    //213505
        player->CLOSE_GOSSIP_MENU();

        return true;
    }
};

//
class npc_nightborne_boat : public CreatureScript
{
public:
    npc_nightborne_boat() : CreatureScript("npc_nightborne_boat") {}

    struct npc_nightborne_boatAI : public ScriptedAI
    {
        npc_nightborne_boatAI(Creature* creature) : ScriptedAI(creature) {}

        enum Phase { IDLE, APPROACH, BOARDING, SAILING, FINISHED };
        Phase phase = IDLE;
        uint32 timer = 0;

        Unit* GetOwner() const
        {
            TempSummon* summon = me->ToTempSummon();
            return summon ? summon->GetSummoner() : nullptr;
        }

        void Cancel()
        {
            phase = FINISHED;
            timer = 0;
            me->StopMoving();
            if (Unit* owner = GetOwner())
                owner->RemoveAurasDueToSpell(210295); // Permit another lantern attempt.
            me->DespawnOrUnsummon(1000);
        }

        bool SurfaceZ(float x, float y, float referenceZ, float& surfaceZ) const
        {
            LiquidData liquid = {};
            ZLiquidStatus status = me->GetMap()->getLiquidStatus(
                x, y, referenceZ, MAP_ALL_LIQUIDS, &liquid);
            if (status == LIQUID_MAP_NO_WATER || !std::isfinite(liquid.level))
                return false;
            surfaceZ = liquid.level;
            return true;
        }

        // Keep the DB route's XY vertices. Sample liquid height along every leg;
        // never ask the walking/swimming navmesh to route this surface vessel.
        bool StartSurfacePath(uint32 pathId, uint32 travelTime, bool alignStart)
        {
            WaypointPath const* waypoints = sWaypointMgr->GetPathScript(pathId);
            if (!waypoints || waypoints->empty() || !travelTime)
                return false;

            Movement::PointsArray points;
            float z = 0.0f;
            if (!SurfaceZ(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), z))
                return false;
            points.push_back(G3D::Vector3(me->GetPositionX(), me->GetPositionY(), z));

            for (WaypointData const* node : *waypoints)
            {
                if (!node || !std::isfinite(node->x) || !std::isfinite(node->y) ||
                    !std::isfinite(node->z) || node->delay || node->event_id)
                    return false; // Do not silently discard scripted waypoint actions.
                G3D::Vector3 start = points.back();
                float dx = node->x - start.x, dy = node->y - start.y;
                float length = std::sqrt(dx * dx + dy * dy);
                if (!std::isfinite(length) || length > 200.0f || points.size() > 1024)
                    return false;
                if (length < 0.001f)
                    continue;
                uint32 steps = std::max<uint32>(1, uint32(std::ceil(length / 4.0f)));
                for (uint32 i = 1; i <= steps; ++i)
                {
                    float x = start.x + dx * float(i) / float(steps);
                    float y = start.y + dy * float(i) / float(steps);
                    if (!SurfaceZ(x, y, node->z, z))
                        return false;
                    points.push_back(G3D::Vector3(x, y, z));
                }
            }

            float distance = 0.0f;
            for (size_t i = 1; i < points.size(); ++i)
            {
                float dx = points[i].x - points[i - 1].x;
                float dy = points[i].y - points[i - 1].y;
                float dz = points[i].z - points[i - 1].z;
                distance += std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            if (!std::isfinite(distance) || distance < 0.1f)
                return false;

            // The existing 13 s / 48 s timings now describe actual travel time.
            // A boat floats at liquid.level; no guessed global Z offset is used.
            me->SetCanFly(true);
            me->SetDisableGravity(true);
            me->SetSwim(false);
            if (alignStart)
                me->NearTeleportTo(points.front().x, points.front().y,
                                   points.front().z, me->GetOrientation());
            Movement::MoveSplineInit init(*me);
            init.MovebyPath(points);
            init.SetFly();
            init.SetWalk(false);
            init.SetVelocity(distance * 1000.0f / float(travelTime));
            int32 duration = init.Launch();
            if (duration <= 0)
                return false;
            timer = uint32(duration) + 15000; // Bounded recovery if movement fails.
            return true;
        }

        void IsSummonedBy(Unit* summoner) override
        {
            if (!summoner || summoner->GetTypeId() != TYPEID_PLAYER ||
                !StartSurfacePath(9100402, 13000, true))
            {
                TC_LOG_ERROR(LOG_FILTER_SQL, "Felsoria CoS boat: cannot start surface route 9100402; check script waypoints and liquid data.");
                Cancel();
                return;
            }
            phase = APPROACH;
        }

        void PassengerBoarded(Unit* passenger, int8 /*seat*/, bool apply) override
        {
            if (!passenger || passenger->GetTypeId() != TYPEID_PLAYER || passenger != GetOwner())
                return;
            if (!apply)
            {
                if (phase == SAILING)
                    Cancel();
                return;
            }
            if (phase != BOARDING)
                return;
            if (!StartSurfacePath(9100403, 48000, false))
            {
                TC_LOG_ERROR(LOG_FILTER_SQL, "Felsoria CoS boat: cannot start surface route 9100403; check script waypoints and liquid data.");
                Cancel();
                return;
            }
            phase = SAILING;
        }

        void UpdateAI(uint32 diff) override
        {
            if (phase == IDLE || phase == FINISHED)
                return;
            Unit* owner = GetOwner();
            if (!owner || !owner->isAlive())
            {
                Cancel();
                return;
            }
            if ((phase == APPROACH || phase == SAILING) && me->movespline->Finalized())
            {
                if (phase == APPROACH)
                {
                    phase = BOARDING; // Set before CastSpell can call PassengerBoarded.
                    timer = 10000;
                    owner->CastSpell(me, 213507, true);
                }
                else
                {
                    phase = FINISHED; // Dismount callbacks must not cancel arrival.
                    timer = 0;
                    owner->CastSpell(owner, 208703, true);
                    me->DespawnOrUnsummon(3000);
                }
                return;
            }
            if (timer <= diff)
                Cancel();
            else
                timer -= diff;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_nightborne_boatAI(creature);
    }
};

//
class npc_lilet_lunarh : public CreatureScript
{
public:
    npc_lilet_lunarh() : CreatureScript("npc_lilet_lunarh") {}

    struct npc_lilet_lunarhAI : public ScriptedAI
    {
        npc_lilet_lunarhAI(Creature* creature) : ScriptedAI(creature) {}
        
        uint32 timer = 0;
        uint8 talk = 0;
        bool event = false;
        ObjectGuid ownerGUID;
        
        void IsSummonedBy(Unit* summoner) override
        {
            if (!summoner || summoner->GetTypeId() != TYPEID_PLAYER)
                return;
            ownerGUID = summoner->GetGUID();
            timer = 13000;
            event = true;
            talk = 0;
        }
        
        void UpdateAI(uint32 diff) override
        {
            if (event)
                if (talk < 4)
                    if (timer <= diff)
                    {
                        if (Player* owner = ObjectAccessor::GetPlayer(*me, ownerGUID))
                            Talk(talk, owner->GetGUID());
                        else
                        {
                            event = false;
                            me->DespawnOrUnsummon(1000);
                            return;
                        }
                        talk++;
                        timer = 16000;
                        if (talk == 4)
                            event = false;
                    } else timer -= diff;
        }
        
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_lilet_lunarhAI(creature);
    }
};

//104251
class npc_duskwatch_sentry : public CreatureScript
{
public:
    npc_duskwatch_sentry() : CreatureScript("npc_duskwatch_sentry") {}

    struct npc_duskwatch_sentryAI : public ScriptedAI
    {
        npc_duskwatch_sentryAI(Creature* creature) : ScriptedAI(creature) {}

        uint32 alarmTimer = 0;
        ObjectGuid alarmBeaconGUID;

        void Reset() override
        {
            alarmTimer = 0;
            alarmBeaconGUID.Clear();
            me->SetReactState(REACT_AGGRESSIVE);
        }

        void EnterCombat(Unit* who) override
        {
            DoCast(who, 209036);
            Talk(0);
            std::list<Creature*> beacons;
            GetCreatureListWithEntryInGrid(beacons, me, NPC_GERDO_ARCANE_BEACON, 100.0f);
            Creature* target = nullptr;
            for (Creature* beacon : beacons)
                if (!beacon->HasAura(SPELL_BEACON_DISABLED) &&
                    (!target || me->GetDistance(beacon) < me->GetDistance(target)))
                    target = beacon;
            if (target)
            {
                me->StopAttack();
                alarmBeaconGUID = target->GetGUID();
                alarmTimer = 15000;
                me->GetMotionMaster()->MovePoint(1, target->GetPosition());
            }
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type == POINT_MOTION_TYPE && id == 1 && alarmTimer)
            {
                Creature* beacon = ObjectAccessor::GetCreature(*me, alarmBeaconGUID);
                if (!beacon || beacon->HasAura(SPELL_BEACON_DISABLED))
                {
                    alarmTimer = 0;
                    me->SetReactState(REACT_AGGRESSIVE);
                    return;
                }
                alarmTimer = 4000;
                DoCast(me, 210261);
            }
        }

        void SpellFinishCast(SpellInfo const* spell) override
        {
            if (spell->Id == 210261)
            {
                alarmTimer = 0;
                me->SetReactState(REACT_AGGRESSIVE);
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (alarmTimer)
            {
                if (alarmTimer > diff)
                    alarmTimer -= diff;
                else
                {
                    alarmTimer = 0;
                    me->SetReactState(REACT_AGGRESSIVE);
                }
            }
            if (UpdateVictim() && !alarmTimer)
                DoMeleeAttackIfReady();
        }
    };
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_duskwatch_sentryAI(creature);
    }
};

//104245
class npc_gerdo_arcane_beacon : public CreatureScript
{
public:
    npc_gerdo_arcane_beacon() : CreatureScript("npc_gerdo_arcane_beacon") {}
    
    struct npc_gerdo_arcane_beaconAI : public ScriptedAI
    {
        npc_gerdo_arcane_beaconAI(Creature* creature) : ScriptedAI(creature)
        {
            me->SetReactState(REACT_PASSIVE);
            me->SetFlag(UNIT_FIELD_NPC_FLAGS, UNIT_NPC_FLAG_SPELLCLICK);
        }

        void Reset() override {}

        void OnSpellClick(Unit* clicker) override
        {
            if (clicker && clicker->IsPlayer() && clicker->isAlive() && !me->HasAura(SPELL_BEACON_DISABLED))
                clicker->CastSpell(me, SPELL_CAST_DISABLE_BEACON);
        }

        void SpellHit(Unit* target, const SpellInfo* spell) override
        {
            if (spell->Id == SPELL_CAST_DISABLE_BEACON)
            {
                me->RemoveFlag(UNIT_FIELD_NPC_FLAGS, UNIT_NPC_FLAG_SPELLCLICK);
                DoCast(me, SPELL_BEACON_DISABLED, true);
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_gerdo_arcane_beaconAI(creature);
    }
};

//110403
class npc_gerdo_streetsweeper : public CreatureScript
{
public:
    npc_gerdo_streetsweeper() : CreatureScript("npc_gerdo_streetsweeper") {}

    struct npc_gerdo_streetsweeperAI : public ScriptedAI
    {
        npc_gerdo_streetsweeperAI(Creature* creature) : ScriptedAI(creature)
        {
            me->SetReactState(REACT_PASSIVE);
        }

        EventMap events;

        void Reset() override {}

        void EnterCombat(Unit* who) override {}

        void IsSummonedBy(Unit* summoner) override
        {
            DoCast(me, SPELL_STREETSWEEPER_VISUAL, true);
            events.RescheduleEvent(EVENT_1, 500);
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);

            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_1:
                    {
                        if (Unit* owner = me->GetAnyOwner())
                            if (Creature* summoner = owner->ToCreature())
                                if (summoner->IsAIEnabled)
                                    if (Unit* target = summoner->AI()->SelectTarget(SELECT_TARGET_RANDOM, 0, 40.0f, true))
                                        me->SetFacingToObject(target);
                        events.RescheduleEvent(EVENT_2, 500);
                        break;
                    }
                    case EVENT_2:
                        DoCast(me, SPELL_STREETSWEEPER_AT, true);
                        events.RescheduleEvent(EVENT_3, 2000);
                        break;
                    case EVENT_3:
                        DoCast(me, SPELL_STREETSWEEPER_VISUAL_BEAM, true);
                        break;
                }
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_gerdo_streetsweeperAI(creature);
    }
};

//207278
class spell_patrol_arcane_lockdown : public SpellScriptLoader
{
    public:
        spell_patrol_arcane_lockdown() : SpellScriptLoader("spell_patrol_arcane_lockdown") { }

        class spell_patrol_arcane_lockdown_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_patrol_arcane_lockdown_AuraScript);

            void OnApply(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
            {
                if (!GetTarget())
                    return;

                if (Aura* aura = GetTarget()->GetAura(GetId()))
                    aura->SetStackAmount(3);
            }

            void OnProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
            {
                PreventDefaultAction();
                if (!(eventInfo.GetTypeMask() & PROC_FLAG_ON_JUMP) || !GetTarget() || !aurEff)
                    return;

                if (Aura* aura = GetTarget()->GetAura(GetId()))
                {
                    uint8 const stack = aura->GetStackAmount();

                    if (stack > 1)
                        aura->SetStackAmount(stack - 1);
                    else
                        aurEff->GetBase()->Remove();
                }
            }

            void Register() override
            {
                OnEffectApply += AuraEffectApplyFn(spell_patrol_arcane_lockdown_AuraScript::OnApply, EFFECT_1, SPELL_AURA_MOD_DECREASE_SPEED, AURA_EFFECT_HANDLE_REAL);
                OnEffectProc += AuraEffectProcFn(spell_patrol_arcane_lockdown_AuraScript::OnProc, EFFECT_1, SPELL_AURA_MOD_DECREASE_SPEED);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_patrol_arcane_lockdown_AuraScript();
        }
};

// 105117: only Rogues or characters with Alchemy may poison the flask before combat.
class npc_court_solemn_night_flask : public CreatureScript
{
public:
    npc_court_solemn_night_flask() : CreatureScript("npc_court_solemn_night_flask") { }

    bool CanPoison(Player* player, Creature* creature) const
    {
        InstanceScript* instance = creature->GetInstanceScript();
        return player && player->isAlive() && !player->isInCombat() && creature->GetMapId() == 1571 &&
            instance && instance->GetBossState(DATA_CAPTAIN_GERDO) != IN_PROGRESS &&
            instance->GetBossState(DATA_CAPTAIN_GERDO) != DONE && !instance->GetData(DATA_FLASK_POISONED) &&
            (player->getClass() == CLASS_ROGUE || player->HasSkill(SKILL_ALCHEMY));
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (CanPoison(player, creature))
            player->ADD_GOSSIP_ITEM(0, "Poison the Flask of the Solemn Night.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
        // Generic gossip text is already used by menu 0 in this database.
        player->SEND_GOSSIP_MENU(0xFFFFFF, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        player->CLOSE_GOSSIP_MENU();
        if (sender != GOSSIP_SENDER_MAIN || action != GOSSIP_ACTION_INFO_DEF + 1 || !CanPoison(player, creature))
            return false;
        creature->GetInstanceScript()->SetData(DATA_FLASK_POISONED, 1);
        return true;
    }

    struct flaskAI : public ScriptedAI
    {
        explicit flaskAI(Creature* creature) : ScriptedAI(creature) { }
        void Reset() override
        {
            me->SetReactState(REACT_PASSIVE);
            me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
            me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            if (!me->HasAura(210385))
                DoCast(me, 210385, true);
        }
        void UpdateAI(uint32 /*diff*/) override { }
    };

    CreatureAI* GetAI(Creature* creature) const override { return new flaskAI(creature); }
};

void AddSC_boss_patrol_captain_gerdo()
{
    new boss_patrol_captain_gerdo();
    new npc_captain_gerdo_vigilant_duskwatch();
    new npc_signal_lantern();
    new npc_nightborne_boat();
    new npc_lilet_lunarh();
    new npc_duskwatch_sentry();
    new npc_gerdo_arcane_beacon();
    new npc_gerdo_streetsweeper();
    new spell_patrol_arcane_lockdown();
    new npc_court_solemn_night_flask();
}
