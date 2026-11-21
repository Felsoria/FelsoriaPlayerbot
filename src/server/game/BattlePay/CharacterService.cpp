#include "CharacterService.h"
#include "QuestData.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Log.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Player.h"
#include "WorldSession.h"
#include "Chat.h" // FELSORIA_BOOST100_CLAIM_HOTFIX_V1
#include "ObjectMgr.h"
#include "Item.h"
#include "Bag.h"
#include "Mail.h"
#include "Map.h"
#include <algorithm>
extern DB2Storage<CharacterLoadoutEntry> sCharacterLoadoutStore;
extern DB2Storage<CharacterLoadoutItemEntry> sCharacterLoadoutItemStore;

CharacterService* CharacterService::instance()
{
    static CharacterService instance;
    return &instance;
}

void CharacterService::SetRename(Player* player)
{
    player->SetAtLoginFlag(AT_LOGIN_RENAME);

    auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
    stmt->setUInt16(0, AT_LOGIN_RENAME);
    stmt->setUInt64(1, player->GetGUID().GetCounter());
    CharacterDatabase.Execute(stmt);
}

void CharacterService::ChangeFaction(Player* player)
{
    player->SetAtLoginFlag(AT_LOGIN_CHANGE_FACTION);

    auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
    stmt->setUInt16(0, AT_LOGIN_CHANGE_FACTION);
    stmt->setUInt64(1, player->GetGUID().GetCounter());
    CharacterDatabase.Execute(stmt);
}

void CharacterService::ChangeRace(Player* player)
{
    player->SetAtLoginFlag(AT_LOGIN_CHANGE_RACE);

    auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
    stmt->setUInt16(0, AT_LOGIN_CHANGE_RACE);
    stmt->setUInt64(1, player->GetGUID().GetCounter());
    CharacterDatabase.Execute(stmt);
}

void CharacterService::Customize(Player* player)
{
    player->SetAtLoginFlag(AT_LOGIN_CUSTOMIZE);

    auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
    stmt->setUInt16(0, AT_LOGIN_CUSTOMIZE);
    stmt->setUInt64(1, player->GetGUID().GetCounter());
    CharacterDatabase.Execute(stmt);
}

void CharacterService::Boost(Player* player)
{
    player->GetSession()->AddAuthFlag(AT_AUTH_FLAG_90_LVL_UP);
}

void CharacterService::RestoreDeletedCharacter(WorldSession* session)
{
    session->AddAuthFlag(AT_AUTH_FLAG_RESTORE_DELETED_CHARACTER);
}

namespace
{
    void LearnBoostSkillLineSpells(Player* player, uint32 skillLineId)
    {
        if (!player || !skillLineId)
            return;

        uint64 const raceMask = player->getRaceMask();
        uint32 const classMask = player->getClassMask();

        for (SkillLineAbilityEntry const* ability :
             sDB2Manager._skillLineAbilityContainer[skillLineId])
        {
            if (!ability || !ability->Spell)
                continue;

            SpellInfo const* spellInfo =
                sSpellMgr->GetSpellInfo(ability->Spell);

            if (!spellInfo ||
                player->HasSpell(ability->Spell) ||
                !SpellMgr::IsSpellValid(spellInfo, player, false))
            {
                continue;
            }

            if (ability->AcquireMethod !=
                    SKILL_LINE_ABILITY_LEARNED_ON_SKILL_VALUE &&
                ability->AcquireMethod !=
                    SKILL_LINE_ABILITY_LEARNED_ON_SKILL_LEARN &&
                ability->AcquireMethod !=
                    SKILL_LINE_ABILITY_NOT_AUTO_LEARN)
            {
                continue;
            }

            if (ability->RaceMask &&
                !(ability->RaceMask & raceMask))
            {
                continue;
            }

            if (ability->ClassMask &&
                !(ability->ClassMask & classMask))
            {
                continue;
            }

            if (player->getLevel() < spellInfo->SpellLevel)
                continue;

            player->learnSpell(
                ability->Spell,
                true,
                ability->SkillLine);
        }
    }

    void LearnBoostClassSpells(Player* player)
    {
        if (!player)
            return;

        uint32 generalSkillLine = 0;
        uint32 classSkillLine = 0;

        switch (player->getClass())
        {
            case CLASS_WARRIOR:
                generalSkillLine = SKILL_GENERAL_WARRIOR;
                classSkillLine = SKILL_WARRIOR;
                break;
            case CLASS_PALADIN:
                generalSkillLine = SKILL_GENERAL_PALADIN;
                break;
            case CLASS_HUNTER:
                generalSkillLine = SKILL_GENERAL_HUNTER;
                break;
            case CLASS_ROGUE:
                generalSkillLine = SKILL_GENERAL_ROGUE;
                classSkillLine = SKILL_ROGUE;
                break;
            case CLASS_PRIEST:
                generalSkillLine = SKILL_GENERAL_PRIEST;
                break;
            case CLASS_DEATH_KNIGHT:
                generalSkillLine = SKILL_GENERAL_DEATH_KNIGHT;
                break;
            case CLASS_SHAMAN:
                generalSkillLine = SKILL_GENERAL_SHAMAN;
                classSkillLine = SKILL_SHAMAN;
                break;
            case CLASS_MAGE:
                generalSkillLine = SKILL_GENERAL_MAGE;
                classSkillLine = SKILL_MAGE;
                break;
            case CLASS_WARLOCK:
                generalSkillLine = SKILL_GENERAL_WARLOCK;
                classSkillLine = SKILL_WARLOCK;
                break;
            case CLASS_MONK:
                classSkillLine = SKILL_MONK;
                break;
            case CLASS_DRUID:
                generalSkillLine = SKILL_GENERAL_DRUID;
                break;
            case CLASS_DEMON_HUNTER:
                classSkillLine = SKILL_DEMON_HUNTER;
                break;
            default:
                return;
        }

        LearnBoostSkillLineSpells(player, generalSkillLine);

        if (classSkillLine != generalSkillLine)
            LearnBoostSkillLineSpells(player, classSkillLine);

        player->LearnSpecializationSpells();
    }
    #include "FelsoriaBoost100.inc"

    void FinishCharacterBoostGear(
        Player* player,
        uint32 accountId,
        uint64 characterGuid,
        uint64 entitlementId,
        uint32 specializationId,
        uint32 attempt)
    {
        if (!player || !player->IsInWorld() || !player->GetSession())
            return;

        if (player->GetSession()->GetAccountId() != accountId ||
            player->GetGUID().GetCounter() != characterGuid)
            return;

        if (!player->IsSettingFinish())
        {
            if (attempt >= 240)
            {
                TC_LOG_ERROR(LOG_FILTER_CHARACTER,
                    "CHARACTER_BOOST_RESET_TIMEOUT account=%u guid=" UI64FMTD
                    " entitlement=" UI64FMTD " attempts=%u",
                    accountId, characterGuid, entitlementId, attempt);
                return;
            }

            player->AddDelayedEvent(250,
                [player, accountId, characterGuid, entitlementId,
                 specializationId, attempt]()
                {
                    FinishCharacterBoostGear(
                        player, accountId, characterGuid,
                        entitlementId, specializationId, attempt + 1);
                });
            return;
        }

        if (!player->EquipCharacterBoostGear())
        {
            TC_LOG_ERROR(LOG_FILTER_CHARACTER,
                "CHARACTER_BOOST_GEAR_FAILED account=%u guid=" UI64FMTD
                " entitlement=" UI64FMTD " specialization=%u",
                accountId, characterGuid, entitlementId, specializationId);
            return;
        }

        // Restore the normal level and specialization spell set after the
        // PlayerBot reset removed and rebuilt the character spells.
        player->UpdateSkillsForLevel();
        player->LearnDefaultSkills();
        player->LearnDefaultSpells();
        player->InitTalentForLevel();
        player->LearnSpecializationSpells();
        LearnBoostClassSpells(player);
        player->SendTalentsInfoData(false);
        player->UpdateAllStats();
        player->SetFullHealth();

        // Complementary Legion boost reward: 12 Marks of Honor.
        // Character boost reward: 30,000 gold.
        static int64 const BoostGold = 30000LL * GOLD;
        player->ModifyMoney(BoostGold);

        TC_LOG_INFO(
            LOG_FILTER_CHARACTER,
            "CHARACTER_BOOST_GOLD_GRANTED account=%u guid=" UI64FMTD
            " entitlement=" UI64FMTD " copper=" UI64FMTD,
            accountId,
            characterGuid,
            entitlementId,
            uint64(BoostGold));
        static uint32 const MarkOfHonorItemId = 137642;
        static uint32 const MarkOfHonorCount = 12;

        if (!player->AddItem(MarkOfHonorItemId, MarkOfHonorCount))
        {
            TC_LOG_ERROR(LOG_FILTER_CHARACTER,
                "CHARACTER_BOOST_MARK_OF_HONOR_FAILED account=%u guid=" UI64FMTD
                " entitlement=" UI64FMTD,
                accountId, characterGuid, entitlementId);
            return;
        }

        // Move boosted hero classes out of unfinished starting scenarios.
        bool const deathKnightStart =
            player->getClass() == CLASS_DEATH_KNIGHT &&
            player->GetMapId() == 609;

        bool const demonHunterStart =
            player->getClass() == CLASS_DEMON_HUNTER &&
            (player->GetMapId() == 1481 || player->GetMapId() == 1494);

        if (deathKnightStart || demonHunterStart)
        {
            if (player->GetTeamId() == TEAM_ALLIANCE)
            {
                WorldLocation const destination(
                    0, -8543.563477f, 461.028870f, 104.730820f, 5.350924f);

                player->SetHomebind(destination, 5390);
                player->TeleportTo(destination);
            }
            else
            {
                WorldLocation const destination(
                    1, 1465.133179f, -4416.354492f, 25.454123f, 0.180618f);

                player->SetHomebind(destination, 1637);
                player->TeleportTo(destination);
            }
        }

        // The boost skips the Demon Hunter starting-zone finale.
        // Persist the faction-specific terminal quest so Legion-intro phasing
        // starts at Khadgar instead of sending the boosted DH back through the starter zone.
        if (player->getClass() == CLASS_DEMON_HUNTER)
        {
            player->SetIllidariChoice(40374, true);

            uint32 terminalQuestId = player->GetTeamId() == TEAM_ALLIANCE ? 39689 : 39690;

            if (!player->GetQuestRewardStatus(terminalQuestId))
            {
                if (Quest const* quest = sQuestDataStore->GetQuestTemplate(terminalQuestId))
                {
                    if (player->GetQuestStatus(terminalQuestId) == QUEST_STATUS_NONE)
                        player->AddQuest(quest, nullptr);

                    if (player->GetQuestStatus(terminalQuestId) != QUEST_STATUS_REWARDED)
                    {
                        player->CompleteQuest(terminalQuestId);
                        player->RewardQuest(quest, 0, player);
                    }
                }
            }
        }

        player->SaveToDB();

        // Consume the entitlement only after every required operation succeeded.
        LoginDatabase.PExecute(
            "UPDATE account_character_boost "
            "SET state = 2, completed_at = NOW() "
            "WHERE id = " UI64FMTD " AND account_id = %u AND state = 1",
            entitlementId, accountId);
        TC_LOG_INFO(LOG_FILTER_CHARACTER,
            "CHARACTER_BOOST_CORE_READY account=%u guid=" UI64FMTD
            " entitlement=" UI64FMTD " specialization=%u level=%u attempts=%u",
            accountId, characterGuid, entitlementId,
            specializationId, player->getLevel(), attempt);
    }
}

void CharacterService::ApplyPendingBoost(Player* player)
{
    if (!player || !player->GetSession() || player->GetSession()->IsBotSession())
        return;

    uint32 const accountId = player->GetSession()->GetAccountId();
    uint64 const characterGuid = player->GetGUID().GetCounter();

    // ApplyPendingBoost runs once at login. Repair old boosts by entitlement, not level.
    if (player->getClass() == CLASS_DEMON_HUNTER)
    {
        QueryResult completedBoost = LoginDatabase.PQuery(
            "SELECT id FROM account_character_boost WHERE account_id = %u "
            "AND target_guid = " UI64FMTD " AND state = 2 LIMIT 1", accountId, characterGuid);
        if (completedBoost)
        {
            if (player->SetIllidariChoice(40374, true))
                player->SaveToDB();
        }
        else if (uint32 marker = player->GetIllidariChoice())
        {
            if (player->SetIllidariChoice(marker))
                player->SaveToDB();
        }
    }

    QueryResult result = LoginDatabase.PQuery(
        "SELECT id, specialization_id, state, product_id "
        "FROM account_character_boost "
        "WHERE account_id = %u AND target_guid = " UI64FMTD " "
        "AND state = 1 "
        "ORDER BY id LIMIT 1",
        accountId, characterGuid);

    if (!result)
        return;

    Field* fields = result->Fetch();
    uint64 const entitlementId = fields[0].GetUInt64();
    uint32 const specializationId = fields[1].GetUInt32();
    uint8 const previousState = fields[2].GetUInt8();

    ChrSpecializationEntry const* selectedSpec = nullptr;
    uint32 selectedIndex = MAX_SPECIALIZATIONS;

    for (uint32 index = 0; index < MAX_SPECIALIZATIONS; ++index)
    {
        ChrSpecializationEntry const* specialization =
            sDB2Manager.GetChrSpecializationByIndex(player->getClass(), index);

        if (specialization && specialization->ID == specializationId)
        {
            selectedSpec = specialization;
            selectedIndex = index;
            break;
        }
    }

    if (!selectedSpec)
    {
        TC_LOG_ERROR(LOG_FILTER_CHARACTER,
            "CHARACTER_BOOST_INVALID_SPEC account=%u guid=" UI64FMTD
            " entitlement=" UI64FMTD " specialization=%u class=%u",
            accountId, characterGuid, entitlementId,
            specializationId, player->getClass());
        return;
    }

    if (fields[3].GetUInt32() == 100) // FELSORIA_LEVEL100_BOOST_P25
    {
        if (player->FelsoriaBoost100InProgress)
        {
            ChatHandler(player->GetSession()).SendSysMessage("Boost 100 is already processing. Please wait.");
            return;
        }
        player->FelsoriaBoost100InProgress = true;
        ChatHandler(player->GetSession()).SendSysMessage("Boost 100: pending purchase found; checking rewards.");
        player->AddDelayedEvent(500,[player,accountId,characterGuid,entitlementId,specializationId,selectedIndex]()
        { ApplyFelsoriaLevel100Boost(player,accountId,characterGuid,entitlementId,specializationId,selectedIndex,1); });
        return;
    }

    player->ActivateTalentGroup(selectedSpec);

    if (!player->ResetPlayerToLevel(110, selectedIndex, false))
    {
        TC_LOG_ERROR(LOG_FILTER_CHARACTER,
            "CHARACTER_BOOST_RESET_START_FAILED account=%u guid=" UI64FMTD
            " entitlement=" UI64FMTD " specialization=%u previousState=%u",
            accountId, characterGuid, entitlementId,
            specializationId, previousState);
        return;
    }

    TC_LOG_INFO(LOG_FILTER_CHARACTER,
        "CHARACTER_BOOST_RESET_STARTED account=%u guid=" UI64FMTD
        " entitlement=" UI64FMTD " specialization=%u specIndex=%u",
        accountId, characterGuid, entitlementId,
        specializationId, selectedIndex);

    player->AddDelayedEvent(250,
        [player, accountId, characterGuid, entitlementId, specializationId]()
        {
            FinishCharacterBoostGear(
                player, accountId, characterGuid,
                entitlementId, specializationId, 1);
        });
}

