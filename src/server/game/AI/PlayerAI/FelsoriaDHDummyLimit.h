#ifndef FELSORIA_DH_DUMMY_LIMIT_H
#define FELSORIA_DH_DUMMY_LIMIT_H

#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "PlayerBotMgr.h"
#include "Log.h"

namespace FelsoriaDHDummyLimit
{
    // The five Fel Hammer entries supplied in the player's screenshots.
    // Key reservations by the actual creature GUID, never by template entry.
    constexpr uint32 MaximumBots = 3;
    constexpr float ReservationRange = 80.0f;

    inline bool IsHallDummy(Unit* target)
    {
        // Creature::Create calls this after its map, position, zone and area
        // have been initialized, before the object enters the world.
        if (!target || !target->ToCreature())
            return false;
        switch (target->GetEntry())
        {
            case 113673: // Imprisoned Executioner
            case 113647:
            case 113636: // Imprisoned Forgefiend
            case 113676: // Imprisoned Weaver
            case 113674:
                return (target->GetZoneId() == 8023 || target->GetAreaId() == 8023) &&
                    PlayerBotMgr::GetDynamicAreaRequiredClass(target->GetMapId(),
                    target->GetZoneId(), target->GetAreaId()) == CLASS_DEMON_HUNTER;
            default:
                return false;
        }
    }

    inline bool IsRestrictedDummy(Unit* target)
    {
        return target && target->IsInWorld() && IsHallDummy(target);
    }

    inline bool MakePassive(Creature* creature)
    {
        if (!creature || !creature->isAlive() || !IsHallDummy(creature))
            return false;

        // This core explicitly rejects Unit::Attack for REACT_ATTACK_OFF.
        // PACIFIED also prevents already queued melee damage. These flags do
        // not make the target immune or unselectable to players or bots.
        creature->SetReactState(REACT_ATTACK_OFF);
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED | UNIT_FLAG_SILENCED);
        if (creature->IsInWorld())
        {
            if (!creature->HasUnitState(UNIT_STATE_STUNNED))
                creature->SetControlled(true, UNIT_STATE_STUNNED);
            if (creature->IsNonMeleeSpellCast(false))
                creature->InterruptNonMeleeSpells(false);
            if (creature->getVictim())
                creature->AttackStop();
        }
        // Do not CombatStop or remove threat/attackers on the dummy: doing so
        // would stop the bots that are legitimately practicing on it.
        return true;
    }

    inline bool IsParticipant(Player* bot, Unit* target)
    {
        return bot && bot->IsPlayerBot() && bot->IsInWorld() && bot->isAlive() &&
            !bot->IsBeingTeleported() && !bot->IsChangeMap() &&
            bot->GetMap() == target->GetMap() && bot->InSamePhase(target) &&
            bot->InSamePhaseId(target) && bot->GetDistance(target) <= ReservationRange;
    }

    inline uint32 Priority(Player* bot, Unit* target)
    {
        if (bot->getVictim() == target)
            return 0; // Preserve bots already attacking before approaching bots.
        if (bot->GetSelection() == target->GetGUID())
            return 1; // Selection reserves a slot while the bot is walking over.
        return 2;
    }

    inline bool CanUse(Player* bot, Unit* target)
    {
        if (!bot || !bot->IsPlayerBot() || !IsRestrictedDummy(target))
            return true;
        if (!IsParticipant(bot, target) || !target->isAlive())
            return false;

        uint32 const ownPriority = Priority(bot, target);
        uint32 preceding = 0;
        // Players on this map are updated sequentially by Map::Update. A target
        // selection becomes the reservation before the next bot chooses one.
        // No persistent registry or raw pointer survives this call.
        for (auto const& reference : bot->GetMap()->GetPlayers())
        {
            Player* other = reference.getSource();
            if (other == bot || !IsParticipant(other, target))
                continue;
            uint32 const priority = Priority(other, target);
            if (priority == 2)
                continue;
            if (priority < ownPriority ||
                (priority == ownPriority && other->GetGUIDLow() < bot->GetGUIDLow()))
                if (++preceding >= MaximumBots)
                    return false;
        }
        return true;
    }

    template<class Movement>
    inline bool Enforce(Player* bot, Movement* movement)
    {
        if (!bot || !bot->IsPlayerBot() || !bot->IsInWorld() || !bot->isAlive() ||
            bot->IsBeingTeleported() || bot->IsChangeMap())
            return false;
        Unit* victim = bot->getVictim();
        Unit* selected = bot->GetSelectedUnit();
        if (victim)
            MakePassive(victim->ToCreature());
        if (selected && selected != victim)
            MakePassive(selected->ToCreature());
        Unit* rejected = nullptr;
        if (IsRestrictedDummy(victim) && !CanUse(bot, victim))
            rejected = victim;
        else if (IsRestrictedDummy(selected) && !CanUse(bot, selected))
            rejected = selected;
        if (!rejected)
            return false;

        // Stop the rejected training fight, including a pending cast/channel.
        // Preserve an unrelated enemy selected while leaving an old dummy.
        if (!selected || selected == rejected)
            bot->InterruptNonMeleeSpells(false);
        if (victim == rejected)
            bot->AttackStop();
        if (selected == rejected)
            bot->SetSelection(ObjectGuid::Empty);
        if (movement)
            movement->ClearMovement();

        bool realAttacker = false;
        for (Unit* attacker : *bot->getAttackers())
            if (attacker && !IsRestrictedDummy(attacker))
            {
                realAttacker = true;
                break;
            }
        if (!realAttacker && !bot->getVictim() && !bot->GetSelectedUnit())
            bot->CombatStop(false);

        TC_LOG_DEBUG(LOG_FILTER_GENERAL,
            "FELSORIA_DH_DUMMY_LIMIT_V1 bot=%u entry=%u dummy=%u max=%u",
            bot->GetGUIDLow(), rejected->GetEntry(), rejected->GetGUIDLow(), MaximumBots);
        return true;
    }
}

#endif
