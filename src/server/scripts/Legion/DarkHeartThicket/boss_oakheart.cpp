/*
    Dungeon : Dark Heart Thicket 100-110
    Encounter: Oakheart
    Normal: 100%, Heroic: 100%, Mythic: 100%
*/

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "darkheart_thicket.h"

enum Says
{
    SAY_AGGRO               = 0,
    SAY_NIGHTMARE           = 1,
    SAY_CRUSHING            = 2,
    SAY_ROOTS               = 3,
    SAY_EARTH               = 4,
    SAY_DEATH               = 5,
};

enum Spells
{
    SPELL_CANCEL_DEEP_ROOTS     = 165953, //Remove 165950
    SPELL_SHATTERED_EARTH       = 204666,
    SPELL_STRANGLING_ROOTS      = 204574,
    SPELL_STRANGLING_ROOTS_AT   = 199067,
    SPELL_NIGHTMARE_BREATH      = 204667,
    SPELL_CRUSHING_GRIP         = 204611,
    //Heroic
    SPELL_UPROOT                = 212786,

    //end
    SPELL_TALK                  = 202883,
};

enum eEvents
{
    EVENT_SHATTERED_EARTH       = 1,
    EVENT_STRANGLING_ROOTS      = 2,
    EVENT_NIGHTMARE_BREATH      = 3,
    EVENT_CRUSHING_GRIP         = 4,
    //Heroic
    EVENT_UPROOT                = 6,
};

//103344
class boss_oakheart : public CreatureScript
{
public:
    boss_oakheart() : CreatureScript("boss_oakheart") {}

    struct boss_oakheartAI : public BossAI
    {
        boss_oakheartAI(Creature* creature) : BossAI(creature, DATA_OAKHEART) {}

        uint32 passengerReleaseTimer = 0;

        void Reset() override
        {
            _Reset();
            passengerReleaseTimer = 0;

            if (me->GetVehicleKit())
                me->GetVehicleKit()->RemoveAllPassengers();
        }

        void EnterCombat(Unit* /*who*/) override
        {
            Talk(SAY_AGGRO);
            _EnterCombat();
            events.RescheduleEvent(EVENT_SHATTERED_EARTH, 6000);
            events.RescheduleEvent(EVENT_STRANGLING_ROOTS, 14000);
            events.RescheduleEvent(EVENT_NIGHTMARE_BREATH, 22000);
            events.RescheduleEvent(EVENT_CRUSHING_GRIP, 30000);

            if (GetDifficultyID() != DIFFICULTY_NORMAL)
                events.RescheduleEvent(EVENT_UPROOT, 34000);
        }

        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DEATH);
            passengerReleaseTimer = 0;
            if (me->GetVehicleKit())
                me->GetVehicleKit()->RemoveAllPassengers();
            _JustDied();
            DoCast(SPELL_TALK);
        }

        void PassengerBoarded(Unit* /*who*/, int8 seatId, bool apply) override
        {
            if (seatId != 0)
                return;
            if (apply)
            {
                // Covers a failed timing cast as well as a failed throw.
                passengerReleaseTimer = 11000;
                DoCast(204644);
            }
            else
                passengerReleaseTimer = 0;
        }

        void SpellHit(Unit* target, const SpellInfo* spell) override
        {
            if (spell->Id == 204644)
            {
                passengerReleaseTimer = 6000;
                if (auto vehicle = me->GetVehicleKit())
                {
                    if (Unit* passenger = vehicle->GetPassenger(0))
                    {
                        Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, [passenger](Unit* unit)
                        {
                            return unit && unit != passenger && unit->IsPlayer() && unit->isAlive();
                        });
                        passenger->CastSpell(target ? target : passenger, 204651);
                        // Keep the watchdog until PassengerBoarded confirms exit.
                        Talk(SAY_CRUSHING);
                    }
                }
            }
        }

        void MoveInLineOfSight(Unit* who) override
        {
            if (me->getVictim())
                return;

            if (who && me->GetDistance(who) < 14.0f && me->IsValidAttackTarget(who) && me->canStartAttack(who, false))
                AttackStart(who);
        }

        void UpdateAI(uint32 diff) override
        {
            // Run before victim/casting checks so a lost target cannot trap the tank.
            if (passengerReleaseTimer)
            {
                if (passengerReleaseTimer <= diff)
                {
                    passengerReleaseTimer = 0;
                    if (me->GetVehicleKit())
                        me->GetVehicleKit()->RemoveAllPassengers();
                }
                else
                    passengerReleaseTimer -= diff;
            }
            if (!UpdateVictim())
                return;

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            if (CheckHomeDistToEvade(diff, 35.0f))
                return;

            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_SHATTERED_EARTH:
                        DoCast(SPELL_SHATTERED_EARTH);
                        Talk(SAY_EARTH);
                        events.RescheduleEvent(EVENT_SHATTERED_EARTH, 40000);
                        break;
                    case EVENT_STRANGLING_ROOTS:
                        DoCast(SPELL_STRANGLING_ROOTS);
                        Talk(SAY_ROOTS);
                        events.RescheduleEvent(EVENT_STRANGLING_ROOTS, 22000);
                        break;
                    case EVENT_NIGHTMARE_BREATH:
                        DoCastVictim(SPELL_NIGHTMARE_BREATH);
                        Talk(SAY_NIGHTMARE);
                        events.RescheduleEvent(EVENT_NIGHTMARE_BREATH, 32000);
                        break;
                    case EVENT_CRUSHING_GRIP:
                        DoCastVictim(SPELL_CRUSHING_GRIP);
                        events.RescheduleEvent(EVENT_CRUSHING_GRIP, 30000);
                        break;
                    case EVENT_UPROOT:
                        DoCast(SPELL_UPROOT);
                        events.RescheduleEvent(EVENT_UPROOT, 34000);
                        break;
                }
            }
            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new boss_oakheartAI (creature);
    }
};

//100991
class npc_oakheart_strangling_roots : public CreatureScript
{
public:
    npc_oakheart_strangling_roots() : CreatureScript("npc_oakheart_strangling_roots") {}

    struct npc_oakheart_strangling_rootsAI : public ScriptedAI
    {
        npc_oakheart_strangling_rootsAI(Creature* creature) : ScriptedAI(creature) 
        {
            me->SetReactState(REACT_PASSIVE);
        }

        void Reset() override {}

        void IsSummonedBy(Unit* summoner) override
        {
            DoCast(me, SPELL_STRANGLING_ROOTS_AT, true);
        }

        void UpdateAI(uint32 diff) override {}
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_oakheart_strangling_rootsAI (creature);
    }
};

void AddSC_boss_oakheart()
{
    new boss_oakheart();
    new npc_oakheart_strangling_roots();
}