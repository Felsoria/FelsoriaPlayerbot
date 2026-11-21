// Mardum combat reinforcements. Included by mardum.cpp; no CMake source entry needed.
#include "Config.h"
#include "WorldSession.h"

namespace FelsoriaMardumReinforcements
{
    constexpr char StateKey[] = "Felsoria.Mardum.Reinforcements.State";
    struct State
    {
        uint32 poll = 1000;
        uint32 cooldown = 0;
        ObjectGuid helper;
        ObjectGuid target;
    };
    uint32 Setting(char const* name, int fallback, int low, int high)
    {
        return uint32(std::max(low, std::min(high, sConfigMgr->GetIntDefault(name, fallback))));
    }
    bool Eligible(Player* player)
    {
        if (!player || !player->IsInWorld() || !player->IsAlive() ||
            player->getClass() != CLASS_DEMON_HUNTER || player->GetMapId() != 1481 ||
            player->GetZoneId() != 7705 || !player->GetSession() || player->GetSession()->IsBotSession() ||
            player->GetQuestRewardStatus(38729) ||
            !sConfigMgr->GetBoolDefault("Felsoria.Mardum.Reinforcements.Enable", true))
            return false;
        // The Fel Hammer shares map 1481 with the starter experience.
        uint32 area = player->GetAreaId();
        for (uint8 depth = 0; area && depth < 8; ++depth)
        {
            if (area == 8023)
                return false;
            AreaTableEntry const* row = sAreaTableStore.LookupEntry(area);
            if (!row || row->ContinentID != 1481 || row->ParentAreaID == area)
                break;
            area = row->ParentAreaID;
        }
        return true;
    }
    bool Opened(Player* player, uint32 quest, uint32 portal)
    {
        return player->GetQuestRewardStatus(quest) || player->GetQuestObjectiveData(quest, portal) != 0;
    }
    uint32 Pool(Player* player, uint32 (&entries)[3])
    {
        uint32 count = 0;
        if (Opened(player, 40378, 88872)) entries[count++] = 96877;
        if (Opened(player, 40379, 94406)) entries[count++] = 96884;
        if (Opened(player, 38765, 94407)) entries[count++] = 96888;
        return count;
    }
    bool ValidTarget(Player* player, Unit* target)
    {
        return target && target->ToCreature() && target->IsAlive() && target->IsInWorld() &&
            !target->GetCharmerOrOwnerPlayerOrPlayerItself() &&
            player->IsWithinDistInMap(target, 35.0f) &&
            player->InSamePhase(target) && player->IsValidAttackTarget(target) &&
            player->IsWithinLOSInMap(target);
    }
    Unit* Target(Player* player, State const& state)
    {
        Unit* target = player->GetVictim();
        if (ValidTarget(player, target)) return target;
        for (Unit* attacker : *player->getAttackers())
            if (ValidTarget(player, attacker)) return attacker;
        target = ObjectAccessor::GetCreature(*player, state.target);
        return ValidTarget(player, target) && target->GetVictim() == player ? target : nullptr;
    }
    void Cleanup(Player* player)
    {
        State state = player->GetAnyDataContainer().GetValue<State>(StateKey, State());
        if (!state.helper.IsEmpty())
            if (Creature* helper = ObjectAccessor::GetCreature(*player, state.helper))
                if (helper->IsSummon() && helper->ToTempSummon()->GetSummonerGUID() == player->GetGUID())
                    helper->DespawnOrUnsummon();
        state.helper.Clear();
        state.target.Clear();
        player->GetAnyDataContainer().Set<State>(StateKey, state);
    }
}

class npc_felsoria_mardum_reinforcement_v1 : public CreatureScript
{
public:
    npc_felsoria_mardum_reinforcement_v1() : CreatureScript("npc_felsoria_mardum_reinforcement_v1") { }
    struct AI : ScriptedAI
    {
        explicit AI(Creature* creature) : ScriptedAI(creature) { }
        ObjectGuid owner;
        uint32 poll = 0;
        void MoveInLineOfSight(Unit*) override { } // Never pull unrelated mobs or players.
        void IsSummonedBy(Unit* summoner) override
        {
            Player* player = summoner ? summoner->ToPlayer() : nullptr;
            if (!FelsoriaMardumReinforcements::Eligible(player))
            {
                me->DespawnOrUnsummon();
                return;
            }
            owner = player->GetGUID();
            me->SetOwnerGUID(owner);
            me->setFaction(player->getFaction());
            me->SetPhaseMask(player->GetPhaseMask(), false);
            me->SetPhaseId(player->GetPhases(), false);
            me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_ATTACKABLE_1 |
                UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_NOT_SELECTABLE |
                UNIT_FLAG_PACIFIED | UNIT_FLAG_STUNNED);
            me->RemoveFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_NO_ACTIONS | UNIT_FLAG2_DISABLE_TURN);
            me->SetCasterPet(false); // This version uses melee support; do not stop at caster chase distance.
            me->SetReactState(REACT_PASSIVE);
        }
        void UpdateAI(uint32 diff) override
        {
            // DB templates remain safe if used by a static spawn elsewhere.
            if (owner.IsEmpty()) return;
            if (poll <= diff)
            {
                poll = 1000;
                Player* player = ObjectAccessor::GetPlayer(*me, owner);
                if (!FelsoriaMardumReinforcements::Eligible(player) || !player->isInCombat() ||
                    !me->IsWithinDistInMap(player, 45.0f) || me->GetPhaseMask() != player->GetPhaseMask() ||
                    me->GetPhases() != player->GetPhases())
                {
                    me->DespawnOrUnsummon();
                    return;
                }
                auto state = player->GetAnyDataContainer().GetValue<FelsoriaMardumReinforcements::State>(
                    FelsoriaMardumReinforcements::StateKey, FelsoriaMardumReinforcements::State());
                Unit* target = FelsoriaMardumReinforcements::Target(player, state);
                if (!target)
                {
                    me->DespawnOrUnsummon();
                    return;
                }
                if (me->GetVictim() != target && me->IsValidAttackTarget(target))
                    AttackStart(target);
            }
            else poll -= diff;
            if (me->GetVictim() && me->IsValidAttackTarget(me->GetVictim())) DoMeleeAttackIfReady();
        }
    };
    CreatureAI* GetAI(Creature* creature) const override { return new AI(creature); }
};

class player_felsoria_mardum_reinforcements_v1 : public PlayerScript
{
public:
    player_felsoria_mardum_reinforcements_v1() : PlayerScript("player_felsoria_mardum_reinforcements_v1") { }
    void OnLogout(Player* player) override { FelsoriaMardumReinforcements::Cleanup(player); }
    void OnMapChanged(Player* player) override { FelsoriaMardumReinforcements::Cleanup(player); }
    void OnEnterCombat(Player* player, Unit* target) override
    {
        using namespace FelsoriaMardumReinforcements;
        if (!Eligible(player) || !ValidTarget(player, target)) return;
        State state = player->GetAnyDataContainer().GetValue<State>(StateKey, State());
        state.target = target->GetGUID();
        state.poll = std::min(state.poll, uint32(1000));
        player->GetAnyDataContainer().Set<State>(StateKey, state);
    }
    void OnUpdate(Player* player, uint32 diff) override
    {
        using namespace FelsoriaMardumReinforcements;
        if (player->getClass() != CLASS_DEMON_HUNTER || player->GetMapId() != 1481 ||
            !player->GetSession() || player->GetSession()->IsBotSession()) return;
        State state = player->GetAnyDataContainer().GetValue<State>(StateKey, State());
        state.cooldown = state.cooldown > diff ? state.cooldown - diff : 0;
        if (state.poll > diff)
        {
            state.poll -= diff;
            player->GetAnyDataContainer().Set<State>(StateKey, state);
            return;
        }
        state.poll = Setting("Felsoria.Mardum.Reinforcements.PollSeconds", 8, 1, 60) * 1000;
        if (!Eligible(player) || !player->isInCombat())
        {
            player->GetAnyDataContainer().Set<State>(StateKey, state);
            Cleanup(player);
            return;
        }
        Creature* existing = ObjectAccessor::GetCreature(*player, state.helper);
        uint32 entries[3];
        uint32 count = Pool(player, entries);
        Unit* target = Target(player, state);
        if ((!existing || !existing->IsAlive()) && !state.cooldown && count && target &&
            urand(1, 100) <= Setting("Felsoria.Mardum.Reinforcements.Chance", 25, 0, 100))
        {
            // Only use ground beneath the live player, never arbitrary coordinates or height.
            Position spawn = player->GetPosition();
            float ground = player->GetMap()->GetHeight(player->GetPhases(), spawn.GetPositionX(),
                spawn.GetPositionY(), spawn.GetPositionZ() + 2.0f);
            if (!std::isfinite(ground) || std::fabs(ground - spawn.GetPositionZ()) > 2.0f)
            {
                player->GetAnyDataContainer().Set<State>(StateKey, state);
                return;
            }
            spawn.Relocate(spawn.GetPositionX(), spawn.GetPositionY(), ground + 0.1f, spawn.GetOrientation());
            if (TempSummon* helper = player->SummonCreature(entries[urand(0, count - 1)], spawn,
                TEMPSUMMON_TIMED_DESPAWN, Setting("Felsoria.Mardum.Reinforcements.LifetimeSeconds", 20, 5, 60) * 1000,
                0, player->GetGUID()))
            {
                state.helper = helper->GetGUID();
                state.cooldown = Setting("Felsoria.Mardum.Reinforcements.CooldownSeconds", 45, 10, 300) * 1000;
                if (helper->GetScriptName() != "npc_felsoria_mardum_reinforcement_v1")
                {
                    helper->DespawnOrUnsummon();
                    state.helper.Clear();
                    TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "MARDUM_REINFORCEMENTS: import 01_Apply.sql; helper AI binding missing");
                }
                else
                {
                    helper->AI()->AttackStart(target);
                    helper->AI()->Talk(0, player->GetGUID());
                }
            }
        }
        player->GetAnyDataContainer().Set<State>(StateKey, state);
    }
};
