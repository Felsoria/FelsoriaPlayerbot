#ifndef FELSORIA_QUEST_HUB_PVP_V1_H
#define FELSORIA_QUEST_HUB_PVP_V1_H

// Bot-only protection for shared quest hubs, LegionCore 7.3.5.
// This does not change AreaTable flags, faction relations or NPC combat.
#include "DB2Stores.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "Unit.h"
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_set>

namespace FelsoriaQuestHubPvP
{
struct Hub
{
    char const* name;
    char const* alias;
};

inline Hub const* Hubs(std::size_t& count)
{
    static Hub const hubs[] =
    {
        // Classic / old Azeroth. Cenarion Hold applies to the old Silithus phase.
        {"Booty Bay", nullptr},
        {"Gadgetzan", nullptr},
        {"Everlook", nullptr},
        {"Thorium Point", nullptr},
        {"Nighthaven", nullptr},
        {"Emerald Sanctuary", nullptr},
        {"Cenarion Hold", nullptr},
        {"Ratchet", nullptr},
        // The Burning Crusade.
        {"Cenarion Refuge", nullptr},
        {"Sporeggar", nullptr},
        {"Area 52", nullptr},
        {"The Stormspire", "Stormspire"},
        {"Cosmowrench", nullptr},
        {"Evergrove", nullptr},
        {"Ogri'la", nullptr},
        {"Aeris Landing", nullptr},
        {"Sanctum of the Stars", nullptr},
        {"Sun's Reach Harbor", nullptr},
        {"Sun's Reach Sanctum", nullptr},
        {"Sun's Reach Armory", nullptr},
        // Wrath of the Lich King.
        {"Unu'pe", nullptr},
        {"Moa'ki Harbor", nullptr},
        {"Kamagua", nullptr},
        {"Wyrmrest Temple", nullptr},
        {"The Argent Stand", "Argent Stand"},
        {"Ebon Watch", nullptr},
        {"Light's Breach", nullptr},
        {"Zim'Torga", "Zim'torga"},
        {"Nesingwary Base Camp", nullptr},
        {"K3", nullptr},
        {"Bouldercrag's Refuge", nullptr},
        {"Dun Niffelem", nullptr},
        {"Argent Vanguard", "The Argent Vanguard"},
        {"Crusaders' Pinnacle", "Crusader's Pinnacle"},
        {"The Shadow Vault", "Shadow Vault"},
        {"Death's Rise", nullptr},
        // Cataclysm and the rebuilt old world.
        {"Nordrassil", nullptr},
        {"Grove of Aessina", nullptr},
        {"Shrine of Aviana", nullptr},
        {"Shrine of Goldrinn", nullptr},
        {"Sanctuary of Malorne", nullptr},
        {"Ramkahen", nullptr},
        {"Oasis of Vir'sar", nullptr},
        {"Schnottz's Landing", nullptr},
        {"Bogpaddle", nullptr},
        {"Fuselight", nullptr},
        {"Marshal's Stand", nullptr},
        {"Mossy Pile", nullptr},
        {"Fizzle & Pozzik's Speedbarge", "Fizzle and Pozzik's Speedbarge"},
        {"Menders' Stead", "The Menders' Stead"},
        {"Hearthglen", nullptr},
        {"Chiselgrip", nullptr},
        // Mists of Pandaria.
        {"Dawn's Blossom", nullptr},
        {"Tian Monastery", nullptr},
        {"Sri-La Village", nullptr},
        {"Halfhill", nullptr},
        {"Stoneplow", nullptr},
        {"Zhu's Watch", nullptr},
        {"Anglers Wharf", "The Anglers Wharf"},
        {"One Keg", nullptr},
        {"Zouchin Village", nullptr},
        {"Longying Outpost", nullptr},
        {"Shado-Pan Garrison", nullptr},
        {"Klaxxi'vess", nullptr},
        {"Soggy's Gamble", nullptr},
        {"Celestial Court", nullptr},
        // Warlords of Draenor.
        {"Exarch's Refuge", nullptr},
        {"Veil Terokk", nullptr},
        {"Talon Watch", nullptr},
        {"Pinchwhistle Gearworks", nullptr},
        // Legion. Thunder Totem includes its child areas / interior.
        {"Illidari Stand", nullptr},
        {"Azurewing Repose", nullptr},
        {"Shackle's Den", nullptr},
        {"Lorlathil", nullptr},
        {"Bradensbrook", nullptr},
        {"Thunder Totem", nullptr},
        {"Valdisdall", nullptr},
        {"Irongrove Retreat", nullptr}
    };
    count = sizeof(hubs) / sizeof(hubs[0]);
    return hubs;
}

inline std::unordered_set<uint32> const& ProtectedAreaIds()
{
    // Resolve names once; combat only uses ID lookups and parent traversal.
    // Function-local static initialization is thread-safe.
    static std::unordered_set<uint32> const ids = []()
    {
        std::unordered_set<uint32> result;
        std::size_t count = 0;
        Hub const* hubs = Hubs(count);
        uint32 resolved = 0;
        std::string missing;
        for (std::size_t i = 0; i < count; ++i)
        {
            bool found = false;
            for (AreaTableEntry const* area : sAreaTableStore)
            {
                if (!area || !area->AreaName || area->ID <= 0)
                    continue;
                char const* name = area->AreaName->Str[LOCALE_enUS];
                if (!name || (std::strcmp(name, hubs[i].name) != 0 &&
                    (!hubs[i].alias || std::strcmp(name, hubs[i].alias) != 0)))
                    continue;
                result.insert(static_cast<uint32>(area->ID));
                found = true;
            }
            if (found)
                ++resolved;
            else
            {
                if (!missing.empty())
                    missing += "; ";
                missing += hubs[i].name;
            }
        }
        TC_LOG_INFO(LOG_FILTER_SERVER_LOADING,
            "FELSORIA_QUEST_HUB_PVP_V1 resolved=%u/%u areaRecords=%u",
            resolved, static_cast<uint32>(count), static_cast<uint32>(result.size()));
        if (!missing.empty())
            TC_LOG_WARN(LOG_FILTER_SERVER_LOADING,
                "FELSORIA_QUEST_HUB_PVP_V1 unresolved hubs (NOT protected by this list): %s",
                missing.c_str());
        return result;
    }();
    return ids;
}

inline bool IsProtected(Unit const* unit)
{
    if (!unit || !unit->IsInWorld())
        return false;
    Map const* map = unit->FindMap();
    if (!map || map->IsDungeon() || map->IsBattlegroundOrArena())
        return false;
    // Do not cache an empty store if called during early initialization.
    if (!sAreaTableStore.GetNumRows())
        return false;
    std::unordered_set<uint32> const& protectedIds = ProtectedAreaIds();
    uint32 areaId = unit->GetAreaId();
    for (uint32 depth = 0; areaId && depth < 32; ++depth)
    {
        if (protectedIds.count(areaId))
            return true;
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
        if (!area || area->ParentAreaID == areaId)
            break;
        areaId = area->ParentAreaID;
    }
    return false;
}

inline bool IsBlocked(Unit const* attacker, Unit const* target)
{
    if (!attacker || !target || attacker == target ||
        !attacker->IsInWorld() || !target->IsInWorld() ||
        !attacker->FindMap() || attacker->FindMap() != target->FindMap())
        return false;
    Player* attackerOwner = attacker->GetAffectingPlayer();
    Player* targetOwner = target->GetAffectingPlayer();
    // NPCs and PvE quest mobs have no affecting player.
    if (!attackerOwner || !targetOwner || attackerOwner == targetOwner)
        return false;
    if (!attackerOwner->IsPlayerBot() && !targetOwner->IsPlayerBot())
        return false;
    if (attackerOwner->InBattleground() || targetOwner->InBattleground() ||
        attackerOwner->InArena() || targetOwner->InArena())
        return false;
    // Both ends of a boundary are protected. Check owners as well as pets.
    return IsProtected(attacker) || IsProtected(target) ||
        IsProtected(attackerOwner) || IsProtected(targetOwner);
}

inline void StopBlockedAttack(Unit* unit)
{
    if (unit && IsBlocked(unit, unit->getVictim()))
    {
        unit->InterruptNonMeleeSpells(false);
        unit->AttackStop();
    }
}
}

#endif
