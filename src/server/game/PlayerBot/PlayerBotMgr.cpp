#include "FelsoriaRegional.h"
#include "FelsoriaDynamicBotMetrics.h"
#include "FelsoriaDynamicLifecycle.h"
#include "FelsoriaDynamicPartyPolicy.h"

#include "PlayerBotMgr.h"
#include "CommandBG/FelsoriaGilneasEntryPolicy.h"
#include "CommandBG/FelsoriaGilneasEntryTrace.h"
#include "PlayerBotSetting.h"
#include "FelsoriaBGRoleBalance.h"
#include "World.h"
#include "DB2Stores.h"
#include "Player.h"
#include "BattlegroundMgr.h"
#include "BotAI.h"
#include "BotFieldAI.h"
#include "BotGroupAI.h"
#include "BotDuelAI.h"
#include "BotArenaAI.h"
#include "OnlineMgr.h"
#include "Group.h"
#include "GroupMgr.h"
#include "SocialMgr.h"
#include "LFGMgr.h"
#include "Config.h"
#include "PlayerBotSession.h"
#include "AccountMgr.h"
#include "CharacterPackets.h"
#include "MapManager.h"
#include "PathGenerator.h"
#include "BotAIHumanTrail.h"
#include "Creature.h"
#include "MMapFactory.h"
#include "MMapManager.h"
#include "DetourNavMeshQuery.h"
#include "Util.h"
#include "RealmList.h"
#include "FelsoriaRealmPolicy.h"

#include <boost/algorithm/string.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <map>

namespace
{
// FELSORIA_ALLIED_RACE_POPULATION_V1
uint32 const AlliedStarterTokenBase = 0xF1000000u;
constexpr uint32 PandarenStarterMap = 860;
constexpr uint32 PandarenStarterZone = 5736;

uint8 AlliedDedicatedStarterRace(uint32 map)
{
    // Shared continents and the campaign Argus map must stay mixed even if
    // a custom playercreateinfo row happens to place an allied race there.
    switch (map)
    {
        case 0: case 1: case 530: case 571: case 870: case 1116:
        case 1220: case 1669: case 609: case 1481:
        case PandarenStarterMap: return RACE_NONE;
        default: break;
    }
    for (uint8 race : {RACE_VOID_ELF, RACE_LIGHTFORGED_DRAENEI})
        if (PlayerInfo const* start = sObjectMgr->GetPlayerInfo(race, CLASS_WARRIOR))
            if (start->mapId == map)
                return race;
    return RACE_NONE;
}

bool IsNightborneStartingHub(uint32 map, uint32 zone, uint32 area)
{
    PlayerInfo const* start = sObjectMgr->GetPlayerInfo(RACE_NIGHTBORNE, CLASS_WARRIOR);
    if (!start || start->mapId != map || !start->areaId)
        return false;
    AreaTableEntry const* root = sAreaTableStore.LookupEntry(start->areaId);
    if (!root || root->ContinentID != map || !root->AreaName ||
        !root->AreaName->Str[LOCALE_enUS] ||
        std::string(root->AreaName->Str[LOCALE_enUS]).find("Nighthold") == std::string::npos)
        return false; // A broad/custom Suramar creation row cannot lock the zone.
    uint32 current = area ? area : zone;
    for (uint8 depth = 0; depth < 8 && current; ++depth)
    {
        if (current == start->areaId) return true;
        AreaTableEntry const* entry = sAreaTableStore.LookupEntry(current);
        if (!entry || entry->ContinentID != map || entry->ParentAreaID == current) break;
        current = entry->ParentAreaID;
    }
    return zone == start->areaId;
}

bool SameAlliedPopulationArea(uint32 map, uint32 zone, uint32 first, uint32 second)
{
    return IsNightborneStartingHub(map, zone, first) == IsNightborneStartingHub(map, zone, second);
}

// Only the initial settlement, and its DB2 children, have a native population.
// A parent questing zone such as Elwynn or Durotar remains mixed.
uint32 StarterZone(uint32 map, uint32 zone, uint32 areaId)
{
    // The entire Wandering Isle is the Pandaren starting map.
    if (map == PandarenStarterMap)
        return PandarenStarterZone;
    if (uint8 race = AlliedDedicatedStarterRace(map))
        return AlliedStarterTokenBase + race;
    if (map == 609 || map == 1481)
        return 0; // Hero scenarios are class-specific, not race-specific.
    // Use the server's configured player creation area for phased starts.
    // Their entire maps are substantially larger than the initial settlement.
    if (map == 648 || map == 638 || map == 654)
    {
        uint8 const races[] = { RACE_GOBLIN, RACE_WORGEN };
        for (uint8 race : races)
        {
            PlayerInfo const* start = sObjectMgr->GetPlayerInfo(race, CLASS_WARRIOR);
            if (!start || start->mapId != map || !start->areaId ||
                start->areaId == 4714 || start->areaId == 4737 || start->areaId == 5736)
                continue; // A broad parent is not evidence of an initial area.
            uint32 current = areaId ? areaId : zone;
            for (uint8 depth = 0; depth < 6 && current; ++depth)
            {
                if (current == start->areaId)
                    return current;
                AreaTableEntry const* entry = sAreaTableStore.LookupEntry(current);
                current = entry && entry->ParentAreaID != current ? entry->ParentAreaID : 0;
            }
            if (zone == start->areaId)
                return zone;
        }
        return 0;
    }
    uint32 current = areaId ? areaId : zone;
    for (uint8 depth = 0; depth < 6 && current; ++depth)
    {
        AreaTableEntry const* entry = sAreaTableStore.LookupEntry(current);
        if (entry && (entry->Flags[0] &
            (AREA_FLAG_CAPITAL | AREA_FLAG_SLAVE_CAPITAL | AREA_FLAG_SLAVE_CAPITAL2)))
            return 0; // City subareas also remain mixed.
        switch (current)
        {
            case 59: return 12;    // Northshire, Elwynn.
            case 363: return 363;  // Valley of Trials, Durotar.
            case 368: return 368;  // Echo Isles, Durotar.
            case 132: return 1;    // Coldridge Valley, Dun Morogh.
            case 6457: return 6457; // New Tinkertown, Dun Morogh.
            case 188: return 141;  // Shadowglen, Teldrassil.
            case 154: return 85;   // Deathknell, Tirisfal.
            case 220: return 215;  // Red Cloud Mesa, Mulgore.
            case 3431: return 3430; // Sunstrider Isle, Eversong.
            case 3526: return 3524; // Ammen Vale, Azuremyst.
            default: break;
        }
        current = entry && entry->ParentAreaID != current ? entry->ParentAreaID : 0;
    }
    // GetZoneId can itself be the named starting subarea. Never fall back to
    // its broad parent merely because a custom child row has a broken chain.
    switch (zone)
    {
        case 59: return 12;
        case 363: return 363;
        case 368: return 368;
        case 132: return 1;
        case 6457: return 6457;
        case 188: return 141;
        case 154: return 85;
        case 220: return 215;
        case 3431: return 3430;
        case 3526: return 3524;
        default:
            return 0;
    }
}

void StarterRaces(uint32 starter, uint8 team, uint8 (&races)[2])
{
    races[0] = races[1] = RACE_NONE;
    if (starter == AlliedStarterTokenBase + RACE_VOID_ELF ||
        starter == AlliedStarterTokenBase + RACE_LIGHTFORGED_DRAENEI)
    {
        races[0] = uint8(starter - AlliedStarterTokenBase);
        return;
    }
    if (starter == PandarenStarterZone)
    {
        if (team == TEAM_ALLIANCE)
            races[0] = RACE_PANDAREN_ALLIANCE;
        else if (team == TEAM_HORDE)
            races[0] = RACE_PANDAREN_HORDE;
        return;
    }
    for (uint8 race : { RACE_GOBLIN, RACE_WORGEN })
        if (PlayerInfo const* start = sObjectMgr->GetPlayerInfo(race, CLASS_WARRIOR))
            if (start->areaId == starter && starter && starter != 4714 &&
                starter != 4737 && starter != 5736)
            {
                races[0] = race;
                return;
            }
    switch (starter)
    {
        case 12: races[0] = RACE_HUMAN; break;
        case 363: races[0] = RACE_ORC; break;
        case 368: races[0] = RACE_TROLL; break;
        case 1: races[0] = RACE_DWARF; break;
        case 6457: races[0] = RACE_GNOME; break;
        case 141: races[0] = RACE_NIGHTELF; break;
        case 85: races[0] = RACE_UNDEAD_PLAYER; break;
        case 215: races[0] = RACE_TAUREN; break;
        case 3430: races[0] = RACE_BLOODELF; break;
        case 3524: races[0] = RACE_DRAENEI; break;
        default: break;
    }
}

uint8 NativeStarterRace(uint32 starter, uint8 klass, uint8 team, uint32 level)
{
    if (starter >= AlliedStarterTokenBase && level < 20) return RACE_NONE;
    uint8 races[2] = { RACE_NONE, RACE_NONE };
    StarterRaces(starter, team, races);
    ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(klass);
    if (!classEntry || classEntry->StartingLevel > level ||
        (klass == CLASS_DEATH_KNIGHT && level < 55) ||
        (klass == CLASS_DEMON_HUNTER && level < 98) ||
        uint32(sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL)) > level)
        return RACE_NONE;
    uint8 eligible[2];
    uint8 count = 0;
    for (uint8 race : races)
        if (race != RACE_NONE && PlayerBotRaceTeam(race) == team &&
            sChrRacesStore.LookupEntry(race) &&
            sChrRacesStore.LookupEntry(race)->StartingLevel <= level &&
            sObjectMgr->GetPlayerInfo(race, klass))
            eligible[count++] = race;
    return count ? eligible[urand(0, count - 1)] : RACE_NONE;
}

bool IsNativeStarterRace(uint32 starter, uint8 race, uint8 team)
{
    uint8 races[2];
    StarterRaces(starter, team, races);
    return race == races[0] || race == races[1];
}

uint8 HeroStarterClass(uint32 map)
{
    if (map == 609)
        return CLASS_DEATH_KNIGHT;
    if (map == 1481)
        return CLASS_DEMON_HUNTER;
    return 0;
}

uint8 ClassHallForArea(uint32 map, uint32 zone, uint32 areaId)
{
    // Area IDs follow SpellMgr's class-hall banishing table. The additional
    // halls use their own AreaTable rows. Require the map in the DB2 record:
    // the shared Dalaran map must never become a mage/rogue-only map.
    uint32 current = areaId ? areaId : zone;
    for (uint8 depth = 0; depth < 6 && current; ++depth)
    {
        AreaTableEntry const* entry = sAreaTableStore.LookupEntry(current);
        if (!entry || entry->ContinentID != map)
            break;
        switch (current)
        {
            case 7679: return CLASS_DEATH_KNIGHT;
            case 7752: case 7753: return CLASS_SHAMAN;
            case 7813: return CLASS_WARRIOR;
            case 7879: return CLASS_MAGE;
            case 8012: return CLASS_ROGUE;
            case 7875: return CLASS_WARLOCK;
            case 7903: return CLASS_MONK; // Temple of Five Dawns, not the entire island.
            case 7834: case 8356: return CLASS_PRIEST;
            case 7846: return CLASS_DRUID;
            case 7877: return CLASS_HUNTER;
            case 7638: return CLASS_PALADIN;
            case 8023: return CLASS_DEMON_HUNTER;
            default: break;
        }
        current = entry->ParentAreaID != current ? entry->ParentAreaID : 0;
    }
    // GetZoneId can be the hall itself while GetAreaId is a custom child
    // without a usable parent chain. Validate the zone's own map before use.
    AreaTableEntry const* zoneEntry = sAreaTableStore.LookupEntry(zone);
    if (zoneEntry && zoneEntry->ContinentID == map && zone != areaId)
        return ClassHallForArea(map, zone, zone);
    return 0;
}

// Creature coordinates are authored world positions. Keep only a bounded,
// uniformly sampled zone index; scanning the creature store each second would
// dominate the snapshot update on populated realms.
struct WorldSpawnAnchor
{
    float x, y, z, o;
    uint32 phaseMask;
    uint32 area;
    std::set<uint32> phaseIds;
};

std::vector<WorldSpawnAnchor> const& ZoneSpawnAnchors(uint32 map, uint32 zone)
{
    using Key = std::pair<uint32, uint32>;
    static std::map<Key, std::vector<WorldSpawnAnchor>> cache;
    static std::vector<WorldSpawnAnchor> empty;
    if (!zone)
        return empty;
    Key const key(map, zone);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;
    if (cache.size() >= 32)
        cache.erase(cache.begin());
    std::vector<WorldSpawnAnchor>& anchors = cache[key];
    uint32 seen = 0;
    for (auto const& pair : *sObjectMgr->GetCreatures())
    {
        CreatureData const& data = pair.second;
        bool inZone = data.zoneId == zone;
        uint32 area = data.areaId;
        for (uint8 depth = 0; !inZone && area && depth < 8; ++depth)
        {
            inZone = area == zone;
            AreaTableEntry const* entry = sAreaTableStore.LookupEntry(area);
            if (!entry || entry->ParentAreaID == area) break;
            area = entry->ParentAreaID;
        }
        if (data.mapid != map || !inZone ||
            !MapManager::IsValidMapCoord(map, data.posX, data.posY, data.posZ, data.orientation))
            continue;
        WorldSpawnAnchor anchor = {data.posX, data.posY, data.posZ,
            data.orientation, data.phaseMask, data.areaId, data.PhaseID};
        ++seen;
        if (anchors.size() < 512)
            anchors.push_back(anchor);
        else
        {
            uint32 index = urand(0, seen - 1);
            if (index < anchors.size())
                anchors[index] = anchor;
        }
    }
    return anchors;
}

bool WalkableSpawnAnchor(uint32 map, float x, float y, float z)
{
    dtNavMeshQuery const* query = MMAP::MMapFactory::createOrGetMMapManager()->
        GetNavMeshQuery(map, GetThreadID());
    if (!query)
        return false;
    // Detour uses Y,Z,X coordinates. A nearby polygon is local evidence of
    // walkability; no route from the human across the entire zone is required.
    float point[3] = {y, z, x};
    float nearest[3] = {};
    dtQueryFilter filter;
    filter.setIncludeFlags(NAV_GROUND);
    filter.setExcludeFlags(NAV_WATER | NAV_MAGMA_SLIME);
    if (!PathGenerator::FindWalkPoly(query, point, filter, nearest, 3.0f))
        return false;
    return std::abs(nearest[0] - y) <= 2.0f &&
        std::abs(nearest[1] - z) <= 2.0f &&
        std::abs(nearest[2] - x) <= 2.0f;
}

bool SameSpawnNeighborhood(uint32 map, uint32 instance, float x, float y,
    uint32 otherMap, uint32 otherInstance, float otherX, float otherY)
{
    return map == otherMap && instance == otherInstance &&
        std::abs(x - otherX) < 6.0f && std::abs(y - otherY) < 6.0f;
}
}

uint8 PlayerBotMgr::GetDynamicAreaRequiredClass(uint32 map, uint32 zone, uint32 area)
{
    uint8 const hall = ClassHallForArea(map, zone, area);
    return hall ? hall : HeroStarterClass(map);
}
bool PlayerBotMgr::IsDynamicRaceAllowedAt(uint32 map, uint32 zone, uint32 area, uint8 race, uint8 team)
{
    if (IsNightborneStartingHub(map, zone, area))
        return team == TEAM_HORDE && PlayerBotRaceTeam(race) == TEAM_HORDE;
    uint32 const starter = StarterZone(map, zone, area);
    return !starter || (PlayerBotRaceTeam(race) == team && IsNativeStarterRace(starter, race, team));
}
//#include <boost/format.hpp>

PlayerBotCharBaseInfo PlayerBotBaseInfo::empty;
std::map<uint32, std::list<UnitAI*> > PlayerBotMgr::m_DelayDestroyAIs;
std::mutex PlayerBotMgr::g_uniqueLock;

std::string PlayerBotCharBaseInfo::GetNameANDClassesText()
{
    //std::string clsName;
    uint32 clsEntry = 620000;
    switch (profession)
    {
        case 1:
            // Removed unreadable legacy localized text.
            clsEntry += 1;
            break;
        case 2:
            // Removed unreadable legacy localized text.
            clsEntry += 2;
            break;
        case 3:
            // Removed unreadable legacy localized text.
            clsEntry += 3;
            break;
        case 4:
            // Removed unreadable legacy localized text.
            clsEntry += 4;
            break;
        case 5:
            // Removed unreadable legacy localized text.
            clsEntry += 5;
            break;
        case 6:
            // Removed unreadable legacy localized text.
            clsEntry += 6;
            break;
        case 7:
            // Removed unreadable legacy localized text.
            clsEntry += 7;
            break;
        case 8:
            // Removed unreadable legacy localized text.
            clsEntry += 8;
            break;
        case 9:
            // Removed unreadable legacy localized text.
            clsEntry += 9;
            break;
        case 11:
            // Removed unreadable legacy localized text.
            clsEntry += 10;
            break;
    }
    std::string clsText = sObjectMgr->GetTrinityStringForDBCLocale(clsEntry);
    //consoleToUtf8(clsName, clsText);
    return clsText + name;
}

uint32 PlayerBotBaseInfo::GetCharIDByNoArenaType(bool faction, uint32 prof, uint32 arenaType, std::vector<ObjectGuid>& fliters)
{
    for (ObjectGuid& guid : fliters)
    {
        if (ExistCharacterByGUID(guid))
            return 0;
    }
    if (characters.size() <= 0)
        return 0;
    for (CharInfoMap::iterator it = characters.begin();
        it != characters.end();
        it++)
    {
        if (!MatchRaceByFuction(faction, it->second.race))
            continue;
        if (it->second.profession != prof)
            continue;
    }
    return 0;
}

PlayerBotMgr::PlayerBotMgr() :
    m_BotAccountAmount(137),
    m_LastBotAccountIndex(0),
    m_MaxOnlineBot(100),
    m_BotOnlineCount(0),
    m_LFGSearchTick(0),
    m_ArenaSearchTick(0)
{
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::BattlegroundWarsongGulch);
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin);
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::BattlegroundBattleForGilneas);
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::BattlegroundEyeOfTheStorm);
#ifndef NON_SINGLE_GAME
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::BattlegroundAlteracValley);
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::BattlegroundIsleOfConquest);
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::BattlegroundRandom);

    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::ArenaOldNagrandArena);
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::ArenaOldBladesEdgeArena);
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::ArenaRuinsOfLordaeron);
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::ArenaOldRingOfValor);
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::ArenaBladesEdge);
    m_BGTypes.push_back(MS::Battlegrounds::BattlegroundTypeId::ArenaAll);
    //m_BGTypes.push_back(BattlegroundTypeId::BATTLEGROUND_SA);
#endif
}

PlayerBotMgr::~PlayerBotMgr()
{
    ClearBaseInfo();
}

PlayerBotMgr* PlayerBotMgr::instance()
{
    static PlayerBotMgr instance;
    return &instance;
}

void PlayerBotMgr::SwitchPlayerBotAI(Player* player, PlayerBotAIType aiType, bool force)
{
    if (!force && player->isInCombat())
        return;
    if (force && player->isInCombat())
        player->ClearInCombat();
    player->SetSelection(ObjectGuid::Empty);
    UnitAI* pAI = player->GetAI();
    player->IsAIEnabled = false;
    switch (aiType)
    {
        case PlayerBotAIType::PBAIT_FIELD:
            if (pAI)
            {
                if (dynamic_cast<BotFieldAI*>(pAI) != NULL)
                {
                    player->IsAIEnabled = true;
                    return;
                }
                PlayerBotMgr::m_DelayDestroyAIs[getMSTime()].push_back(pAI);
                player->SetAI(NULL);
            }
            pAI = BotFieldAI::CreateBotFieldAIByPlayerClass(player);
            if (pAI)
            {
                pAI->Reset();
                player->SetAI(pAI);
                player->IsAIEnabled = true;
            }
            break;
        case PlayerBotAIType::PBAIT_GROUP:
            if (pAI)
            {
                if (dynamic_cast<BotGroupAI*>(pAI) != NULL)
                {
                    player->IsAIEnabled = true;
                    return;
                }
                PlayerBotMgr::m_DelayDestroyAIs[getMSTime()].push_back(pAI);
                player->SetAI(NULL);
            }
            pAI = BotGroupAI::CreateBotGroupAIByPlayerClass(player);
            if (pAI)
            {
                pAI->Reset();
                player->SetAI(pAI);
                player->IsAIEnabled = true;
            }
            break;
        case PlayerBotAIType::PBAIT_DUEL:
            if (pAI)
            {
                if (dynamic_cast<BotDuelAI*>(pAI) != NULL)
                {
                    player->IsAIEnabled = true;
                    return;
                }
                PlayerBotMgr::m_DelayDestroyAIs[getMSTime()].push_back(pAI);
                player->SetAI(NULL);
            }
            pAI = BotDuelAI::CreateBotDuelAIByPlayerClass(player);
            if (pAI)
            {
                pAI->Reset();
                player->SetAI(pAI);
                player->IsAIEnabled = true;
                ((BotDuelAI*)pAI)->ResetBotAI();
            }
            break;
        case PlayerBotAIType::PBAIT_ARENA:
#ifndef CONVERT_ARENAAI_TOBG
            if (pAI)
            {
                if (dynamic_cast<BotArenaAI*>(pAI) != NULL)
                {
                    player->IsAIEnabled = true;
                    return;
                }
                PlayerBotMgr::m_DelayDestroyAIs[getMSTime()].push_back(pAI);
                player->SetAI(NULL);
            }
            pAI = BotArenaAI::CreateBotArenaAIByPlayerClass(player);
            if (pAI)
            {
                pAI->Reset();
                player->SetAI(pAI);
                player->IsAIEnabled = true;
            }
            break;
#endif
        case PlayerBotAIType::PBAIT_BG:
            if (pAI)
            {
                if (dynamic_cast<BotBGAI*>(pAI) != NULL)
                {
                    player->IsAIEnabled = true;
                    return;
                }
                PlayerBotMgr::m_DelayDestroyAIs[getMSTime()].push_back(pAI);
                player->SetAI(NULL);
            }
            player->SetAI(BotBGAI::CreateBotBGAIByPlayerClass(player));
            player->IsAIEnabled = true;
            break;
        case PlayerBotAIType::PBAIT_DUNGEON:
            if (pAI)
            {
                //if (dynamic_cast<BotFieldAI*>(pAI) != NULL)
                //{
                //	player->IsAIEnabled = true;
                //	return;
                //}
                PlayerBotMgr::m_DelayDestroyAIs[getMSTime()].push_back(pAI);
                player->SetAI(NULL);
            }
            break;
    }
}

std::string PlayerBotMgr::GetPlayerLinkText(Player const* player) const
{
    const std::string& name = player->GetName();
    if (player->GetTeamId() == TeamId::TEAM_ALLIANCE)
        return "|cff0000ff|Hplayer:" + name + "|h[" + name + "]|h|r";
    else if (player->GetTeamId() == TeamId::TEAM_HORDE)
        return "|cffff0000|Hplayer:" + name + "|h[" + name + "]|h|r";
    return "|cffffffff|Hplayer:" + name + "|h[" + name + "]|h|r";
}

PlayerBotSession* PlayerBotMgr::GetBotSessionByCharGUID(ObjectGuid& guid)
{
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator itInfo = m_idPlayerBotBase.begin();
        itInfo != m_idPlayerBotBase.end();
        itInfo++)
    {
        PlayerBotBaseInfo* pInfo = itInfo->second;
        if (!(pInfo->ExistCharacterByGUID(guid)))
            continue;
        WorldSession* pWorldSession = sWorld->FindSession(pInfo->id).get();
        if (!pWorldSession)
            return NULL;
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(pWorldSession);
        return pSession;
    }
    return NULL;
}

TeamId PlayerBotMgr::GetTeamIDByPlayerBotGUID(ObjectGuid& guid)
{
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator itInfo = m_idPlayerBotBase.begin();
        itInfo != m_idPlayerBotBase.end();
        itInfo++)
    {
        PlayerBotBaseInfo* pInfo = itInfo->second;
        TeamId team = pInfo->GetTeamIDByChar(guid);
        if (team == TeamId::TEAM_NEUTRAL)
            continue;
        return team;
    }
    return TeamId::TEAM_NEUTRAL;
}

bool PlayerBotMgr::IsPlayerBot(WorldSession* pSession)
{
    if (pSession && dynamic_cast<PlayerBotSession*> (pSession))
        return true;
    return false;
}

bool PlayerBotMgr::IsBotAccuntName(std::string name)
{
    if (name.size() < 10)
        return false;
    std::string head = name.substr(0, 9);
    if (head != "playerbot")
        return false;
    std::string numText = name.substr(9);
    int num = atoi(numText.c_str());
    return num > 0;
}

bool PlayerBotMgr::IsIDLEPlayerBot(Player* player)
{
    if (!player || player->isBeingLoaded() || !player->IsInWorld())
        return false;
    if (player->isInCombat() || !player->isAlive())
        return false;
    if (player->InBattleground() || player->InArena() || player->GetMap()->IsDungeon() ||
        player->GetGroup() || player->isUsingLfg())
        return false;
    if (player->InBattlegroundQueue())
        return false;
    if (!player->IsSettingFinish())
        return false;
    if (player->HasAura(26013))
        return false;
    return true;
}

std::set<uint32> PlayerBotMgr::GetArenaTeamPlayerBotIDCountByTeam(TeamId team, int32 count, ArenaTeamTypes type)
{
    std::vector<uint32> allInfoEntrys;
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator itInfo = m_idPlayerBotBase.begin(); itInfo != m_idPlayerBotBase.end(); itInfo++)
        allInfoEntrys.push_back(itInfo->first);
    Trinity::Containers::RandomShuffle(allInfoEntrys);
    //unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    //std::shuffle(allInfoEntrys.begin(), allInfoEntrys.end(), std::default_random_engine(seed));

    std::set<uint32> ids;
    if (allInfoEntrys.empty())
        return ids;
    for (uint32 infoEntry : allInfoEntrys)
    {
        PlayerBotBaseInfo* info = GetPlayerBotAccountInfo(infoEntry);
        if (!info)
            continue;
        std::vector<uint32> faildIDs = info->GetNoArenaTeamCharacterIDsByFuction((team == TEAM_ALLIANCE) ? true : false, type);
        if (faildIDs.empty())
            continue;
        for (uint32 id : faildIDs)
        {
            if (ids.find(id) != ids.end())
                continue;
            ids.insert(id);
            break;
        }
        if (ids.size() >= uint32(count))
            return ids;
    }
    ids.clear();
    return ids;
}

PlayerBotBaseInfo* PlayerBotMgr::GetPlayerBotAccountInfo(uint32 guid)
{
    std::map<uint32, PlayerBotBaseInfo*>::iterator it = m_idPlayerBotBase.find(guid);
    if (it == m_idPlayerBotBase.end())
        return NULL;
    return it->second;
}

PlayerBotBaseInfo* PlayerBotMgr::GetAccountBotAccountInfo(uint32 guid)
{
    std::map<uint32, PlayerBotBaseInfo*>::iterator it = m_idAccountBotBase.find(guid);
    if (it == m_idAccountBotBase.end())
        return NULL;
    return it->second;
}

bool PlayerBotMgr::ExistClassByRace(uint8 race, uint8 prof)
{
    if (prof < CLASS_WARRIOR || prof > CLASS_DEMON_HUNTER)
        return false;
    // Never let permissive/custom create-info rows invent combinations that
    // the Legion client cannot create (notably Pandaren/allied DKs or DHs).
    if (prof == CLASS_DEMON_HUNTER)
        return (race == RACE_NIGHTELF || race == RACE_BLOODELF) &&
            sObjectMgr->GetPlayerInfo(race, prof);
    if (prof == CLASS_DEATH_KNIGHT && (race == RACE_PANDAREN_ALLIANCE ||
        race == RACE_PANDAREN_HORDE || race == RACE_NIGHTBORNE ||
        race == RACE_HIGHMOUNTAIN_TAUREN || race == RACE_VOID_ELF || race == RACE_LIGHTFORGED_DRAENEI))
        return false;
    if (prof == CLASS_MONK && (race == RACE_GOBLIN || race == RACE_WORGEN))
        return false;
    return sObjectMgr->GetPlayerInfo(race, prof) != nullptr;
}
void PlayerBotMgr::InitializeCreatePlayerBotName()
{
    allName.clear();
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ALLRANDOM_NAME);
    PreparedQueryResult result = LoginDatabase.Query(stmt);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            std::string& dbName = fields[0].GetString();
            if (dbName.size() > 0)
                allName.push_back(dbName);
        } while (result->NextRow());
    }

    allArenaName.clear();
    PreparedStatement* stmt2 = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ALLARENA_NAME);
    PreparedQueryResult result2 = LoginDatabase.Query(stmt2);
    if (result2)
    {
        do
        {
            Field* fields = result2->Fetch();
            std::string& dbName = fields[0].GetString();
            if (dbName.size() > 0)
                allArenaName.push_back(dbName);
        } while (result2->NextRow());
    }
}
std::string PlayerBotMgr::RandomName(uint8 race)
{
    // Allied races share the name style of their requested parent race.
    switch (race)
    {
        case RACE_VOID_ELF: race = RACE_BLOODELF; break;
        case RACE_LIGHTFORGED_DRAENEI: race = RACE_DRAENEI; break;
        case RACE_HIGHMOUNTAIN_TAUREN: race = RACE_TAUREN; break;
        case RACE_NIGHTBORNE: race = RACE_NIGHTELF; break;
        default: break;
    }

    static char const* bloodElfPrefixes[] = { "Ael", "Bel", "Kael", "Lor", "Quel", "Sel", "Syl", "Thal", "Val", "Vel" };
    static char const* bloodElfSuffixes[] = { "dorei", "eth", "ian", "ira", "ion", "riel", "rin", "thas", "wyn" };
    static char const* draeneiPrefixes[] = { "Aka", "Ara", "Dora", "Isha", "Kara", "Mara", "Nobu", "Ora", "Vela", "Yara" };
    static char const* draeneiSuffixes[] = { "da", "dra", "ena", "ion", "los", "naar", "ria", "ron", "sha" };
    static char const* taurenPrefixes[] = { "Asha", "Boro", "Hamu", "Kora", "Maho", "Mato", "Naru", "Rono", "Taho", "Tura" };
    static char const* taurenSuffixes[] = { "hoof", "horn", "mane", "runner", "stride", "stone", "tail", "tusk", "wind" };
    static char const* nightElfPrefixes[] = { "Ala", "Ara", "Ely", "Ily", "Luna", "Mala", "Nara", "Shala", "Tyla", "Ysera" };
    static char const* nightElfSuffixes[] = { "driel", "lora", "lune", "rien", "riel", "shae", "thil", "wyn", "yra" };
    static char const* pandarenPrefixes[] = { "Bao", "Bai", "Chen", "Fen", "Jia", "Lan", "Lin", "Mei", "Shen", "Tao", "Wei", "Xiao" };
    static char const* pandarenSuffixes[] = { "bo", "chu", "fen", "hua", "ji", "lin", "mei", "shen", "wei", "yu" };

    static char const* prefixes[] =
    {
        "Aer", "Ael", "Al", "Ar", "Bel", "Cal", "Dar", "Del",
        "Dra", "Eld", "El", "Fen", "Gal", "Gor", "Hal", "Il",
        "Kal", "Kael", "Kor", "Lor", "Mal", "Mor", "Nar", "Nel",
        "Or", "Quel", "Ran", "Sar", "Sel", "Syl", "Tal", "Thal",
        "Tor", "Val", "Vel", "Vor", "Xar", "Zal"
    };

    static char const* suffixes[] =
    {
        "ador", "an", "ar", "as", "dan", "dor", "dren", "dus",
        "en", "eth", "ian", "il", "ion", "ir", "is", "kor",
        "lan", "lor", "mir", "mon", "na", "nor", "on", "or",
        "os", "ran", "ren", "ric", "rin", "ron", "ros", "thar",
        "thas", "us", "van", "vin", "yn"
    };

    uint32 const prefixCount =
        sizeof(prefixes) / sizeof(prefixes[0]);
    uint32 const suffixCount =
        sizeof(suffixes) / sizeof(suffixes[0]);

    char const* const* selectedPrefixes = prefixes;
    char const* const* selectedSuffixes = suffixes;
    uint32 selectedPrefixCount = prefixCount;
    uint32 selectedSuffixCount = suffixCount;
    switch (race)
    {
        case RACE_BLOODELF:
            selectedPrefixes = bloodElfPrefixes; selectedSuffixes = bloodElfSuffixes;
            selectedPrefixCount = sizeof(bloodElfPrefixes) / sizeof(bloodElfPrefixes[0]);
            selectedSuffixCount = sizeof(bloodElfSuffixes) / sizeof(bloodElfSuffixes[0]);
            break;
        case RACE_DRAENEI:
            selectedPrefixes = draeneiPrefixes; selectedSuffixes = draeneiSuffixes;
            selectedPrefixCount = sizeof(draeneiPrefixes) / sizeof(draeneiPrefixes[0]);
            selectedSuffixCount = sizeof(draeneiSuffixes) / sizeof(draeneiSuffixes[0]);
            break;
        case RACE_TAUREN:
            selectedPrefixes = taurenPrefixes; selectedSuffixes = taurenSuffixes;
            selectedPrefixCount = sizeof(taurenPrefixes) / sizeof(taurenPrefixes[0]);
            selectedSuffixCount = sizeof(taurenSuffixes) / sizeof(taurenSuffixes[0]);
            break;
        case RACE_NIGHTELF:
            selectedPrefixes = nightElfPrefixes; selectedSuffixes = nightElfSuffixes;
            selectedPrefixCount = sizeof(nightElfPrefixes) / sizeof(nightElfPrefixes[0]);
            selectedSuffixCount = sizeof(nightElfSuffixes) / sizeof(nightElfSuffixes[0]);
            break;
        case RACE_PANDAREN_ALLIANCE:
        case RACE_PANDAREN_HORDE:
            selectedPrefixes = pandarenPrefixes; selectedSuffixes = pandarenSuffixes;
            selectedPrefixCount = sizeof(pandarenPrefixes) / sizeof(pandarenPrefixes[0]);
            selectedSuffixCount = sizeof(pandarenSuffixes) / sizeof(pandarenSuffixes[0]);
            break;
        default: break;
    }

    // Character creation is queued, so names absent from the database can
    // still be in flight. Release the reservation on create success or failure.
    for (uint32 attempt = 0; attempt < 1200; ++attempt)
    {
        // Keep the chosen race's style even after its two-syllable names fill.
        std::string name = selectedPrefixes[urand(0, selectedPrefixCount - 1)];
        if (attempt >= 250)
        {
            std::string middle = selectedSuffixes[urand(0, selectedSuffixCount - 1)];
            name += middle.substr(0, 2);
        }
        name += selectedSuffixes[urand(0, selectedSuffixCount - 1)];
        if (name.size() > 12)
            continue;

        {
            std::lock_guard<std::mutex> lock(m_pendingBotNamesLock);
            if (m_PendingBotNames.find(name) != m_PendingBotNames.end())
                continue;
        }

        std::string sql =
            "SELECT guid FROM characters WHERE name = '" +
            name + "' LIMIT 1";

        QueryResult result = CharacterDatabase.Query(sql.c_str());

        if (!result)
        {
            std::lock_guard<std::mutex> lock(m_pendingBotNamesLock);
            if (m_PendingBotNames.insert(name).second)
                return name;
        }
    }

    return "";
}
void PlayerBotMgr::ReleasePendingBotName(std::string const& name)
{
    std::lock_guard<std::mutex> lock(m_pendingBotNamesLock);
    m_PendingBotNames.erase(name);
}
std::string PlayerBotMgr::RandomArenaName()
{
    return "Arena" + std::to_string(urand(0, 100));
}
uint8 PlayerBotMgr::RandomRace(bool group, uint8 prof, uint32 targetLevel)
{
    static uint8 const playableRaces[] =
    {
        RACE_HUMAN, RACE_ORC, RACE_DWARF, RACE_NIGHTELF,
        RACE_UNDEAD_PLAYER, RACE_TAUREN, RACE_GNOME, RACE_TROLL,
        RACE_GOBLIN, RACE_BLOODELF, RACE_DRAENEI, RACE_WORGEN,
        RACE_PANDAREN_ALLIANCE, RACE_PANDAREN_HORDE,
        RACE_NIGHTBORNE, RACE_HIGHMOUNTAIN_TAUREN,
        RACE_VOID_ELF, RACE_LIGHTFORGED_DRAENEI
    };
    uint8 candidates[sizeof(playableRaces) / sizeof(playableRaces[0])];
    uint32 count = 0;
    ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(prof);
    if (!classEntry || (prof == CLASS_DEATH_KNIGHT && targetLevel < 55) ||
        (prof == CLASS_DEMON_HUNTER && targetLevel < 98))
        return RACE_NONE;
    for (uint8 race : playableRaces)
    {
        ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race);
        bool const allied = race == RACE_NIGHTBORNE || race == RACE_HIGHMOUNTAIN_TAUREN ||
            race == RACE_VOID_ELF || race == RACE_LIGHTFORGED_DRAENEI;
        if ((!allied || targetLevel >= 20) && raceEntry && raceEntry->StartingLevel <= targetLevel &&
            classEntry->StartingLevel <= targetLevel &&
            uint32(sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL)) <= targetLevel &&
            PlayerBotRaceTeam(race) == (group ? TeamId::TEAM_ALLIANCE : TeamId::TEAM_HORDE) &&
            ExistClassByRace(race, prof))
            candidates[count++] = race;
    }

    if (count)
        return candidates[urand(0, count - 1)];

    TC_LOG_ERROR(LOG_FILTER_PLAYER, "server.loading", "No playable race for PlayerBot class %u (alliance %u)", uint32(prof), uint32(group));
    return RACE_NONE;
}
uint8 PlayerBotMgr::RandomSkinColor(uint8 race, uint8 gender, uint8 prof)
{
    uint8 skinColor = 0;
    for (int i = 0; i < 80; i++)
    {
        skinColor = irand(0, 9);
        CharSectionsEntry const* entry = sDB2Manager.GetCharSectionEntry(race, gender, CharBaseSectionVariation::Skin, 0, skinColor);
        if (entry)
        {
            if ((entry->Flags & SECTION_FLAG_DEATH_KNIGHT) && prof != CLASS_DEATH_KNIGHT)
                continue;
            if (!(entry->Flags & SECTION_FLAG_PLAYER))
                continue;
            return skinColor;
        }
    }
    TC_LOG_INFO(LOG_FILTER_PLAYER, "server.loading", ">> Random player custom SkinColor timeout!");
    return 1;
}
uint8 PlayerBotMgr::RandomFace(uint8 race, uint8 gender, uint8 skinColor, uint8 prof)
{
    uint8 faceID = 0;
    for (int i = 0; i < 80; i++)
    {
        faceID = irand(0, 9);
        CharSectionsEntry const* entry = sDB2Manager.GetCharSectionEntry(race, gender, CharBaseSectionVariation::Face, faceID, skinColor);
        if (entry)
        {
            if ((entry->Flags & SECTION_FLAG_DEATH_KNIGHT) && prof != CLASS_DEATH_KNIGHT)
                continue;
            if (!(entry->Flags & SECTION_FLAG_PLAYER))
                continue;
            return faceID;
        }
    }
    TC_LOG_INFO(LOG_FILTER_PLAYER, "server.loading", ">> Random player custom Face timeout!");
    return 1;
}
uint8 PlayerBotMgr::RandomHair(uint8 race, uint8 gender, uint8 prof)
{
    uint8 hairID = 0;
    for (uint8 hc = 0; hc < 80; hc++)
    {
        for (int i = 0; i < 80; i++)
        {
            hairID = irand(0, 8);
            CharSectionsEntry const* entry = sDB2Manager.GetCharSectionEntry(race, gender, CharBaseSectionVariation::Hair, hairID, hc);
            if (entry)
            {
                if ((entry->Flags & SECTION_FLAG_DEATH_KNIGHT) && prof != CLASS_DEATH_KNIGHT)
                    continue;
                if (!(entry->Flags & SECTION_FLAG_PLAYER))
                    continue;
                return hairID;
            }
        }
    }
    TC_LOG_INFO(LOG_FILTER_PLAYER, "server.loading", ">> Random player custom Hair timeout!");
    return 0;
}
uint8 PlayerBotMgr::RandomHairColor(uint8 race, uint8 gender, uint8 hairID, uint8 prof)
{
    uint8 hairColor = 0;
    for (int i = 0; i < 80; i++)
    {
        hairColor = irand(0, 8);
        CharSectionsEntry const* entry = sDB2Manager.GetCharSectionEntry(race, gender, CharBaseSectionVariation::Hair, hairID, hairColor);
        if (entry)
        {
            if ((entry->Flags & SECTION_FLAG_DEATH_KNIGHT) && prof != CLASS_DEATH_KNIGHT)
                continue;
            if (!(entry->Flags & SECTION_FLAG_PLAYER))
                continue;
            return hairColor;
        }
    }
    TC_LOG_INFO(LOG_FILTER_PLAYER, "server.loading", ">> Random player custom HairColor timeout!");
    return 1;
}
uint8 PlayerBotMgr::RandomFacialHair(uint8 race, uint8 gender, uint8 hairColor, uint8 prof)
{
    uint8 facialHair = 0;
    for (int i = 0; i < 80; i++)
    {
        facialHair = irand(0, 7);
        CharSectionsEntry const* entry = sDB2Manager.GetCharSectionEntry(race, gender, CharBaseSectionVariation::FacialHair, facialHair, hairColor);
        if (entry)
        {
            if ((entry->Flags & SECTION_FLAG_DEATH_KNIGHT) && prof != CLASS_DEATH_KNIGHT)
                continue;
            if (!(entry->Flags & SECTION_FLAG_PLAYER))
                continue;
            return facialHair;
        }
    }
    TC_LOG_INFO(LOG_FILTER_PLAYER, "server.loading", ">> Random player custom FacialHair timeout!");
    return 0;
}

WorldPacket PlayerBotMgr::BuildCreatePlayerData(bool group, uint8 prof, uint32 targetLevel, uint8 preferredRace)
{
    uint8 race = preferredRace != RACE_NONE ? preferredRace : RandomRace(group, prof, targetLevel);
    std::string name = RandomName(race);
    uint8 gender = irand(0, 1);
    uint8 skinColor = RandomSkinColor(race, gender, prof);
    uint8 faceID = RandomFace(race, gender, skinColor, prof);
    uint8 hairID = RandomHair(race, gender, prof);
    uint8 hairColor = RandomHairColor(race, gender, hairID, prof);
    uint8 facialHair = RandomFacialHair(race, gender, hairColor, prof);

    WorldPacket cmd(CMSG_CREATE_CHARACTER);
    cmd.WriteBits(name.length(), 6);
    cmd.WriteBit(false);
    cmd.FlushBits();

    cmd << uint8(race);
    cmd << uint8(prof);
    cmd << uint8(gender);
    cmd << uint8(skinColor);
    cmd << uint8(faceID);
    cmd << uint8(hairID);
    cmd << uint8(hairColor);
    cmd << uint8(facialHair);
    cmd << uint8(0);

    cmd << uint8(0);
    cmd << uint8(0);
    cmd << uint8(0);

    cmd.WriteString(name);
    return cmd;
}

void PlayerBotMgr::CreateOncePlayerBot()
{
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator it = m_idPlayerBotBase.begin();
        it != m_idPlayerBotBase.end();
        it++)
    {
        PlayerBotBaseInfo* pInfo = it->second;
        if (pInfo->needCreateBots.size() > 0)
        {
            WorldPacket& packet = pInfo->needCreateBots.front();
            WorldSession* pSession = sWorld->FindSession(pInfo->id).get();
            if (pSession)
            {
                //std::shared_ptr<CharacterCreateInfo> CreateInfo;
                WorldPackets::Character::CreateChar createPacket(std::move(packet));
                createPacket.Read();
                pSession->HandleCharCreateOpcode(createPacket);
            }
            pInfo->needCreateBots.pop();
            break;
        }
    }
}

void PlayerBotMgr::ClearBaseInfo()
{
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator it = m_idPlayerBotBase.begin();
        it != m_idPlayerBotBase.end();
        it++)
    {
        delete it->second;
    }
    m_idPlayerBotBase.clear();
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator it = m_idAccountBotBase.begin();
        it != m_idAccountBotBase.end();
        it++)
    {
        delete it->second;
    }
    m_idAccountBotBase.clear();
}

void PlayerBotMgr::UpdateLastAccountIndex(std::string& username)
{
    //std::unique_lock<std::mutex> sessionGuard(PlayerBotMgr::g_uniqueLock);
    if (username.empty())
        return;
    std::string querySql = "SELECT id FROM account WHERE username='" + username + "'";
    QueryResult result = LoginDatabase.Query(querySql.c_str());
    if (result)
    {
        Field* fields = result->Fetch();
        if (fields)
        {
            uint32 id = fields[0].GetUInt32();
            m_LastBotAccountIndex = id;
        }
    }
}

void PlayerBotMgr::SupplementAccount()
{
    uint32 targetAccountCount = m_BotAccountAmount * 2;
    uint32 currentAccountCount = uint32(m_idPlayerBotBase.size());

    if (currentAccountCount >= targetAccountCount)
        return;

    uint32 needAccount = targetAccountCount - currentAccountCount;

    for (uint32 i = 0; i < needAccount; i++)
    {
        ++m_LastBotAccountIndex;
        char indexText[25] ={ 0 };
        itoa(m_LastBotAccountIndex, indexText, 10);
        std::string userName = "playerbot";
        userName += indexText;
        if (AccountMgr::CreateAccount(userName, "botxxx") == AccountOpResult::AOR_OK)
        {
            std::string querySql = "SELECT id, username, sha_pass_hash FROM account WHERE username='";
            querySql += userName + "'";
            QueryResult result = LoginDatabase.Query(querySql.c_str());
            if (result)
            {
                Field* fields = result->Fetch();
                if (fields)
                {
                    uint32 id = fields[0].GetUInt32();
                    std::string username = fields[1].GetString();
                    std::string pass = fields[2].GetString();

                    std::string lowerName = boost::algorithm::to_lower_copy(username);
                    if (m_idPlayerBotBase.find(id) == m_idPlayerBotBase.end())
                    {
                        PlayerBotBaseInfo* pInfo = new PlayerBotBaseInfo(id, username.c_str(), pass, false);
                        m_idPlayerBotBase[id] = pInfo;
                    }
                    m_LastBotAccountIndex = id;
                }
            }
        }
    }
}

void PlayerBotMgr::DestroyBotMail(uint32 guid)
{
    char sql[256] ={ 0 };
    sprintf_s(sql, 255, "DELETE FROM mail WHERE receiver = %d", guid);
    CharacterDatabase.Execute(sql);
    //memset(sql, 0, 256);
    //sprintf_s(sql, 255, "DELETE FROM mail_items WHERE receiver = %d", guid);
    //CharacterDatabase.Execute(sql);
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_ITEMS);
    stmt->setUInt32(0, guid);
    CharacterDatabase.Execute(stmt);
}

void PlayerBotMgr::AddNewAccountBotBaseInfo(std::string name)
{
    std::string upperName = boost::algorithm::to_upper_copy(name);
    std::string sql("SELECT id, username, sha_pass_hash FROM account WHERE `username`='"); sql += upperName + "'";
    QueryResult result = LoginDatabase.Query(sql.c_str());
    if (!result)
        return;
    Field* fields = result->Fetch();
    uint32 id = fields[0].GetUInt32();
    std::string username = fields[1].GetString();
    std::string pass = fields[2].GetString();
    if (m_idAccountBotBase.find(id) == m_idAccountBotBase.end())
    {
        PlayerBotBaseInfo* pInfo = new PlayerBotBaseInfo(id, username.c_str(), pass, true);
        m_idAccountBotBase[id] = pInfo;
    }
}

void PlayerBotMgr::LoadPlayerBotBaseInfo()
{
    uint32 oldMSTime = getMSTime();

    ClearBaseInfo();
    if (DynamicBotsEnabled())
        sObjectMgr->EnsurePlayerBotAlliedInfo();
    QueryResult result = LoginDatabase.Query("SELECT id, username, sha_pass_hash FROM account");
    if (!result)
    {
        TC_LOG_INFO(LOG_FILTER_PLAYER, "server.loading", ">> LoadPlayerBot Find 0 account!");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 id = fields[0].GetUInt32();
        std::string username = fields[1].GetString();
        std::string pass = fields[2].GetString();
        if (sRegionalGuildMgr.ProtectedAccount(id)) continue;
        sOnlineMgr->AddNewAccount(id, username); // Real player acc and bot acc all in

        std::string lowerName = boost::algorithm::to_lower_copy(username);
        if (lowerName.compare(0, 7, "lf_dyn_") == 0)
        {
            std::string suffix = lowerName.substr(7);
            if (!suffix.empty() && suffix.size() <= 10 && suffix.find_first_not_of("0123456789") == std::string::npos)
            {
                uint64 numeric = 0;
                for (char digit : suffix) numeric = numeric * 10 + uint64(digit - '0');
                if (numeric < 0xFFFFFFFFu) m_dynamicNextAccount = std::max(m_dynamicNextAccount, uint32(numeric) + 1);
            }
        }
        if (IsBotAccuntName(lowerName))
        {
            sRegionalGuildMgr.RegisterBotAccount(id);
            if (m_idPlayerBotBase.find(id) == m_idPlayerBotBase.end())
            {
                PlayerBotBaseInfo* pInfo = new PlayerBotBaseInfo(id, username.c_str(), pass, false);
                m_idPlayerBotBase[id] = pInfo;
            }
            m_LastBotAccountIndex = id;
        }
        else
        {
            if (m_idAccountBotBase.find(id) == m_idAccountBotBase.end())
            {
                PlayerBotBaseInfo* pInfo = new PlayerBotBaseInfo(id, username.c_str(), pass, true);
                m_idAccountBotBase[id] = pInfo;
            }
        }
    } while (result->NextRow());

    if (!DynamicBotsEnabled())
        SupplementAccount();

    if (DynamicBotsEnabled())
        LoadDynamicAccounts();

    if (m_idPlayerBotBase.size() > 0 || m_idAccountBotBase.size() > 0)
        LoadCharBaseInfo();

    if (DynamicBotsEnabled())
        for (auto& entry : m_dynamicLeases)
        {
            auto account = m_idPlayerBotBase.find(entry.first);
            if (entry.second.guid && account != m_idPlayerBotBase.end())
            {
                auto character = account->second->characters.find(entry.second.guid);
                if (character != account->second->characters.end())
                    entry.second.targetLevel = character->second.level;
            }
            if (entry.second.guid && DynamicGroupHasHumanGuid(entry.second.guid))
            {
                // A persisted party keeps this identity through a restart.
                // It stays dormant until a party member is online.
                entry.second.purpose = 4;
                entry.second.retiring = false;
            }
            else if (!entry.second.guid)
            {
                QueryResult stray = CharacterDatabase.PQuery(
                    "SELECT guid FROM characters WHERE account = %u LIMIT 2", entry.first);
                if (stray && stray->GetRowCount() == 1)
                {
                    uint32 guid = stray->Fetch()[0].GetUInt32();
                    CharacterDatabase.DirectPExecute(
                        "UPDATE lf_dynamic_bot_owned SET guid = %u WHERE account = %u AND guid = 0", guid, entry.first);
                    QueryResult owned = CharacterDatabase.PQuery(
                        "SELECT guid FROM lf_dynamic_bot_owned WHERE account = %u", entry.first);
                    if (owned && owned->Fetch()[0].GetUInt32() == guid)
                    {
                        entry.second.guid = guid;
                        entry.second.creating = false;
                        entry.second.retiring = !DynamicGroupHasHumanGuid(guid);
                        if (!entry.second.retiring)
                        {
                            entry.second.purpose = 4;
                            auto character = account->second->characters.find(guid);
                            if (character != account->second->characters.end())
                                entry.second.targetLevel = character->second.level;
                        }
                    }
                    else
                        QuarantineDynamicAccount(entry.first, "recovery_ledger_update_failed");
                }
                else if (stray)
                {
                    QuarantineDynamicAccount(entry.first, "multiple_characters");
                }
            }
        }

    if (DynamicBotsEnabled())
        for (auto& entry : m_dynamicLeases)
        {
            DynamicLease& lease = entry.second;
            if (lease.quarantined || !lease.guid || lease.purpose == 4) continue;
            QueryResult identity = CharacterDatabase.PQuery(
                "SELECT COUNT(*),COALESCE(SUM(guid=%u),0) FROM characters WHERE account=%u", lease.guid, entry.first);
            if (identity && identity->Fetch()[0].GetUInt32() == 1 && identity->Fetch()[1].GetUInt32() == 1)
            {
                lease.retiring = false;
                lease.cached = true;
                lease.changedAt = getMSTime();
            }
        }

    TC_LOG_INFO(LOG_FILTER_PLAYER, "server.loading", ">> Loaded %u Player bot account base info in %u ms", m_idPlayerBotBase.size(), GetMSTimeDiffToNow(oldMSTime));
}

void PlayerBotMgr::LoadCharBaseInfo()
{
    QueryResult result = CharacterDatabase.Query("SELECT guid, account, name, race, class, gender, level FROM characters");

    if (!result)
    {
        TC_LOG_INFO(LOG_FILTER_PLAYER, "server.loading", ">> LoadPlayerBot Find 0 characters!");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint64 guid = fields[0].GetUInt64();
        uint32 accID = fields[1].GetUInt32();
        PlayerBotBaseInfo* pInfo = GetPlayerBotAccountInfo(accID);
        if (pInfo)
        {
            if (pInfo->characters.find(guid) == pInfo->characters.end())
            {
                std::string charName = fields[2].GetString();
                uint16 race = fields[3].GetInt16();
                uint16 pro = fields[4].GetInt16();
                uint16 gender = fields[5].GetInt16();
                uint16 level = fields[6].GetInt16();
                pInfo->characters.emplace(guid, PlayerBotCharBaseInfo{ guid, accID, charName, race, pro, gender, level });
                DestroyBotMail(guid);
            }
        }
        else if (pInfo = GetAccountBotAccountInfo(accID))
        {
            if (pInfo->characters.find(guid) == pInfo->characters.end())
            {
                std::string charName = fields[2].GetString();
                uint16 race = fields[3].GetInt16();
                uint16 pro = fields[4].GetInt16();
                uint16 gender = fields[5].GetInt16();
                uint16 level = fields[6].GetInt16();
                pInfo->characters.emplace(guid, PlayerBotCharBaseInfo{ guid, accID, charName, race, pro, gender, level });
                //DestroyBotMail(guid);
            }
        }
    } while (result->NextRow());
}

void PlayerBotMgr::UpAllPlayerBotSession()
{
    PlayerBotSetting::Initialize();
    if (DynamicBotsEnabled())
        return; // No idle legacy roster or prelogged dynamic reserve.
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator it = m_idPlayerBotBase.begin();
        it != m_idPlayerBotBase.end();
        it++)
    {
        PlayerBotBaseInfo* pInfo = it->second;
        UpPlayerBotSessionByBaseInfo(pInfo, false);
    }

    for (auto const& dynamic : m_dynamicLeases)
    {
        auto found = m_idPlayerBotBase.find(dynamic.first);
        if (found != m_idPlayerBotBase.end())
            UpPlayerBotSessionByBaseInfo(found->second, false);
    }
}

PlayerBotSession* PlayerBotMgr::UpPlayerBotSessionByBaseInfo(PlayerBotBaseInfo* pAcc, bool accountInfo)
{
    if (!pAcc)
        return NULL;
    auto pSession = sWorld->FindSession(pAcc->id).get();
    if (pSession)
        return NULL;
    std::string name = pAcc->username.c_str();
    auto pBotSession = std::shared_ptr<PlayerBotSession>(new PlayerBotSession{ pAcc->id, name, AccountTypes::SEC_GAMEMASTER, 2, 0, LocaleConstant::LOCALE_zhTW, 0, false });
    //pbSession->ReadAddonsInfo("");
    if (accountInfo)
        pBotSession->SetAccountBotSession();

    sWorld->AddSession(pBotSession);
    return pBotSession.get();
}

void PlayerBotMgr::OnPlayerBotCreate(ObjectGuid const& guid, uint32 accountId, std::string const& name,
    uint8 gender, uint8 race, uint8 playerClass, uint8 level, uint64 operation)
{
    {
        std::lock_guard<std::mutex> lock(m_dynamicLock);
        ReleasePendingBotName(name);
        auto dynamic = m_dynamicLeases.find(accountId);
        if (operation && (dynamic == m_dynamicLeases.end() || dynamic->second.operation != operation))
            return; // An old callback never mutates a newer reservation.
        PlayerBotBaseInfo* info = GetPlayerBotAccountInfo(accountId);
        if (!info) return;
        uint32 id = guid.GetGUIDLow();
        info->characters.emplace(id, PlayerBotCharBaseInfo(id, accountId, name,
            uint16(race), uint16(playerClass), uint16(gender), uint16(level)));
        if (dynamic != m_dynamicLeases.end() && dynamic->second.creating && !dynamic->second.guid)
        {
            // SaveToDB(true) queues a transaction. A callback is not commit proof.
            // Keep the operation occupied until the exact character is observed.
            dynamic->second.pendingGuid = id;
            dynamic->second.commitCheckedAt = getMSTime() - 501;
            if (FelsoriaDynamicMetrics::Enabled()) dynamic->second.perfCreatedAt = getMSTime();
        }
    }
    if (!DynamicBotsEnabled()) CreateOncePlayerBot();
}

bool PlayerBotMgr::DynamicBotsEnabled() const
{
    return sConfigMgr->GetBoolDefault("LegionForge.DynamicBots.Enable", false);
}

uint32 PlayerBotMgr::GetDynamicBotTargetLevel(uint32 accountId) const
{
    std::lock_guard<std::mutex> lock(m_dynamicLock);
    auto found = m_dynamicLeases.find(accountId);
    return found != m_dynamicLeases.end() ? found->second.targetLevel : 0;
}

bool PlayerBotMgr::IsPreservedDynamicAccount(uint32 accountId) const
{
    std::lock_guard<std::mutex> lock(m_dynamicLock);
    auto found = m_dynamicLeases.find(accountId);
    return found != m_dynamicLeases.end() && found->second.purpose == 4;
}

void PlayerBotMgr::OnDynamicBotSaveQueued(uint32 accountId, uint64 operation, FelsoriaDynamicLifecycle::Receipt receipt)
{
    std::lock_guard<std::mutex> lock(m_dynamicLock);
    auto found = m_dynamicLeases.find(accountId);
    if (found != m_dynamicLeases.end() && found->second.operation == operation)
        found->second.saveReceipt = std::move(receipt);
}

void PlayerBotMgr::OnDynamicBotCreateFailed(uint32 accountId, uint64 operation, bool cancelled)
{
    std::lock_guard<std::mutex> lock(m_dynamicLock);
    auto found = m_dynamicLeases.find(accountId);
    if (found != m_dynamicLeases.end() && (!operation || found->second.operation == operation))
    {
        if (!cancelled && FelsoriaDynamicMetrics::Enabled())
            FelsoriaDynamicMetrics::Get().createFailures.fetch_add(1, std::memory_order_relaxed);
        found->second.pendingGuid = 0;
        found->second.creating = false;
        found->second.changedAt = getMSTime();
    }
}

void PlayerBotMgr::OnDynamicBotQueueFailed(uint32 accountId)
{
    std::lock_guard<std::mutex> lock(m_dynamicLock);
    auto found = m_dynamicLeases.find(accountId);
    if (found != m_dynamicLeases.end() && (found->second.purpose == 2 || found->second.purpose == 3))
    {
        if (found->second.guid && (DynamicGroupHasHumanGuid(found->second.guid) ||
            (found->second.purpose == 2 &&
            sLFGMgr->IsDynamicBotInHumanActivity(
                ObjectGuid::Create<HighGuid::Player>(found->second.guid),
                ObjectGuid::Create<HighGuid::Player>(found->second.humanGuid), found->second.queueId))))
            return;
        if(found->second.purpose==3)FelsoriaBGRole::Registry().Cancel(accountId,getMSTime());
        found->second.retiring = true;
        found->second.changedAt = getMSTime();
        if (PlayerBotSession* bot = dynamic_cast<PlayerBotSession*>(sWorld->FindSession(accountId).get()))
            bot->RequestDynamicRetire();
    }
}

void PlayerBotMgr::PublishDynamicSnapshot(WorldSession* session)
{
    if (!session)
        return;
    Player* player = session->GetPlayer();
    if (player && !session->IsBotSession() && player->GetGroup())
        SyncDynamicPartyGroup(player->GetGroup(), true);
    FelsoriaDynamicMetrics::Scope perfSnapshot(FelsoriaDynamicMetrics::SnapshotWork);
    FelsoriaDynamicMetrics::Scope perfSnapshotWait(FelsoriaDynamicMetrics::SnapshotLockWait);
    std::lock_guard<std::mutex> lock(m_dynamicLock);
    perfSnapshotWait.Stop();
    if (!player)
    {
        m_dynamicSnapshots.erase(session->GetAccountId());
        return;
    }
    DynamicSnapshot& snapshot = m_dynamicSnapshots[session->GetAccountId()];
    uint32 now = getMSTime();
    if (now - snapshot.updated < 500 && snapshot.map == player->GetMapId() &&
        snapshot.zone == player->GetZoneId() && snapshot.area == player->GetAreaId() &&
        snapshot.instance == player->GetInstanceId() && snapshot.phase == player->GetPhaseMask() &&
        snapshot.phaseIds == player->GetPhases() && snapshot.level == player->getLevel() &&
        snapshot.group == (player->GetGroup() ? player->GetGroup()->GetGUIDLow() : 0) &&
        snapshot.groupSize == (player->GetGroup() ? player->GetGroup()->GetMembersCount() : 1))
        return;
    if (FelsoriaDynamicMetrics::Enabled() && !session->IsBotSession() &&
        (!snapshot.perfContextAt || snapshot.map != player->GetMapId() ||
            snapshot.zone != player->GetZoneId() || snapshot.instance != player->GetInstanceId() ||
            snapshot.phase != player->GetPhaseMask() || snapshot.phaseIds != player->GetPhases() ||
            snapshot.level != player->getLevel() ||
            ClassHallForArea(snapshot.map, snapshot.zone, snapshot.area) !=
                ClassHallForArea(player->GetMapId(), player->GetZoneId(), player->GetAreaId()) ||
            !SameAlliedPopulationArea(player->GetMapId(), player->GetZoneId(), snapshot.area, player->GetAreaId()) ||
            GetDynamicAreaRequiredClass(snapshot.map, snapshot.zone, snapshot.area) !=
                GetDynamicAreaRequiredClass(player->GetMapId(), player->GetZoneId(), player->GetAreaId())))
        snapshot.perfContextAt = now;
    snapshot.account = session->GetAccountId();
    snapshot.guid = player->GetGUIDLow();
    snapshot.map = player->GetMapId();
    snapshot.zone = player->GetZoneId();
    snapshot.area = player->GetAreaId();
    snapshot.phase = player->GetPhaseMask();
    snapshot.instance = player->GetInstanceId();
    snapshot.phaseIds = player->GetPhases();
    snapshot.level = player->getLevel();
    snapshot.klass = player->getClass();
    snapshot.race = player->getRace();
    snapshot.team = uint8(player->GetTeamId());
    snapshot.bot = session->IsBotSession();
    snapshot.dungeon = player->GetMap() && player->GetMap()->IsDungeon();
    snapshot.battleground = player->InBattleground() || player->InArena();
    snapshot.bgQueue = player->InBattlegroundQueue();
    snapshot.bgInvited = false;
    snapshot.originRealm = session->FelsoriaRealmFilterRequired() ? session->GetFelsoriaSelectedRealm() : realm.Id.Realm;
    snapshot.bgTeam = uint8(player->GetBGTeamId());
    Battleground* currentBG = player->GetBattleground();
    snapshot.normalBG = currentBG && FelsoriaRealmPolicy::NormalBG(currentBG->IsBattleground(),
        currentBG->IsArena(), currentBG->IsRated(), currentBG->IsWargame(), currentBG->IsBrawl());
    // FELSORIA_BG_ROLE_BALANCE_V1: publish actual roles on this session's owner thread.
    if (snapshot.normalBG)
        FelsoriaBGRole::Registry().ObserveActive(session->GetAccountId(),player->GetGUIDLow(),
            {uint32(currentBG->GetTypeID(true)),uint32(currentBG->GetBracketId()),currentBG->GetInstanceID(),
             uint32(player->GetBGTeamId()),currentBG->GetMaxPlayersPerTeam()},
            FelsoriaBGRole::ObservedMask(player->GetSpecializationRole(),session->IsBotSession()),session->IsBotSession(),now);
    else if (!snapshot.bgQueue && !player->IsBeingTeleported())
        FelsoriaBGRole::Registry().Idle(session->GetAccountId());
    snapshot.normalBGQueues.clear();
    for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
    {
        uint8 queue = player->GetBattlegroundQueueTypeId(i);
        if (queue && player->IsInvitedForBattlegroundQueueType(queue)) snapshot.bgInvited = true;
        if (queue)
            for (BattlegroundTypeId type : m_BGTypes)
            {
                Battleground* templ = sBattlegroundMgr->GetBattlegroundTemplate(type);
                if (templ && FelsoriaRealmPolicy::NormalBG(templ->IsBattleground(), templ->IsArena(),
                        templ->IsRated(), templ->IsWargame(), templ->IsBrawl()) &&
                    queue == MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(type, 0))
                {
                    snapshot.normalBGQueues[queue] = player->GetBattlegroundQueueJoinTime(queue);
                    if(!snapshot.battleground&&!player->IsInvitedForBattlegroundQueueType(queue))
                        if(auto*bracket=sDB2Manager.GetBattlegroundBracketByLevel(templ->GetMapId(),player->getLevel())){
                            unsigned mask=player->IsPlayerBot()?FelsoriaBGRole::Mask(player->GetSpecializationRole()):player->GetQueueRoleMask(bracket->RangeIndex);
                            if(FelsoriaBGRole::Role(mask)>2)mask=FelsoriaBGRole::ObservedMask(player->GetSpecializationRole(),player->IsPlayerBot());
                            FelsoriaBGRole::Registry().Queue(session->GetAccountId(),player->GetGUIDLow(),
                                {uint32(type),uint32(bracket->RangeIndex),0,uint32(player->GetTeamId()),templ->GetMaxPlayersPerTeam()},mask,player->IsPlayerBot(),now);
                        }
                }
            }
    }
    snapshot.lfgQueue = player->isUsingLfg();
    snapshot.busy = player->isInCombat() || player->IsBeingTeleported() ||
        snapshot.battleground || snapshot.bgQueue || snapshot.lfgQueue;
    snapshot.ready = player->IsInWorld() && !player->IsBeingTeleported() &&
        (!snapshot.bot || player->IsSettingFinish());
    snapshot.preparationFailed = snapshot.bot && player->m_PlayerBotSetting &&
        player->m_PlayerBotSetting->IsPreparationFailed();
    if (FelsoriaDynamicMetrics::Enabled() && snapshot.bot && snapshot.ready)
    {
        auto diagnostic = m_dynamicLeases.find(session->GetAccountId());
        if (diagnostic != m_dynamicLeases.end() && !diagnostic->second.perfReadyAt)
            diagnostic->second.perfReadyAt = now;
    }
    if (!snapshot.bot)
        BotAIHumanTrail::Record(player);
    snapshot.group = player->GetGroup() ? player->GetGroup()->GetGUIDLow() : 0;
    snapshot.groupSize = player->GetGroup() ? player->GetGroup()->GetMembersCount() : 1;
    snapshot.nativeGroup = player->GetGroup() ? player->GetGroup()->GetGUID() : ObjectGuid::Empty;
    snapshot.groupLeader = player->GetGroup() ? player->GetGroup()->GetLeaderGUID().GetGUIDLow() : 0;
    snapshot.lfgGroup = player->GetGroup() && player->GetGroup()->isLFGGroup();
    snapshot.groupMembers.clear();
    if (Group* group = player->GetGroup())
        for (Group::MemberSlot const& member : group->GetMemberSlots())
            snapshot.groupMembers.push_back(member.Guid.GetCounter());
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(snapshot.zone);
    snapshot.territory = area ? area->FactionGroupMask : 0;
    snapshot.x = player->GetPositionX();
    snapshot.y = player->GetPositionY();
    snapshot.z = player->GetPositionZ();
    snapshot.placements.clear();
    snapshot.placementValid = false;
    snapshot.placementArea = 0;
    uint32 const worldLimit = uint32(std::max(0,
        sConfigMgr->GetIntDefault("LegionForge.DynamicBots.WorldLimit", 50)));
    uint32 worldLeases = 0;
    for (auto const& active : m_dynamicLeases)
        if (active.second.purpose == 1 && !active.second.retiring &&
            active.second.humanGuid == snapshot.guid && active.second.targetMap == snapshot.map &&
            active.second.targetZone == snapshot.zone && active.second.targetInstance == snapshot.instance)
            ++worldLeases;
    bool const needPlacement = worldLeases < worldLimit;
    if (needPlacement && !snapshot.bot && player->GetMap() &&
        (!snapshot.dungeon || AlliedDedicatedStarterRace(snapshot.map) ||
            GetDynamicAreaRequiredClass(snapshot.map, snapshot.zone, snapshot.area)) &&
        !snapshot.battleground && player->IsInWorld() && !player->IsBeingTeleported())
    {
        FelsoriaDynamicMetrics::Scope perfPositions(FelsoriaDynamicMetrics::SpawnPositionWork);
        std::vector<WorldSpawnAnchor> const& anchors = ZoneSpawnAnchors(snapshot.map, snapshot.zone);
        uint8 const humanHall = ClassHallForArea(snapshot.map, snapshot.zone, snapshot.area);
        // Randomize the first anchor, then try a bounded number of alternatives.
        // This distributes bots throughout the zone even when humans stand still.
        if (!anchors.empty())
        {
            size_t const first = urand(0, uint32(anchors.size() - 1));
            std::vector<size_t> order;
            for (size_t n = 0; n < anchors.size(); ++n) order.push_back((first + n) % anchors.size());
            if (worldLeases < 8)
                std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
                {
                    bool loadedA = player->GetMap()->IsGridLoaded(anchors[a].x, anchors[a].y);
                    bool loadedB = player->GetMap()->IsGridLoaded(anchors[b].x, anchors[b].y);
                    if (loadedA != loadedB) return loadedA;
                    float ax = anchors[a].x - snapshot.x, ay = anchors[a].y - snapshot.y;
                    float bx = anchors[b].x - snapshot.x, by = anchors[b].y - snapshot.y;
                    return ax * ax + ay * ay < bx * bx + by * by;
                });
            uint32 searchStarted = getMSTime();
            uint32 const searchBudget = uint32(std::max(1, std::min(20,
                sConfigMgr->GetIntDefault("LegionForge.DynamicBots.SpawnSearchBudgetMs", 4))));
            uint8 terrainChecks = 0;
            uint32 const spawnBatch = uint32(std::max(1, std::min(8,
                sConfigMgr->GetIntDefault("LegionForge.DynamicBots.SpawnBatch", 8))));
            for (size_t attempt = 0; attempt < 96 && snapshot.placements.size() < spawnBatch; ++attempt)
            {
                if (attempt && uint32(getMSTime() - searchStarted) >= searchBudget) break;
                WorldSpawnAnchor const& anchor = anchors[order[attempt % order.size()]];
                if (!(anchor.phaseMask & snapshot.phase))
                    continue;
                if (!anchor.phaseIds.empty() && std::none_of(anchor.phaseIds.begin(), anchor.phaseIds.end(),
                        [&](uint32 id) { return snapshot.phaseIds.count(id) != 0; }))
                    continue;
                // Filter by authored hall region before loading a remote grid.
                // Live area checks below remain authoritative.
                if (anchor.area && ClassHallForArea(snapshot.map, snapshot.zone, anchor.area) != humanHall)
                    continue;
                if (++terrainChecks > 24)
                    break;
                float const angle = frand(0.0f, 6.2831853f);
                // Several walkable positions per anchor: the old 3 m ring with
                // 12 m exclusion permitted only one bot per usable anchor.
                float const radius = frand(0.0f, 24.0f);
                float const x = anchor.x + radius * std::cos(angle);
                float const y = anchor.y + radius * std::sin(angle);
                float const z = anchor.z;
                // The authored area may be stale. Resolve the live zone and area
                // against the current map, phase and terrain before choosing it.
                float ground = player->GetMap()->GetHeight(snapshot.phaseIds, x, y, z + 2.0f);
                if (!std::isfinite(ground) || std::abs(ground - z) > 2.5f ||
                    !MapManager::IsValidMapCoord(snapshot.map, x, y, ground, anchor.o) ||
                    player->GetMap()->IsInWater(x, y, ground))
                    continue;
                uint32 const candidateZone = player->GetMap()->GetZoneId(x, y, ground);
                if (candidateZone != snapshot.zone)
                    continue;
                uint32 const candidateArea = player->GetMap()->GetAreaId(x, y, ground);
                if (!SameAlliedPopulationArea(snapshot.map, snapshot.zone, candidateArea, snapshot.area) ||
                    ClassHallForArea(snapshot.map, candidateZone, candidateArea) != humanHall ||
                    GetDynamicAreaRequiredClass(snapshot.map, candidateZone, candidateArea) !=
                        GetDynamicAreaRequiredClass(snapshot.map, snapshot.zone, snapshot.area) ||
                    !WalkableSpawnAnchor(snapshot.map, x, y, ground))
                    continue;
                bool occupied = false;
                for (auto const& active : m_dynamicLeases)
                    if (active.second.purpose == 1 && !active.second.retiring &&
                        SameSpawnNeighborhood(snapshot.map, snapshot.instance, x, y,
                            active.second.targetMap, active.second.targetInstance,
                            active.second.x, active.second.y))
                    {
                        occupied = true;
                        break;
                    }
                for (auto const& point : snapshot.placements)
                    if (SameSpawnNeighborhood(snapshot.map, snapshot.instance, x, y,
                            snapshot.map, snapshot.instance, point.x, point.y))
                        occupied = true;
                if (occupied)
                    continue;
                snapshot.placementX = x;
                snapshot.placementY = y;
                snapshot.placementZ = ground;
                snapshot.placementO = anchor.o;
                snapshot.placementArea = candidateArea;
                snapshot.placementValid = true;
                snapshot.placements.push_back({x, y, ground, anchor.o, candidateArea});
            }
        }
    }
    if (needPlacement && !snapshot.bot && !snapshot.placementValid &&
        (!snapshot.lastPlacementWarning || now - snapshot.lastPlacementWarning >= 60000))
    {
        TC_LOG_INFO(LOG_FILTER_PLAYER,
            "DYNAMIC_BOT_NO_SAFE_SPAWN map=%u zone=%u area=%u (check anchors, phase, terrain and mmaps)",
            snapshot.map, snapshot.zone, snapshot.area);
        snapshot.lastPlacementWarning = now;
    }
    snapshot.updated = now;
}

// The characters DB ledger and a dedicated account prefix must BOTH match.
// Existing playerbot accounts and characters are never eligible for deletion.
void PlayerBotMgr::LoadDynamicAccounts()
{
    TC_LOG_INFO(LOG_FILTER_PLAYER, "LegionForge DynamicBots Update6 active");
    TC_LOG_INFO(LOG_FILTER_PLAYER,
        "FELSORIA_PARTY_LOGOUT_V4_ACTIVE built=%s %s party_quota_exempt=1 world=48/2 party_reserve_min=10 native_membership=1 staged_logout=1 human_account_detection=1",
        __DATE__, __TIME__);
    QueryResult ledger = CharacterDatabase.Query("SELECT account, guid FROM lf_dynamic_bot_owned");
    if (!ledger)
    {
        // An empty table also returns no rows. New accounts may be created by
        // CreateDynamicAccount only after verifying the table exists there.
        return;
    }
    do
    {
        Field* fields = ledger->Fetch();
        uint32 account = fields[0].GetUInt32();
        uint32 guid = fields[1].GetUInt32();
        auto source = m_idAccountBotBase.find(account);
        if (source == m_idAccountBotBase.end())
            continue;
        PlayerBotBaseInfo* info = source->second;
        std::string lower = boost::algorithm::to_lower_copy(info->username);
        if (lower.compare(0, 7, "lf_dyn_") != 0)
            continue;
        sRegionalGuildMgr.RegisterBotAccount(account);
        m_idAccountBotBase.erase(source);
        info->isAccountInfo = false;
        m_idPlayerBotBase[account] = info;
        DynamicLease& lease = m_dynamicLeases[account];
        lease.guid = guid;
        lease.retiring = guid != 0; // Crash recovery: retire only ledger-owned GUID.
        // An interrupted create can leave a character with ledger GUID zero.
        // Park that account; never create a second character in it.
        lease.creating = !guid && !info->characters.empty();
        uint32 suffix = uint32(atoi(lower.c_str() + 7));
        m_dynamicNextAccount = std::max(m_dynamicNextAccount, suffix + 1);
    } while (ledger->NextRow());
}

// FELSORIA_DYNAMIC_CAPACITY_V1: caller owns m_dynamicLock (or startup is single-threaded).
uint32 PlayerBotMgr::DynamicUsableAccountCount() const
{
    uint32 count = 0;
    for (auto const& entry : m_dynamicLeases)
        if (!entry.second.quarantined && entry.second.purpose != 4)
            ++count;
    return count;
}

void PlayerBotMgr::QuarantineDynamicAccount(uint32 account, char const* reason)
{
    auto found = m_dynamicLeases.find(account);
    if (found == m_dynamicLeases.end() || found->second.quarantined)
        return;
    DynamicLease& lease = found->second;
    if(lease.purpose==3)FelsoriaBGRole::Registry().Cancel(account,getMSTime());
    lease.quarantined = true;
    if (!lease.recoverable) lease.creating = false;
    lease.retiring = false;
    lease.purpose = 0;
    lease.placed = false;
    lease.queueScheduled = false;
    TC_LOG_ERROR(LOG_FILTER_PLAYER,
        "DYNAMIC_BOT_RECOVERY_QUARANTINE account=%u guid=%u reason=%s capacity_released=1",
        account, lease.guid, reason);
}

void PlayerBotMgr::CreateDynamicAccount()
{
    uint32 now = getMSTime();
    if (m_dynamicProvisionAt && uint32(now - m_dynamicProvisionAt) < 1000) return;
    if (m_dynamicProvisionName.empty() && DynamicUsableAccountCount() >= uint32(std::max(0,
        int(m_dynamicWorldCapacity) + sConfigMgr->GetIntDefault("LegionForge.DynamicBots.WorldLimit", 50) +
        sConfigMgr->GetIntDefault("LegionForge.DynamicBots.InstanceLimit", 100) +
        std::max(10, sConfigMgr->GetIntDefault("LegionForge.DynamicBots.PartyReserve", 10))))) return;
    if (!CharacterDatabase.Query("SHOW TABLES LIKE 'lf_dynamic_bot_owned'")) return;
    m_dynamicProvisionAt = now;
    if (m_dynamicProvisionName.empty())
    {
        m_dynamicProvisionName = "LF_DYN_" + std::to_string(m_dynamicNextAccount++);
        m_dynamicProvisionPassword = "dyn" + std::to_string(urand(100000000, 999999999));
    }
    if (!m_dynamicProvisionCreated)
    {
        if (AccountMgr::CreateAccount(m_dynamicProvisionName, m_dynamicProvisionPassword, false) != AccountOpResult::AOR_OK)
        { m_dynamicProvisionName.clear(); m_dynamicProvisionPassword.clear(); return; }
        m_dynamicProvisionCreated = true;
    }
    if (!m_dynamicProvisionAccount) m_dynamicProvisionAccount = AccountMgr::GetId(m_dynamicProvisionName);
    if (!m_dynamicProvisionAccount) return;
    uint32 account = m_dynamicProvisionAccount;
    CharacterDatabase.DirectPExecute("INSERT IGNORE INTO lf_dynamic_bot_owned (account, guid) VALUES (%u, 0)", account);
    QueryResult owned = CharacterDatabase.PQuery("SELECT guid FROM lf_dynamic_bot_owned WHERE account = %u", account);
    QueryResult empty = CharacterDatabase.PQuery("SELECT COUNT(*) FROM characters WHERE account = %u", account);
    if (!owned || !empty) return; // Retry THIS authenticated account, not an unbounded series of new ones.
    if (owned->Fetch()[0].GetUInt32() != 0 || empty->Fetch()[0].GetUInt32() != 0)
    {
        TC_LOG_ERROR(LOG_FILTER_PLAYER, "DYNAMIC_PROVISION_CONFLICT account=%u preserved=1", account);
        m_dynamicProvisionName.clear(); m_dynamicProvisionPassword.clear();
        m_dynamicProvisionAccount = 0; m_dynamicProvisionCreated = false;
        return;
    }
    sRegionalGuildMgr.RegisterBotAccount(account);
    auto info = new PlayerBotBaseInfo(account, m_dynamicProvisionName.c_str(), m_dynamicProvisionPassword, false);
    m_idPlayerBotBase[account] = info;
    m_dynamicLeases[account] = DynamicLease();
    sOnlineMgr->AddNewAccount(account, m_dynamicProvisionName);
    UpPlayerBotSessionByBaseInfo(info, false);
    m_dynamicProvisionName.clear(); m_dynamicProvisionPassword.clear();
    m_dynamicProvisionAccount = 0; m_dynamicProvisionCreated = false; m_dynamicProvisionAt = 0;
}

// m_idAccountBotBase is the catalog of ALL ordinary player accounts.
// Its presence alone never proves that a character is controlled by a bot.
// Dedicated bot accounts and an actual bot session are the bot evidence.
bool PlayerBotMgr::DynamicPartyMemberIsHuman(uint32 guid) const
{
    if (!guid) return false;
    for (auto const& info : m_idPlayerBotBase)
        if (info.second->characters.count(guid)) return false;
    for (auto const& info : m_idAccountBotBase)
        if (info.second->characters.count(guid))
        {
            auto session = sWorld->FindSession(info.first);
            if (session && session->GetPlayer() && session->GetPlayer()->GetGUIDLow() == guid)
                return !session->IsBotSession();
            return true; // An offline ordinary-account character retains ownership.
        }
    return true; // Unknown ownership is not permission to retire the identity.
}

bool PlayerBotMgr::DynamicGroupHasHuman(Player const* player) const
{
    if (!player || !player->GetGroup())
        return false;
    std::lock_guard<std::mutex> lock(m_dynamicLock);
    for (Group::MemberSlot const& slot : player->GetGroup()->GetMemberSlots())
        if (DynamicPartyMemberIsHuman(slot.Guid.GetCounter()))
            return true;
    return false;
}

// FELSORIA_PARTY_LOGOUT_V4: native group hooks own these value-only records.
void PlayerBotMgr::SyncDynamicPartyGroup(Group const* group, bool initialOnly)
{
    if (!DynamicBotsEnabled() || !group) return;
    if (group->isBGGroup() || group->isBFGroup() || group->isLFGGroup())
    {
        m_dynamicPartyRegistry.Forget(group->GetGUID().GetCounter());
        return;
    }
    if (initialOnly && m_dynamicPartyRegistry.Contains(group->GetGUID().GetCounter())) return;
    std::vector<uint32> members;
    std::map<uint32, uint32> humans;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        members.push_back(slot.Guid.GetCounter());
    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->getSource())
            if (WorldSession* session = member->GetSession())
                if (!session->IsBotSession() &&
                    std::find(members.begin(), members.end(), member->GetGUIDLow()) != members.end())
                    humans[member->GetGUIDLow()] = session->GetAccountId();
    if (m_dynamicPartyRegistry.Observe(group->GetGUID().GetCounter(), members, humans))
        TC_LOG_INFO(LOG_FILTER_PLAYER,
            "DYNAMIC_PARTY_MEMBERSHIP group=%u members=%u connected_humans=%u",
            group->GetGUIDLow(), uint32(members.size()), uint32(humans.size()));
}

void PlayerBotMgr::RemoveDynamicPartyMember(ObjectGuid const& group, ObjectGuid const& member)
{
    if (m_dynamicPartyRegistry.Remove(group.GetCounter(), member.GetCounter()))
        TC_LOG_INFO(LOG_FILTER_PLAYER, "DYNAMIC_PARTY_RELEASE_MEMBER group=%u guid=%u",
            group.GetGUIDLow(), member.GetGUIDLow());
}

void PlayerBotMgr::ForgetDynamicPartyGroup(ObjectGuid const& group)
{
    if (m_dynamicPartyRegistry.Close(group.GetCounter()))
        TC_LOG_INFO(LOG_FILTER_PLAYER, "DYNAMIC_PARTY_RELEASE_GROUP group=%u", group.GetGUIDLow());
}

bool PlayerBotMgr::IsDynamicPartyMemberProtected(ObjectGuid const& group, ObjectGuid const& member) const
{
    return m_dynamicPartyRegistry.Protects(member.GetCounter(), group.GetCounter());
}

bool PlayerBotMgr::IsRememberedDynamicParty(uint32 guid) const
{
    return m_dynamicPartyRegistry.Protects(guid);
}

bool PlayerBotMgr::IsRememberedDynamicPartyOwner(uint32 member, uint32 human) const
{
    return m_dynamicPartyRegistry.Owners(member).count(human) != 0;
}

bool PlayerBotMgr::RememberedDynamicPartyConnected(uint32 guid) const
{
    for (auto const& owner : m_dynamicPartyRegistry.Owners(guid))
    {
        auto session = sWorld->FindSession(owner.second);
        if (session && session->GetPlayer() && session->GetPlayer()->GetGUIDLow() == owner.first)
            return true;
    }
    return false;
}

bool PlayerBotMgr::IsDynamicBotPartyProtected(Player const* player) const
{
    return (player && IsRememberedDynamicParty(player->GetGUIDLow())) || DynamicGroupHasHuman(player);
}

bool PlayerBotMgr::IsDynamicBotPartyHumanConnected(Player const* player) const
{
    if (player && RememberedDynamicPartyConnected(player->GetGUIDLow())) return true;
    if (!player || !player->GetGroup()) return false;
    for (GroupReference const* ref = player->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->getSource();
        if (member && member->GetSession() && !member->GetSession()->IsBotSession() &&
            member->GetSession()->GetPlayer() == member)
            return true; // No IsInWorld gate: teleport loading is connected.
    }
    return false;
}

bool PlayerBotMgr::IsDynamicBotHumanActivityProtected(uint32 accountId) const
{
    std::lock_guard<std::mutex> lock(m_dynamicLock);
    auto found = m_dynamicLeases.find(accountId);
    if (found == m_dynamicLeases.end() || !found->second.guid)
        return false;
    DynamicLease const& lease = found->second;
    if (lease.purpose == 2 && lease.humanGuid)
        for (auto const& entry : m_dynamicSnapshots)
        {
            DynamicSnapshot const& snapshot = entry.second;
            if (snapshot.bot || snapshot.guid != lease.humanGuid ||
                getMSTime() - snapshot.updated >= 60000)
                continue;
            auto session = sWorld->FindSession(snapshot.account);
            if (session && session->GetPlayer() &&
                sLFGMgr->IsDynamicBotInHumanActivity(
                    ObjectGuid::Create<HighGuid::Player>(lease.guid),
                    ObjectGuid::Create<HighGuid::Player>(lease.humanGuid), lease.queueId))
                return true;
        }
    return DynamicGroupHasConnectedHumanGuid(lease.guid);
}

bool PlayerBotMgr::DynamicHumanIsConnectedGuid(uint32 guid) const
{
    for (auto const& entry : m_dynamicSnapshots)
    {
        DynamicSnapshot const& snapshot = entry.second;
        if (snapshot.bot || snapshot.guid != guid || getMSTime() - snapshot.updated >= 60000)
            continue;
        auto session = sWorld->FindSession(snapshot.account);
        if (session && session->GetPlayer())
            return true; // Loading is connected even while IsInWorld is false.
    }
    return false;
}

// FELSORIA_PARTY_QUOTA_V1: no raw Player pointers are retained in snapshots.
Group* PlayerBotMgr::FindDynamicPartyGroup(uint32 guid, bool& unresolved) const
{
    unresolved = false;
    if (!guid) return nullptr;
    ObjectGuid const member = ObjectGuid::Create<HighGuid::Player>(guid);
    for (auto const& entry : m_dynamicSnapshots)
    {
        DynamicSnapshot const& snapshot = entry.second;
        if (!snapshot.nativeGroup || std::find(snapshot.groupMembers.begin(),
                snapshot.groupMembers.end(), guid) == snapshot.groupMembers.end()) continue;
        Group* group = sGroupMgr->GetGroupByGUID(snapshot.nativeGroup);
        if (group && group->IsMember(member)) return group;
    }
    // COUNT always yields a row when the query succeeds. A failed read is
    // never evidence that the player left the party, including after restart.
    QueryResult membership = CharacterDatabase.PQuery(
        "SELECT COUNT(*),COALESCE(MAX(guid),0) FROM group_member WHERE memberGuid=%u", guid);
    if (!membership) { unresolved = true; return nullptr; }
    if (!membership->Fetch()[0].GetUInt32()) return nullptr;
    Group* group = sGroupMgr->GetGroupByDbStoreId(membership->Fetch()[1].GetUInt32());
    unresolved = !group;
    return group;
}

bool PlayerBotMgr::DynamicGroupHasConnectedHumanGuid(uint32 guid) const
{
    if (RememberedDynamicPartyConnected(guid)) return true;
    bool unresolved = false;
    Group* group = FindDynamicPartyGroup(guid, unresolved);
    if (!group) return false;
    for (auto const& entry : m_dynamicSnapshots)
    {
        DynamicSnapshot const& human = entry.second;
        if (human.bot || !group->IsMember(ObjectGuid::Create<HighGuid::Player>(human.guid))) continue;
        auto session = sWorld->FindSession(human.account);
        // No snapshot age limit: loading/teleporting is still connected.
        if (session && session->GetPlayer() && session->GetPlayer()->GetGUIDLow() == human.guid)
            return true;
    }
    return false;
}

bool PlayerBotMgr::DynamicGroupHasHumanGuid(uint32 guid) const
{
    if (IsRememberedDynamicParty(guid)) return true;
    bool unresolved = false;
    Group* group = FindDynamicPartyGroup(guid, unresolved);
    if (!group) return unresolved;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (DynamicPartyMemberIsHuman(slot.Guid.GetCounter()))
            return true;
    return false;
}

void PlayerBotMgr::ProtectDynamicPartyLeases(uint32 now)
{
    for (auto& entry : m_dynamicLeases)
    {
        DynamicLease& lease = entry.second;
        if (!lease.guid || (lease.quarantined && !lease.recoverable)) continue;
        bool candidate = lease.purpose == 4 || lease.retiring || lease.quarantined || IsRememberedDynamicParty(lease.guid);
        for (auto const& item : m_dynamicSnapshots)
            if (std::find(item.second.groupMembers.begin(), item.second.groupMembers.end(),
                    lease.guid) != item.second.groupMembers.end()) { candidate = true; break; }
        if (!candidate || !DynamicGroupHasHumanGuid(lease.guid)) continue;
        // Active RDF/BG jobs retain their queue/role preparation. Their final
        // retirement and session logout paths still enforce party protection.
        if (lease.purpose != 1 && lease.purpose != 4 && !lease.retiring && !lease.quarantined) continue;
        bool const first = lease.purpose != 4 || lease.retiring || lease.quarantined;
        if(lease.purpose==3)FelsoriaBGRole::Registry().Cancel(entry.first,now);
        lease.purpose = 4;
        lease.retiring = false;
        lease.quarantined = false;
        lease.recoverable = false;
        lease.cached = false;
        lease.discard = false;
        lease.retireStartedAt = 0;
        lease.queueScheduled = false;
        lease.placed = false;
        if (DynamicGroupHasConnectedHumanGuid(lease.guid))
        {
            lease.lastHumanGroupSeen = now;
            if (PlayerBotSession* bot = dynamic_cast<PlayerBotSession*>(sWorld->FindSession(entry.first).get()))
                bot->RequestDynamicPartyProtection();
        }
        if (first) TC_LOG_INFO(LOG_FILTER_PLAYER,
            "DYNAMIC_PARTY_PROTECTED account=%u guid=%u quota_exempt=1", entry.first, lease.guid);
    }
}

void PlayerBotMgr::RetireDynamicCharacter(uint32 account)
{
    auto found = m_dynamicLeases.find(account);
    if (found == m_dynamicLeases.end() || found->second.quarantined || found->second.retiring)
        return;
    if (found->second.guid && DynamicGroupHasHumanGuid(found->second.guid))
        return;
    if (found->second.purpose == 2 && found->second.guid &&
        sLFGMgr->IsDynamicBotInHumanActivity(
            ObjectGuid::Create<HighGuid::Player>(found->second.guid),
            ObjectGuid::Create<HighGuid::Player>(found->second.humanGuid), found->second.queueId))
        return;
    WorldSession* session = sWorld->FindSession(account).get();
    if(found->second.purpose==3)FelsoriaBGRole::Registry().Cancel(account,getMSTime());
    found->second.retiring = true;
    found->second.changedAt = getMSTime();
    if (PlayerBotSession* bot = dynamic_cast<PlayerBotSession*>(session))
    {
        if (found->second.purpose == 2 || (found->second.purpose == 4 && found->second.dungeonCount))
            bot->RequestDynamicRDFRetire();
        else
            bot->RequestDynamicRetire();
    }
}

void PlayerBotMgr::FinishDynamicRetire(uint32 account)
{
    if (sRegionalGuildMgr.ProtectedAccount(account)) return;
    FelsoriaDynamicMetrics::Scope perfCleanup(FelsoriaDynamicMetrics::RetireCleanup);
    DynamicLease& lease = m_dynamicLeases[account];
    if (lease.quarantined) return;
    if (lease.guid && DynamicGroupHasHumanGuid(lease.guid))
    {
        lease.retiring = false;
        lease.purpose = 4;
        lease.cached = false;
        return;
    }
    PlayerBotSession* session = dynamic_cast<PlayerBotSession*>(sWorld->FindSession(account).get());
    if (!session)
    {
        auto info = m_idPlayerBotBase.find(account);
        if (info != m_idPlayerBotBase.end()) UpPlayerBotSessionByBaseInfo(info->second, false);
        return;
    }
    if (!session->DynamicRetireComplete())
    {
        if (uint32(getMSTime() - lease.changedAt) >= 2000)
        { session->RequestDynamicRetire(); lease.changedAt = getMSTime(); }
        return;
    }
    // Logout is not a database barrier: older saves can be on other workers.
    // Wait for EVERY full character save before cache reuse or deletion.
    if (lease.creating || session->DynamicOperationBusy() || session->DynamicWritesPending() || (lease.writes && lease.writes->load())) return;
    if (lease.guid && (DynamicGroupHasHumanGuid(lease.guid) ||
        (lease.purpose == 2 && sLFGMgr->IsDynamicBotInHumanActivity(
            ObjectGuid::Create<HighGuid::Player>(lease.guid),
            ObjectGuid::Create<HighGuid::Player>(lease.humanGuid), lease.queueId))))
    { lease.retiring = false; lease.deleteSubmitted = false; return; }
    QueryResult state = CharacterDatabase.PQuery(
        "SELECT COUNT(*),COALESCE(SUM(guid=%u),0),COALESCE(MAX(level),0) FROM characters WHERE account=%u", lease.guid, account);
    if (!state) return; // Aggregate SELECT distinguishes an empty account from a failed read.
    uint32 count = state->Fetch()[0].GetUInt32(), exact = state->Fetch()[1].GetUInt32();
    if (!lease.guid)
    {
        if (count) { QuarantineDynamicAccount(account, "unowned_characters_on_retire"); return; }
        lease = DynamicLease();
        return;
    }
    QueryResult owner = CharacterDatabase.PQuery("SELECT guid FROM lf_dynamic_bot_owned WHERE account=%u", account);
    if (!owner) return;
    if (lease.originReleased && owner->Fetch()[0].GetUInt32() == 0)
    {
        if (!count) lease = DynamicLease();
        else QuarantineDynamicAccount(account, "characters_after_ledger_clear");
        return;
    }
    if (owner->Fetch()[0].GetUInt32() != lease.guid) return;
    if (count && (count != 1 || exact != 1))
    { QuarantineDynamicAccount(account, "retire_identity_mismatch"); return; }
    if (count == 1 && lease.purpose == 1 && lease.placed && !lease.discard &&
        session->DynamicLogoutSaveStatus() == 1 && state->Fetch()[2].GetUInt32() == lease.targetLevel &&
        sConfigMgr->GetIntDefault("LegionForge.DynamicBots.WorldCacheLimit", 50) > 0)
    {
        auto info = m_idPlayerBotBase.find(account);
        if (info != m_idPlayerBotBase.end())
        {
            auto character = info->second->characters.find(lease.guid);
            if (character != info->second->characters.end()) character->second.level = uint16(lease.targetLevel);
        }
        uint32 guid = lease.guid;
        lease = DynamicLease();
        lease.guid = guid;
        lease.cached = true;
        lease.changedAt = getMSTime();
        return;
    }
    if (count)
    {
        if (!lease.deleteSubmitted || uint32(getMSTime() - lease.changedAt) >= 60000)
        {
            Player::DeleteFromDB(ObjectGuid::Create<HighGuid::Player>(lease.guid), account, true, true);
            lease.deleteSubmitted = true;
            lease.changedAt = getMSTime();
        }
        return;
    }
    QueryResult global = CharacterDatabase.PQuery("SELECT COUNT(*) FROM characters WHERE guid=%u", lease.guid);
    if (!global) return;
    if (global->Fetch()[0].GetUInt32())
    { QuarantineDynamicAccount(account, "retire_owner_mismatch"); return; }
    if (!sWorld->ReleaseFelsoriaDynamicBotOrigin(lease.guid, account)) return;
    sWorld->DeleteCharacterNameData(lease.guid);
    auto info = m_idPlayerBotBase.find(account);
    if (info != m_idPlayerBotBase.end()) info->second->characters.erase(lease.guid);
    lease.originReleased = true;
    CharacterDatabase.DirectPExecute("UPDATE lf_dynamic_bot_owned SET guid=0 WHERE account=%u AND guid=%u", account, lease.guid);
    QueryResult cleared = CharacterDatabase.PQuery("SELECT guid FROM lf_dynamic_bot_owned WHERE account=%u", account);
    QueryResult remaining = CharacterDatabase.PQuery("SELECT COUNT(*) FROM characters WHERE account=%u", account);
    if (!cleared || !remaining || cleared->Fetch()[0].GetUInt32() != 0) return;
    if (remaining->Fetch()[0].GetUInt32())
    { lease.guid = 0; QuarantineDynamicAccount(account, "characters_remain_after_retire"); return; }
    lease = DynamicLease();
}

// FELSORIA_DYNAMIC_FAST_V1: bounded commit observation and late-result recovery.
void PlayerBotMgr::RefreshDynamicCreates(uint32 now)
{
    uint32 checks = 0;
    if (m_dynamicLeases.empty()) return;
    auto cursor = m_dynamicLeases.upper_bound(m_dynamicCommitCursor);
    if (cursor == m_dynamicLeases.end()) cursor = m_dynamicLeases.begin();
    for (size_t visited = 0; visited < m_dynamicLeases.size() && checks < 4; ++visited)
    {
        auto item = cursor++;
        if (cursor == m_dynamicLeases.end()) cursor = m_dynamicLeases.begin();
        DynamicLease& lease = item->second;
        if (lease.purpose == 4 && lease.guid && DynamicGroupHasHumanGuid(lease.guid)) continue;
        PlayerBotSession* session = dynamic_cast<PlayerBotSession*>(sWorld->FindSession(item->first).get());
        if (lease.creating && !lease.quarantined &&
            FelsoriaDynamicLifecycle::Expired(now, lease.createStartedAt, uint32(std::max(10000, std::min(300000,
                sConfigMgr->GetIntDefault("LegionForge.DynamicBots.CreateTimeoutMs", 60000))))))
        {
            lease.recoverable = true;
            QuarantineDynamicAccount(item->first, "create_operation_timeout");
            if (session) session->RequestDynamicRetire();
        }
        if (lease.retiring && !lease.retireStartedAt) lease.retireStartedAt = now;
        bool loginExpired = session && session->DynamicLoginOperation() &&
            FelsoriaDynamicLifecycle::Expired(now, lease.loginStartedAt, uint32(std::max(10000, std::min(300000,
                sConfigMgr->GetIntDefault("LegionForge.DynamicBots.LoginTimeoutMs", 60000)))));
        bool retireExpired = lease.retiring && FelsoriaDynamicLifecycle::Expired(now, lease.retireStartedAt, 120000);
        if (!lease.quarantined && (loginExpired || retireExpired))
        {
            lease.recoverable = true;
            QuarantineDynamicAccount(item->first, loginExpired ? "login_operation_timeout" : "retire_operation_timeout");
            if (session) session->RequestDynamicRetire();
        }
        if (lease.quarantined && lease.recoverable && lease.guid)
        {
            if (DynamicGroupHasHumanGuid(lease.guid))
            {
                lease.quarantined = false;
                lease.recoverable = false;
                lease.purpose = 4;
                lease.retiring = false;
            }
            else if (session && session->DynamicRetireComplete() && !session->DynamicOperationBusy() &&
                !session->DynamicWritesPending() && (!lease.writes || !lease.writes->load()))
            {
                lease.quarantined = false;
                lease.recoverable = false;
                lease.retiring = true;
                lease.discard = true;
                lease.retireStartedAt = now;
            }
        }
        bool recoverEmpty = lease.quarantined && lease.recoverable && !lease.creating && !lease.pendingGuid;
        if ((!lease.pendingGuid && !recoverEmpty) || uint32(now - lease.commitCheckedAt) < 500) continue;
        lease.commitCheckedAt = now;
        m_dynamicCommitCursor = item->first;
        ++checks;
        if (recoverEmpty)
        {
            if (!session || !session->DynamicRetireComplete() || session->DynamicOperationBusy() ||
                session->DynamicWritesPending() || (lease.writes && lease.writes->load())) continue;
            QueryResult empty = CharacterDatabase.PQuery("SELECT COUNT(*) FROM characters WHERE account=%u", item->first);
            if (empty && empty->Fetch()[0].GetUInt32() == 0) lease = DynamicLease();
            continue;
        }
        if (!lease.saveReceipt || lease.saveReceipt->load() == 0 ||
            (lease.writes && lease.writes->load()) || (session && session->DynamicWritesPending())) continue;
        int receiptStatus = lease.saveReceipt->load();
        QueryResult committed = CharacterDatabase.PQuery(
            "SELECT COUNT(*),COALESCE(SUM(guid=%u),0) FROM characters WHERE account=%u", lease.pendingGuid, item->first);
        if (!committed) continue; // Failed reads never prove absence/commit.
        uint32 count = committed->Fetch()[0].GetUInt32();
        if (!count && receiptStatus < 0)
        {
            // The native transaction finished unsuccessfully. Claim only this
            // operation's generated GUID, then use normal verified retirement
            // to release its name/origin metadata and reuse the empty account.
            CharacterDatabase.DirectPExecute("UPDATE lf_dynamic_bot_owned SET guid=%u WHERE account=%u AND guid=0",
                lease.pendingGuid, item->first);
            QueryResult failedOwner = CharacterDatabase.PQuery("SELECT guid FROM lf_dynamic_bot_owned WHERE account=%u", item->first);
            if (!failedOwner || failedOwner->Fetch()[0].GetUInt32() != lease.pendingGuid) continue;
            lease.guid = lease.pendingGuid;
            lease.pendingGuid = 0;
            lease.creating = false;
            lease.quarantined = false;
            lease.recoverable = false;
            lease.retiring = true;
            lease.discard = true;
            lease.changedAt = now;
            if (session) { session->CompleteDynamicCreate(lease.operation); session->RequestDynamicRetire(); }
            continue;
        }
        if (!count) continue; // A successful receipt alone is not identity proof.
        if (count != 1 || committed->Fetch()[1].GetUInt32() != 1)
        {
            lease.recoverable = false;
            QuarantineDynamicAccount(item->first, "create_commit_identity_mismatch");
            if (session) session->CompleteDynamicCreate(lease.operation);
            lease.pendingGuid = 0;
            continue;
        }
        CharacterDatabase.DirectPExecute("UPDATE lf_dynamic_bot_owned SET guid=%u WHERE account=%u AND guid=0",
            lease.pendingGuid, item->first);
        QueryResult owner = CharacterDatabase.PQuery("SELECT guid FROM lf_dynamic_bot_owned WHERE account=%u", item->first);
        if (!owner) continue;
        if (owner->Fetch()[0].GetUInt32() != lease.pendingGuid)
        {
            lease.recoverable = false;
            QuarantineDynamicAccount(item->first, "create_commit_ledger_mismatch");
            if (session) session->CompleteDynamicCreate(lease.operation);
            lease.pendingGuid = 0;
            continue;
        }
        bool late = lease.quarantined || receiptStatus < 0;
        lease.guid = lease.pendingGuid;
        lease.pendingGuid = 0;
        lease.creating = false;
        lease.quarantined = false;
        lease.recoverable = false;
        lease.changedAt = now - 1501;
        if (session) session->CompleteDynamicCreate(lease.operation);
        if (late)
        {
            lease.retiring = true;
            lease.discard = true;
            lease.changedAt = now;
            if (session) session->RequestDynamicRetire();
        }
    }
}

void PlayerBotMgr::PruneDynamicCache()
{
    uint32 cached = 0, empty = 0, creating = 0, quarantined = 0;
    for (auto const& item : m_dynamicLeases)
    {
        if (item.second.cached && !item.second.retiring) ++cached;
        if (item.second.Available()) ++empty;
        if (item.second.creating && !item.second.quarantined) ++creating;
        if (item.second.quarantined) ++quarantined;
    }
    uint32 keep = uint32(std::max(0, std::min(500,
        sConfigMgr->GetIntDefault("LegionForge.DynamicBots.WorldCacheLimit", 50))));
    uint32 capacity = uint32(std::max(0, int(m_dynamicWorldCapacity) +
        sConfigMgr->GetIntDefault("LegionForge.DynamicBots.WorldLimit", 50) +
        sConfigMgr->GetIntDefault("LegionForge.DynamicBots.InstanceLimit", 100) +
        std::max(10, sConfigMgr->GetIntDefault("LegionForge.DynamicBots.PartyReserve", 10))));
    if (!empty && cached && DynamicUsableAccountCount() >= capacity) keep = std::min(keep, cached - 1);
    for (uint32 evicted = 0; cached > keep && evicted < 4; ++evicted)
    {
        auto oldest = m_dynamicLeases.end();
        uint32 now = getMSTime();
        for (auto it = m_dynamicLeases.begin(); it != m_dynamicLeases.end(); ++it)
            if (it->second.cached && !it->second.retiring &&
                (oldest == m_dynamicLeases.end() || uint32(now - it->second.changedAt) > uint32(now - oldest->second.changedAt)))
                oldest = it;
        if (oldest == m_dynamicLeases.end()) break;
        oldest->second.discard = true;
        RetireDynamicCharacter(oldest->first);
        --cached;
    }
    uint32 now = getMSTime();
    if (uint32(now - m_dynamicLastPipelineStatus) >= 10000)
    {
        TC_LOG_INFO(LOG_FILTER_PLAYER,
            "DYNAMIC_PIPELINE_STATE cached=%u empty=%u creating=%u quarantined=%u reused_total=%u",
            cached, empty, creating, quarantined, m_dynamicReused);
        m_dynamicLastPipelineStatus = now;
    }
}

uint32 PlayerBotMgr::ReuseDynamicWorldBot(uint32 plannedAccount)
{
    DynamicLease& plan = m_dynamicLeases[plannedAccount];
    if (plan.purpose != 1 || sConfigMgr->GetIntDefault("LegionForge.DynamicBots.WorldCacheLimit", 50) <= 0) return 0;
    uint32 account = 0;
    uint32 requiredClass = GetDynamicAreaRequiredClass(plan.targetMap, plan.targetZone, plan.targetArea);
    for (auto const& candidate : m_dynamicLeases)
    {
        DynamicLease const& cached = candidate.second;
        if (!cached.cached || cached.quarantined || cached.retiring || cached.purpose || !cached.guid ||
            DynamicGroupHasHumanGuid(cached.guid)) continue;
        auto info = m_idPlayerBotBase.find(candidate.first);
        if (info == m_idPlayerBotBase.end()) continue;
        auto identity = info->second->characters.find(cached.guid);
        if (identity == info->second->characters.end()) continue;
        PlayerBotCharBaseInfo const& character = identity->second;
        if (character.level != plan.targetLevel || PlayerBotRaceTeam(character.race) != TeamId(plan.team) ||
            (requiredClass && character.profession != requiredClass) ||
            !IsDynamicRaceAllowedAt(plan.targetMap, plan.targetZone, plan.targetArea, uint8(character.race), plan.team)) continue;
        PlayerBotSession* session = dynamic_cast<PlayerBotSession*>(sWorld->FindSession(candidate.first).get());
        if (!session || !session->DynamicOffline() || session->DynamicOperationBusy() || session->DynamicWritesPending()) continue;
        account = candidate.first;
        break;
    }
    if (!account) return 0;
    uint32 guid = m_dynamicLeases[account].guid;
    DynamicLease assigned = plan;
    assigned.guid = guid;
    assigned.operation = ++m_dynamicNextOperation;
    assigned.changedAt = getMSTime() - 1501;
    m_dynamicLeases[account] = assigned;
    plan = DynamicLease();
    ++m_dynamicReused;
    return account;
}

// P08: origins are chosen under the lease lock before the owning session creates a character.
uint32 PlayerBotMgr::GetFelsoriaDynamicBGRealm(uint32 accountId) const
{
    if (!DynamicBotsEnabled() || !sConfigMgr->GetBoolDefault("Felsoria.Battleground.LanguageTeams.Enable", false)) return 0;
    std::lock_guard<std::mutex> lock(m_dynamicLock);
    auto found = m_dynamicLeases.find(accountId);
    return found != m_dynamicLeases.end() && found->second.purpose == 3 && !found->second.retiring ?
        found->second.bgOriginRealm : 0;
}

uint8 PlayerBotMgr::GetFelsoriaBGTeamGroup(uint32 type, uint32 bracket, uint32 instance,
    uint32 humanGuid, uint32 originRealm, uint8 humanTeam, uint8 botTeam, uint32 joinTime)
{
    if (humanTeam > 1 || botTeam > 1) return 0;
    uint8 ownGroup = sRealmList->GetFelsoriaBGLanguage(originRealm);
    if (!ownGroup) return 0;
    std::lock_guard<std::mutex> lock(m_felsoriaBGLock);
    FelsoriaBGPlan* plan = nullptr;
    if (instance)
        plan = &m_felsoriaBGInstancePlans[instance];
    else
    {
        FelsoriaBGPlan& pending = m_felsoriaBGQueuedPlans[std::make_pair(type, bracket)];
        if (pending.anchor != humanGuid || pending.joinTime != joinTime)
            pending = FelsoriaBGPlan();
        plan = &pending;
    }
    if (!plan->groups[0] || !plan->groups[1])
    {
        plan->anchor = humanGuid;
        plan->joinTime = joinTime;
        plan->groups[humanTeam] = ownGroup;
        plan->groups[1 - humanTeam] = FelsoriaRealmPolicy::EnemyGroup(urand(0, 3));
        TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,
            "FELSORIA_BG_LANGUAGE_PLAN type=%u bracket=%u instance=%u human=%u own=%u enemy=%u",
            type, bracket, instance, humanGuid, uint32(ownGroup), uint32(plan->groups[1 - humanTeam]));
    }
    return plan->groups[botTeam];
}

void PlayerBotMgr::BindFelsoriaBGPlan(Battleground const* bg, uint32 queuedType)
{
    if (!DynamicBotsEnabled() || !sConfigMgr->GetBoolDefault("Felsoria.Battleground.LanguageTeams.Enable", false) || !bg ||
        !FelsoriaRealmPolicy::NormalBG(bg->IsBattleground(), bg->IsArena(), bg->IsRated(), bg->IsWargame(), bg->IsBrawl())) return;
    std::lock_guard<std::mutex> lock(m_felsoriaBGLock);
    if (m_felsoriaBGInstancePlans.count(bg->GetInstanceID())) return;
    auto pending = m_felsoriaBGQueuedPlans.find(std::make_pair(queuedType, uint32(bg->GetBracketId())));
    if (pending == m_felsoriaBGQueuedPlans.end() || !pending->second.groups[0] || !pending->second.groups[1]) return;
    uint8 queueType = MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(queuedType, 0);
    Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(pending->second.anchor));
    if (!anchor || anchor->GetBattlegroundQueueIndex(queueType) >= PLAYER_MAX_BATTLEGROUND_QUEUES ||
        anchor->GetBattlegroundQueueJoinTime(queueType) != pending->second.joinTime) return;
    // Once copied, the match keeps both groups even if the initiating player leaves.
    m_felsoriaBGInstancePlans[bg->GetInstanceID()] = pending->second;
    pending->second.boundInstance = bg->GetInstanceID();
}

bool PlayerBotMgr::IsFelsoriaBGBotAllowed(Player* player, Battleground const* bg, uint32 team) const
{
    if (!DynamicBotsEnabled() || !sConfigMgr->GetBoolDefault("Felsoria.Battleground.LanguageTeams.Enable", false) ||
        !player || !player->IsPlayerBot() || !bg ||
        !FelsoriaRealmPolicy::NormalBG(bg->IsBattleground(), bg->IsArena(), bg->IsRated(), bg->IsWargame(), bg->IsBrawl())) return true;
    uint8 side = team == ALLIANCE ? 0 : team == HORDE ? 1 : 2;
    if (side > 1) return false;
    uint8 wanted = 0;
    {
        std::lock_guard<std::mutex> lock(m_felsoriaBGLock);
        auto plan = m_felsoriaBGInstancePlans.find(bg->GetInstanceID());
        if (plan == m_felsoriaBGInstancePlans.end()) return false;
        wanted = plan->second.groups[side];
    }
    uint32 originId = Battlenet::RealmHandle(sWorld->GetFelsoriaBotRealmAddress(player->GetGUID())).Realm;
    return wanted && sRealmList->GetFelsoriaBGLanguage(originId) == wanted;
}

void PlayerBotMgr::CreateDynamicCharacter()
{
    FelsoriaDynamicMetrics::Scope perfCreate(FelsoriaDynamicMetrics::CreateDispatch);
    uint32 now = getMSTime();
    uint32 requested = 0, inFlight = 0;
    uint32 const maximum = uint32(std::max(1, std::min(16,
        sConfigMgr->GetIntDefault("LegionForge.DynamicBots.MaxInFlightCreates", 8))));
    for (auto const& item : m_dynamicLeases)
        if (!item.second.quarantined && item.second.creating) ++inFlight;
    if (inFlight >= maximum) return;
    uint32 const batch = uint32(std::max(1, std::min(8,
        sConfigMgr->GetIntDefault("LegionForge.DynamicBots.SpawnBatch", 8))));
    std::vector<uint32> candidates;
    for (auto const& item : m_dynamicLeases)
        if (!item.second.quarantined && item.second.purpose && !item.second.guid && !item.second.retiring && !item.second.creating)
            candidates.push_back(item.first);
    auto priority = [&](uint32 account)
    {
        DynamicLease const& lease = m_dynamicLeases.at(account);
        if (lease.purpose == 2) return (lease.role == lfg::LfgRoles::PLAYER_ROLE_TANK ||
            lease.role == lfg::LfgRoles::PLAYER_ROLE_HEALER) ? 0 : 1;
        return int(FelsoriaGilneasEntry::CreationPriority(lease.purpose,lease.queueId,false));
    };
    std::stable_sort(candidates.begin(), candidates.end(), [&](uint32 a, uint32 b)
    {
        if (priority(a) != priority(b)) return priority(a) < priority(b);
        bool afterA = a > m_dynamicCreateCursor, afterB = b > m_dynamicCreateCursor;
        return afterA != afterB ? afterA : a < b;
    });
    for (uint32 account : candidates)
    {
        auto& entry = *m_dynamicLeases.find(account);
        DynamicLease& lease = entry.second;
        if (lease.quarantined || !lease.purpose || lease.guid || lease.retiring)
            continue;
        if (lease.creating)
            continue;
        if (now - lease.changedAt < 3000)
            continue;
        PlayerBotSession* session = dynamic_cast<PlayerBotSession*>(sWorld->FindSession(entry.first).get());
        if (!session)
        {
            auto info = m_idPlayerBotBase.find(entry.first);
            if (info != m_idPlayerBotBase.end())
                UpPlayerBotSessionByBaseInfo(info->second, false);
            continue;
        }
        if (session->DynamicOperationBusy() || !session->DynamicOffline()) continue;
        // Never create another character beside an unowned residual identity.
        // COUNT returns a row even for an empty account; a failed read defers work.
        QueryResult existing = CharacterDatabase.PQuery(
            "SELECT COUNT(*) FROM characters WHERE account = %u", entry.first);
        if (!existing)
        {
            lease.changedAt = now;
            continue;
        }
        if (existing->Fetch()[0].GetUInt32())
        {
            QuarantineDynamicAccount(entry.first, "unowned_characters_before_create");
            continue;
        }
        uint8 const classes[] = { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER,
            CLASS_ROGUE, CLASS_PRIEST, CLASS_DEATH_KNIGHT, CLASS_SHAMAN,
            CLASS_MAGE, CLASS_WARLOCK, CLASS_MONK, CLASS_DRUID, CLASS_DEMON_HUNTER };
        std::vector<uint8> eligible;
        uint8 const requiredClass = lease.purpose == 1 ?
            GetDynamicAreaRequiredClass(lease.targetMap, lease.targetZone, lease.targetArea) : 0;
        uint32 const starter = lease.purpose == 1 ?
            StarterZone(lease.targetMap, lease.targetZone, lease.targetArea) : 0;
        for (uint8 klass : classes)
        {
            if (requiredClass && klass != requiredClass)
                continue;
            if (starter && NativeStarterRace(starter, klass, lease.team, lease.targetLevel) == RACE_NONE)
                continue;
            if (lease.purpose == 3 && lease.role && !(FelsoriaBGRole::Supported(klass,
                [](unsigned c,unsigned i){return sDB2Manager.GetChrSpecializationByIndex(c,i);}) & lease.role))
                continue;
            if (lease.purpose == 2)
            {
                bool tank = klass == CLASS_WARRIOR || klass == CLASS_PALADIN ||
                    klass == CLASS_DEATH_KNIGHT || klass == CLASS_DRUID ||
                    klass == CLASS_MONK || klass == CLASS_DEMON_HUNTER;
                bool healer = klass == CLASS_PALADIN || klass == CLASS_PRIEST ||
                    klass == CLASS_SHAMAN || klass == CLASS_MONK || klass == CLASS_DRUID;
                if ((lease.role == lfg::LfgRoles::PLAYER_ROLE_TANK && !tank) ||
                    (lease.role == lfg::LfgRoles::PLAYER_ROLE_HEALER && !healer))
                    continue;
            }
            if ((starter && NativeStarterRace(starter, klass, lease.team, lease.targetLevel) != RACE_NONE) ||
                (!starter && RandomRace(lease.team == TEAM_ALLIANCE, klass, lease.targetLevel) != RACE_NONE))
                eligible.push_back(klass);
        }
        if (eligible.empty())
        {
            lease.changedAt = now;
            continue;
        }
        if (lease.purpose == 3 && sConfigMgr->GetBoolDefault("Felsoria.Battleground.LanguageTeams.Enable", false) &&
            (!lease.bgLanguageGroup || !lease.bgOriginRealm))
            continue;
        uint8 klass = eligible[urand(0, uint32(eligible.size() - 1))];
        uint8 nativeRace = starter ? NativeStarterRace(starter, klass, lease.team, lease.targetLevel) : RACE_NONE;
        WorldPacket packet = BuildCreatePlayerData(lease.team == TEAM_ALLIANCE, klass, lease.targetLevel, nativeRace);
        uint64 operation = ++m_dynamicNextOperation;
        WorldPacket namePacket = packet;
        WorldPackets::Character::CreateChar named(std::move(namePacket));
        named.Read();
        if (!session->RequestDynamicCreate(std::move(packet), operation))
        {
            if (named.CreateInfo) ReleasePendingBotName(named.CreateInfo->Name);
            continue;
        }
        if(FelsoriaGilneasEntry::Fast(lease.purpose,lease.queueId))FelsoriaGilneasEntry::Record(now,account,"create_requested",0,0,0,uint32(now-lease.allocatedAt));
        m_dynamicCreateCursor = account;
        lease.operation = operation;
        lease.writes = session->DynamicWriteFence();
        lease.createStartedAt = now;
        lease.pendingGuid = 0;
        lease.saveReceipt.reset();
        lease.recoverable = false;
        lease.creating = true;
        lease.changedAt = now;
        if (FelsoriaDynamicMetrics::Enabled())
            lease.perfCreateAt = getMSTime();
        m_dynamicLastCreate = now;
        if (++requested >= batch || ++inFlight >= maximum)
            return; // Bounded writes, without serialising all fifty logins.
    }
}

void PlayerBotMgr::RequestDynamicRDFPartyCleanup(Group* group, ObjectGuid leavingGuid)
{
    if (!DynamicBotsEnabled() || !group || !group->isLFGGroup() || group->isRaidGroup())
        return;
    DynamicRDFDeparture request;
    request.group = group->GetGUID();
    request.leaving = leavingGuid;
    for (Group::MemberSlot const& member : group->GetMemberSlots())
        if (member.Guid != leavingGuid)
            request.members.push_back(member.Guid.GetCounter());
    std::lock_guard<std::mutex> lock(m_dynamicRDFDepartureLock);
    m_dynamicRDFDepartures.push_back(std::move(request));
}

void PlayerBotMgr::UpdateDynamicBots()
{
    uint32 now = getMSTime();
    if (now - m_dynamicLastUpdate < 500)
        return;
    FelsoriaDynamicMetrics::Scope perfManager(FelsoriaDynamicMetrics::ManagerWork);
    FelsoriaDynamicMetrics::Scope perfLock(FelsoriaDynamicMetrics::ManagerLockWait);
    std::lock_guard<std::mutex> leaseLock(m_dynamicLock);
    perfLock.Stop();
    m_dynamicLastUpdate = now;
    ProtectDynamicPartyLeases(now);
    RefreshDynamicCreates(now);
    PruneDynamicCache();
    // P15: process after native member removal, outside the group hook.
    std::vector<DynamicRDFDeparture> departures;
    {
        std::lock_guard<std::mutex> lock(m_dynamicRDFDepartureLock);
        departures.swap(m_dynamicRDFDepartures);
    }
    for (DynamicRDFDeparture const& departure : departures)
    {
        Group* oldGroup = sGroupMgr->GetGroupByGUID(departure.group);
        bool removalPending = oldGroup && !departure.leaving;
        if (oldGroup && departure.leaving)
            for (Group::MemberSlot const& member : oldGroup->GetMemberSlots())
                if (member.Guid == departure.leaving)
                    removalPending = true;
        if (removalPending)
        {
            std::lock_guard<std::mutex> lock(m_dynamicRDFDepartureLock);
            m_dynamicRDFDepartures.push_back(departure);
            continue;
        }
        for (auto& entry : m_dynamicLeases)
        {
            DynamicLease& lease = entry.second;
            if ((lease.purpose != 2 && !(lease.purpose == 4 && lease.dungeonCount)) || lease.retiring || !lease.guid ||
                std::find(departure.members.begin(), departure.members.end(), lease.guid) == departure.members.end())
                continue;
            if (DynamicGroupHasHumanGuid(lease.guid))
                continue; // Another human still owns this party.
            lease.humanGuid = 0; // Requeue by the old owner cannot reclaim these bots.
            RetireDynamicCharacter(entry.first);
        }
    }
    std::map<uint32, DynamicSnapshot> snapshots = m_dynamicSnapshots;
    for (auto& reserved : m_dynamicLeases) if (reserved.second.purpose==3 && !reserved.second.retiring && !reserved.second.quarantined) {
        FelsoriaBGRole::Context context;unsigned mask=0;FelsoriaBGRole::Stage stage=FelsoriaBGRole::Planned;
        bool const planned=FelsoriaBGRole::Registry().Plan(reserved.first,reserved.second.queueId,context,mask,&stage);
        if(planned&&context.instance)reserved.second.targetInstance=context.instance;
        auto observed=snapshots.find(reserved.first);
        // FELSORIA_BG_ENTRY_V1_2: a terminal setup failure cannot recover by waiting.
        // Read only the value snapshot, never another map thread's Player state.
        if(planned && FelsoriaBGRole::Role(mask)<3 &&
           observed!=snapshots.end() && observed->second.preparationFailed &&
           !observed->second.battleground &&
           uint32(now-observed->second.updated)<5000 &&
           !(reserved.second.guid && DynamicGroupHasHumanGuid(reserved.second.guid))){
            TC_LOG_ERROR(LOG_FILTER_PLAYER,"FELSORIA_BG_PREP_FAILED account=%u guid=%u bg=%u role=%u age_ms=%u replacement_requested=1",
                reserved.first,reserved.second.guid,reserved.second.queueId,uint32(reserved.second.role),uint32(now-reserved.second.allocatedAt));
            RetireDynamicCharacter(reserved.first);continue;
        }
        if(planned&&stage==FelsoriaBGRole::Departed){
            // Native member removal/cancel is authoritative, including an absent/offline session.
            // Preserve a player already observed in another battleground and all normal party protections.
            if(observed==snapshots.end()||!observed->second.battleground||observed->second.instance==context.instance)
                RetireDynamicCharacter(reserved.first);
            continue;
        }
        if(context.instance&&observed!=snapshots.end()&&!observed->second.battleground&&!observed->second.bgInvited&&
           stage!=FelsoriaBGRole::Invited&&stage!=FelsoriaBGRole::Active)
            if(auto*match=sBattlegroundMgr->GetBattleground(context.instance,MS::Battlegrounds::BattlegroundTypeId::None))
                if(match->GetPlayersCountByTeam(reserved.second.team==TEAM_ALLIANCE?ALLIANCE:HORDE)>=match->GetMaxPlayersPerTeam()){
                    RetireDynamicCharacter(reserved.first);continue; // Only an actually full roster cancels an uninvited surplus.
                }
        FelsoriaBGRole::Registry().Touch(reserved.first,now);
    }
    std::vector<DynamicSnapshot> humans;
    for (auto const& entry : snapshots)
        if (!entry.second.bot && entry.second.ready && now - entry.second.updated < 5000)
            humans.push_back(entry.second);

    std::vector<DynamicSnapshot> field;
    for (DynamicSnapshot const& human : humans)
    {
        if ((human.dungeon && !AlliedDedicatedStarterRace(human.map) &&
                !GetDynamicAreaRequiredClass(human.map, human.zone, human.area)) ||
            human.battleground)
            continue;
        auto& observed = m_dynamicHumanZone[human.account];
        if (observed.first != human.zone)
            observed = std::make_pair(human.zone, now);
        if (now - observed.second >= 2000)
        {
            auto shared = std::find_if(field.begin(), field.end(), [&](DynamicSnapshot const& other)
            {
                return other.map == human.map && other.zone == human.zone && other.instance == human.instance &&
                    other.phase == human.phase && other.phaseIds == human.phaseIds && other.level == human.level &&
                    SameAlliedPopulationArea(human.map, human.zone, other.area, human.area) &&
                    GetDynamicAreaRequiredClass(other.map, other.zone, other.area) ==
                        GetDynamicAreaRequiredClass(human.map, human.zone, human.area);
            });
            if (shared == field.end())
                field.push_back(human);
        }
    }
    for (auto it = m_dynamicHumanZone.begin(); it != m_dynamicHumanZone.end(); )
        if (snapshots.find(it->first) == snapshots.end() ||
            now - snapshots[it->first].updated >= 5000)
            it = m_dynamicHumanZone.erase(it);
        else
            ++it;

    uint32 worldLimit = uint32(std::max(0, sConfigMgr->GetIntDefault("LegionForge.DynamicBots.WorldLimit", 50)));
    uint32 instanceLimit = uint32(std::max(0, sConfigMgr->GetIntDefault("LegionForge.DynamicBots.InstanceLimit", 100)));
    uint32 const zoneLimit = worldLimit;
    worldLimit *= uint32(field.size());
    m_dynamicWorldCapacity = std::max(zoneLimit, worldLimit);
    auto factionTargets = [&](DynamicSnapshot const& human)
    {
        int forcedTeam = -1;
        if (uint8 race = AlliedDedicatedStarterRace(human.map)) forcedTeam = int(PlayerBotRaceTeam(race));
        if (IsNightborneStartingHub(human.map, human.zone, human.area)) forcedTeam = TEAM_HORDE;
        return FelsoriaDynamicPartyPolicy::TargetsFor(zoneLimit,
            human.territory == FACTION_MASK_ALLIANCE, human.territory == FACTION_MASK_HORDE, forcedTeam);
    };
    std::map<uint32, uint32> wanted;
    for (DynamicSnapshot const& human : field)
        wanted[human.guid] = zoneLimit;
    for (auto& entry : m_dynamicLeases)
    {
        DynamicLease& lease = entry.second;
        if (lease.purpose != 1 || lease.retiring)
            continue;
        bool const originalPresent = std::any_of(field.begin(), field.end(),
            [&](DynamicSnapshot const& h) { return h.guid == lease.humanGuid; });
        if (originalPresent)
            continue;
        for (DynamicSnapshot const& h : field)
            if (h.map == lease.targetMap && h.zone == lease.targetZone && h.instance == lease.targetInstance &&
                h.phase == lease.phase && h.phaseIds == lease.phaseIds &&
                std::abs(int32(h.level) - int32(lease.targetLevel)) <= 3 &&
                (h.level != 110 || lease.targetLevel == 110) &&
                GetDynamicAreaRequiredClass(h.map, h.zone, h.area) ==
                    GetDynamicAreaRequiredClass(lease.targetMap, lease.targetZone, lease.targetArea))
            {
                lease.humanGuid = h.guid;
                break;
            }
    }

    // A bot that never reaches an invite (e.g. failed settings/login) must not
    // hold a demand lease forever. Accepted players are never retired here.
    for (auto& held : m_dynamicLeases)
    {
        DynamicLease& lease = held.second;
        auto snap = snapshots.find(held.first);
        if (lease.purpose != 3 || lease.retiring || now - lease.allocatedAt < 120000 ||
            (snap != snapshots.end() && snap->second.battleground)) continue;
        // A healthy queue before a match starts is not a failed entry.
        bool ownerPlaying = false;
        for (DynamicSnapshot const& human : humans)
            if (human.guid == lease.humanGuid && human.battleground) ownerPlaying = true;
        if (!ownerPlaying && snap != snapshots.end() && snap->second.bgQueue && !snap->second.bgInvited) continue;
        if (lease.guid && DynamicGroupHasHumanGuid(lease.guid)) continue;
        RetireDynamicCharacter(held.first); // Cancels the role job immediately; native queue/teleport cleanup stays bounded.
        TC_LOG_ERROR(LOG_FILTER_PLAYER, "BOT_BG_LEASE_EXPIRED account=%u replacement_requested=1", held.first);
    }

    // Queue demand is represented by leases only when a human has requested
    // the activity. The extra capacity is a logical limit, not prelogged bots.
    std::map<uint32, uint32> current;
    uint32 worldCount = 0, instanceCount = 0;
    for (auto const& entry : m_dynamicLeases)
    {
        DynamicLease const& lease = entry.second;
        if (!lease.purpose)
            continue;
        if (lease.purpose == 1 || lease.purpose == 4)
        {
            if (lease.purpose == 1 && !lease.retiring)
                ++worldCount;
            if (lease.purpose == 1 && !lease.retiring)
                ++current[lease.humanGuid];
        }
        else if (lease.purpose == 2 || lease.purpose == 3)
            ++instanceCount;
    }

    auto parkProtected = [&](uint32 account, DynamicLease& lease)
    {
        if(lease.purpose==3)FelsoriaBGRole::Registry().Cancel(account,now);
        lease.purpose = 4;
        lease.queueScheduled = false;
        lease.placed = false;
        bool onlineHuman = DynamicGroupHasConnectedHumanGuid(lease.guid);
        if (onlineHuman)
            lease.lastHumanGroupSeen = now;
        // A teleport temporarily hides the human from ready snapshots. Keep
        // the party bot online through that short transfer, then go dormant.
        if (!onlineHuman && (!lease.lastHumanGroupSeen ||
            now - lease.lastHumanGroupSeen >= 15000))
            if (PlayerBotSession* bot = dynamic_cast<PlayerBotSession*>(sWorld->FindSession(account).get()))
                bot->RequestDynamicDormantLogout();
    };

    // Advance through leases from the previous account so a slow asynchronous
    // delete cannot monopolize the bounded retirement budget every tick.
    uint32 retiringProcessed = 0;
    if (!m_dynamicLeases.empty())
    {
        auto it = m_dynamicLeases.upper_bound(m_dynamicRetireCursor);
        if (it == m_dynamicLeases.end())
            it = m_dynamicLeases.begin();
        for (size_t examined = 0; examined < m_dynamicLeases.size() && retiringProcessed < 8; ++examined)
        {
            auto currentLease = it++;
            if (it == m_dynamicLeases.end())
                it = m_dynamicLeases.begin();
            DynamicLease& lease = currentLease->second;
            if (!lease.retiring)
                continue;
            m_dynamicRetireCursor = currentLease->first;
            ++retiringProcessed;
            if (lease.guid && DynamicGroupHasHumanGuid(lease.guid))
            {
                lease.retiring = false;
                parkProtected(currentLease->first, lease);
            }
            else if (lease.purpose == 2 && lease.guid &&
                sLFGMgr->IsDynamicBotInHumanActivity(
                    ObjectGuid::Create<HighGuid::Player>(lease.guid),
                    ObjectGuid::Create<HighGuid::Player>(lease.humanGuid), lease.queueId))
                lease.retiring = false;
            else
                FinishDynamicRetire(currentLease->first);
        }
    }
    uint32 worldRetireRequested = 0;
    for (auto& entry : m_dynamicLeases)
    {
        DynamicLease& lease = entry.second;
        if (lease.retiring)
            continue;
        if (lease.purpose == 4)
        {
            if (!DynamicGroupHasHumanGuid(lease.guid))
                RetireDynamicCharacter(entry.first);
            else
                parkProtected(entry.first, lease);
            continue;
        }
        // FELSORIA_RDF_INITIAL_PREPARE_V1: guard the phase before queue jobs exist.
        if (lease.purpose == 2 && !lease.queueScheduled)
        {
            auto preparing = snapshots.find(entry.first);
            bool const ready = preparing != snapshots.end() && preparing->second.ready;
            bool const queued = preparing != snapshots.end() && preparing->second.lfgQueue;
            uint32 const elapsed = uint32(now - lease.allocatedAt);
            if (!lease.prepareTraceAt || uint32(now - lease.prepareTraceAt) >= 10000)
            {
                lease.prepareTraceAt = now;
                TC_LOG_ERROR(LOG_FILTER_LFG,
                    "RDF_PREPARE_STATE account=%u guid=%u role=%u class=%u creating=%u snapshot=%u ready=%u queued=%u elapsed_ms=%u",
                    entry.first,lease.guid,uint32(lease.role),
                    preparing == snapshots.end() ? 0u : uint32(preparing->second.klass),
                    uint32(lease.creating),uint32(preparing != snapshots.end()),
                    uint32(ready),uint32(queued),elapsed);
            }
            if (lease.allocatedAt && elapsed >= 120000 && !ready && !queued)
            {
                // Keep bots already accepted into a human party/proposal/activity.
                if (lease.guid && DynamicGroupHasHumanGuid(lease.guid))
                    parkProtected(entry.first,lease);
                else if (lease.guid && sLFGMgr->IsDynamicBotInHumanActivity(
                    ObjectGuid::Create<HighGuid::Player>(lease.guid),
                    ObjectGuid::Create<HighGuid::Player>(lease.humanGuid),lease.queueId))
                    continue;
                else
                {
                    TC_LOG_ERROR(LOG_FILTER_LFG,
                        "RDF_PREPARE_TIMEOUT account=%u guid=%u role=%u creating=%u elapsed_ms=%u; replacing stalled candidate",
                        entry.first,lease.guid,uint32(lease.role),uint32(lease.creating),elapsed);
                    RetireDynamicCharacter(entry.first);
                }
                continue;
            }
        }
        if (lease.purpose != 1)
            continue;
        auto observedBot = snapshots.find(entry.first);
        if (lease.allocatedAt && now - lease.allocatedAt >= 120000 && !lease.placed &&
            (observedBot == snapshots.end() || !observedBot->second.ready))
        {
            TC_LOG_ERROR(LOG_FILTER_PLAYER,
                "DYNAMIC_BOT_START_TIMEOUT account=%u guid=%u creating=%u; retiring without blocking population",
                entry.first, lease.guid, uint32(lease.creating));
            if (lease.guid && DynamicGroupHasHumanGuid(lease.guid))
                parkProtected(entry.first, lease);
            else
                RetireDynamicCharacter(entry.first);
            continue;
        }
        // A native transfer hides a human briefly. Keep the old population
        // through that transition; a stable destination drives replacement.
        bool transferring = false;
        for (auto const& observed : snapshots)
            if (!observed.second.bot && observed.second.guid == lease.humanGuid &&
                now - observed.second.updated < 5000 && !observed.second.ready)
                transferring = true;
        if (transferring)
            continue;
        bool inTargetZone = false;
        for (DynamicSnapshot const& human : field)
            if (human.guid == lease.humanGuid &&
                human.map == lease.targetMap && human.zone == lease.targetZone &&
                human.instance == lease.targetInstance &&
                SameAlliedPopulationArea(human.map, human.zone, human.area, lease.targetArea) &&
                ClassHallForArea(human.map, human.zone, human.area) ==
                    ClassHallForArea(lease.targetMap, lease.targetZone, lease.targetArea) &&
                GetDynamicAreaRequiredClass(human.map, human.zone, human.area) ==
                    GetDynamicAreaRequiredClass(lease.targetMap, lease.targetZone, lease.targetArea) &&
                std::abs(int32(lease.targetLevel) - int32(human.level)) <= 3 &&
                (human.level != 110 || lease.targetLevel == 110))
            {
                inTargetZone = true;
                // Phase changes do not require another teleport. The owning
                // session applies them only after it confirms its map/instance.
                lease.phase = human.phase;
                lease.phaseIds = human.phaseIds;
                break;
            }
        uint8 const requiredClass = GetDynamicAreaRequiredClass(lease.targetMap, lease.targetZone, lease.targetArea);
        auto bot = snapshots.find(entry.first);
        if (inTargetZone && bot != snapshots.end() && bot->second.ready &&
            ((!IsDynamicRaceAllowedAt(lease.targetMap, lease.targetZone, lease.targetArea, bot->second.race, lease.team)) ||
                (requiredClass && bot->second.klass != requiredClass)))
            inTargetZone = false;
        if (inTargetZone && current[lease.humanGuid] <= wanted[lease.humanGuid])
            continue;
        if (lease.guid && DynamicGroupHasHumanGuid(lease.guid))
        {
            --current[lease.humanGuid];
            parkProtected(entry.first, lease);
            continue;
        }
        --current[lease.humanGuid];
        RetireDynamicCharacter(entry.first);
        if (++worldRetireRequested >= 8)
            break;
    }

    // Queue candidates are ephemeral. Once the requesting human has left the
    // queue, retire them unless their actual party still contains a human.
    for (auto& entry : m_dynamicLeases)
    {
        DynamicLease& lease = entry.second;
        if ((lease.purpose != 2 && lease.purpose != 3) || lease.retiring)
            continue;
        auto owner = std::find_if(humans.begin(), humans.end(), [&](DynamicSnapshot const& s)
            { return s.guid == lease.humanGuid; });
        if (owner != humans.end() &&
            (lease.purpose == 2 ? owner->lfgQueue :
                (owner->bgQueue || owner->battleground)))
            continue;
        if (lease.purpose == 2 && lease.guid && DynamicHumanIsConnectedGuid(lease.humanGuid) &&
            sLFGMgr->IsDynamicBotInHumanActivity(
                ObjectGuid::Create<HighGuid::Player>(lease.guid),
                ObjectGuid::Create<HighGuid::Player>(lease.humanGuid), lease.queueId))
            continue;
        if (lease.purpose == 3)
        {
            // Snapshots are indexed by account, not player GUID.
            auto ownerTransfer=std::find_if(snapshots.begin(),snapshots.end(),[&](std::pair<uint32 const,DynamicSnapshot>const& p){
                return !p.second.bot && p.second.guid==lease.humanGuid &&
                    uint32(now-p.second.updated)<5000 && !p.second.ready &&
                    (p.second.bgQueue || p.second.battleground);
            });
            if(FelsoriaBGRole::Role(lease.role)<3 && ownerTransfer!=snapshots.end())continue;
            auto bot = snapshots.find(entry.first);
            if (bot != snapshots.end() && bot->second.battleground)
            {
                bool occupied = std::any_of(humans.begin(), humans.end(), [&](DynamicSnapshot const& human)
                    { return human.battleground && human.map == bot->second.map &&
                        human.instance == bot->second.instance; });
                if (occupied)
                    continue;
            }
        }
        if (lease.guid && DynamicGroupHasHumanGuid(lease.guid))
        {
            --instanceCount;
            parkProtected(entry.first, lease);
            continue;
        }
        RetireDynamicCharacter(entry.first);
        if (lease.purpose == 3)
            break; // P15: retain the existing BG retirement budget.
    }

    // Repair actual faction deficits, including an already full 49/1 zone.
    for (DynamicSnapshot const& human : field)
    {
        auto const target = factionTargets(human);
        uint32 counts[2] = {};
        for (auto const& entry : m_dynamicLeases)
            if (entry.second.purpose == 1 && !entry.second.retiring &&
                entry.second.humanGuid == human.guid && entry.second.targetMap == human.map &&
                entry.second.targetZone == human.zone && entry.second.targetInstance == human.instance)
                ++counts[entry.second.team == TEAM_ALLIANCE ? 0 : 1];
        for (auto& entry : m_dynamicLeases)
        {
            DynamicLease& lease = entry.second;
            if (lease.purpose != 1 || lease.retiring || lease.humanGuid != human.guid ||
                lease.targetMap != human.map || lease.targetZone != human.zone || lease.targetInstance != human.instance) continue;
            uint32 const team = lease.team == TEAM_ALLIANCE ? 0 : 1;
            uint32 const desired = team ? target.horde : target.alliance;
            if (counts[team] <= desired || DynamicGroupHasHumanGuid(lease.guid)) continue;
            RetireDynamicCharacter(entry.first);
            if (lease.retiring) --counts[team];
        }
    }
    current.clear();
    worldCount = 0;
    instanceCount = 0;
    for (auto const& entry : m_dynamicLeases)
    {
        if (entry.second.retiring) continue;
        if (entry.second.purpose == 1) { ++worldCount; ++current[entry.second.humanGuid]; }
        else if (entry.second.purpose == 2 || entry.second.purpose == 3) ++instanceCount;
    }

    // Match the validated LFG queue's missing role to a current human
    // snapshot. Reserve all missing roles; no idle dungeon sessions.
    auto lfgDemands = sLFGMgr->GetDynamicBotDemands();
    for (lfg::BotQueueDemand const& demand : lfgDemands)
    {
        DynamicSnapshot const* human = nullptr;
        for (ObjectGuid const& memberGuid : demand.memberGuids)
            for (DynamicSnapshot const& candidate : humans)
                if (candidate.guid == memberGuid.GetCounter() &&
                    (!human || candidate.guid < human->guid))
                {
                    human = &candidate;
                    break;
                }
        if (!human || !human->lfgQueue || demand.dungeons.empty())
            continue;
        uint32 pending = 0;
        for (auto const& entry : m_dynamicLeases)
            if (entry.second.purpose == 2 && !entry.second.retiring &&
                entry.second.queueId == demand.queueId &&
                entry.second.role == demand.role &&
                entry.second.humanGuid == human->guid &&
                (!entry.second.guid ||
                 std::find(demand.memberGuids.begin(), demand.memberGuids.end(),
                    ObjectGuid::Create<HighGuid::Player>(entry.second.guid)) == demand.memberGuids.end()))
                ++pending;
        while (pending < demand.count && instanceCount < instanceLimit)
        {
        uint32 account = 0;
        for (auto const& entry : m_dynamicLeases)
            if (entry.second.Available())
            {
                account = entry.first;
                break;
            }
        if (!account)
        {
            CreateDynamicAccount();
            break;
        }
        DynamicLease& lease = m_dynamicLeases[account];
        lease.purpose = 2;
        lease.queueId = demand.queueId;
        lease.role = demand.role;
        lease.humanGuid = human->guid;
        lease.team = human->team;
        lease.targetLevel = human->level;
        lease.changedAt = now - 3001;
            lease.allocatedAt = now;
        for (uint32 dungeon : demand.dungeons)
        {
            if (lease.dungeonCount >= 3)
                break;
            lease.dungeons[lease.dungeonCount++] = dungeon;
        }
        ++instanceCount;
        ++pending;
        }
    }

    // Reserve bounded battleground waves for actual real-player demand.
    // Bracket, faction and the role budget determine each candidate.
    bool const languageTeams = sConfigMgr->GetBoolDefault("Felsoria.Battleground.LanguageTeams.Enable", false);
    if (languageTeams)
    {
        std::lock_guard<std::mutex> lock(m_felsoriaBGLock);
        for (auto it = m_felsoriaBGInstancePlans.begin(); it != m_felsoriaBGInstancePlans.end(); )
            if (!sBattlegroundMgr->GetBattleground(it->first, MS::Battlegrounds::BattlegroundTypeId::None))
                it = m_felsoriaBGInstancePlans.erase(it);
            else ++it;
    }
    // Explicit AB demand must not wait behind backfilling an unrelated match.
    BattleGroundTypes requestedBGTypes=m_BGTypes;
    uint8 const abQueue=MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin);
    bool abWaiting=std::any_of(humans.begin(),humans.end(),[abQueue](DynamicSnapshot const& h){return h.normalBGQueues.count(abQueue)!=0 && !h.bgInvited;});
    uint8 const gilneasQueue=MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(MS::Battlegrounds::BattlegroundTypeId::BattlegroundBattleForGilneas);
    bool gilneasWaiting=std::any_of(humans.begin(),humans.end(),[gilneasQueue](DynamicSnapshot const& h){return h.normalBGQueues.count(gilneasQueue)!=0&&!h.bgInvited;});
    if(abWaiting||gilneasWaiting)std::stable_sort(requestedBGTypes.begin(),requestedBGTypes.end(),[abWaiting,gilneasWaiting](BattlegroundTypeId a,BattlegroundTypeId b){
        auto urgent=[&](BattlegroundTypeId type){return (abWaiting&&type==MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin)||(gilneasWaiting&&type==MS::Battlegrounds::BattlegroundTypeId::BattlegroundBattleForGilneas);};
        return urgent(a)&&!urgent(b);
    });
    // Reserve a small fair wave; creation/login dispatch keep their existing caps.
    uint32 const bgAllocationBatch=uint32(std::max(1,std::min(8,
        sConfigMgr->GetIntDefault("LegionForge.DynamicBots.SpawnBatch",8))));
    bool stopBGWave=false;
    for(uint32 bgAllocationPass=0; bgAllocationPass<bgAllocationBatch && instanceCount<instanceLimit; ++bgAllocationPass)
    {
        bool allocatedThisPass=false;
    for (BattlegroundTypeId bgType : requestedBGTypes)
    {
        Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(bgType);
        if (!bg || bg->IsArena() || (languageTeams &&
            !FelsoriaRealmPolicy::NormalBG(bg->IsBattleground(), bg->IsArena(), bg->IsRated(), bg->IsWargame(), bg->IsBrawl())))
            continue;
        bool const balanceRoles=FelsoriaRealmPolicy::NormalBG(bg->IsBattleground(),bg->IsArena(),bg->IsRated(),bg->IsWargame(),bg->IsBrawl());
        uint8 queueType = MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(bgType, 0);
        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(queueType);
        bool allocated = false;
        for (uint8 bracket = 0; bracket < MS::Battlegrounds::MaxBrackets; ++bracket)
        {
            PVPDifficultyEntry const* entry = sDB2Manager.GetBattlegroundBracketById(bg->GetMapId(), bracket);
            if (!entry) continue;
            int32 allianceNeed = 0, hordeNeed = 0;
            if (bgQueue.ExistRealPlayer(entry))
                bgQueue.QueryNeedPlayerCount(bgType, bracket, 0, allianceNeed, hordeNeed);
            int32 runningNeed[2] = {0, 0};
            uint32 runningHuman = 0;
            uint32 languageInstance = 0;
            std::set<uint32> countedInstances;
            for (DynamicSnapshot const& human : humans)
            {
                if (!human.battleground || !human.instance ||
                    human.level < entry->MinLevel || human.level > entry->MaxLevel) continue;
                Battleground* running = sBattlegroundMgr->GetBattleground(human.instance, MS::Battlegrounds::BattlegroundTypeId::None);
                if (!running || !running->IsBattleground() || running->IsRated() || running->IsWargame() || running->IsBrawl() ||
                    running->GetTypeID(true) != uint16(bgType) || running->GetBracketId() != bracket ||
                    (running->GetStatus() != STATUS_WAIT_JOIN && running->GetStatus() != STATUS_IN_PROGRESS)) continue;
                // Fill one concrete match at a time; never combine language plans across instances.
                if (languageInstance && languageInstance != human.instance) continue;
                if (!runningHuman) { runningHuman = human.guid; languageInstance = human.instance; }
                if (!countedInstances.insert(human.instance).second) continue;
                // GetFreeSlotsForTeam is the native balance window for the next invite,
                // not the team's physical vacancy. Prepare both sides independently.
                uint32 const teamSize=running->GetMaxPlayersPerTeam();
                runningNeed[0] += int32(teamSize-std::min(teamSize,running->GetInvitedCount(ALLIANCE)));
                runningNeed[1] += int32(teamSize-std::min(teamSize,running->GetInvitedCount(HORDE)));
                if (running->HasFreeSlots()) running->AddToBGFreeSlotQueue();
                if(bgAllocationPass==0 && (!m_dynamicLastStatus||now-m_dynamicLastStatus>=60000)){
                    FelsoriaBGRole::Context c{uint32(bgType),uint32(bracket),human.instance,0,teamSize};
                    unsigned pendingA=FelsoriaBGRole::Registry().Pending(c,now);auto rolesA=FelsoriaBGRole::Registry().Usage(c,now);
                    c.team=1;unsigned pendingH=FelsoriaBGRole::Registry().Pending(c,now);auto rolesH=FelsoriaBGRole::Registry().Usage(c,now);
                    TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_BG_REFILL_V1_1 instance=%u size=%u insideA=%u insideH=%u occupiedA=%u occupiedH=%u pendingA=%u pendingH=%u deficitA=%d deficitH=%d rolesA=%u/%u/%u rolesH=%u/%u/%u",
                        human.instance,teamSize,running->GetPlayersCountByTeam(ALLIANCE),running->GetPlayersCountByTeam(HORDE),
                        running->GetInvitedCount(ALLIANCE),running->GetInvitedCount(HORDE),pendingA,pendingH,
                        std::max(0,runningNeed[0]-int32(pendingA)),std::max(0,runningNeed[1]-int32(pendingH)),
                        rolesA.n[0],rolesA.n[1],rolesA.n[2],rolesH.n[0],rolesH.n[1],rolesH.n[2]);
                }
            }
            for (uint8 sideOffset = 0; sideOffset < 2; ++sideOffset)
            {
                uint8 const side = uint8((sideOffset + (balanceRoles?m_dynamicBGTeamCursor:0)) % 2);
                TeamId team = side == 0 ? TEAM_ALLIANCE : TEAM_HORDE;
                int32 queuedNeed = side == 0 ? allianceNeed : hordeNeed;
                int32 activeNeed = runningNeed[side];
                if(balanceRoles){
                    FelsoriaBGRole::Context c{uint32(bgType),uint32(bracket),languageInstance,uint32(side),bg->GetMaxPlayersPerTeam()};
                    if(languageInstance)activeNeed-=int32(FelsoriaBGRole::Registry().Pending(c,now));
                    c.instance=0;queuedNeed-=int32(FelsoriaBGRole::Registry().Pending(c,now,true));
                }
                else for (auto const& held : m_dynamicLeases)
                {
                    if (held.second.purpose != 3 || held.second.retiring ||
                        held.second.queueId != uint32(bgType) || held.second.team != team ||
                        held.second.targetLevel < entry->MinLevel || held.second.targetLevel > entry->MaxLevel) continue;
                    if (activeNeed > 0 && held.second.targetInstance &&
                        held.second.targetInstance != languageInstance) continue;
                    auto snap = snapshots.find(held.first);
                    if (snap != snapshots.end() && (snap->second.battleground || snap->second.bgInvited)) continue;
                    --activeNeed; // An invite already occupies a native slot; an uninvited lease does not.
                    if (snap == snapshots.end() || !snap->second.bgQueue) --queuedNeed;
                }
                if (std::max(queuedNeed, activeNeed) <= 0) continue;
                uint32 humanGuid = activeNeed > 0 ? runningHuman : 0;
                if (!humanGuid)
                    for (DynamicSnapshot const& candidate : humans)
                        if (candidate.bgQueue && candidate.level >= entry->MinLevel && candidate.level <= entry->MaxLevel &&
                            (!languageTeams || (!candidate.bgInvited && candidate.normalBGQueues.count(queueType))))
                        { humanGuid = candidate.guid; break; }
                if (!humanGuid) continue;
                uint8 languageGroup = 0;
                uint32 originRealm = 0;
                uint32 contextInstance = activeNeed > 0 ? languageInstance : 0;
                if (languageTeams)
                {
                    auto human = std::find_if(humans.begin(), humans.end(), [&](DynamicSnapshot const& h) { return h.guid == humanGuid; });
                    if (human == humans.end() || (contextInstance && !human->normalBG)) continue;
                    uint32 joinTime = 0;
                    auto join = human->normalBGQueues.find(queueType);
                    if (join != human->normalBGQueues.end()) joinTime = join->second;
                    languageGroup = GetFelsoriaBGTeamGroup(uint32(bgType), bracket, contextInstance,
                        humanGuid, human->originRealm, contextInstance ? human->bgTeam : human->team, uint8(team), joinTime);
                    auto origins = sRealmList->GetFelsoriaBGRealmIds(languageGroup);
                    if (origins.empty()) {
                        static uint32 lastABRegionLog=0;
                        if(bgType==MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin && now-lastABRegionLog>15000) {
                            lastABRegionLog=now;
                            TC_LOG_ERROR(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_JOIN_BLOCKED reason=regional_pool human=%u realm=%u language=%u team=%u",
                                humanGuid,human->originRealm,uint32(languageGroup),uint32(team));
                        }
                        continue;
                    }
                    originRealm = origins[urand(0, uint32(origins.size() - 1))];
                }
                uint32 account = 0;
                for (auto const& vacant : m_dynamicLeases)
                    if (vacant.second.Available())
                    {
                        account = vacant.first;
                        break;
                    }
                if (!account)
                {
                    CreateDynamicAccount();
                    stopBGWave = true;
                    allocated = true;
                    break;
                }
                uint32 roleTeamSize=bg->GetMaxPlayersPerTeam();
                if(contextInstance)if(auto*running=sBattlegroundMgr->GetBattleground(contextInstance,MS::Battlegrounds::BattlegroundTypeId::None))roleTeamSize=running->GetMaxPlayersPerTeam();
                uint8 const wantedRole=balanceRoles?uint8(FelsoriaBGRole::Registry().Reserve(account,
                    {uint32(bgType),uint32(bracket),contextInstance,uint32(team),roleTeamSize},now,14,true)):0;
                if(balanceRoles&&!wantedRole)continue;
                DynamicLease& lease = m_dynamicLeases[account];
                lease.role=wantedRole;
                TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_BG_ROLE_BALANCE_V1 reserve account=%u bg=%u bracket=%u instance=%u team=%u mask=%u size=%u",
                    account,uint32(bgType),uint32(bracket),contextInstance,uint32(team),uint32(wantedRole),roleTeamSize);
                lease.purpose = 3;
                lease.bgOriginRealm = originRealm;
                lease.bgLanguageGroup = languageGroup;
                lease.targetInstance = contextInstance;
                lease.queueId = uint32(bgType);
                lease.humanGuid = humanGuid;
                lease.team = team;
                lease.targetLevel = std::min(uint32(entry->MaxLevel), uint32(110));
                lease.changedAt = now - 3001;
                lease.allocatedAt = now;
                if(FelsoriaGilneasEntry::Fast(lease.purpose,lease.queueId))FelsoriaGilneasEntry::Record(now,account,"reserved",0,0,0,0);
                if(bgType==MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin)
                    TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_RESERVE account=%u team=%u level=%u human=%u realm=%u",
                        account,uint32(team),lease.targetLevel,humanGuid,originRealm);
                ++instanceCount;
                if(balanceRoles)m_dynamicBGTeamCursor = uint8(1-side);
                allocatedThisPass = true;
                if(!balanceRoles)stopBGWave=true; // Excluded modes retain one reservation per call.
                allocated = true;
                break;
            }
            if (allocated)
                break;
        }
        if (allocated)
            break;
    }
        if(!allocatedThisPass || stopBGWave)break;
    }

    if (now - m_dynamicLastCreate >= 500 && !humans.empty())
    {
        uint32 const batch = uint32(std::max(1, std::min(8,
            sConfigMgr->GetIntDefault("LegionForge.DynamicBots.SpawnBatch", 8))));
        uint32 requested = 0;
        std::vector<DynamicSnapshot> spawnRequests;
        for (DynamicSnapshot const& human : field)
            for (auto const& point : human.placements)
            {
                DynamicSnapshot request = human;
                request.placements.clear();
                request.placementX = point.x; request.placementY = point.y;
                request.placementZ = point.z; request.placementO = point.o;
                request.placementArea = point.area;
                spawnRequests.push_back(std::move(request));
            }
        for (DynamicSnapshot const& human : spawnRequests)
        {
            if (!human.placementValid)
                continue;
            // A snapshot is reused for up to a second while allocations run
            // every 500 ms. Consume its selected point until it is refreshed.
            bool placementTaken = false;
            for (auto const& active : m_dynamicLeases)
                if (active.second.purpose == 1 && !active.second.retiring &&
                    SameSpawnNeighborhood(human.map, human.instance,
                        human.placementX, human.placementY,
                        active.second.targetMap, active.second.targetInstance,
                        active.second.x, active.second.y))
                {
                    placementTaken = true;
                    break;
                }
            if (placementTaken)
                continue;
            uint32 matching = 0;
            for (auto const& entry : m_dynamicLeases)
                if (entry.second.purpose == 1 && !entry.second.retiring &&
                    entry.second.humanGuid == human.guid &&
                    entry.second.targetMap == human.map &&
                    entry.second.targetZone == human.zone)
                    ++matching;
            if (matching >= wanted[human.guid] || worldCount >= worldLimit)
                continue;
            uint32 account = 0;
            for (auto const& entry : m_dynamicLeases)
                if (entry.second.Available())
                {
                    account = entry.first;
                    break;
                }
            if (!account)
            {
                CreateDynamicAccount();
                for (auto const& vacant : m_dynamicLeases)
                    if (vacant.second.Available())
                    { account = vacant.first; break; }
                if (!account)
                    break;
            }
            uint32 counts[2] = {};
            for (auto const& other : m_dynamicLeases)
                if (other.second.purpose == 1 && !other.second.retiring && other.second.humanGuid == human.guid &&
                    other.second.targetMap == human.map && other.second.targetZone == human.zone &&
                    other.second.targetInstance == human.instance)
                    ++counts[other.second.team == TEAM_ALLIANCE ? 0 : 1];
            int forcedTeam = -1;
            uint32 const starter = StarterZone(human.map, human.zone, human.placementArea);
            // Both factions have a native Pandaren race on the Wandering Isle.
            if (starter && starter != PandarenStarterZone)
            {
                uint8 races[2]; StarterRaces(starter, human.team, races);
                if (races[0] != RACE_NONE) forcedTeam = int(PlayerBotRaceTeam(races[0]));
            }
            if (IsNightborneStartingHub(human.map, human.zone, human.placementArea)) forcedTeam = TEAM_HORDE;
            int const selectedTeam = FelsoriaDynamicPartyPolicy::PickTeam(factionTargets(human), counts[0], counts[1],
                human.territory == FACTION_MASK_ALLIANCE, human.territory == FACTION_MASK_HORDE, forcedTeam);
            if (selectedTeam < 0) continue; // Wait for a legal point for the missing faction.
            DynamicLease& lease = m_dynamicLeases[account];
            lease.purpose = 1;
            lease.humanGuid = human.guid;
            lease.targetMap = human.map;
            lease.targetZone = human.zone;
            lease.targetArea = human.placementArea;
            lease.targetInstance = human.instance;
            lease.phase = human.phase;
            lease.phaseIds = human.phaseIds;
            lease.targetLevel = human.level == 110 ? 110 :
                uint32(std::max(1, std::min(110, int32(human.level) + irand(-3, 3))));
            if (HeroStarterClass(human.map) == CLASS_DEATH_KNIGHT)
                lease.targetLevel = std::max(uint32(55), lease.targetLevel);
            else if (HeroStarterClass(human.map) == CLASS_DEMON_HUNTER)
                lease.targetLevel = std::max(uint32(98), lease.targetLevel);
            if (AlliedDedicatedStarterRace(human.map))
                lease.targetLevel = std::max(uint32(20), lease.targetLevel);
            lease.x = human.placementX; lease.y = human.placementY;
            lease.z = human.placementZ; lease.o = human.placementO;
            lease.team = uint8(selectedTeam);
            lease.changedAt = now - 3001;
            lease.allocatedAt = now;
            ReuseDynamicWorldBot(account);
            ++current[human.guid];
            ++worldCount;
            if (++requested >= batch)
                break;
        }
        CreateDynamicCharacter();
    }

    if (!m_dynamicLastStatus || now - m_dynamicLastStatus >= 60000)
    {
        uint32 online = 0, placed = 0, preparing = 0, retiring = 0, quarantined = 0;
        for (auto const& entry : m_dynamicLeases)
        {
            auto bot = snapshots.find(entry.first);
            if (bot != snapshots.end() && bot->second.bot) ++online;
            if (entry.second.placed && !entry.second.retiring) ++placed;
            if (entry.second.creating) ++preparing;
            if (entry.second.retiring) ++retiring;
            if (entry.second.quarantined) ++quarantined;
        }
        TC_LOG_INFO(LOG_FILTER_PLAYER,
            "DYNAMIC_BOT_STATUS humans=%u contexts=%u target=%u reservedWorld=%u online=%u placed=%u creating=%u retiring=%u instances=%u usableAccounts=%u quarantined=%u",
            uint32(humans.size()), uint32(field.size()), worldLimit, worldCount, online, placed, preparing, retiring, instanceCount,
            DynamicUsableAccountCount(), quarantined);
        m_dynamicLastStatus = now;
    }

    uint32 loginInFlight = 0;
    uint32 const maxLogins = uint32(std::max(1, std::min(16,
        sConfigMgr->GetIntDefault("LegionForge.DynamicBots.MaxInFlightLogins", 8))));
    for (auto const& held : m_dynamicLeases)
        if (!held.second.quarantined)
            if (PlayerBotSession* bot = dynamic_cast<PlayerBotSession*>(sWorld->FindSession(held.first).get()))
                if (bot->DynamicOperationBusy() && !bot->DynamicCreateOperation()) ++loginInFlight;
    // Session-owned operations retain their existing concurrency caps.
    std::vector<uint32> dispatchAccounts;for(auto const&entry:m_dynamicLeases)dispatchAccounts.push_back(entry.first);
    std::stable_sort(dispatchAccounts.begin(),dispatchAccounts.end(),[&](uint32 a,uint32 b){
        auto const&x=m_dynamicLeases.at(a);auto const&y=m_dynamicLeases.at(b);
        return FelsoriaGilneasEntry::LoginPriority(x.purpose,x.queueId)<FelsoriaGilneasEntry::LoginPriority(y.purpose,y.queueId);
    });
    for(uint32 account:dispatchAccounts)
    {
        auto&entry=*m_dynamicLeases.find(account);
        DynamicLease& lease = entry.second;
        if (!lease.purpose || !lease.guid || lease.retiring)
            continue;
        if (lease.purpose == 4)
        {
            if (!DynamicGroupHasConnectedHumanGuid(lease.guid))
                continue;
        }
        PlayerBotSession* session = dynamic_cast<PlayerBotSession*>(sWorld->FindSession(entry.first).get());
        if (!session)
        {
            auto info = m_idPlayerBotBase.find(entry.first);
            if (info != m_idPlayerBotBase.end())
                UpPlayerBotSessionByBaseInfo(info->second, false);
            continue;
        }
        auto snapshot = snapshots.find(entry.first);
        if (lease.purpose == 4 && snapshot != snapshots.end() && snapshot->second.ready)
        {
            DynamicSnapshot const* owner = nullptr;
            for (DynamicSnapshot const& human : humans)
                if ((human.group || IsRememberedDynamicPartyOwner(lease.guid, human.guid)) &&
                    !human.lfgGroup && !human.battleground && !human.busy &&
                    (std::find(human.groupMembers.begin(), human.groupMembers.end(), lease.guid) != human.groupMembers.end() ||
                        IsRememberedDynamicPartyOwner(lease.guid, human.guid)))
                {
                    if (!owner || human.guid == lease.humanGuid || human.guid == human.groupLeader) owner = &human;
                    if (human.guid == human.groupLeader) break;
                }
            if (owner)
            {
                session->RequestDynamicPhase(owner->map, owner->instance, owner->phase, owner->phaseIds);
                if (FelsoriaDynamicPartyPolicy::NeedsFollow(snapshot->second.map, snapshot->second.instance,
                    snapshot->second.x, snapshot->second.y, snapshot->second.z, owner->map, owner->instance,
                    owner->x, owner->y, owner->z))
                    session->RequestDynamicPartySummon(owner->guid);
            }
        }
        uint32 const summonLeader = session->DynamicPartySummonLeader();
        if (summonLeader && snapshot != snapshots.end())
        {
            for (DynamicSnapshot const& human : humans)
                if (human.guid == summonLeader && (human.group || IsRememberedDynamicPartyOwner(lease.guid, human.guid)) &&
                    !human.lfgGroup &&
                    (std::find(human.groupMembers.begin(), human.groupMembers.end(), lease.guid) != human.groupMembers.end() ||
                        IsRememberedDynamicPartyOwner(lease.guid, human.guid)) &&
                    !human.battleground && !human.busy &&
                    (!human.dungeon || (AlliedDedicatedStarterRace(human.map) &&
                        IsDynamicRaceAllowedAt(human.map, human.zone, human.area, snapshot->second.race, snapshot->second.team)) ||
                        GetDynamicAreaRequiredClass(human.map, human.zone, human.area) == snapshot->second.klass) &&
                    (!GetDynamicAreaRequiredClass(human.map, human.zone, human.area) ||
                        GetDynamicAreaRequiredClass(human.map, human.zone, human.area) == snapshot->second.klass))
                {
                    session->RequestDynamicPlace(human.map, human.x, human.y, human.z, 0.0f,
                        human.phase, human.phaseIds, human.instance, summonLeader,
                        GetDynamicAreaRequiredClass(human.map, human.zone, human.area));
                    break;
                }
            if (!snapshot->second.group && !IsRememberedDynamicParty(lease.guid))
                session->CancelDynamicPartySummon(summonLeader);
            continue;
        }
        if (snapshot == snapshots.end())
        {
            if (FelsoriaGilneasEntry::LoginDelayPassed(lease.purpose,lease.queueId,uint32(now-lease.changedAt)))
            {
                if (FelsoriaDynamicMetrics::Enabled() && !lease.perfLoginAt)
                    lease.perfLoginAt = getMSTime();
                if (!session->DynamicOperationBusy() && loginInFlight < maxLogins)
                {
                    uint64 operation = ++m_dynamicNextOperation;
                    if (session->RequestDynamicLogin(lease.guid, lease.purpose == 1, operation))
                    {
                        lease.operation = operation;
                        lease.writes = session->DynamicWriteFence();
                        if(FelsoriaGilneasEntry::Fast(lease.purpose,lease.queueId))FelsoriaGilneasEntry::Record(now,entry.first,"login_requested",lease.guid,0,0,uint32(now-lease.allocatedAt));
                        lease.loginStartedAt = now;
                        lease.changedAt = now;
                        ++loginInFlight;
                    }
                }
            }
        }
        else if (lease.purpose == 1)
        {
            session->RequestDynamicPhase(lease.targetMap, lease.targetInstance,
                lease.phase, lease.phaseIds);
            if (lease.placed && (snapshot->second.map != lease.targetMap ||
                snapshot->second.zone != lease.targetZone ||
                snapshot->second.instance != lease.targetInstance))
            {
                if (DynamicGroupHasHumanGuid(lease.guid))
                    parkProtected(entry.first, lease);
                else
                    RetireDynamicCharacter(entry.first);
                continue;
            }
            if (!lease.placed)
            {
                session->RequestDynamicPlace(lease.targetMap, lease.x, lease.y, lease.z, lease.o,
                    lease.phase, lease.phaseIds, lease.targetInstance, 0,
                    GetDynamicAreaRequiredClass(lease.targetMap, lease.targetZone, lease.targetArea));
                lease.placed = session->DynamicPlacementComplete();
            }
        }
        else if (lease.purpose == 2 && snapshot->second.ready && !lease.queueScheduled && lease.dungeonCount)
        {
            BotGlobleSchedule setting(BotGlobleScheduleType::BGSType_Settting, 0);
            setting.parameter1 = lease.targetLevel;
            setting.parameter2 = lease.targetLevel;
            setting.parameter3 = GetScheduleTalentByLFGRequirement(
                lfg::LfgRoles(lease.role), snapshot->second.klass) + 1;
            if (snapshot->second.klass == CLASS_DRUID && lease.role == lfg::LfgRoles::PLAYER_ROLE_HEALER)
                setting.parameter3 = 5; // Session maps this to explicit Restoration.
            else if (snapshot->second.klass == CLASS_DRUID && lease.role == lfg::LfgRoles::PLAYER_ROLE_TANK)
                setting.parameter3 = 6; // Session maps this to explicit Guardian.
            BotGlobleSchedule queue(BotGlobleScheduleType::BGSType_InLFGQueue, 0);
            queue.parameter1 = lease.role;
            queue.parameter2 = lease.dungeonCount;
            queue.parameter3 = lease.dungeons[0];
            queue.parameter4 = lease.dungeons[1];
            queue.parameter5 = lease.dungeons[2];
            session->RequestDynamicQueue(setting, queue);
            lease.queueScheduled = true;
        }
        else if (lease.purpose == 3 && snapshot->second.ready && !lease.queueScheduled)
        {
            BotGlobleSchedule setting(BotGlobleScheduleType::BGSType_Settting, 0);
            setting.parameter1 = lease.targetLevel;
            setting.parameter2 = lease.targetLevel;
            setting.parameter3=4; // Existing random specialization for excluded modes only.
            if(lease.role){
                setting.parameter3=FelsoriaBGRole::SelectCode(snapshot->second.klass,lease.role,0,
                    [](unsigned c,unsigned i){return sDB2Manager.GetChrSpecializationByIndex(c,i);},
                    [](unsigned n){return urand(0,n-1);});
                if(!setting.parameter3){FelsoriaBGRole::Registry().Cancel(entry.first,now);RetireDynamicCharacter(entry.first);continue;}
            }
            BotGlobleSchedule queue(BotGlobleScheduleType::BGSType_InBGQueue, 0);
            queue.parameter1 = lease.queueId;
            queue.parameter2 = lease.role;
            session->RequestDynamicQueue(setting, queue);
            lease.queueScheduled = true;
        }
    }

    if(sConfigMgr->GetBoolDefault("LegionForge.DynamicBots.Diagnostics.Enable",false) &&
       (!m_dynamicLastBGEntryTrace || uint32(now-m_dynamicLastBGEntryTrace)>=10000)){
        unsigned waitingCreate=0,creating=0,waitingLogin=0,preparing=0,queued=0,invited=0;uint32 oldest=0;
        for(auto const&held:m_dynamicLeases){
            auto const&lease=held.second;if(lease.purpose!=3||lease.retiring||lease.quarantined)continue;
            auto snap=snapshots.find(held.first);if(snap!=snapshots.end()&&snap->second.battleground)continue;
            oldest=std::max(oldest,uint32(now-lease.allocatedAt));
            if(lease.creating)++creating;
            else if(!lease.guid)++waitingCreate;
            else if(snap==snapshots.end())++waitingLogin;
            else if(snap->second.bgInvited)++invited;
            else if(snap->second.bgQueue)++queued;
            else ++preparing;
        }
        if(waitingCreate+creating+waitingLogin+preparing+queued+invited)
            TC_LOG_WARN(LOG_FILTER_PLAYER,"FELSORIA_BG_ENTRY_V1_2 wait_create=%u creating=%u wait_login=%u preparing=%u queued=%u invited=%u oldest_ms=%u wave=%u",
                waitingCreate,creating,waitingLogin,preparing,queued,invited,oldest,bgAllocationBatch);
        m_dynamicLastBGEntryTrace=now;
    }

    // FELSORIA_DYNAMIC_V2_BASELINE: inspect existing snapshots only, under the lease lock.
    if (FelsoriaDynamicMetrics::Enabled())
    {
        uint32 const measuredNow = getMSTime();
        uint32 active = 0, allocated = 0, creating = 0, pendingCreate = 0,
            pendingLogin = 0, retiring = 0, unready = 0, oldest = 0;
        for (auto& item : m_dynamicLeases)
        {
            DynamicLease& lease = item.second;
            if (!lease.purpose) continue;
            if (lease.retiring) { ++retiring; continue; }
            ++allocated;
            if (lease.creating) ++creating;
            if (!lease.guid && !lease.creating) ++pendingCreate;
            auto observed = m_dynamicSnapshots.find(item.first);
            bool const ready = observed != m_dynamicSnapshots.end() && observed->second.ready &&
                measuredNow - observed->second.updated < 5000;
            if (lease.guid && observed == m_dynamicSnapshots.end()) ++pendingLogin;
            if (!ready) ++unready;
            bool const placedReady = lease.purpose == 1 && lease.placed && ready &&
                observed->second.map == lease.targetMap && observed->second.zone == lease.targetZone &&
                observed->second.instance == lease.targetInstance && observed->second.phase == lease.phase &&
                observed->second.phaseIds == lease.phaseIds;
            if (placedReady) ++active;
            if (!placedReady && lease.purpose == 1)
                oldest = std::max(oldest, measuredNow - lease.allocatedAt);
            if (placedReady && !lease.perfActiveReported)
            {
                lease.perfActiveReported = true;
                if (FelsoriaDynamicMetrics::Get().events.load(std::memory_order_relaxed))
                    TC_LOG_INFO(LOG_FILTER_PLAYER,
                        "DYNAMIC_V2_BOT account=%u guid=%u map=%u zone=%u allocation_to_active_ms=%u queue_to_create_ms=%u create_callback_ms=%u callback_to_login_ms=%u login_to_ready_ms=%u ready_to_active_ms=%u timestamps_complete=%u",
                        item.first, lease.guid, lease.targetMap, lease.targetZone,
                        measuredNow - lease.allocatedAt,
                        lease.perfCreateAt ? lease.perfCreateAt - lease.allocatedAt : 0,
                        lease.perfCreatedAt && lease.perfCreateAt ? lease.perfCreatedAt - lease.perfCreateAt : 0,
                        lease.perfLoginAt && lease.perfCreatedAt ? lease.perfLoginAt - lease.perfCreatedAt : 0,
                        lease.perfReadyAt && lease.perfLoginAt ? lease.perfReadyAt - lease.perfLoginAt : 0,
                        lease.perfReadyAt ? measuredNow - lease.perfReadyAt : 0,
                        uint32(lease.perfCreateAt && lease.perfCreatedAt && lease.perfLoginAt && lease.perfReadyAt));
            }
        }
        auto& metrics = FelsoriaDynamicMetrics::Get();
        metrics.activeWorld.store(active); metrics.allocated.store(allocated);
        metrics.creating.store(creating); metrics.pendingCreate.store(pendingCreate);
        metrics.pendingLogin.store(pendingLogin); metrics.retiring.store(retiring);
        metrics.unready.store(unready); metrics.queueOldestMs.store(oldest);
        for (DynamicSnapshot const& human : field)
        {
            DynamicPerfContext& context = m_dynamicPerfContexts[human.account];
            if (context.startedAt != human.perfContextAt)
            {
                context = DynamicPerfContext();
                context.startedAt = human.perfContextAt;
                TC_LOG_INFO(LOG_FILTER_PLAYER,
                    "DYNAMIC_V2_ZONE_BEGIN human=%u map=%u zone=%u instance=%u phase=%u target=%u demand_age_ms=%u",
                    human.guid, human.map, human.zone, human.instance, human.phase, zoneLimit,
                    measuredNow - context.startedAt);
            }
            uint32 present = 0;
            for (auto const& item : m_dynamicLeases)
            {
                DynamicLease const& lease = item.second;
                auto observed = m_dynamicSnapshots.find(item.first);
                if (lease.purpose == 1 && !lease.retiring && lease.placed &&
                    lease.humanGuid == human.guid && lease.targetMap == human.map &&
                    lease.targetZone == human.zone && lease.targetInstance == human.instance &&
                    observed != m_dynamicSnapshots.end() && observed->second.ready &&
                    measuredNow - observed->second.updated < 5000 &&
                    observed->second.map == human.map && observed->second.zone == human.zone &&
                    observed->second.instance == human.instance && observed->second.phase == human.phase &&
                    observed->second.phaseIds == human.phaseIds)
                    ++present;
            }
            if (present && !context.firstReported)
            {
                context.firstReported = true;
                TC_LOG_INFO(LOG_FILTER_PLAYER,
                    "DYNAMIC_V2_ZONE_FIRST human=%u map=%u zone=%u ready=%u elapsed_ms=%u",
                    human.guid, human.map, human.zone, present, measuredNow - context.startedAt);
            }
            if (zoneLimit && present >= zoneLimit && !context.fullReported)
            {
                context.fullReported = true;
                TC_LOG_INFO(LOG_FILTER_PLAYER,
                    "DYNAMIC_V2_ZONE_FULL human=%u map=%u zone=%u ready=%u target=%u elapsed_ms=%u",
                    human.guid, human.map, human.zone, present, zoneLimit, measuredNow - context.startedAt);
            }
        }
        for (auto it = m_dynamicPerfContexts.begin(); it != m_dynamicPerfContexts.end(); )
            if (std::none_of(humans.begin(), humans.end(), [&](DynamicSnapshot const& human)
                { return human.account == it->first; }))
                it = m_dynamicPerfContexts.erase(it);
            else ++it;
    }
}

void PlayerBotMgr::OnAccountBotCreate(ObjectGuid const& guid, uint32 accountId, std::string const& name, uint8 gender, uint8 race, uint8 playerClass, uint8 level)
{
    PlayerBotBaseInfo* pInfo = GetAccountBotAccountInfo(accountId);
    if (!pInfo)
        return;
    uint32 id = guid.GetGUIDLow();
    if (pInfo->characters.find(id) != pInfo->characters.end())
    {
        return;
    }
    pInfo->characters[id] = PlayerBotCharBaseInfo(id, accountId, name, uint16(race), uint16(playerClass), uint16(gender), uint16(level));
}

void PlayerBotMgr::OnAccountBotDelete(ObjectGuid& guid, uint32 accountId)
{
    PlayerBotBaseInfo* pInfo = GetAccountBotAccountInfo(accountId);
    if (!pInfo)
        return;
    pInfo->RemoveCharacterByGUID(guid);
}

void PlayerBotMgr::OnPlayerBotLogin(WorldSession* pSession, Player* pPlayer)
{
    ++m_BotOnlineCount;
    Group* pGroup = pPlayer->GetGroup();
    if (pGroup)
    {
        if (!pGroup->GroupExistRealPlayer() &&
            !(DynamicBotsEnabled() && IsDynamicBotPartyProtected(pPlayer)))
        {
            pPlayer->RemoveFromGroup(RemoveMethod::GROUP_REMOVEMETHOD_LEAVE);
        }
        else
            SwitchPlayerBotAI(pPlayer, PlayerBotAIType::PBAIT_GROUP, false);
    }
    else
        SwitchPlayerBotAI(pPlayer, PlayerBotAIType::PBAIT_FIELD, false);

    if (pPlayer->GetBattleground())
    {
        pPlayer->LeaveBattleground();
        if (pSession)
            pSession->HandleWorldPortAck();
    }

    // Move PlayerBot hero classes out of their isolated introductory maps.
    // This runs only while they are still in a DK or DH starting scenario.
    bool const deathKnightStart =
        pPlayer->getClass() == CLASS_DEATH_KNIGHT &&
        pPlayer->GetMapId() == 609;

    bool const demonHunterStart =
        pPlayer->getClass() == CLASS_DEMON_HUNTER &&
        pPlayer->GetMapId() == 1481;

    if ((deathKnightStart || demonHunterStart) &&
        !(DynamicBotsEnabled() && pSession && GetDynamicBotTargetLevel(pSession->GetAccountId())))
    {
        WorldLocation factionHome;
        uint32 homeArea = 0;

        if (pPlayer->GetTeamId() == TEAM_ALLIANCE)
        {
            // Alliance arrival point near Stormwind.
            factionHome = WorldLocation(
                0, -9076.77f, 424.74f, 92.42f, 3.14159f);
            homeArea = 9;
        }
        else
        {
            // Horde arrival point near Orgrimmar.
            factionHome = WorldLocation(
                1, 1349.72f, -4374.50f, 26.15f, 3.14159f);
            homeArea = 363;
        }

        pPlayer->SetHomebind(factionHome, homeArea);
        pPlayer->TeleportTo(factionHome);
        pPlayer->SaveToDB();

        TC_LOG_INFO(
            LOG_FILTER_PLAYER,
            "Moved hero PlayerBot %s (class %u) to faction home.",
            pPlayer->GetName(),
            uint32(pPlayer->getClass()));
    }
    BotUtility::RemoveArenaBotSpellsByPlayer(pPlayer);
    sPlayerBotTalkMgr->JoinDefaultChannel(pPlayer);
    if (pSession && !GetDynamicBotTargetLevel(pSession->GetAccountId()))
    {
        std::string outString;
        consoleToUtf8(std::string("PlayerBot status."), outString);
        sWorld->SendGlobalText((GetPlayerLinkText(pPlayer) + outString).c_str(), NULL);
    }
    if (PlayerBotSession* pBotSession = dynamic_cast<PlayerBotSession*>(pSession))
    {
        uint32 const dynamicLevel = GetDynamicBotTargetLevel(pSession->GetAccountId());
        uint32 const targetLevel = dynamicLevel
            ? PlayerBotSetting::CheckBotLevel(pPlayer, dynamicLevel)
            : 110;
        uint32 selectedTalentCount = 0;

        for (TalentEntry const* talentInfo : sTalentStore)
        {
            if (!talentInfo ||
                talentInfo->ClassID != pPlayer->getClass())
            {
                continue;
            }

            if (pPlayer->HasTalent(
                    talentInfo->ID,
                    pPlayer->GetActiveTalentGroup()))
            {
                ++selectedTalentCount;
            }
        }
        if (!IsPreservedDynamicAccount(pSession->GetAccountId()) &&
            !pBotSession->HasScheduleByType(BotGlobleScheduleType::BGSType_Settting) &&
            (pPlayer->getLevel() != targetLevel ||
             !pPlayer->EquipIsTidiness() ||
             selectedTalentCount < std::min(uint32(MAX_TALENT_TIERS),
                 pPlayer->GetUInt32Value(PLAYER_FIELD_MAX_TALENT_TIERS))))
        {
            uint32 curTType = pPlayer->FindTalentType();
            BotGlobleSchedule schedule2(BotGlobleScheduleType::BGSType_Settting, 0);
            schedule2.parameter1 = targetLevel;
            schedule2.parameter2 = targetLevel;
            schedule2.parameter3 = curTType + 1;
            if(dynamicLevel)
                for(BattlegroundTypeId bgType:m_BGTypes){
                    FelsoriaBGRole::Context plan;unsigned mask=0;FelsoriaBGRole::Stage stage=FelsoriaBGRole::Planned;
                    if(!FelsoriaBGRole::Registry().Plan(pSession->GetAccountId(),uint32(bgType),plan,mask,&stage) ||
                       stage==FelsoriaBGRole::Departed || FelsoriaBGRole::Role(mask)>2)continue;
                    unsigned code=FelsoriaBGRole::SelectCode(pPlayer->getClass(),mask,pPlayer->GetSpecializationId(),
                        [](unsigned c,unsigned i){return sDB2Manager.GetChrSpecializationByIndex(c,i);},
                        [](unsigned n){return urand(0,n-1);});
                    if(code)schedule2.parameter3=code;
                    break;
                }
            pBotSession->PushScheduleToQueue(schedule2);
        }
    }
}

void PlayerBotMgr::OnPlayerBotLogout(WorldSession* pSession)
{
    --m_BotOnlineCount;
    if (m_BotOnlineCount < 0) m_BotOnlineCount = 0;

    if (!GetDynamicBotTargetLevel(pSession->GetAccountId()))
    {
        std::string outString;
        consoleToUtf8(std::string("PlayerBot status."), outString);
        sWorld->SendGlobalText(outString.c_str(), NULL);
    }
    PlayerBotSession* pBotSession = dynamic_cast<PlayerBotSession*>(pSession);
    if (pBotSession && !pBotSession->HasScheduleByType(BotGlobleScheduleType::BGSType_Online) &&
        !pBotSession->HasScheduleByType(BotGlobleScheduleType::BGSType_Online_GUID))
        pBotSession->ClearAllSchedule();
}

void PlayerBotMgr::OnPlayerBotLeaveOriginalGroup(Player* pPlayer)
{
    SwitchPlayerBotAI(pPlayer, PlayerBotAIType::PBAIT_FIELD, true);
}

void PlayerBotMgr::LoginGroupBotByPlayer(Player* pPlayer)
{
#ifndef INCOMPLETE_BOT
    if (pPlayer->IsPlayerBot())
        return;
    int32 isok = sConfigMgr->GetIntDefault("pbot", 1);
    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup || pGroup->isBFGroup() || pGroup->isBGGroup())
        return;
    Group::MemberSlotList const& memList = pGroup->GetMemberSlots();
    for (Group::MemberSlot const& slot : memList)
    {
        ObjectGuid guid = slot.Guid;
        if (guid == pPlayer->GetGUID())
            continue;
        Player* friendPlayer = ObjectAccessor::FindPlayer(guid);
        if (friendPlayer)
            continue;
        if (isok==0)
            AddNewPlayerBotByGUID2(guid);
        else
            DelayLoginPlayerBotByGUID(guid);
    }
#endif
}

void PlayerBotMgr::LoginFriendBotByPlayer(Player* pPlayer)
{
    //#ifndef INCOMPLETE_BOT
    //	if (pPlayer->IsPlayerBot())
    //		return;
    //	int32 isok = sConfigMgr->GetIntDefault("pbot", 1);
    //
    //	std::vector<ObjectGuid> friends = pPlayer->GetSocial()->Get();
    //	for (ObjectGuid& guid : friends)
    //	{
    //
    //	if (isok==0)
    //		AddNewPlayerBotByGUID2(guid);
    //		else
    //		DelayLoginPlayerBotByGUID(guid);
    //	}
    //#else
    //	std::string allonlineText;
    // Removed unreadable legacy localized text.
    //	sWorld->SendGlobalText(allonlineText.c_str(), NULL);
    //#endif
}

void PlayerBotMgr::LogoutAllGroupPlayerBot(Group* pGroup, bool force)
{
    if (!pGroup)
        return;
    if (!force && pGroup->GroupExistRealPlayer())
        return;
    Group::MemberSlotList const& memList = pGroup->GetMemberSlots();
    for (Group::MemberSlot const& slot : memList)
    {
        ObjectGuid guid = slot.Guid;
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player || !player->IsPlayerBot())
            continue;
        if (DynamicBotsEnabled() && IsDynamicBotPartyProtected(player)) continue;
        WorldSession* pSession = player->GetSession();
        if (pSession)
            pSession->LogoutPlayer(false, "bot");
    }
}

bool PlayerBotMgr::AllPlayerLeaveBG(uint32 account)
{
    if (account == 0)
        return false;
    //std::unique_lock<std::mutex> sessionGuard(PlayerBotMgr::g_uniqueLock);
    WorldSession* pSession = sWorld->FindSession(account).get();
    if (pSession && pSession->IsBotSession() && !pSession->PlayerLoading())
    {
        Player* pPlayer = pSession->GetPlayer();
        if (!pPlayer || pPlayer->GetTransport() || !pPlayer->InBattleground())
            return false;
        const Battleground* bg = pPlayer->GetBattleground();
        if (!bg || bg->GetStatus() == BattlegroundStatus::STATUS_WAIT_LEAVE || bg->GetStatus() == BattlegroundStatus::STATUS_WAIT_JOIN)
            return false;
        ((Battleground*)bg)->EndBattleground((pPlayer->GetTeamId() == TEAM_ALLIANCE) ? HORDE : ALLIANCE);
        return true;
    }
    return false;
}

void PlayerBotMgr::AllPlayerBotRandomLogin(const char* name)
{
    int32 needOnline = m_MaxOnlineBot - m_BotOnlineCount;
    if (needOnline <= 0)
        return;

    std::set<uint32> scheduledAccounts;

    auto LoginCharacter =
        [this, &needOnline, &scheduledAccounts]
        (PlayerBotBaseInfo* pInfo, PlayerBotCharBaseInfo& charInfo) -> bool
    {
        if (!pInfo || needOnline <= 0)
            return false;

        if (scheduledAccounts.find(pInfo->id) != scheduledAccounts.end())
            return false;

        WorldSession* pSession = sWorld->FindSession(pInfo->id).get();
        if (!pSession || !pSession->IsBotSession() ||
            pSession->PlayerLoading() || pSession->GetPlayer())
            return false;

        WorldPacket worldPacket(CMSG_PLAYER_LOGIN);
        WorldPackets::Character::PlayerLogin cmd(std::move(worldPacket));
        cmd.Guid = ObjectGuid::Create<HighGuid::Player>(charInfo.guid);
        cmd.FarClip = 0.0f;

        pSession->HandlePlayerLoginOpcode(cmd);
        pSession->HandleContinuePlayerLogin();

        scheduledAccounts.insert(pInfo->id);
        --needOnline;
        return true;
    };

    // Preserve manual login by character name.
    if (name && name[0] != '\0')
    {
        for (auto& accountPair : m_idPlayerBotBase)
        {
            PlayerBotBaseInfo* pInfo = accountPair.second;
            if (!pInfo)
                continue;

            for (auto& characterPair : pInfo->characters)
            {
                PlayerBotCharBaseInfo& charInfo = characterPair.second;
                if (charInfo.name == name)
                {
                    LoginCharacter(pInfo, charInfo);
                    return;
                }
            }
        }
        return;
    }


    // Keep all twelve requested Legion-class bots online. This consists of
    // the two existing characters plus the ten newly generated characters
    // from each requested class.
    static uint8 const priorityOnlineClasses[] =
    {
        CLASS_DEMON_HUNTER,
        CLASS_DEATH_KNIGHT,
        CLASS_MAGE,
        CLASS_WARLOCK,
        CLASS_MONK
    };

    for (uint8 classIndex = 0;
        classIndex < 5 && needOnline > 0;
        ++classIndex)
    {
        uint8 const wantedClass = priorityOnlineClasses[classIndex];

        for (uint8 round = 0; round < 12 && needOnline > 0; ++round)
        {
            bool classScheduled = false;

            for (auto accountIt = m_idPlayerBotBase.rbegin();
                accountIt != m_idPlayerBotBase.rend();
                ++accountIt)
            {
                PlayerBotBaseInfo* pInfo = accountIt->second;
                if (!pInfo)
                    continue;

                if (scheduledAccounts.find(pInfo->id) !=
                    scheduledAccounts.end())
                {
                    continue;
                }

                for (auto& characterPair : pInfo->characters)
                {
                    PlayerBotCharBaseInfo& charInfo =
                        characterPair.second;

                    if (charInfo.profession != wantedClass)
                        continue;

                    if (LoginCharacter(pInfo, charInfo))
                    {
                        classScheduled = true;
                        break;
                    }
                }

                if (classScheduled)
                    break;
            }

            if (!classScheduled)
                break;
        }
    }

    // Reserve two slots for every other available class.
    for (uint8 round = 0; round < 2 && needOnline > 0; ++round)
    {
        for (uint16 wantedClass = CLASS_WARRIOR;
            wantedClass <= CLASS_DEMON_HUNTER && needOnline > 0;
            ++wantedClass)
        {
            if (wantedClass == CLASS_DEMON_HUNTER ||
                wantedClass == CLASS_DEATH_KNIGHT ||
                wantedClass == CLASS_MAGE ||
                wantedClass == CLASS_WARLOCK ||
                wantedClass == CLASS_MONK)
            {
                continue;
            }

            bool classScheduled = false;

            for (auto accountIt = m_idPlayerBotBase.rbegin();
                accountIt != m_idPlayerBotBase.rend();
                ++accountIt)
            {
                PlayerBotBaseInfo* pInfo = accountIt->second;
                if (!pInfo)
                    continue;

                if (scheduledAccounts.find(pInfo->id) !=
                    scheduledAccounts.end())
                {
                    continue;
                }

                for (auto& characterPair : pInfo->characters)
                {
                    PlayerBotCharBaseInfo& charInfo =
                        characterPair.second;

                    if (charInfo.profession != wantedClass)
                        continue;

                    if (LoginCharacter(pInfo, charInfo))
                    {
                        classScheduled = true;
                        break;
                    }
                }

                if (classScheduled)
                    break;
            }
        }
    }
    // Fill the remaining online slots after every class received its quota.
    for (auto accountIt = m_idPlayerBotBase.rbegin();
        accountIt != m_idPlayerBotBase.rend() && needOnline > 0;
        ++accountIt)
    {
        PlayerBotBaseInfo* pInfo = accountIt->second;
        if (!pInfo || pInfo->characters.empty())
            continue;

        if (scheduledAccounts.find(pInfo->id) != scheduledAccounts.end())
            continue;

        PlayerBotCharBaseInfo& charInfo = pInfo->characters.begin()->second;
        LoginCharacter(pInfo, charInfo);
    }
}
void PlayerBotMgr::AllPlayerBotLogout()
{
    //std::unique_lock<std::mutex> sessionGuard(PlayerBotMgr::g_uniqueLock);
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator it = m_idPlayerBotBase.begin();
        it != m_idPlayerBotBase.end();
        it++)
    {
        PlayerBotBaseInfo* pInfo = it->second;
        PlayerBotLogout(pInfo->id);
    }
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator it = m_idAccountBotBase.begin();
        it != m_idAccountBotBase.end();
        it++)
    {
        PlayerBotBaseInfo* pInfo = it->second;
        PlayerBotLogout(pInfo->id);
    }
}

bool PlayerBotMgr::PlayerBotLogout(uint32 account)
{
    if (account == 0)
        return false;
    //std::unique_lock<std::mutex> sessionGuard(PlayerBotMgr::g_uniqueLock);
    WorldSession* pSession = sWorld->FindSession(account).get();
    if (pSession && pSession->IsBotSession() && !pSession->PlayerLoading())
    {
        Player* pBot = pSession->GetPlayer();
        if (!pBot || !pBot->IsPlayerBot() || !pBot->IsSettingFinish())
            return false;
        if (DynamicBotsEnabled() && IsDynamicBotPartyProtected(pBot)) return false;
        PlayerBotSession* pBotSession = dynamic_cast<PlayerBotSession*>(pSession);
        if (pBotSession)
        {
            if (pBotSession->HasSchedules())
                pBotSession->ClearAllSchedule();

            pSession->LogoutPlayer(false, "bot");
            return true;
        }
    }
    return false;
}

void PlayerBotMgr::SupplementPlayerBot()
{
    // These five classes currently have two characters each.
    // Raise every class to twelve, creating exactly ten new characters
    // per class and fifty new characters in total.
    static uint8 const priorityBotClasses[] =
    {
        CLASS_DEMON_HUNTER,
        CLASS_DEATH_KNIGHT,
        CLASS_MAGE,
        CLASS_WARLOCK,
        CLASS_MONK
    };

    uint32 priorityClassCounts[5] = { 0, 0, 0, 0, 0 };

    // Count the already existing characters once before scheduling any
    // new characters. The counters remain valid for the whole pass.
    for (auto const& accountPair : m_idPlayerBotBase)
    {
        PlayerBotBaseInfo const* accountInfo = accountPair.second;
        if (!accountInfo)
            continue;

        for (auto const& characterPair : accountInfo->characters)
        {
            uint8 const characterClass =
                characterPair.second.profession;

            for (uint8 classIndex = 0; classIndex < 5; ++classIndex)
            {
                if (characterClass == priorityBotClasses[classIndex])
                {
                    ++priorityClassCounts[classIndex];
                    break;
                }
            }
        }
    }

    for (auto itInfo = m_idPlayerBotBase.begin();
        itInfo != m_idPlayerBotBase.end();
        ++itInfo)
    {
        PlayerBotBaseInfo* pInfo = itInfo->second;

        if (!pInfo || pInfo->username.empty())
            continue;

        // One character per PlayerBot account is enough for one
        // concurrent bot session.
        if (!pInfo->characters.empty() ||
            !pInfo->needCreateBots.empty())
        {
            continue;
        }

        WorldSession* pSession =
            sWorld->FindSession(pInfo->id).get();

        if (!pSession)
        {
            TC_LOG_INFO(
                LOG_FILTER_PLAYER,
                "server.loading",
                ">> Create bot, but session offline.");

            continue;
        }

        uint8 botClass = 0;

        // Fill every requested class to twelve. Increment immediately
        // when queued so this pass cannot over-schedule one class.
        for (uint8 classIndex = 0; classIndex < 5; ++classIndex)
        {
            if (priorityClassCounts[classIndex] < 12)
            {
                botClass = priorityBotClasses[classIndex];
                ++priorityClassCounts[classIndex];
                break;
            }
        }

        // All five requested classes reached twelve. Do not create
        // additional fallback characters.
        if (botClass == 0)
            break;

        bool const alliance = urand(0, 1) != 0;

        pInfo->needCreateBots.push(
            BuildCreatePlayerData(alliance, botClass));
    }

    CreateOncePlayerBot();
}
void PlayerBotMgr::OnRealPlayerJoinBattlegroundQueue(uint32 bgTypeId, uint32 level)
{
}

void PlayerBotMgr::OnRealPlayerLeaveBattlegroundQueue(uint32 bgTypeId, uint32 level)
{
    uint8 bgQueueTypeId = MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(bgTypeId, 0);
    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundTypeId(bgTypeId));
    if (!bg)
        return;
    PVPDifficultyEntry const* bracketEntry = sDB2Manager.GetBattlegroundBracketByLevel(bg->GetMapId(), level);
    if (!bracketEntry)
        return;

    if (bgQueue.ExistRealPlayer(bracketEntry))
        return;
    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>((WorldSession*)itSession->second.get());
        if (!pSession)
            continue;
        Player* player = pSession->GetPlayer();
        if (!player || player->isBeingLoaded())
            continue;
        if (player->InBattleground() || player->InArena() || !player->IsInWorld())
            continue;
        uint32 lv = player->getLevel();
        if (lv < bracketEntry->MinLevel || lv > bracketEntry->MaxLevel)
            continue;
        if (player->InBattlegroundQueue())
        {
            BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_OutBGQueue, 0);
            schedule1.parameter1 = bgTypeId;
            pSession->PushScheduleToQueue(schedule1);
        }
    }
}

void PlayerBotMgr::OnRealPlayerLeaveArenaQueue(uint32 bgTypeId, uint32 level, uint32 aaType)
{
    uint8 bgQueueTypeId = MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(bgTypeId, aaType);
    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundTypeId(bgTypeId));
    if (!bg)
        return;
    PVPDifficultyEntry const* bracketEntry = sDB2Manager.GetBattlegroundBracketByLevel(bg->GetMapId(), level);
    if (!bracketEntry)
        return;

    if (bgQueue.ExistRealPlayer(bracketEntry))
        return;
    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>((WorldSession*)itSession->second.get());
        if (!pSession)
            continue;
        Player* player = pSession->GetPlayer();
        if (!player || player->isBeingLoaded())
            continue;
        if (player->InBattleground() || player->InArena() || !player->IsInWorld())
            continue;
        uint32 lv = player->getLevel();
        if (lv < bracketEntry->MinLevel || lv > bracketEntry->MaxLevel)
            continue;
        if (player->InBattlegroundQueue())
        {
            BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_OutAAQueue, 0);
            schedule1.parameter1 = bgTypeId;
            schedule1.parameter2 = bracketEntry->RangeIndex;
            schedule1.parameter3 = aaType;
            pSession->PushScheduleToQueue(schedule1);
        }
    }
}

void PlayerBotMgr::OnRealPlayerEnterBattleground(uint32 bgTypeId, uint32 level)
{
}

void PlayerBotMgr::OnRealPlayerLeaveBattleground(const Player* player)
{
    Battleground* bg = player->GetBattleground();
    if (!bg || bg->GetStatus() != BattlegroundStatus::STATUS_WAIT_LEAVE)
        return;
    PVPDifficultyEntry const* bracketEntry = sDB2Manager.GetBattlegroundBracketByLevel(bg->GetMapId(), player->getLevel());
    if (!bracketEntry)
        return;

    auto& allPlayerIDs = bg->GetPlayers();
    for (auto itIDs = allPlayerIDs.begin(); itIDs != allPlayerIDs.end(); itIDs++)
    {
        Player* otherPlayer = ObjectAccessor::FindPlayer(itIDs->first);
        if (!otherPlayer || otherPlayer->isBeingLoaded())
            continue;
        if (!otherPlayer->InBattleground())
            continue;
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>((WorldSession*)otherPlayer->GetSession());
        if (pSession)
        {
            BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_LeaveBG, 0);
            pSession->PushScheduleToQueue(schedule1);
        }
    }
}

bool PlayerBotMgr::LoginBotByAccountIndex(uint32 account, uint32 index)
{
#ifndef INCOMPLETE_BOT
    PlayerBotBaseInfo* botInfo = GetPlayerBotAccountInfo(account);
    if (!botInfo)
        return false;
    if (index >= botInfo->characters.size())
        return false;
    for (auto itChar = botInfo->characters.begin();
        itChar != botInfo->characters.end();
        itChar++)
    {
        if (index > 0)
        {
            --index;
            continue;
        }
        PlayerBotCharBaseInfo& charInfo = itChar->second;
        WorldSession* pWorldSession = sWorld->FindSession(account).get();
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(pWorldSession);
        if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules() || pSession->GetPlayer())
            return false;
        BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online_GUID, charInfo.guid);
        pSession->PushScheduleToQueue(schedule1);
        return true;
    }
#endif
    return false;
}

bool PlayerBotMgr::AddNewPlayerBotByGUID(ObjectGuid& guid)
{
    int32 isok = sConfigMgr->GetIntDefault("pbot", 1);

    if (isok==0 && !guid)
        return false;

    int32 allianceCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_ALLIANCE, true);
    int32 hordeCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_HORDE, true);

    if ((allianceCount+hordeCount) >= m_MaxOnlineBot)
        return false;


    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return false;
            if (player->GetSession()->PlayerLoading())
                return false;

        }
    }

    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return false;
            if (player->GetSession()->PlayerLoading())
                return false;

        }
    }


    for (std::map<uint32, PlayerBotBaseInfo*>::iterator itInfo = m_idPlayerBotBase.begin();
        itInfo != m_idPlayerBotBase.end();
        itInfo++)
    {
        if (!(itInfo->second->ExistCharacterByGUID(guid)))
            continue;
        WorldSession* pWorldSession = sWorld->FindSession(itInfo->first).get();
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(pWorldSession);
        if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules())
            return false;
        if (Player* player = pSession->GetPlayer())
        {
            if (!player->GetGroup() && !player->isInCombat() && pSession->GetPlayer()->GetGUID() != guid)
            {
                BotGlobleSchedule schedule(BotGlobleScheduleType::BGSType_Offline, 0);
                pSession->PushScheduleToQueue(schedule);
            }
            else
                return false;
        }
        BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online_GUID, guid);
        pSession->PushScheduleToQueue(schedule1);
        return true;
    }

    for (std::map<uint32, PlayerBotBaseInfo*>::iterator itInfo = m_idAccountBotBase.begin();
        itInfo != m_idAccountBotBase.end();
        itInfo++)
    {
        if (!(itInfo->second->ExistCharacterByGUID(guid)))
            continue;
        WorldSession* pWorldSession = sWorld->FindSession(itInfo->first).get();
        if (pWorldSession)
        {
            PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(pWorldSession);
            if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules())
                return false;
            if (Player* player = pSession->GetPlayer())
            {
                if (!player->GetGroup() && !player->isInCombat() && pSession->GetPlayer()->GetGUID() != guid)
                {
                    BotGlobleSchedule schedule(BotGlobleScheduleType::BGSType_Offline, 0);
                    pSession->PushScheduleToQueue(schedule);
                }
                else
                    return false;
            }
            BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online_GUID, guid);
            pSession->PushScheduleToQueue(schedule1);
            return true;
        }
        else
        {
            PlayerBotSession* pSession = UpPlayerBotSessionByBaseInfo(itInfo->second, true);
            if (!pSession)
                return false;
            BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online_GUID, guid);
            pSession->PushScheduleToQueue(schedule1);
            return true;
        }
    }
    return false;
}


bool PlayerBotMgr::AddNewPlayerBotByGUID2(ObjectGuid& guid)
{
    int32 allianceCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_ALLIANCE, true);
    int32 hordeCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_HORDE, true);

    if ((allianceCount+hordeCount) >= m_MaxOnlineBot) return false;


    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return false;
            if (player->GetSession()->PlayerLoading())
                return false;

        }
    }

    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return false;
            if (player->GetSession()->PlayerLoading())
                return false;

        }
    }

    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return false;
            if (player->GetSession()->PlayerLoading())
                return false;

        }
    }

    for (std::map<uint32, PlayerBotBaseInfo*>::iterator itInfo = m_idPlayerBotBase.begin();
        itInfo != m_idPlayerBotBase.end();
        itInfo++)
    {
        if (!(itInfo->second->ExistCharacterByGUID(guid)))
            continue;
        WorldSession* pWorldSession = sWorld->FindSession(itInfo->first).get();
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(pWorldSession);
        if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules())
            return false;
        if (Player* player = pSession->GetPlayer())
        {
            if (!player->GetGroup() && !player->isInCombat() && pSession->GetPlayer()->GetGUID() != guid)
            {
                BotGlobleSchedule schedule(BotGlobleScheduleType::BGSType_Offline, 0);
                pSession->PushScheduleToQueue(schedule);
            }
            else
                return false;
        }
        BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online_GUID, guid);
        pSession->PushScheduleToQueue(schedule1);
        return true;
    }

    for (std::map<uint32, PlayerBotBaseInfo*>::iterator itInfo = m_idAccountBotBase.begin();
        itInfo != m_idAccountBotBase.end();
        itInfo++)
    {
        if (!(itInfo->second->ExistCharacterByGUID(guid)))
            continue;
        WorldSession* pWorldSession = sWorld->FindSession(itInfo->first).get();
        if (pWorldSession)
        {
            PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(pWorldSession);
            if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules())
                return false;
            if (Player* player = pSession->GetPlayer())
            {
                if (!player->GetGroup() && !player->isInCombat() && pSession->GetPlayer()->GetGUID() != guid)
                {
                    BotGlobleSchedule schedule(BotGlobleScheduleType::BGSType_Offline, 0);
                    pSession->PushScheduleToQueue(schedule);
                }
                else
                    return false;
            }
            BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online_GUID, guid);
            pSession->PushScheduleToQueue(schedule1);
            return true;
        }
        else
        {
            PlayerBotSession* pSession = UpPlayerBotSessionByBaseInfo(itInfo->second, true);
            if (!pSession)
                return false;
            BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online_GUID, guid);
            pSession->PushScheduleToQueue(schedule1);
            return true;
        }
    }
    return false;
}

void PlayerBotMgr::AddNewPlayerBot(bool faction, Classes prof, uint32 count)
{

    int32 isok = sConfigMgr->GetIntDefault("pbot", 1);
    if (isok==0 && prof == CLASS_NONE)
        return;

    int32 allianceCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_ALLIANCE, true);
    int32 hordeCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_HORDE, true);

    if ((allianceCount+hordeCount) >= m_MaxOnlineBot)
        return;

    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {
        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return;
            if (player->GetSession()->PlayerLoading())
                return;

        }
    }


    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return;
            if (player->GetSession()->PlayerLoading())
                return;

        }
    }

    if (count <= 0)
        return;



    if (prof == CLASS_NONE)
    {
        uint32 rndCls = 0;
        while (rndCls == 0 || rndCls > 12)
        {
            rndCls = urand(Classes::CLASS_WARRIOR, Classes::CLASS_DEMON_HUNTER);
        }
        prof = Classes(rndCls);
    }
#ifdef INCOMPLETE_BOT
    if (prof != 1 && prof != 5 && prof != 9)
    {
        if (prof == 2 || prof == 6)
            prof = Classes::CLASS_WARRIOR;
        else if (prof == 3 || prof == 4 || prof == 8 || prof == 10)
            prof = Classes::CLASS_WARLOCK;
        else if (prof == 7 || prof == 11)
            prof = Classes::CLASS_PRIEST;
    }
#endif
    //const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {
        if (count <= 0)
            return;
        if (!itSession->second->IsBotSession())
            continue;
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>((WorldSession*)itSession->second.get());
        if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules() || pSession->IsAccountBotSession())
            continue;
        Player* player = pSession->GetPlayer();
        if (player)
            continue;
        BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online, 0);
        if (faction)
        {
            schedule1.parameter1 = 1;
        }
        else
        {
            schedule1.parameter1 = 2;
        }
        schedule1.parameter2 = prof;
        pSession->PushScheduleToQueue(schedule1);
        --count;
    }

    if (count > 0)
    {
        std::string allonlineText;
        consoleToUtf8(std::string("|cffff8800PlayerBot status.|r"), allonlineText);
        sWorld->SendGlobalText(allonlineText.c_str(), NULL);
    }
}

void PlayerBotMgr::AddNewAccountBot(bool faction, Classes prof)
{
    int32 isok = sConfigMgr->GetIntDefault("pbotall", 1);
    if (isok==0)
        return;

    int32 allianceCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_ALLIANCE, true);
    int32 hordeCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_HORDE, true);

    if ((allianceCount+hordeCount) >= m_MaxOnlineBot)
        return;


    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return;
            if (player->GetSession()->PlayerLoading())
                return;

        }
    }

    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return;
            if (player->GetSession()->PlayerLoading())
                return;

        }
    }
    std::string allonlineText;
#ifdef INCOMPLETE_BOT
    consoleToUtf8(std::string("|cffff8800PlayerBot status.|r"), allonlineText);
    sWorld->SendGlobalText(allonlineText.c_str(), NULL);
    return;
#endif
    if (prof == CLASS_NONE)
    {
        uint32 rndCls = 0;
        while (rndCls == 0 || rndCls > 12)
        {
            rndCls = urand(Classes::CLASS_WARRIOR, Classes::CLASS_DEMON_HUNTER);
        }
        prof = Classes(rndCls);
    }
    //std::set<uint32> hasSessionIDs;
//	const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {
        if (!itSession->second->IsBotSession())
            continue;
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>((WorldSession*)itSession->second.get());
        if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules() || !pSession->IsAccountBotSession())
            continue;
        //hasSessionIDs.insert(pSession->GetAccountId());
        Player* player = pSession->GetPlayer();
        if (player)
            continue;
        PlayerBotBaseInfo* pBaseInfo = GetAccountBotAccountInfo(pSession->GetAccountId());
        if (!pBaseInfo)
            continue;
        if (!pBaseInfo->ExistClass(faction, prof))
            continue;
        BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online, 0);
        if (faction)
            schedule1.parameter1 = 1;
        else
            schedule1.parameter1 = 2;
        schedule1.parameter2 = prof;
        pSession->PushScheduleToQueue(schedule1);
        return;
    }

    for (std::map<uint32, PlayerBotBaseInfo*>::iterator it = m_idAccountBotBase.begin();
        it != m_idAccountBotBase.end();
        it++)
    {
        PlayerBotBaseInfo* pInfo = it->second;
        WorldSession* pSession = sWorld->FindSession(pInfo->id).get();
        if (pSession)
            continue;
        if (!pInfo->ExistClass(faction, prof))
            continue;
        PlayerBotSession* pBotSession = UpPlayerBotSessionByBaseInfo(pInfo, true);
        if (pBotSession)
        {
            BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online, 0);
            if (faction)
                schedule1.parameter1 = 1;
            else
                schedule1.parameter1 = 2;
            schedule1.parameter2 = prof;
            pBotSession->PushScheduleToQueue(schedule1);
            return;
        }
    }

    consoleToUtf8(std::string("|cffff8800PlayerBot status.|r"), allonlineText);
    sWorld->SendGlobalText(allonlineText.c_str(), NULL);
}

void PlayerBotMgr::AddNewPlayerBotByClass(uint32 count, Classes prof)
{
    int32 isok = sConfigMgr->GetIntDefault("pbotall", 1);
    if (isok==0)
        return;

    int32 allianceCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_ALLIANCE, true);
    int32 hordeCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_HORDE, true);

    if ((allianceCount+hordeCount) >= m_MaxOnlineBot) return;

    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return;
            if (player->GetSession()->PlayerLoading())
                return;

        }
    }


    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return;
            if (player->GetSession()->PlayerLoading())
                return;

        }
    }

#ifdef INCOMPLETE_BOT
    if (prof != 1 && prof != 5 && prof != 9)
    {
        if (prof == 2 || prof == 6)
            prof = Classes::CLASS_WARRIOR;
        else if (prof == 3 || prof == 4 || prof == 8 || prof == 10)
            prof = Classes::CLASS_WARLOCK;
        else if (prof == 7 || prof == 11)
            prof = Classes::CLASS_PRIEST;
    }
#endif

    //	const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {
        if (allianceCount <= 0 && hordeCount <= 0)
            return;
        if (!itSession->second->IsBotSession())
            continue;
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>((WorldSession*)itSession->second.get());
        if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules() || pSession->IsAccountBotSession())
            continue;
        Player* player = pSession->GetPlayer();
        if (player && player->getClass() != prof)
            continue;
        if (player)
        {
            if (player->GetTeamId() == TeamId::TEAM_ALLIANCE)
                --allianceCount;
            else
                --hordeCount;
        }
        else
        {
            BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online, 0);
            if (allianceCount > 0)
            {
                schedule1.parameter1 = 1;
                --allianceCount;
            }
            else if (hordeCount > 0)
            {
                schedule1.parameter1 = 2;
                --hordeCount;
            }
            else
                continue;
            schedule1.parameter2 = prof;
            pSession->PushScheduleToQueue(schedule1);
        }
    }

    if (allianceCount > 0 || hordeCount > 0)
    {
        std::string allonlineText;
        consoleToUtf8(std::string("|cffff8800PlayerBot status.|r"), allonlineText);
        sWorld->SendGlobalText(allonlineText.c_str(), NULL);
    }
}

bool PlayerBotMgr::ChangePlayerBotSetting(uint32 account, uint32 minLV, uint32 maxLV, uint32 talent)
{
    if (account == 0)
        return false;
    maxLV = PlayerBotSetting::CheckMaxLevel(maxLV);
    if (maxLV < minLV)
        maxLV = minLV;
    WorldSession* pWorldSession = sWorld->FindSession(account).get();
    if (pWorldSession && pWorldSession->IsBotSession() && !pWorldSession->PlayerLoading())
    {
        Player* player = pWorldSession->GetPlayer();
        if (!player || player->InBattlegroundQueue() || player->InBattleground() ||
            player->GetBattleground() || player->isInCombat())
            return false;
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(pWorldSession);
        if (!pSession || pSession->HasSchedules())
            return false;
        BotGlobleSchedule schedule2(BotGlobleScheduleType::BGSType_Settting, 0);
        schedule2.parameter1 = minLV;
        schedule2.parameter2 = maxLV;
        schedule2.parameter3 = talent + 1;
        pSession->PushScheduleToQueue(schedule2);
        return true;
    }
    return false;
}

void PlayerBotMgr::AddNewPlayerBotToBG(TeamId team, uint32 minLV, uint32 maxLV, BattlegroundTypeId bgTypeID)
{
    // Rotate eligible accounts, randomizing ties. History is intentionally
    // in memory: restart starts a new rotation and never edits account data.
    static std::map<uint32, uint64> lastBGSelection;
    static uint64 selectionSequence = 0;
    std::vector<PlayerBotSession*> onlineCandidates;
    std::vector<PlayerBotSession*> offlineCandidates;

    for (auto const& entry : sWorld->GetAllSessions())
    {
        if (!entry.second->IsBotSession())
            continue;
        PlayerBotSession* session = dynamic_cast<PlayerBotSession*>(entry.second.get());
        if (!session || !session->BattlegroundRetryAllowed() || session->PlayerLoading() || session->HasSchedules() ||
            session->IsAccountBotSession())
            continue;

        Player* player = session->GetPlayer();
        if (!player)
        {
            offlineCandidates.push_back(session);
            continue;
        }

        if (player->isBeingLoaded() || !player->IsInWorld() ||
            player->IsBeingTeleported() || player->isInCombat() ||
            player->InBattleground() || player->InArena() ||
            player->GetMap()->IsDungeon() || player->isUsingLfg() ||
            player->InBattlegroundQueue() || !player->IsSettingFinish() ||
            player->GetTeamId() != team)
            continue;
        onlineCandidates.push_back(session);
    }

    // Prefer already online bots. An unrelated login no longer blocks every
    // eligible candidate; loading players were filtered individually above.
    auto const& candidates = onlineCandidates.empty() ? offlineCandidates : onlineCandidates;
    if (candidates.empty())
        return;

    std::vector<PlayerBotSession*> oldest;
    uint64 oldestSelection = 0;
    for (PlayerBotSession* candidate : candidates)
    {
        auto const found = lastBGSelection.find(candidate->GetAccountId());
        uint64 const selectedAt = found == lastBGSelection.end() ? 0 : found->second;
        if (oldest.empty() || selectedAt < oldestSelection)
        {
            oldest.clear();
            oldestSelection = selectedAt;
        }
        if (selectedAt == oldestSelection)
            oldest.push_back(candidate);
    }
    PlayerBotSession* session = oldest[urand(0, uint32(oldest.size() - 1))];
    lastBGSelection[session->GetAccountId()] = ++selectionSequence;

    maxLV = PlayerBotSetting::CheckMaxLevel(maxLV);
    if (maxLV < minLV)
        maxLV = minLV;
    bool const online = session->GetPlayer() != nullptr;
    if (!online)
    {
        BotGlobleSchedule login(BotGlobleScheduleType::BGSType_Online, 0);
        login.parameter1 = team == TEAM_ALLIANCE ? 1 : 2;
        session->PushScheduleToQueue(login);
    }

    BotGlobleSchedule setting(BotGlobleScheduleType::BGSType_Settting, 0);
    setting.parameter1 = online ? maxLV : minLV;
    setting.parameter2 = maxLV;
    setting.parameter3 = 4; // Keep mixed/random specializations.
    session->PushScheduleToQueue(setting);

    BotGlobleSchedule queue(BotGlobleScheduleType::BGSType_InBGQueue, 0);
    queue.parameter1 = bgTypeID;
    session->PushScheduleToQueue(queue);

    TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,
        "BG_BOT_ROTATION account=%u team=%u bgType=%u eligible=%u",
        session->GetAccountId(), uint32(team), uint32(bgTypeID), uint32(candidates.size()));
}

//void PlayerBotMgr::AddNewPlayerBotToLFG(lfg::LFGBotRequirement* botRequirement)
//{
//	if (!botRequirement || botRequirement->selectedDungeons.empty())
//		return;
//		
//	int32 isok = sConfigMgr->GetIntDefault("pbotall", 1);
//	if (isok==0)
//		return;
//
//int32 isoka = sConfigMgr->GetIntDefault("pbotasl", 88);
//
//	int32 allianceCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_ALLIANCE, true);
//	int32 hordeCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_HORDE, true);
//
//if ((allianceCount+hordeCount) >isoka) return;
//
//
//	const SessionMap& allSession = sWorld->GetAllSessions();
//	for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
//	{
//
//		Player* player = itSession->second->GetPlayer();
//		if (player)
//{
//		if (player->isBeingLoaded())
//			return;
//if (player->GetSession()->PlayerLoading())
//return;
////if (player->isInCombat())
////return;
//	}
//	}
//	
//
//	//const SessionMap& allSession = sWorld->GetAllSessions();
//	for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
//	{
//
//		Player* player = itSession->second->GetPlayer();
//		if (player)
//{
//		if (player->isBeingLoaded())
//			return;
//if (player->GetSession()->PlayerLoading())
//return;
////if (player->isInCombat())
////return;
//	}
//	}
//		
//	std::vector<uint32> duns;
//	for (lfg::LfgDungeonSet::iterator itLfgSet = botRequirement->selectedDungeons.begin(); itLfgSet != botRequirement->selectedDungeons.end(); itLfgSet++)
//	{
//		uint32 dun = *itLfgSet;
//		duns.push_back(dun);
//	}
////	const SessionMap& allSession = sWorld->GetAllSessions();
//	for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
//	{
//		if (!itSession->second->IsBotSession())
//			continue;
//		PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>((WorldSession*)itSession->second);
//		if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules() || pSession->IsAccountBotSession())
//			continue;
//		Player* player = pSession->GetPlayer();
//		if (!player)
//		{
//			BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online, 0);
//			if (!FillOnlineBotScheduleByLFGRequirement(botRequirement, &schedule1))
//				continue;
//			pSession->PushScheduleToQueue(schedule1);
//
//			BotGlobleSchedule schedule2(BotGlobleScheduleType::BGSType_Settting, 0);
//			schedule2.parameter1 = botRequirement->needLevel;
//			schedule2.parameter2 = botRequirement->needLevel;
//			schedule2.parameter3 = GetScheduleTalentByLFGRequirement(botRequirement->needRole, schedule1.parameter2) + 1;
//			pSession->PushScheduleToQueue(schedule2);
//
//			BotGlobleSchedule schedule3(BotGlobleScheduleType::BGSType_InLFGQueue, 0);
//			schedule3.parameter1 = uint32(botRequirement->needRole);
//			uint32 count = botRequirement->selectedDungeons.size();
//			schedule3.parameter2 = (count > 3) ? 3 : count;
//			if (count >= 1)
//				schedule3.parameter3 = duns[0];
//			if (count >= 2)
//				schedule3.parameter4 = duns[1];
//			if (count >= 3)
//				schedule3.parameter5 = duns[2];
//			pSession->PushScheduleToQueue(schedule3);
//
//			return;
//		}
//		else if (IsIDLEPlayerBot(player))
//		{
//			if (player->GetTeamId() != botRequirement->needTeam)
//				continue;
//			lfg::LfgRoles playerRole = lfg::LfgRoles::PLAYER_ROLE_NONE;
//			if (BotFieldAI* pFieldAI = dynamic_cast<BotFieldAI*>(player->GetAI()))
//			{
//				if ((player->getClass() == 1 && player->FindTalentType() == 2) || (player->getClass() == 2 && player->FindTalentType() == 1))
//					playerRole = lfg::LfgRoles::PLAYER_ROLE_TANK;
//				else if (pFieldAI->IsHealerBotAI())
//					playerRole = lfg::LfgRoles::PLAYER_ROLE_HEALER;
//				else
//					playerRole = lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
//			}
//			else if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
//			{
//				if (pGroupAI->IsTankBotAI())
//					playerRole = lfg::LfgRoles::PLAYER_ROLE_TANK;
//				else if (pGroupAI->IsHealerBotAI())
//					playerRole = lfg::LfgRoles::PLAYER_ROLE_HEALER;
//				else
//					playerRole = lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
//			}
//			if (playerRole != botRequirement->needRole)
//				continue;
//
//			BotGlobleSchedule schedule2(BotGlobleScheduleType::BGSType_Settting, 0);
//			schedule2.parameter1 = botRequirement->needLevel;
//			schedule2.parameter2 = botRequirement->needLevel;
//			schedule2.parameter3 = player->FindTalentType() + 1;
//			pSession->PushScheduleToQueue(schedule2);
//
//			BotGlobleSchedule schedule3(BotGlobleScheduleType::BGSType_InLFGQueue, 0);
//			schedule3.parameter1 = uint32(botRequirement->needRole);
//			uint32 count = botRequirement->selectedDungeons.size();
//			schedule3.parameter2 = (count > 3) ? 3 : count;
//			if (count >= 1)
//				schedule3.parameter3 = duns[0];
//			if (count >= 2)
//				schedule3.parameter4 = duns[1];
//			if (count >= 3)
//				schedule3.parameter5 = duns[2];
//			pSession->PushScheduleToQueue(schedule3);
//
//			return;
//		}
//	}
//
//	std::string allonlineText;
// Removed unreadable legacy localized text.
//	sWorld->SendGlobalText(allonlineText.c_str(), NULL);
//}

void PlayerBotMgr::AddNewPlayerBotToAA(TeamId team, BattlegroundTypeId bgTypeID, uint32 bracketID, uint32 aaType)
{
    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return;
            if (player->GetSession()->PlayerLoading())
                return;
            //if (player->isInCombat())
            //return;
        }
    }

    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {

        Player* player = itSession->second->GetPlayer();
        if (player)
        {
            if (player->isBeingLoaded())
                return;
            if (player->GetSession()->PlayerLoading())
                return;
            //if (player->isInCombat())
            //return;
        }
    }


    if (aaType != 2 && aaType != 3 && aaType != 5)
        return;
    //	const SessionMap& allSession = sWorld->GetAllSessions();
    std::vector<uint32> rndIDs;
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
        rndIDs.push_back(itSession->first);
    Trinity::Containers::RandomShuffle(rndIDs);
    for (uint32 account : rndIDs)
    {
        WorldSession* pWorldSession = sWorld->FindSession(account).get();
        if (!pWorldSession || !pWorldSession->IsBotSession())
            continue;
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(pWorldSession);
        if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules() || pSession->IsAccountBotSession())
            continue;
        Player* player = pSession->GetPlayer();
        if (player)
        {
            if (!player || player->isBeingLoaded() || !player->IsInWorld())
                continue;
            if (player->InBattleground() || player->InArena() || player->GetMap()->IsDungeon() || player->isUsingLfg())
                continue;
            if (player->InBattlegroundQueue())
                continue;
            if (!player->IsSettingFinish())
                continue;
            if (player->GetTeamId() != team)
                continue;

            BotGlobleSchedule schedule2(BotGlobleScheduleType::BGSType_Settting, 0);
            schedule2.parameter1 = 110;
            schedule2.parameter2 = 110;
            schedule2.parameter3 = 4;
            pSession->PushScheduleToQueue(schedule2);

            BotGlobleSchedule schedule3(BotGlobleScheduleType::BGSType_InAAQueue, 0);
            schedule3.parameter1 = 14;
            schedule3.parameter2 = 4;
            pSession->PushScheduleToQueue(schedule3);

            return;
        }
    }

    for (uint32 account : rndIDs)
    {
        WorldSession* pWorldSession = sWorld->FindSession(account).get();
        if (!pWorldSession || !pWorldSession->IsBotSession())
            continue;
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(pWorldSession);
        if (!pSession || pSession->PlayerLoading() || pSession->HasSchedules() || pSession->IsAccountBotSession())
            continue;
        Player* player = pSession->GetPlayer();
        if (!player)
        {
            BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online, 0);
            schedule1.parameter1 = (team == TeamId::TEAM_ALLIANCE) ? 1 : 2;
            pSession->PushScheduleToQueue(schedule1);

            BotGlobleSchedule schedule2(BotGlobleScheduleType::BGSType_Settting, 0);
            schedule2.parameter1 = 110;
            schedule2.parameter2 = 110;
            schedule2.parameter3 = 4;
            pSession->PushScheduleToQueue(schedule2);

            BotGlobleSchedule schedule3(BotGlobleScheduleType::BGSType_InAAQueue, 0);
            schedule3.parameter1 = 14;
            schedule3.parameter2 = 4;
            pSession->PushScheduleToQueue(schedule3);

            return;
        }
    }

    std::string allonlineText;
    consoleToUtf8(std::string("|cffff8800PlayerBot status.|r"), allonlineText);
    sWorld->SendGlobalText(allonlineText.c_str(), NULL);
}

void PlayerBotMgr::AddTeamBotToRatedArena(uint32 arenaTeamId)
{
    //	int32 isok = sConfigMgr->GetIntDefault("pbot", 1);
    //	if (isok==0)
    //		return;
    //
    //int32 isoka = sConfigMgr->GetIntDefault("pbotasl", 88);
    //
    //	int32 allianceCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_ALLIANCE, true);
    //	int32 hordeCount = (int32)sPlayerBotMgr->GetOnlineBotCount(TEAM_HORDE, true);
    //
    //if ((allianceCount+hordeCount) >isoka) return;
    //
    //
    //	const SessionMap& allSession = sWorld->GetAllSessions();
    //	for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    //	{
    //
    //		Player* player = itSession->second->GetPlayer();
    //		if (player)
    //{
    //		if (player->isBeingLoaded())
    //			return;
    //if (player->GetSession()->PlayerLoading())
    //return;
    //
    ////if (player->isInCombat())
    ////return;
    //	}
    //	}
    //	
    //
    //	if (arenaTeamId == 0)
    //		return;
    //	ArenaTeam* arenaTeam = sArenaTeamMgr->GetArenaTeamById(arenaTeamId);
    //	if (!arenaTeam)
    //		return;
    //	BattlegroundQueueTypeId bgQueueTypeID = BattlegroundMgr::BGQueueTypeId(BattlegroundTypeId(BattlegroundTypeId::BATTLEGROUND_AA), arenaTeam->GetType());
    //	BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeID);
    //	for (ArenaTeam::MemberList::iterator itaMem = arenaTeam->m_membersBegin(); itaMem != arenaTeam->m_membersEnd(); itaMem++)
    //	{
    //		ArenaTeamMember& mem = *itaMem;
    //		ObjectGuid guid = mem.Guid;
    //		if (bgQueue.ExistQueueByRatedArena(guid, true))
    //			continue;
    //		if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
    //		{
    //			if (!player->IsPlayerBot() || player->GetGroup())
    //				continue;
    //			if (PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(player->GetSession()))
    //			{
    //				BotGlobleSchedule schedule2(BotGlobleScheduleType::BGSType_Settting, 0);
    //				schedule2.parameter1 = 80;
    //				schedule2.parameter2 = 80;
    //				schedule2.parameter3 = sArenaTeamMgr->FindArenaTeamPlayerBotTalent(guid) + 1;
    //				schedule2.parameter4 = (pSession->IsAccountBotSession()) ? 0 : 1;
    //				pSession->PushScheduleToQueue(schedule2);
    //
    //				BotGlobleSchedule schedule3(BotGlobleScheduleType::BGSType_InAAQueue, 0);
    //				schedule3.parameter1 = BattlegroundTypeId::BATTLEGROUND_AA;
    //				if (arenaTeam->GetType() == 2)
    //					schedule3.parameter2 = 0;
    //				else if (arenaTeam->GetType() == 3)
    //					schedule3.parameter2 = 1;
    //				else if (arenaTeam->GetType() == 5)
    //					schedule3.parameter2 = 2;
    //				schedule3.parameter3 = 1;
    //				pSession->PushScheduleToQueue(schedule3);
    //
    //				BotGlobleSchedule schedule4(BotGlobleScheduleType::BGSType_EnterAA, 0);
    //				schedule4.parameter1 = BattlegroundTypeId::BATTLEGROUND_AA;
    //				schedule4.parameter2 = 14;
    //				schedule4.parameter3 = arenaTeam->GetType();
    //				schedule4.parameter4 = 1;
    //				pSession->PushScheduleToQueue(schedule4);
    //			}
    //		}
    //		else if (PlayerBotSession* pSession = GetBotSessionByCharGUID(guid))
    //		{
    //			Player* player = pSession->GetPlayer();
    //			if (player)
    //			{
    //				BotGlobleSchedule schedule(BotGlobleScheduleType::BGSType_Offline, 0);
    //				pSession->PushScheduleToQueue(schedule);
    //			}
    //			BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_Online_GUID, 0);
    //			schedule1.parameter1 = guid.GetCounter();
    //			pSession->PushScheduleToQueue(schedule1);
    //
    //			BotGlobleSchedule schedule2(BotGlobleScheduleType::BGSType_Settting, 0);
    //			schedule2.parameter1 = 80;
    //			schedule2.parameter2 = 80;
    //			schedule2.parameter3 = sArenaTeamMgr->FindArenaTeamPlayerBotTalent(guid) + 1;
    //			schedule2.parameter4 = (pSession->IsAccountBotSession()) ? 0 : 1;
    //			pSession->PushScheduleToQueue(schedule2);
    //
    //			BotGlobleSchedule schedule3(BotGlobleScheduleType::BGSType_InAAQueue, 0);
    //			schedule3.parameter1 = BattlegroundTypeId::BATTLEGROUND_AA;
    //			if (arenaTeam->GetType() == 2)
    //				schedule3.parameter2 = 0;
    //			else if (arenaTeam->GetType() == 3)
    //				schedule3.parameter2 = 1;
    //			else if (arenaTeam->GetType() == 5)
    //				schedule3.parameter2 = 2;
    //			schedule3.parameter3 = 1;
    //			pSession->PushScheduleToQueue(schedule3);
    //
    //			BotGlobleSchedule schedule4(BotGlobleScheduleType::BGSType_EnterAA, 0);
    //			schedule4.parameter1 = BattlegroundTypeId::BATTLEGROUND_AA;
    //			schedule4.parameter2 = 14;
    //			schedule4.parameter3 = arenaTeam->GetType();
    //			schedule4.parameter4 = 1;
    //			pSession->PushScheduleToQueue(schedule4);
    //		}
    //	}
}

//bool PlayerBotMgr::FillOnlineBotScheduleByLFGRequirement(lfg::LFGBotRequirement* botRequirement, BotGlobleSchedule* botSchedule)
//{
//	if (!botRequirement || !botSchedule)
//		return false;
//	botSchedule->parameter1 = 0;
//	if (botRequirement->needTeam == TEAM_ALLIANCE)
//		botSchedule->parameter1 = 1;
//	else if (botRequirement->needTeam == TEAM_HORDE)
//		botSchedule->parameter1 = 2;
//	else
//		return false;
//	std::vector<uint32> matchClasses;
//	botSchedule->parameter2 = 0;
//	if (botRequirement->needRole == lfg::LfgRoles::PLAYER_ROLE_TANK)
//	{
//		matchClasses.push_back(1);
//#ifndef INCOMPLETE_BOT
//		matchClasses.push_back(2);
//#endif
//		//matchClasses.push_back(6);
//	}
//	else if (botRequirement->needRole == lfg::LfgRoles::PLAYER_ROLE_HEALER)
//	{
//		matchClasses.push_back(5);
//#ifndef INCOMPLETE_BOT
//		matchClasses.push_back(2);
//		matchClasses.push_back(7);
//		matchClasses.push_back(11);
//#endif
//	}
//	else if (botRequirement->needRole == lfg::LfgRoles::PLAYER_ROLE_DAMAGE)
//	{
//#ifndef INCOMPLETE_BOT
//		matchClasses.push_back(3);
//		matchClasses.push_back(4);
//		matchClasses.push_back(8);
//#endif
//		matchClasses.push_back(9);
//	}
//	if (matchClasses.empty())
//		return false;
//	botSchedule->parameter2 = matchClasses[urand(0, matchClasses.size() - 1)];
//	return true;
//}

uint32 PlayerBotMgr::GetScheduleTalentByLFGRequirement(lfg::LfgRoles roles, uint32 botCls)
{
    uint32 talentType = 3;
    switch (botCls)
    {
        case 1:
            if (roles == lfg::LfgRoles::PLAYER_ROLE_TANK)
                talentType = 2;
            else
                talentType = urand(0, 1);
            break;
        case 2:
            if (roles == lfg::LfgRoles::PLAYER_ROLE_TANK)
                talentType = 1;
            else if (roles == lfg::LfgRoles::PLAYER_ROLE_HEALER)
                talentType = 0;
            else
                talentType = 2;
            break;
        case 6:
            if (roles == lfg::LfgRoles::PLAYER_ROLE_TANK)
                talentType = 0; // Blood is the first DK specialization.
            else
            {
                talentType = urand(1, 2);
            }
            break;
        case 5:
            if (roles == lfg::LfgRoles::PLAYER_ROLE_DAMAGE)
                talentType = 2;
            else
                talentType = urand(0, 1);
            break;
        case 7:
            if (roles == lfg::LfgRoles::PLAYER_ROLE_HEALER)
                talentType = 2;
            else
                talentType = urand(0, 1);
            break;
        case 11:
            if (roles == lfg::LfgRoles::PLAYER_ROLE_HEALER)
                talentType = 3;
            else if (roles == lfg::LfgRoles::PLAYER_ROLE_TANK)
                talentType = 2;
            else
                talentType = urand(0, 1);
            break;
        case CLASS_MONK:
            talentType = roles == lfg::LfgRoles::PLAYER_ROLE_TANK ? 0 :
                roles == lfg::LfgRoles::PLAYER_ROLE_HEALER ? 1 : 2;
            break;
        case CLASS_DEMON_HUNTER:
            talentType = roles == lfg::LfgRoles::PLAYER_ROLE_TANK ? 1 : 0;
            break;
    }
    return talentType;
}

lfg::LfgRoles PlayerBotMgr::GetPlayerBotCurrentLFGRoles(Player* player)
{
    if (!player)
        return lfg::LfgRoles::PLAYER_ROLE_NONE;
    uint32 talentType = player->FindTalentType();
    switch (player->getClass())
    {
        case 1:
            if (talentType == 2)
                return lfg::LfgRoles::PLAYER_ROLE_TANK;
            else
                return lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
            break;
        case 2:
            if (talentType == 0)
                return lfg::LfgRoles::PLAYER_ROLE_HEALER;
            else if (talentType == 1)
                return lfg::LfgRoles::PLAYER_ROLE_TANK;
            else
                return lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
            break;
        case 6:
            if (talentType == 1)
                return lfg::LfgRoles::PLAYER_ROLE_TANK;
            else
                return lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
            break;
        case 3:
        case 4:
        case 8:
        case 9:
            return lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
            break;
        case 5:
            if (talentType == 2)
                return lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
            else
                return lfg::LfgRoles::PLAYER_ROLE_HEALER;
            break;
        case 7:
            if (talentType == 2)
                return lfg::LfgRoles::PLAYER_ROLE_HEALER;
            else
                return lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
            break;
        case 11:
            if (talentType == 2)
                return lfg::LfgRoles::PLAYER_ROLE_HEALER;
            else
                return lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
            break;
        default:
            return lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
    }
    return lfg::LfgRoles::PLAYER_ROLE_DAMAGE;
}

ObjectGuid PlayerBotMgr::GetNoArenaMatchCharacter(TeamId team, uint32 arenaType, Classes cls, std::vector<ObjectGuid>& fliters)
{
    if (team != TEAM_ALLIANCE && team != TEAM_HORDE)
        return ObjectGuid::Empty;
    if (arenaType != 2 && arenaType != 3 && arenaType != 5)
        return ObjectGuid::Empty;
    if (m_idPlayerBotBase.empty())
        return ObjectGuid::Empty;
    std::vector<PlayerBotBaseInfo*> allBotBaseInfos;
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator itInfo = m_idPlayerBotBase.begin();
        itInfo != m_idPlayerBotBase.end();
        itInfo++)
        allBotBaseInfos.push_back(itInfo->second);
    for (uint32 i = allBotBaseInfos.size() - 1; i >= 0; --i)
    {
        PlayerBotBaseInfo* pInfo = allBotBaseInfos[i];
        uint32 id = pInfo->GetCharIDByNoArenaType((team == TEAM_ALLIANCE) ? true : false, cls, arenaType, fliters);
        if (id == 0)
            continue;
        bool exist = false;
        for (ObjectGuid& guid : fliters)
        {
            if (guid.GetCounter() == id)
            {
                exist = true;
                break;
            }
        }
        if (!exist)
            return ObjectGuid::Create<HighGuid::Player>(id);
    }

    //for (std::map<uint32, PlayerBotBaseInfo*>::iterator itInfo = m_idPlayerBotBase.begin();
    //	itInfo != m_idPlayerBotBase.end();
    //	itInfo++)
    //{
    //	PlayerBotBaseInfo* pInfo = itInfo->second;
    //	uint32 id = pInfo->GetCharIDByNoArenaType((team == TEAM_ALLIANCE) ? true : false, cls, arenaType, fliters);
    //	if (id == 0)
    //		continue;
    //	bool exist = false;
    //	for (ObjectGuid& guid : fliters)
    //	{
    //		if (guid.GetCounter() == id)
    //		{
    //			exist = true;
    //			break;
    //		}
    //	}
    //	if (!exist)
    //		return ObjectGuid(uint64(id));
    //}
    return ObjectGuid::Empty;
}

std::string PlayerBotMgr::GetNameANDClassesText(ObjectGuid& guid)
{
    for (std::map<uint32, PlayerBotBaseInfo*>::iterator itInfo = m_idPlayerBotBase.begin();
        itInfo != m_idPlayerBotBase.end();
        itInfo++)
    {
        PlayerBotBaseInfo* pInfo = itInfo->second;
        std::string text = pInfo->GetCharNameANDClassesText(guid);
        if (!text.empty())
            return text;
    }
    return "";
}

bool PlayerBotMgr::CanReadyArenaByArenaTeamID(uint32 arenaTeamId)
{

    return true;
}

void PlayerBotMgr::QueryBattlegroundRequirement()
{
    for (BattleGroundTypes::iterator itType = m_BGTypes.begin(); itType != m_BGTypes.end(); itType++)
    {
        BattlegroundTypeId bgTypeID = *itType;
        Battleground* bg_template = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeID);
        if (!bg_template)
            continue;
        auto& bgFreeSlotQueues = sBattlegroundMgr->BGFreeSlotQueue;
        for (auto itr = bgFreeSlotQueues.begin(); itr != bgFreeSlotQueues.end(); itr++)
        {
            Battleground* bg = *itr;
            if (!bg->ExistRealPlayer())
                continue;
            uint32 minLV = bg->GetMinLevel();
            uint32 maxLV = bg->GetMaxLevel();
            if (!bg->IsRated() && bg->GetStatus() > STATUS_WAIT_QUEUE && bg->GetStatus() < STATUS_WAIT_LEAVE)
            {
                if (bg->GetFreeSlotsForTeam(Team::ALLIANCE) > 0 && bg->GetFreeSlotsForTeam(Team::ALLIANCE) >= bg->GetFreeSlotsForTeam(Team::HORDE))
                    AddNewPlayerBotToBG(TeamId::TEAM_ALLIANCE, minLV, maxLV, bgTypeID);
                else if (bg->GetFreeSlotsForTeam(Team::HORDE) > 0)
                    AddNewPlayerBotToBG(TeamId::TEAM_HORDE, minLV, maxLV, bgTypeID);
                else
                    continue;
                return;
            }
        }

        uint8 bgQueueTypeID = MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(bgTypeID, 0);
        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeID);
        for (uint8 j = 0; j < MS::Battlegrounds::MaxBrackets; j++)
        {
            uint8 bracket = j;
            PVPDifficultyEntry const* bracketEntry = sDB2Manager.GetBattlegroundBracketById(bg_template->GetMapId(), bracket);
            if (!bracketEntry)
                continue;
            if (!bgQueue.ExistRealPlayer(bracketEntry))
                continue;
            int32 needAlliancePlayerCount = 0;
            int32 needHordePlayerCount = 0;
            if (bgQueue.QueryNeedPlayerCount(bgTypeID, bracket, 0, needAlliancePlayerCount, needHordePlayerCount))
            {
                if (needAlliancePlayerCount > 0 && needAlliancePlayerCount >= needHordePlayerCount)
                    AddNewPlayerBotToBG(TeamId::TEAM_ALLIANCE, bracketEntry->MinLevel, bracketEntry->MaxLevel, bgTypeID);
                else if (needHordePlayerCount > 0)
                    AddNewPlayerBotToBG(TeamId::TEAM_HORDE, bracketEntry->MinLevel, bracketEntry->MaxLevel, bgTypeID);
                else
                    continue;
                return;
            }
        }
    }
}

void PlayerBotMgr::QueryRatedArenaRequirement()
{
    if (m_ArenaSearchTick > 0)
    {
        --m_ArenaSearchTick;
        return;
    }
    Battleground* bg_template = sBattlegroundMgr->GetBattlegroundTemplate(MS::Battlegrounds::BattlegroundTypeId::ArenaAll);
    if (!bg_template)
        return;
    PVPDifficultyEntry const* bracketEntry = FindBGBracketEntry(bg_template, 80);
    if (!bracketEntry)
        return;

    uint32 arenaType[3] ={ 2, 3, 5 };
    for (uint32 i = 0; i < 3; i++)
    {
        uint8 bgQueueTypeID = MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(MS::Battlegrounds::BattlegroundTypeId::ArenaAll, arenaType[i]);
        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeID);
        if (!bgQueue.ExistRealPlayer(bracketEntry, true))
        {
            bgQueue.AllPlayerBotLeaveQueueFromRatedArena(bracketEntry->RangeIndex);
            continue;
        }
        uint32 allianceGroupID = 0;
        uint32 hordeGroupID = 0;
        GroupQueueInfo* allianceGroupInfo = bgQueue.GetFirstRealPlayerGroupInfo(bracketEntry->RangeIndex, MS::Battlegrounds::QueueGroupTypes::PremadeAlliance);
        GroupQueueInfo* hordeGroupInfo = bgQueue.GetFirstRealPlayerGroupInfo(bracketEntry->RangeIndex, MS::Battlegrounds::QueueGroupTypes::PremadeHorde);
        if (!allianceGroupInfo && !hordeGroupInfo)
            continue;
        if (allianceGroupInfo && hordeGroupInfo)
        {
            allianceGroupID = allianceGroupInfo->GroupId;
            hordeGroupID = hordeGroupInfo->GroupId;
        }
        /*else if (allianceGroupInfo && !BotUtility::DownBotArenaTeam)
        {
            allianceGroupID = allianceGroupInfo->GroupId;
            hordeGroupID = hordeGroupInfo->GroupId;
            if (hordeGroupID == 0)
                continue;
            AddTeamBotToRatedArena(hordeGroupID);
        }
        else if (hordeGroupInfo && !BotUtility::DownBotArenaTeam)
        {
            hordeGroupID = hordeGroupInfo->GroupId;
            allianceGroupID = sArenaTeamMgr->SearchEnemyArenaTeam(hordeGroupID, ALLIANCE);
            if (allianceGroupID == 0)
                continue;
            AddTeamBotToRatedArena(allianceGroupID);
        }*/
        else
            continue;
        m_ArenaSearchTick = 14;
        break;
    }
}

void PlayerBotMgr::QueryNonRatedArenaRequirement()
{
    Battleground* bg_template = sBattlegroundMgr->GetBattlegroundTemplate(MS::Battlegrounds::BattlegroundTypeId::ArenaAll);
    if (!bg_template)
        return;
    PVPDifficultyEntry const* bracketEntry = FindBGBracketEntry(bg_template, 110);
    if (!bracketEntry)
        return;
    auto& bgFreeSlotQueues = sBattlegroundMgr->BGFreeSlotQueue;
    for (auto itr = bgFreeSlotQueues.begin(); itr != bgFreeSlotQueues.end(); itr++)
    {
        Battleground* bg = *itr;
        if (!bg->ExistRealPlayer())
            continue;
        if (bg->GetStatus() > STATUS_WAIT_QUEUE && bg->GetStatus() < STATUS_WAIT_LEAVE)
        {
            if (bg->GetFreeSlotsForTeam(Team::ALLIANCE) > 0)
                AddNewPlayerBotToAA(TeamId::TEAM_ALLIANCE, MS::Battlegrounds::BattlegroundTypeId::ArenaAll, bracketEntry->RangeIndex, bg->GetMaxPlayers() / 2);
            else if (bg->GetFreeSlotsForTeam(Team::HORDE) > 0)
                AddNewPlayerBotToAA(TeamId::TEAM_HORDE, MS::Battlegrounds::BattlegroundTypeId::ArenaAll, bracketEntry->RangeIndex, bg->GetMaxPlayers() / 2);
            else
                continue;
            return;
        }
    }

    uint32 arenaType[3] ={ 2, 3, 5 };
    for (uint32 i = 0; i < 3; i++)
    {
        uint8 bgQueueTypeID = MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(MS::Battlegrounds::BattlegroundTypeId::ArenaAll, arenaType[i]);
        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeID);
        if (!bgQueue.ExistRealPlayer(bracketEntry, false))
            continue;
        int32 needAlliancePlayerCount = 0;
        int32 needHordePlayerCount = 0;
        if (bgQueue.QueryNeedPlayerCount(MS::Battlegrounds::BattlegroundTypeId::ArenaAll, bracketEntry->RangeIndex, arenaType[i], needAlliancePlayerCount, needHordePlayerCount))
        {
            if (needAlliancePlayerCount > 0 && needAlliancePlayerCount >= needHordePlayerCount)
                AddNewPlayerBotToAA(TeamId::TEAM_ALLIANCE, MS::Battlegrounds::BattlegroundTypeId::ArenaAll, bracketEntry->RangeIndex, arenaType[i]);
            else if (needHordePlayerCount > 0)
                AddNewPlayerBotToAA(TeamId::TEAM_HORDE, MS::Battlegrounds::BattlegroundTypeId::ArenaAll, bracketEntry->RangeIndex, arenaType[i]);
            else
                continue;
            return;
        }
    }
}

void PlayerBotMgr::OnlinePlayerBotByGUIDQueue()
{
    while (!m_DelayOnlineBots.empty())
    {
        ObjectGuid& guid = m_DelayOnlineBots.front();
        m_DelayOnlineBots.pop();
        if (AddNewPlayerBotByGUID(guid))
            return;
    }
}

bool PlayerBotMgr::ExistUnBGPlayerBot()
{
    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {
        if (!itSession->second->IsBotSession())
            continue;
        PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>((WorldSession*)itSession->second.get());
        if (!pSession)
            continue;
        if (pSession->PlayerLoading() || pSession->IsAccountBotSession())
            continue;
        Player* player = pSession->GetPlayer();
        if (!player || !player->IsInWorld())
            continue;
        if (player->InBattleground() || player->InBattlegroundQueue() || player->isUsingLfg() || player->GetGroup())
            continue;
        if (player->HasAura(26013))
            continue;
        if (!pSession->HasSchedules())
            continue;
        return true;
    }
    return false;
}

PVPDifficultyEntry const* PlayerBotMgr::FindBGBracketEntry(Battleground* bg_template, uint32 level)
{
    if (!bg_template)
        return NULL;
    for (uint32 j = 0; j < MS::Battlegrounds::MaxBrackets; j++)
    {
        uint8 bracket = j;
        PVPDifficultyEntry const* bracketEntry = sDB2Manager.GetBattlegroundBracketById(bg_template->GetMapId(), bracket);
        if (!bracketEntry)
            continue;
        if (bracketEntry->MinLevel <= level && bracketEntry->MaxLevel >= level)
            return bracketEntry;
    }
    return NULL;
}

uint32 PlayerBotMgr::GetOnlineBotCount(TeamId team, bool hasReal)
{
    uint32 onlineCount = 0;
    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {
        if (itSession->second->IsBotSession())
        {
            PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>((WorldSession*)itSession->second.get());
            if (!pSession)
                continue;
            if (pSession->PlayerLoading())
            {
                ++onlineCount;
                continue;
            }
            Player* player = pSession->GetPlayer();
            if (!player || !player->IsInWorld())
                continue;
            if (team != TEAM_NEUTRAL)
            {
                if (player->GetTeamId() != team)
                    continue;
            }
            ++onlineCount;
        }
        else if (hasReal)
        {
            WorldSession* pSession = itSession->second.get();
            if (pSession->PlayerLoading())
                continue;
            Player* player = pSession->GetPlayer();
            if (!player || !player->IsInWorld())
                continue;
            if (team != TEAM_NEUTRAL)
            {
                if (player->GetTeamId() != team)
                    continue;
            }
            ++onlineCount;
        }
    }
    return onlineCount;
}


uint32 PlayerBotMgr::GetOnlineBotCount2(TeamId team, bool hasReal)
{
    uint32 onlineCount = 0;
    const SessionMap& allSession = sWorld->GetAllSessions();
    for (SessionMap::const_iterator itSession = allSession.begin(); itSession != allSession.end(); itSession++)
    {
        if (itSession->second->IsBotSession())
        {
            PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>((WorldSession*)itSession->second.get());
            if (!pSession)
                continue;
            if (pSession->PlayerLoading())
            {
                //++onlineCount;
                continue;
            }
            Player* player = pSession->GetPlayer();
            if (!player || !player->IsInWorld())
                continue;

            if (!player->InBattleground() &&  !player->InBattlegroundQueue())
                continue;

            if (team != TEAM_NEUTRAL)
            {
                if (player->GetTeamId() != team)
                    continue;
            }
            ++onlineCount;
        }
        else if (hasReal)
        {
            WorldSession* pSession = itSession->second.get();
            if (pSession->PlayerLoading())
                continue;
            Player* player = pSession->GetPlayer();
            if (!player || !player->IsInWorld())
                continue;

            if (!player->InBattleground() &&  !player->InBattlegroundQueue())
                continue;

            if (team != TEAM_NEUTRAL)
            {
                if (player->GetTeamId() != team)
                    continue;
            }
            ++onlineCount;
        }
    }
    return onlineCount;
}

void PlayerBotMgr::Update()
{
    if (DynamicBotsEnabled())
    {
        UpdateDynamicBots();
        // Existing static population, arena and BG routines select from
        // standing bot rosters. Dynamic queues are scheduled separately.
        return;
    }
    static uint32 botCreateDelayTicks = 0;
    static bool botCreateStarted = false;

    if (!botCreateStarted && ++botCreateDelayTicks >= 10)
    {
        botCreateStarted = true;
        TC_LOG_INFO(LOG_FILTER_PLAYER, "Starting automatic PlayerBot character generation...");
        SupplementPlayerBot();

        TC_LOG_INFO(LOG_FILTER_PLAYER, "Starting automatic PlayerBot login...");
        AllPlayerBotRandomLogin("");
    }

    OnlinePlayerBotByGUIDQueue();

    //if (!ExistUnBGPlayerBot())
    QueryBattlegroundRequirement();
#ifndef INCOMPLETE_BOT
    //sArenaTeamMgr->CheckPlayerBotArenaTeam();
    QueryRatedArenaRequirement();
    QueryNonRatedArenaRequirement();
    if (m_LFGSearchTick > 1)
    {
        m_LFGSearchTick = 0;
        //if (lfg::LFGBotRequirement* pbotReq = sLFGMgr->SearchLFGBotRequirement())
        //{
        //	AddNewPlayerBotToLFG(pbotReq);
        //	delete pbotReq;
        //}
    }
    else
        ++m_LFGSearchTick;
#endif

    std::list<std::map<uint32, std::list<UnitAI*> >::iterator > delITer;
    uint32 currentTick = getMSTime();
    for (std::map<uint32, std::list<UnitAI*> >::iterator itDelayAi = m_DelayDestroyAIs.begin();
        itDelayAi != m_DelayDestroyAIs.end();
        itDelayAi++)
    {
        uint32 delayTick = itDelayAi->first;
        if (delayTick + 5000 >= currentTick)
        {
            for (std::list<UnitAI*>::iterator itAI = itDelayAi->second.begin();
                itAI != itDelayAi->second.end();
                itAI++)
            {
                UnitAI* pAI = (*itAI);
                if (pAI)
                    delete pAI;
            }
            itDelayAi->second.clear();
            delITer.push_back(itDelayAi);
        }
    }
    for (std::list<std::map<uint32, std::list<UnitAI*> >::iterator >::iterator itDel = delITer.begin();
        itDel != delITer.end();
        itDel++)
    {
        m_DelayDestroyAIs.erase(*itDel);
    }
}
