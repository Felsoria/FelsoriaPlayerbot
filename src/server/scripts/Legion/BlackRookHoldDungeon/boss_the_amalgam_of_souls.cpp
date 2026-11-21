/*
    Dungeon : Black Rook Hold Dungeon 100-110
    Encounter: The Amalgam of Souls
    Normal: 100%, Heroic: 100%, Mythic: 100%
*/

#include "black_rook_hold_dungeon.h"

enum Says
{
    SAY_AGGRO           = 0,
    SAY_SCYTHE          = 1,
    SAY_SOUL            = 2,
    SAY_REAP            = 3
};

enum Spells
{
    SPELL_SWIRLING_SCYTHE       = 195254,
    SPELL_SOUL_ECHOES           = 194966,
    SPELL_REAP_SOUL             = 194956,

    //Heroic
    SPELL_CALL_SOULS            = 196078,
    SPELL_CALL_SOULS_AT         = 196925,
    SPELL_SOUL_BURST            = 196587,
    SPELL_SOULGORGE             = 196930,

    //Trash
    SPELL_SOUL_ECHOES_CLONE_PLR = 194981,
    SPELL_SOUL_ECHOES_DMG       = 194960,

    SPELL_SUMMON_MINIBOSS_A     = 196619, //Суммонятся что бы открыть двери, после смерти босса
    SPELL_SUMMON_MINIBOSS_B     = 196620,
    SPELL_SUMMON_MINIBOSS_C     = 196646,
    SPELL_SUMMON_CONVERSATION   = 197075,
};

enum eEvents
{
    EVENT_SWIRLING_SCYTHE       = 1,
    EVENT_SOUL_ECHOES           = 2,
    EVENT_REAP_SOUL             = 3,
    EVENT_MOVE_CENTR_POS        = 4,
    EVENT_CALL_SOULS            = 5,
    EVENT_CHECK_SOULS           = 6,
    EVENT_REMOVE_SOULS          = 7,

    ACHIEVEMENT,
    EVENT_SOUL_BURST,
};

enum eAnim //SMSG_PLAY_ONE_SHOT_ANIM_KIT
{
    STALKER_ANIM_1 = 9038,
    STALKER_ANIM_2 = 9039,
    STALKER_ANIM_3 = 9176
};

uint32 AnimRand[3] =
{
    STALKER_ANIM_1,
    STALKER_ANIM_2,
    STALKER_ANIM_3
};

Position const centrPos = {3251.35f, 7582.79f, 12.75f};

Position const soulsPos[7] =
{
    {3288.37f, 7593.30f, 14.10f, 3.45f},
    {3259.17f, 7620.32f, 14.10f, 4.53f},
    {3239.09f, 7618.44f, 14.10f, 5.05f},
    {3213.97f, 7587.99f, 14.10f, 6.12f},
    {3215.85f, 7567.98f, 14.10f, 0.36f},
    {3245.75f, 7541.42f, 14.10f, 1.41f},
    {3265.50f, 7543.86f, 14.10f, 1.90f}
};

//98542
struct boss_the_amalgam_of_souls : public BossAI
{
    boss_the_amalgam_of_souls(Creature* creature) : BossAI(creature, DATA_AMALGAM) 
    {
        if (me->isAlive())
            instance->SetData(DATA_AMALGAM_OUTRO, IN_PROGRESS);   
    }

    bool soulEventActive = false;
    bool achievement = false;
    uint8 healthPct = 0;
    bool soulPhaseStarted = false;

    void Reset() override
    {
        _Reset();
        me->RemoveAurasDueToSpell(SPELL_CALL_SOULS_AT);
        me->RemoveAurasDueToSpell(SPELL_SOULGORGE);
        me->SetReactState(REACT_AGGRESSIVE);
        soulEventActive = false;
        achievement = false;
        soulPhaseStarted = false;
        healthPct = 50;
    }

    void DoAction(int32 const action) override
    {
        if (action == ACTION_1 && soulEventActive)
            achievement = true;
    }

    uint32 GetData(uint32 type) const override
    {
        if (type == ACHIEVEMENT)
            return achievement ? 1 : 0;

        return 0;
    }

    void EnterCombat(Unit* /*who*/) override
    {
        Talk(SAY_AGGRO); //Consume! Devour!
        _EnterCombat();
        DefaultEvents();
    }

    void DefaultEvents()
    {
        events.RescheduleEvent(EVENT_SWIRLING_SCYTHE, 8000);
        events.RescheduleEvent(EVENT_SOUL_ECHOES, 14000);
        events.RescheduleEvent(EVENT_REAP_SOUL, 20000);
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        DoCast(me, SPELL_SUMMON_MINIBOSS_C, true);
        DoCast(me, SPELL_SUMMON_MINIBOSS_B, true);
        DoCast(me, SPELL_SUMMON_MINIBOSS_A, true);
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType dmgType) override
    {
        if (healthPct && me->HealthBelowPctDamaged(healthPct, damage))
        {
            healthPct = 0;

            if (GetDifficultyID() != DIFFICULTY_LFR && GetDifficultyID() != DIFFICULTY_NORMAL)
            {
                me->StopAttack(true, true);
                events.Reset();
                events.RescheduleEvent(EVENT_MOVE_CENTR_POS, 1000);
            }
        }

        if (soulEventActive && me->HealthBelowPctDamaged(15, damage))
            PhaseSoulEnd();
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id != 1)
            return;

        events.RescheduleEvent(EVENT_CALL_SOULS, 1000);
    }

    void SpellHit(Unit* caster, const SpellInfo* spell) override
    {
        if (spell->Id != SPELL_CALL_SOULS || caster != me || soulPhaseStarted ||
            !me->isAlive() || !me->isInCombat() ||
            GetDifficultyID() == DIFFICULTY_LFR || GetDifficultyID() == DIFFICULTY_NORMAL)
            return;

        soulPhaseStarted = true;
        soulEventActive = true;
        // Clear a previous phase's buff before any new soul can be absorbed.
        me->RemoveAurasDueToSpell(SPELL_SOULGORGE);
        DoCast(me, SPELL_CALL_SOULS_AT, true);

        for (uint8 i = 0; i < 7; ++i)
            me->SummonCreature(NPC_RESTLESS_SOUL, soulsPos[i]);

        events.RescheduleEvent(EVENT_CHECK_SOULS, 2000);
        if (me->HealthBelowPct(15))
            PhaseSoulEnd();
    }

    void SpellFinishCast(const SpellInfo* spell) override
    {
        // This core invokes SpellFinishCast BEFORE applying the spell's hits.
        // Remove Soulgorge on the next AI tick, after instant Soul Burst hits.
        if (spell->Id == SPELL_SOUL_BURST)
            events.RescheduleEvent(EVENT_REMOVE_SOULS, 1);
    }

    bool HasPendingSouls() const
    {
        // Track this boss's live souls, including the transformed entry.
        for (auto const& guid : summons)
            if (auto soul = Creature::GetCreature(*me, guid))
                if (soul->isAlive() && soul->IsVisible() &&
                    (soul->GetEntry() == NPC_RESTLESS_SOUL || soul->GetEntry() == NPC_FRUSTRATED_SOUL))
                    return true;

        return false;
    }

    void PhaseSoulEnd()
    {
        if (!soulEventActive)
            return;

        soulEventActive = false;
        events.CancelEvent(EVENT_CHECK_SOULS);
        events.CancelEvent(EVENT_REMOVE_SOULS);
        summons.DespawnEntry(NPC_RESTLESS_SOUL);
        summons.DespawnEntry(NPC_FRUSTRATED_SOUL);
        me->RemoveAurasDueToSpell(SPELL_CALL_SOULS_AT);
        // DamageTaken/SpellHit may run while Call Souls is still casting.
        events.RescheduleEvent(EVENT_SOUL_BURST, 1);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        if (CheckHomeDistToEvade(diff, 15.0f))
            return;

        if (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SWIRLING_SCYTHE:
                    Talk(SAY_SCYTHE);
                    if (auto pTarget = SelectTarget(SELECT_TARGET_RANDOM, 0, [this](Unit const* t) { return t->IsPlayer() && t->ToPlayer()->IsRangedDamageDealer(true); }))
                        DoCast(pTarget, SPELL_SWIRLING_SCYTHE);
                    else if (auto pTarget = SelectTarget(SELECT_TARGET_RANDOM, 0, 60.0f, true))
                        DoCast(pTarget, SPELL_SWIRLING_SCYTHE);
                    events.RescheduleEvent(EVENT_SWIRLING_SCYTHE, 20000);
                    break;
                case EVENT_SOUL_ECHOES:
                    Talk(SAY_SOUL);
                    if (auto pTarget = SelectTarget(SELECT_TARGET_RANDOM, 0, 60.0f, true))
                        DoCast(pTarget, SPELL_SOUL_ECHOES);
                    events.RescheduleEvent(EVENT_SOUL_ECHOES, 28000);
                    break;
                case EVENT_REAP_SOUL:
                    Talk(SAY_REAP);
                    me->StopAttack();
                    me->CastSpell(me, SPELL_REAP_SOUL);
                    me->SetReactState(REACT_AGGRESSIVE, 4500);
                    events.RescheduleEvent(EVENT_REAP_SOUL, 13000);
                    break;
                case EVENT_MOVE_CENTR_POS:
                    me->GetMotionMaster()->MovePoint(1, me->GetHomePosition(), false);
                    break;
                case EVENT_CALL_SOULS:
                    me->CastSpell(me, SPELL_CALL_SOULS);
                    break;
                case EVENT_CHECK_SOULS:
                    if (!soulEventActive)
                        break;
                    if (HasPendingSouls())
                        events.RescheduleEvent(EVENT_CHECK_SOULS, 2000);
                    else
                        PhaseSoulEnd();
                    break;
                case EVENT_SOUL_BURST:
                    DoCast(SPELL_SOUL_BURST);
                    me->SetReactState(REACT_AGGRESSIVE);
                    DefaultEvents();
                    break;
                case EVENT_REMOVE_SOULS:
                    me->RemoveAurasDueToSpell(SPELL_SOULGORGE);
                    break;
            }
        }
        DoMeleeAttackIfReady();
    }
};

//99090
struct npc_soul_echoes_stalker : public ScriptedAI
{
    npc_soul_echoes_stalker(Creature* creature) : ScriptedAI(creature) 
    {
        me->SetReactState(REACT_PASSIVE);
    }

    EventMap events;

    void Reset() override {}

    void IsSummonedBy(Unit* summoner) override
    {
        summoner->CastSpell(me, SPELL_SOUL_ECHOES_CLONE_PLR, true);
        me->PlayOneShotAnimKit(AnimRand[urand(0, 2)]);
        events.RescheduleEvent(EVENT_1, 4000);
    }

    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        if (uint32 eventId = events.ExecuteEvent())
        {
            if (eventId == EVENT_1)
            {
                DoCast(me, SPELL_SOUL_ECHOES_DMG, true);
                me->DespawnOrUnsummon(500);
            }
        }
    }
};

//
struct npc_soul_echoes_outro : public ScriptedAI
{
    npc_soul_echoes_outro(Creature* creature) : ScriptedAI(creature)
    {
        instance = me->GetInstanceScript();
        instance->SetData(DATA_AMALGAM_OUTRO, IN_PROGRESS);
    }

    EventMap events;
    InstanceScript* instance;
    bool first = false;
    bool two = false;
    bool three = false;
    bool outro = false;

    void IsSummonedBy(Unit* summoner) override
    {
        events.RescheduleEvent(EVENT_1, 10);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE)
            return;

        switch (id)
        {
            case 1:
                instance->SetData(DATA_AMALGAM_OUTRO, 0);
                Talk(0);
                break;
            case 2:
                instance->SetData(DATA_AMALGAM_OUTRO, 0);
                break;
            case 3:
                instance->SetData(DATA_AMALGAM_OUTRO, 0);
                break;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        if (instance->GetData(DATA_AMALGAM_OUTRO) >= 3 && !outro)
        {
            outro = true;

            if (me->GetEntry() == 99426)
                me->CastSpell(me, 205210, true);

            if (me->GetEntry() == 99858)
                me->CastSpell(me, 205211, true);

            if (me->GetEntry() == 99857)
                me->CastSpell(me, 205212, true);

            events.RescheduleEvent(EVENT_3, 5000);
        }

        if (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_1:
                    if (me->GetEntry() == 99857)
                        DoCast(SPELL_SUMMON_CONVERSATION);        
                    events.RescheduleEvent(EVENT_2, 7000);
                    break;
               case EVENT_2:
                    if (me->GetEntry() == 99857)
                        me->GetMotionMaster()->MovePoint(1, 3226.46f, 7553.97f, 15.26f);
                    if (me->GetEntry() == 99858)
                        me->GetMotionMaster()->MovePoint(2, 3228.56f, 7548.91f, 14.85f);
                    if (me->GetEntry() == 99426)
                        me->GetMotionMaster()->MovePoint(3, 3233.58f, 7548.09f, 15.04f);
                    break;
               case EVENT_3:
                    me->CastStop();
                    instance->SetData(DATA_AMALGAM_OUTRO, DONE);
                    me->DespawnOrUnsummon(2000);
                    break;          
            }
        }
    }
};

//99664, 99663
struct npc_amalgam_restless_soul : public ScriptedAI
{
    npc_amalgam_restless_soul(Creature* creature) : ScriptedAI(creature) 
    {
        me->SetReactState(REACT_PASSIVE);
        me->SetSpeed(MOVE_FLIGHT, 0.3f);
        // Restless Souls must remain susceptible to displacement for the encounter/achievement.
        me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_FEAR, true);
        me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_POLYMORPH, true);
        me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_SAPPED, true);
        me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_CHARM, true);
        me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_DISORIENTED, true);
        me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_DISTRACT, true);
    }

    uint16 moveTimer = 1000;
    uint32 transformTimer = 60000;
    bool transformed = false;

    void Reset() override {}

    void IsSummonedBy(Unit* summoner) override {}

    void JustDied(Unit* /*killer*/) override
    {
        if (transformed && me->GetAnyOwner())
            if (auto owner = me->GetAnyOwner()->ToCreature())
                owner->AI()->DoAction(ACTION_1);
    }

    void UpdateAI(uint32 diff) override
    {
        if (moveTimer)
        {
            if (moveTimer <= diff)
            {
                moveTimer = 1000;

                if (me->GetDistance(centrPos) < 3.0f)
                {
                    moveTimer = 0;
                    me->DespawnOrUnsummon(4000);
                    DoCast(me, SPELL_SOULGORGE, true);
                    me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_NOT_ATTACKABLE_1);
                    me->SetVisible(false);
                    transformTimer = 0;
                    return;
                }
                else
                {
                    if (!me->HasUnitState(UNIT_STATE_LOST_CONTROL) && !me->HasUnitState(UNIT_STATE_NOT_MOVE))
                        me->GetMotionMaster()->MovePoint(1, centrPos);
                }
            }
            else
                moveTimer -= diff;
        }

        if (transformTimer)
        {
            if (transformTimer <= diff)
            {
                transformTimer = 0;
                // UpdateEntry refills health; a transformation must happen once.
                transformed = me->UpdateEntry(NPC_FRUSTRATED_SOUL);
            }
            else
                transformTimer -= diff;
        }
    }
};

//32385
class achievement_black_rook_moan : public AchievementCriteriaScript
{
public:
    achievement_black_rook_moan() : AchievementCriteriaScript("achievement_black_rook_moan") { }

    bool OnCheck(Player* /*player*/, Unit* target) override
    {
        if (!target)
            return false;

        if (Creature* boss = target->ToCreature())
            if (boss->GetMap()->GetDifficultyID() == DIFFICULTY_MYTHIC_DUNGEON || boss->GetMap()->GetDifficultyID() == DIFFICULTY_MYTHIC_KEYSTONE)
                if (boss->GetAI()->GetData(ACHIEVEMENT))
                    return true;

        return false;
    }
};

void AddSC_boss_the_amalgam_of_souls()
{
    RegisterCreatureAI(boss_the_amalgam_of_souls);
    RegisterCreatureAI(npc_soul_echoes_stalker);
    RegisterCreatureAI(npc_soul_echoes_outro);
    RegisterCreatureAI(npc_amalgam_restless_soul);
    new achievement_black_rook_moan();
}