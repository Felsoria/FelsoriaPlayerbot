// Bot-only title assignment, called after login scripts; no SQL or periodic scan.
#include "Player.h"
#include "WorldSession.h"
#include "Log.h"
#include "Random.h"
#include "AchievementMgr.h"
#include "CharacterData.h"
#include "QuestData.h"
#include "QuestDef.h"
#include "DB2Stores.h"
#include "DBCEnums.h"
#include <map>
#include <set>
#include <vector>
#include <mutex>

namespace FelsoriaBotTitles
{
    enum : uint8 { Alliance = 1, Horde = 2, Both = 3 };
    struct Pools
    {
        std::vector<CharTitlesEntry const*> team[2];
        std::vector<CharTitlesEntry const*> forbidden[2];
    };
    bool Valid(CharTitlesEntry const* title)
    {
        // MaskID, not the DB2 row ID, is stored in PLAYER_FIELD_PLAYER_TITLE.
        if (!title || title->ID <= 0 || title->MaskID <= 0 || uint32(title->MaskID) >= KNOWN_TITLES_SIZE * 64)
            return false;
        auto meaningful = [](LocalizedString const* name)
        {
            if (!name || !name->Str[LOCALE_enUS] || !name->Str[LOCALE_enUS][0]) return false;
            std::string text(name->Str[LOCALE_enUS]);
            return text.find("%s") != std::string::npos && text.find("[PH]") == std::string::npos &&
                text.find("DND") == std::string::npos && text.find("TEST") == std::string::npos &&
                text.find("Test") == std::string::npos && text.find("Game Master") == std::string::npos &&
                text.find("Gamemaster") == std::string::npos;
        };
        return meaningful(title->Name) || meaningful(title->Name1);
    }
    Pools BuildPools()
    {
        std::map<uint32, uint8> restrictions;
        auto restrictTo = [&](uint32 id, uint8 mask)
        {
            if (!id) return;
            auto inserted = restrictions.emplace(id, Both);
            inserted.first->second &= mask; // Contradictory data fails closed.
        };
        // Legion faction-change pairs also protect realms with an incomplete conversion table.
        static uint32 const legionPairs[][2] = {
            {1,15},{2,16},{3,17},{4,18},{5,19},{6,20},{7,21},{8,22},{9,23},{10,24},{11,25},{12,26},{13,27},{14,28},
            {48,47},{75,76},{113,153},{126,127},{146,152},{147,154},{148,151},{149,150},{193,194},
            {195,209},{196,210},{197,211},{198,212},{199,213},{200,214},{201,215},{202,216},{203,217},
            {204,218},{205,219},{206,220},{207,221},{208,222},{223,224},{260,259},{351,349},{382,383},{420,419},{423,422}
        };
        for (auto const& pair : legionPairs) { restrictTo(pair[0], Alliance); restrictTo(pair[1], Horde); }
        for (auto const& pair : sCharacterDataStore->GetFactionChangeTitles())
            if (pair.first != pair.second) { restrictTo(pair.first, Alliance); restrictTo(pair.second, Horde); }

        // Union all acquisition routes before applying restrictions: a shared reward stays shared.
        std::map<uint32, uint8> rewards;
        for (AchievementEntry const* achievement : sAchievementStore)
        {
            if (!achievement) continue;
            AchievementReward const* reward = sAchievementMgr->GetAchievementReward(achievement);
            if (!reward) continue;
            uint8 faction = achievement->Faction == ACHIEVEMENT_FACTION_ALLIANCE ? Alliance :
                achievement->Faction == ACHIEVEMENT_FACTION_HORDE ? Horde : Both;
            for (uint8 index = 0; index < 2; ++index)
            {
                uint8 allowed = faction;
                if (!reward->genderTitle && reward->titleId[0] != reward->titleId[1])
                    allowed &= index == 0 ? Alliance : Horde;
                if (reward->titleId[index]) rewards[reward->titleId[index]] |= allowed;
            }
        }
        for (auto const& pair : sQuestDataStore->GetQuestTemplates())
        {
            Quest const* quest = pair.second;
            if (!quest || !quest->RewardTitleId) continue;
            uint8 allowed = Both;
            if (quest->AllowableRaces)
            {
                allowed = 0;
                if (quest->AllowableRaces & uint64(RACEMASK_ALLIANCE)) allowed |= Alliance;
                if (quest->AllowableRaces & uint64(RACEMASK_HORDE)) allowed |= Horde;
                // Neutral pandaren can choose either faction later.
                if (quest->AllowableRaces & (uint64(1) << (RACE_PANDAREN_NEUTRAL - 1))) allowed |= Both;
            }
            rewards[quest->RewardTitleId] |= allowed;
        }
        for (auto const& pair : rewards) restrictTo(pair.first, pair.second);

        Pools result;
        std::map<uint32, std::pair<CharTitlesEntry const*, uint8>> byMask;
        for (CharTitlesEntry const* title : sCharTitlesStore)
        {
            if (!Valid(title)) continue;
            auto found = restrictions.find(uint32(title->ID));
            uint8 allowed = found == restrictions.end() ? uint8(Both) : found->second;
            auto inserted = byMask.emplace(uint32(title->MaskID), std::make_pair(title, allowed));
            if (!inserted.second) inserted.first->second.second &= allowed;
        }
        for (auto const& pair : byMask)
            for (uint8 index = 0; index < 2; ++index)
            {
                if (pair.second.second & (index == 0 ? Alliance : Horde))
                    result.team[index].push_back(pair.second.first);
                else result.forbidden[index].push_back(pair.second.first);
            }
        return result;
    }
    void Assign(Player* player)
    {
        if (!player || !player->GetSession() || !player->GetSession()->IsBotSession() ||
            (player->GetTeam() != ALLIANCE && player->GetTeam() != HORDE)) return;
        // Built once after startup; C++ local-static initialization is thread-safe.
        static Pools const pools = BuildPools();
        uint8 index = player->GetTeam() == ALLIANCE ? 0 : 1;
        bool changed = false;
        for (CharTitlesEntry const* title : pools.forbidden[index])
            if (player->HasTitle(title)) { player->SetTitle(title, true); changed = true; }
        CharTitlesEntry const* chosen = nullptr;
        if (player->getClass() == CLASS_DEMON_HUNTER)
        {
            chosen = sCharTitlesStore.LookupEntry(458);
            if (!Valid(chosen))
            {
                static std::once_flag reported;
                std::call_once(reported, [] { TC_LOG_ERROR(LOG_FILTER_PLAYER,
                    "BOT_TITLES: Illidari CharTitles ID 458 missing/invalid; verify Legion DB2 data."); });
                chosen = nullptr;
            }
        }
        else
        {
            auto const& candidates = pools.team[index];
            uint32 active = player->GetUInt32Value(PLAYER_FIELD_PLAYER_TITLE);
            for (CharTitlesEntry const* title : candidates)
                if (uint32(title->MaskID) == active && player->HasTitle(title)) { chosen = title; break; }
            if (!chosen && !candidates.empty()) chosen = candidates[urand(0, uint32(candidates.size() - 1))];
        }
        if (chosen)
        {
            if (!player->HasTitle(chosen)) { player->SetTitle(chosen); changed = true; }
            if (player->GetUInt32Value(PLAYER_FIELD_PLAYER_TITLE) != uint32(chosen->MaskID))
            {
                player->SetUInt32Value(PLAYER_FIELD_PLAYER_TITLE, uint32(chosen->MaskID));
                changed = true;
            }
        }
        else if (player->GetUInt32Value(PLAYER_FIELD_PLAYER_TITLE))
        {
            player->SetUInt32Value(PLAYER_FIELD_PLAYER_TITLE, 0);
            changed = true;
        }
        if (changed) player->SaveToDB();
    }
}
