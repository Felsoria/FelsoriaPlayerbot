#include "ScriptMgr.h"
#include "BattlePayMgr.h"
#include "CharacterService.h" // FELSORIA_BOOST100_CLAIM_HOTFIX_V1
#include "BattlePayData.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Player.h"
#include "WorldSession.h"

class battlepay_commandscript : public CommandScript
{
public:
    battlepay_commandscript() : CommandScript("battlepay_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> BattlepayCommandTable =
        {
            { "reload", SEC_ADMINISTRATOR, false, &HandleReloadBattlePay, "" }
        };

        static std::vector<ChatCommand> BoostCommandTable =
        {
            { "claim", SEC_PLAYER, false, &HandleBoostClaim, "" },
            { "retry", SEC_PLAYER, false, &HandleBoostRetry, "" }
        };

        static std::vector<ChatCommand> CommandTable =
        {
            { "battlepay", SEC_ADMINISTRATOR, true, nullptr, "", BattlepayCommandTable },
            { "boost",     SEC_PLAYER,        false, nullptr, "", BoostCommandTable }
        };

        return CommandTable;
    }

    static bool HandleReloadBattlePay(ChatHandler* /*chatHandler*/, char const* /*args*/)
    {
        sBattlePayDataStore->Initialize();
        return true;
    }

    static bool HandleBoostRetry(ChatHandler* chatHandler, char const* /*args*/)
    {
        WorldSession* session = chatHandler ? chatHandler->GetSession() : nullptr;
        Player* player = session ? session->GetPlayer() : nullptr;
        if (!player || session->IsBotSession()) return false;
        QueryResult pending = LoginDatabase.PQuery(
            "SELECT product_id FROM account_character_boost WHERE account_id=%u "
            "AND target_guid=" UI64FMTD " AND state=1 ORDER BY id LIMIT 1",
            session->GetAccountId(), player->GetGUID().GetCounter());
        if (!pending || pending->Fetch()[0].GetUInt32()!=100)
        {
            chatHandler->SendSysMessage("This character has no pending level 100 boost. No purchase was made.");
            return false;
        }
        sCharacterService->ApplyPendingBoost(player);
        return true;
    }

    static bool HandleBoostClaim(ChatHandler* chatHandler, char const* /*args*/)
    {
        if (!chatHandler)
            return false;

        WorldSession* session = chatHandler->GetSession();
        Player* player = session ? session->GetPlayer() : nullptr;

        if (!session || !player)
        {
            chatHandler->SendSysMessage(
                "You must be logged into the character you want to boost.");
            return false;
        }

        if (player->getLevel() >= 110)
        {
            chatHandler->SendSysMessage(
                "This character is already level 110 or higher.");
            return false;
        }

        QueryResult pending = LoginDatabase.PQuery(
            "SELECT id FROM account_character_boost WHERE account_id=%u "
            "AND target_guid=" UI64FMTD " AND state=1 ORDER BY id LIMIT 1",
            session->GetAccountId(), player->GetGUID().GetCounter());
        if (pending)
        {
            chatHandler->SendSysMessage("This character already has a pending boost. Relog, or use .boost retry for a level 100 boost. No additional boost was assigned.");
            return true;
        }

        uint32 const specializationId =
            player->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID);

        ChrSpecializationEntry const* specialization =
            sChrSpecializationStore.LookupEntry(specializationId);

        if (!specialization ||
            specialization->ClassID != player->getClass())
        {
            chatHandler->SendSysMessage(
                "Select a valid specialization before claiming the boost.");
            return false;
        }

        QueryResult entitlement = LoginDatabase.PQuery(
            "SELECT product_id, distribution_id "
            "FROM account_character_boost "
            "WHERE account_id = %u AND state = 0 "
            "ORDER BY id LIMIT 1",
            session->GetAccountId());

        if (!entitlement)
        {
            chatHandler->SendSysMessage(
                "Your account has no unused character boost.");
            return false;
        }

        Field* fields = entitlement->Fetch();
        uint32 const productId = fields[0].GetUInt32();
        uint64 const distributionId = fields[1].GetUInt64();

        if (!sBattlePayDataStore->ProductExist(productId))
        {
            chatHandler->SendSysMessage(
                "The boost product is missing from the world database.");
            return false;
        }

        if (productId==100 && player->getLevel()>=100)
        {
            chatHandler->SendSysMessage("The level 100 boost requires a character below level 100. Purchase preserved.");
            return false;
        }

        session->GetBattlePayMgr()->AssignDistributionToCharacter(
            player->GetGUID(),
            distributionId,
            productId,
            uint16(specializationId),
            0);

        QueryResult assigned = LoginDatabase.PQuery(
            "SELECT id FROM account_character_boost WHERE account_id=%u AND product_id=%u "
            "AND distribution_id=" UI64FMTD " AND target_guid=" UI64FMTD
            " AND specialization_id=%u AND state=1",
            session->GetAccountId(),productId,distributionId,player->GetGUID().GetCounter(),specializationId);
        if (!assigned)
        {
            chatHandler->SendSysMessage("Boost assignment failed. No success was confirmed. Report this message; do not buy another boost.");
            return false;
        }

        chatHandler->SendSysMessage(
            "Character boost assigned. Log out to the character selection "
            "screen, then log back into this character.");

        return true;
    }
};

void AddSC_battlepay_commandscript()
{
    new battlepay_commandscript();
}