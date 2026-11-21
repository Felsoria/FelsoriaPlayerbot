/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "BattlePayPackets.h"
#include "BattlePayMgr.h"
#include "BattlePayData.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "PlayerBotMgr.h"
#include "ScriptMgr.h"
#include "DatabaseEnv.h"
#include "Configuration/Config.h"
#include "World.h"
#include "RealmList.h"
#include "FelsoriaRealmPolicy.h"
#include "FelsoriaArtifactShopPolicy.h"
#include "Chat.h"
#include <algorithm>
#include "PlayerDefines.h"
#include "WorldSession.h"

namespace
{
bool FelsoriaTransferEnabled()
{
    return sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.Transfer.Enable", false) && sWorld->FelsoriaPlayerRealmsEnabled();
}
}

void WorldSession::HandleFelsoriaVasTransferQueues(WorldPackets::BattlePay::GetVasTransferQueues& /*packet*/)
{
    if (FelsoriaTransferEnabled() && !GetPlayer() && !IsAccountBotSession() &&
        GetBattlePayMgr()->IsAvailable())
    {
        WorldPackets::BattlePay::FelsoriaVasTransferQueueStatus response;
        SendPacket(response.Write());
    }
}

void WorldSession::SendFelsoriaVasRealmList(uint32 clientToken)
{
    WorldPackets::BattlePay::FelsoriaVasRealmList response;
    CancelFelsoriaTransfer();
    response.ClientToken = clientToken;
    if (!FelsoriaTransferEnabled() || GetPlayer() || IsAccountBotSession() ||
        !GetBattlePayMgr()->IsAvailable() || !sWorld->FelsoriaPlayerRealmsEnabled())
        response.Result = Battlepay::Error::PurchaseDenied;
    else
        for (uint32 id : sRealmList->GetFelsoriaRealmIds())
            if (Realm const* target = sRealmList->GetRealm(::Battlenet::RealmHandle(realm.Id.Region, realm.Id.Site, id)))
            {
                WorldPackets::BattlePay::FelsoriaVasRealm entry;
                entry.Address = target->Id.GetAddress();
                entry.ConfigId = target->GetConfigId();
                entry.Name = target->Name;
                response.Realms.emplace_back(std::move(entry));
            }
    TC_LOG_INFO(LOG_FILTER_WORLDSERVER, "FELSORIA_VAS_REALMS account=%u count=%u result=%u",
        GetAccountId(), uint32(response.Realms.size()), response.Result);
    SendPacket(response.Write());
}

void WorldSession::HandleFelsoriaVasCharacterList(WorldPackets::BattlePay::GetVasCharacterList& packet)
{
    WorldPackets::BattlePay::FelsoriaVasCharacterList response;
    response.ClientToken = packet.ClientToken;
    if (!FelsoriaTransferEnabled() || GetPlayer() || IsAccountBotSession() ||
        !GetBattlePayMgr()->IsAvailable() || packet.ServiceType != 15 || !sWorld->FelsoriaPlayerRealmsEnabled())
        response.Result = Battlepay::Error::PurchaseDenied;
    else
    {
        if (QueryResult result = CharacterDatabase.PQuery(
            "SELECT guid,name,level,race,class,gender FROM characters "
            "WHERE account=%u AND (deleteDate IS NULL OR deleteDate=0) ORDER BY guid LIMIT 100", GetAccountId()))
            do
            {
                Field* fields = result->Fetch();
                uint64 guid = fields[0].GetUInt64();
                if (!sWorld->ReloadFelsoriaPlayerRealm(guid, GetAccountId()))
                    continue;
                uint32 homeId = sWorld->GetFelsoriaPlayerRealm(guid, GetAccountId());
                Realm const* home = sRealmList->GetRealm(::Battlenet::RealmHandle(realm.Id.Region, realm.Id.Site, homeId));
                if (!home || !sRealmList->IsFelsoriaRealmKnown(homeId))
                    continue;
                WorldPackets::BattlePay::FelsoriaVasCharacter character;
                character.AccountGuid = ObjectGuid::Create<HighGuid::WowAccount>(GetAccountId());
                character.CharacterGuid = ObjectGuid::Create<HighGuid::Player>(guid);
                character.Name = fields[1].GetString();
                character.Level = fields[2].GetUInt8();
                character.Race = fields[3].GetUInt8();
                character.Class = fields[4].GetUInt8();
                character.Sex = fields[5].GetUInt8();
                character.RealmAddress = home->Id.GetAddress();
                character.RealmName = home->Name;
                response.Characters.emplace_back(std::move(character));
            } while (result->NextRow());
    }
    TC_LOG_INFO(LOG_FILTER_WORLDSERVER, "FELSORIA_VAS_CHARACTERS account=%u count=%u result=%u",
        GetAccountId(), uint32(response.Characters.size()), response.Result);
    SendPacket(response.Write());
}


auto GetBagsFreeSlots = [](Player* player) -> uint32
{
    uint32 freeBagSlots = 0;
    for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
        if (auto bag = player->GetBagByPos(i))
            freeBagSlots += bag->GetFreeSlots();

    uint8 inventoryEnd = INVENTORY_SLOT_ITEM_START + player->GetInventorySlotCount();
    for (uint8 i = INVENTORY_SLOT_ITEM_START; i < inventoryEnd; i++)
        if (!player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            ++freeBagSlots;

    return freeBagSlots;
};

auto SendStartPurchaseResponse = [](WorldSession* session, Battlepay::Purchase const& purchase, Battlepay::Error const& result) -> void
{
    WorldPackets::BattlePay::StartPurchaseResponse response;
    response.PurchaseID = purchase.PurchaseID;
    response.ClientToken = purchase.ClientToken;
    response.PurchaseResult = result;
    session->SendPacket(response.Write());
};

auto SendPurchaseUpdate = [](WorldSession* session, Battlepay::Purchase const& purchase, uint32 result) -> void
{
    WorldPackets::BattlePay::PurchaseUpdate packet;
    WorldPackets::BattlePay::BattlePayPurchase data;
    data.PurchaseID = purchase.PurchaseID;
    data.UnkLong = 0;
    data.UnkLong2 = 0;
    data.Status = purchase.Status;
    data.ResultCode = result;
    data.ProductID = purchase.ProductID;
    data.UnkInt = purchase.ServerToken;
    data.WalletName = session->GetBattlePayMgr()->GetDefaultWalletName();
    packet.Purchase.emplace_back(data);
    session->SendPacket(packet.Write());
};

// FELSORIA_ARTIFACT_SHOP_V1: account-wide, one-time cosmetic service.
bool WorldSession::RefreshFelsoriaArtifactSkins(bool forceBalance)
{
    _felsoriaArtifactSkinsLoaded = false;
    _felsoriaArtifactSkinsOwned = false;
    if (IsAccountBotSession() || (GetPlayer() && GetPlayer()->IsPlayerBot()))
        return false;
    QueryResult result = LoginDatabase.PQuery(
        "SELECT a.balans,COALESCE(u.owned,0) FROM account a "
        "LEFT JOIN felsoria_artifact_skin_unlock u ON u.account_id=a.id WHERE a.id=%u", GetAccountId());
    if (!result)
        return false;
    Field* fields = result->Fetch();
    // Do not race an existing asynchronous shop debit's session callback.
    if (forceBalance || !GetPlayer() || GetPlayer()->GetCanUseDonate())
        ChangeBattlePayBalance(fields[0].GetInt64() - GetBattlePayBalance());
    _felsoriaArtifactSkinsOwned = fields[1].GetUInt8() == 1;
    _felsoriaArtifactSkinsLoaded = true;
    return true;
}

void WorldSession::SendFelsoriaArtifactSkinsState()
{
    if (!_felsoriaArtifactSkinsLoaded ||
        std::find(_registeredAddonPrefixes.begin(), _registeredAddonPrefixes.end(), "FelsArtSkin") == _registeredAddonPrefixes.end())
        return;
    if (Player* player = GetPlayer())
        if (player->IsInWorld() && !player->IsPlayerBot() && !IsAccountBotSession())
        {
            std::ostringstream data;
            data << GetAccountId() << ':' << (HasFelsoriaArtifactSkins() ? 1 : 0);
            player->SendCustomMessage("FELSORIA_ARTIFACT_SKINS", data);
        }
}

bool WorldSession::HandleFelsoriaArtifactStart(ObjectGuid const& target, uint32 clientToken, uint32 productID)
{
    using namespace FelsoriaArtifactShop;
    // Keep the old marker until another purchase actually replaces the manager
    // transaction. A rejected native Start must not expose our SKU to its flow.
    if (productID != ProductID || sBattlePayDataStore->ProductExist(ProductID))
        return false;
    Battlepay::Purchase purchase;
    purchase.ProductID = ProductID;
    purchase.ClientToken = clientToken;
    purchase.CurrentPrice = Price;
    purchase.Status = Battlepay::UpdateStatus::Loading;
    Player* player = GetPlayer();
    if (!player || player->IsPlayerBot() || !player->GetCanUseDonate() || IsAccountBotSession() ||
        !GetBattlePayMgr()->IsAvailable() || !_felsoriaArtifactCatalogAvailable ||
        !sConfigMgr->GetBoolDefault("Felsoria.ArtifactSkinsShop.Enable", true) ||
        (!target.IsEmpty() && target != player->GetGUID()))
    {
        SendStartPurchaseResponse(this, purchase, Battlepay::Error::PurchaseDenied);
        return true;
    }
    if (!RefreshFelsoriaArtifactSkins())
    {
        SendStartPurchaseResponse(this, purchase, Battlepay::Error::PaymentFailed);
        return true;
    }
    if (HasFelsoriaArtifactSkins())
    {
        SendFelsoriaArtifactSkinsState();
        ChatHandler(player).SendSysMessage("All artifact appearances are already unlocked for this account.");
        SendStartPurchaseResponse(this, purchase, Battlepay::Error::PurchaseDenied);
        return true;
    }
    if (GetBattlePayBalance() < int64(Price))
    {
        SendStartPurchaseResponse(this, purchase, Battlepay::Error::InsufficientBalance);
        return true;
    }
    purchase.PurchaseID = GetBattlePayMgr()->GenerateNewPurchaseID();
    purchase.DistributionId = GetBattlePayMgr()->GenerateNewDistributionId();
    purchase.TargetCharacter = player->GetGUID();
    purchase.ServerToken = urand(1, 0xFFFFFFFE);
    GetBattlePayMgr()->RegisterStartPurchase(purchase);
    _felsoriaArtifactPurchase = purchase.PurchaseID;
    SendStartPurchaseResponse(this, purchase, Battlepay::Error::Ok);
    SendPurchaseUpdate(this, purchase, Battlepay::Error::Ok);
    WorldPackets::BattlePay::ConfirmPurchase confirm;
    confirm.PurchaseID = purchase.PurchaseID;
    confirm.ServerToken = purchase.ServerToken;
    SendPacket(confirm.Write());
    return true;
}

bool WorldSession::HandleFelsoriaArtifactConfirm(WorldPackets::BattlePay::ConfirmPurchaseResponse& packet)
{
    using namespace FelsoriaArtifactShop;
    Battlepay::Purchase* purchase = GetBattlePayMgr()->GetPurchase();
    if (!purchase || purchase->ProductID != ProductID ||
        !IsFelsoriaArtifactPurchase(purchase->PurchaseID))
        return false;
    Player* player = GetPlayer();
    if (!player || player->IsPlayerBot() || !player->GetCanUseDonate() || IsAccountBotSession() ||
        !GetBattlePayMgr()->IsAvailable() ||
        !sConfigMgr->GetBoolDefault("Felsoria.ArtifactSkinsShop.Enable", true) ||
        purchase->TargetCharacter != player->GetGUID() ||
        !ConfirmationMatches(purchase->ProductID, purchase->CurrentPrice, purchase->ServerToken,
            packet.ServerToken, packet.ClientCurrentPriceFixedPoint, packet.ConfirmPurchase))
    {
        purchase->Lock = true;
        purchase->ServerToken = 0;
        SendPurchaseUpdate(this, *purchase, Battlepay::Error::PurchaseDenied);
        return true;
    }
    purchase->Lock = true;
    struct CoinGate
    {
        Player* player;
        explicit CoinGate(Player* p) : player(p) { player->ModifyCanUseDonate(false); }
        ~CoinGate() { player->ModifyCanUseDonate(true); }
    } coinGate(player);
    // The unpaid row is harmless; only the atomic statement can make it owned.
    LoginDatabase.DirectPExecute("INSERT IGNORE INTO felsoria_artifact_skin_unlock (account_id) VALUES (%u)", GetAccountId());
    LoginDatabase.DirectExecute(DebitSQL(GetAccountId(), purchase->PurchaseID).c_str());
    QueryResult receipt = LoginDatabase.PQuery(
        "SELECT owned,COALESCE(purchase_id,0) FROM felsoria_artifact_skin_unlock WHERE account_id=%u", GetAccountId());
    if (!receipt)
    {
        // An ambiguous connection failure retains this purchase ID for retry.
        // Until a balance can be verified, keep the session cache conservative.
        if (!RefreshFelsoriaArtifactSkins(true))
            ChangeBattlePayBalance(-GetBattlePayBalance());
        GetBattlePayMgr()->SendPointsBalance();
        SendFelsoriaArtifactSkinsState();
        SendPurchaseUpdate(this, *purchase, Battlepay::Error::PaymentFailed);
        return true;
    }
    Field* fields = receipt->Fetch();
    bool const delivered = OwnReceipt(fields[0].GetUInt8() == 1, fields[1].GetUInt64(), purchase->PurchaseID);
    if (!RefreshFelsoriaArtifactSkins(true))
    {
        ChangeBattlePayBalance(-GetBattlePayBalance());
        if (delivered)
        {
            _felsoriaArtifactSkinsLoaded = true;
            _felsoriaArtifactSkinsOwned = true;
        }
    }
    GetBattlePayMgr()->SendPointsBalance();
    SendFelsoriaArtifactSkinsState();
    if (!delivered)
    {
        SendPurchaseUpdate(this, *purchase, GetBattlePayBalance() < int64(Price) && !HasFelsoriaArtifactSkins()
            ? Battlepay::Error::InsufficientBalance : Battlepay::Error::PurchaseDenied);
        return true;
    }
    purchase->Status = Battlepay::UpdateStatus::Finish;
    SendPurchaseUpdate(this, *purchase, Battlepay::Error::Ok);
    ChatHandler(player).SendSysMessage("All artifact appearances are permanently unlocked for this account.");
    TC_LOG_INFO(LOG_FILTER_SQL, "FELSORIA_ARTIFACT_SHOP_V1 account=%u purchase=" UI64FMTD " price=25 owned=1",
        GetAccountId(), purchase->PurchaseID);
    return true;
}

// Felsoria P05: native, offline-only realm transfer; price is exactly 1 Battle Coin.
void WorldSession::CancelFelsoriaTransfer()
{
    if (_felsoriaTransferPurchase)
        LoginDatabase.DirectPExecute("UPDATE felsoria_realm_purchases SET status=2 "
            "WHERE purchase_id=" UI64FMTD " AND account_id=%u AND status=0", _felsoriaTransferPurchase, GetAccountId());
    _felsoriaTransferPurchase = 0;
    _felsoriaTransferServerToken = 0;
    _felsoriaTransferCharacter.Clear();
    _felsoriaTransferDestination = 0;
}

void WorldSession::HandleFelsoriaStartVasPurchase(WorldPackets::BattlePay::StartVasPurchase& packet)
{
    CancelFelsoriaTransfer();
    auto reject = [this, &packet](uint32 error)
    {
        WorldPackets::BattlePay::BattlePayVasPurchaseStarted response;
        response.UnkInt = packet.ClientToken;
        response.VasPurchase.PlayerGuid = packet.CharacterGuid;
        response.VasPurchase.UnkInt = packet.ProductID;
        response.VasPurchase.UnkInt2 = 1; // pre-purchase validation
        response.VasPurchase.ItemIDs.emplace_back(error);
        SendPacket(response.Write());
    };
    if (!FelsoriaTransferEnabled() || !_felsoriaTransferCatalogAvailable ||
        !GetBattlePayMgr()->IsAvailable() || !FelsoriaRealmFilterRequired() || GetPlayer() || PlayerLoading() ||
        sPlayerBotMgr->GetPlayerBotAccountInfo(GetAccountId()) || packet.ProductID != 189 ||
        !packet.CharacterGuid.IsPlayer() || !packet.NewName.empty() ||
        (!packet.DestinationWowAccount.IsEmpty() && packet.DestinationWowAccount != ObjectGuid::Create<HighGuid::WowAccount>(GetAccountId())) ||
        !packet.DestinationBnetAccount.IsEmpty())
    {
        reject(43); // RealmNotEligible; unsupported bundle/name/account requests never charge.
        return;
    }
    uint64 guid = packet.CharacterGuid.GetCounter();
    uint32 source = sWorld->GetFelsoriaPlayerRealm(guid, GetAccountId());
    uint32 destination = ::Battlenet::RealmHandle(packet.DestinationRealm).Realm;
    Realm const* target = sRealmList->GetRealm(::Battlenet::RealmHandle(realm.Id.Region, realm.Id.Site, destination));
    if (!sRealmList->IsFelsoriaRealmKnown(source) || !sRealmList->IsFelsoriaRealmVisible(destination) || source == destination ||
        !target || target->Id.GetAddress() != packet.DestinationRealm ||
        packet.SourceRealm != ::Battlenet::RealmHandle(realm.Id.Region, realm.Id.Site, source).GetAddress())
    {
        reject(43);
        return;
    }
    QueryResult character = CharacterDatabase.PQuery("SELECT online FROM characters WHERE guid=" UI64FMTD
        " AND account=%u AND (deleteDate IS NULL OR deleteDate=0)", guid, GetAccountId());
    if (!character || character->Fetch()[0].GetUInt8() || ObjectAccessor::FindPlayer(packet.CharacterGuid))
    {
        reject(58); // CharLocked
        return;
    }
    QueryResult capacity = LoginDatabase.PQuery("SELECT COUNT(*) FROM felsoria_character_realms WHERE account_id=%u AND realm_id=%u AND active=1", GetAccountId(), destination);
    if (!capacity || capacity->Fetch()[0].GetUInt32() >= sWorld->getIntConfig(CONFIG_CHARACTERS_PER_REALM))
    {
        reject(45); // MaxCharactersOnServer
        return;
    }
    QueryResult balance = LoginDatabase.PQuery("SELECT balans FROM account WHERE id=%u", GetAccountId());
    if (!balance || balance->Fetch()[0].GetInt64() < 1)
    {
        reject(43);
        return;
    }
    Battlepay::Purchase purchase;
    purchase.PurchaseID = GetBattlePayMgr()->GenerateNewPurchaseID();
    purchase.ClientToken = packet.ClientToken;
    purchase.ServerToken = urand(1, 0xFFFFFFFE);
    purchase.ProductID = 189;
    purchase.TargetCharacter = packet.CharacterGuid;
    purchase.CurrentPrice = 1;
    purchase.Status = Battlepay::UpdateStatus::Loading;
    LoginDatabase.DirectPExecute("INSERT INTO felsoria_realm_purchases "
        "(purchase_id,account_id,guid,source_realm,destination_realm,price,server_token,status,created_at) "
        "VALUES (" UI64FMTD ",%u," UI64FMTD ",%u,%u,1,%u,0,UNIX_TIMESTAMP())",
        purchase.PurchaseID, GetAccountId(), guid, source, destination, purchase.ServerToken);
    QueryResult saved = LoginDatabase.PQuery("SELECT status FROM felsoria_realm_purchases WHERE purchase_id=" UI64FMTD
        " AND account_id=%u AND guid=" UI64FMTD " AND source_realm=%u AND destination_realm=%u AND server_token=%u AND price=1",
        purchase.PurchaseID, GetAccountId(), guid, source, destination, purchase.ServerToken);
    if (!saved || saved->Fetch()[0].GetUInt8() != 0)
    {
        reject(43);
        return;
    }
    _felsoriaTransferPurchase = purchase.PurchaseID;
    _felsoriaTransferServerToken = purchase.ServerToken;
    _felsoriaTransferClientToken = packet.ClientToken;
    _felsoriaTransferCharacter = packet.CharacterGuid;
    _felsoriaTransferDestination = packet.DestinationRealm;
    GetBattlePayMgr()->RegisterStartPurchase(purchase);
    WorldPackets::BattlePay::BattlePayVasPurchaseStarted ready;
    ready.UnkInt = packet.ClientToken;
    ready.VasPurchase.PlayerGuid = packet.CharacterGuid;
    ready.VasPurchase.UnkInt = 189;
    ready.VasPurchase.UnkInt2 = 1;
    SendPacket(ready.Write());
    SendStartPurchaseResponse(this, purchase, Battlepay::Error::Ok);
    SendPurchaseUpdate(this, purchase, Battlepay::Error::Ok);
    WorldPackets::BattlePay::ConfirmPurchase confirm;
    confirm.PurchaseID = purchase.PurchaseID;
    confirm.ServerToken = purchase.ServerToken;
    SendPacket(confirm.Write());
    TC_LOG_INFO(LOG_FILTER_WORLDSERVER, "FELSORIA_TRANSFER_PENDING account=%u guid=" UI64FMTD " source=%u target=%u price=1",
        GetAccountId(), guid, source, destination);
}

bool WorldSession::HandleFelsoriaTransferConfirmation(WorldPackets::BattlePay::ConfirmPurchaseResponse& packet)
{
    if (!_felsoriaTransferPurchase)
        return false;
    if (packet.ServerToken != _felsoriaTransferServerToken)
        return true; // stale/forged confirmation cannot enter another delivery path
    Battlepay::Purchase purchase;
    purchase.PurchaseID = _felsoriaTransferPurchase;
    purchase.ClientToken = _felsoriaTransferClientToken;
    purchase.ServerToken = _felsoriaTransferServerToken;
    purchase.ProductID = 189;
    purchase.CurrentPrice = 1;
    purchase.TargetCharacter = _felsoriaTransferCharacter;
    purchase.Status = Battlepay::UpdateStatus::Finish;
    if (!packet.ConfirmPurchase)
    {
        CancelFelsoriaTransfer();
        SendPurchaseUpdate(this, purchase, Battlepay::Error::PurchaseDenied);
        return true;
    }
    uint64 guid = _felsoriaTransferCharacter.GetCounter();
    if (!sRealmList->IsFelsoriaRealmVisible(::Battlenet::RealmHandle(_felsoriaTransferDestination).Realm))
    {
        CancelFelsoriaTransfer();
        SendPurchaseUpdate(this, purchase, Battlepay::Error::PurchaseDenied);
        return true;
    }
    if (packet.ClientCurrentPriceFixedPoint != 10000 || !FelsoriaTransferEnabled() ||
        !_felsoriaTransferCatalogAvailable || GetPlayer() || PlayerLoading() || !GetBattlePayMgr()->IsAvailable() ||
        ObjectAccessor::FindPlayer(_felsoriaTransferCharacter))
    {
        CancelFelsoriaTransfer();
        SendPurchaseUpdate(this, purchase, Battlepay::Error::PurchaseDenied);
        return true;
    }
    QueryResult capacity = LoginDatabase.PQuery("SELECT COUNT(*) FROM felsoria_character_realms WHERE account_id=%u AND realm_id=%u AND active=1",
        GetAccountId(), ::Battlenet::RealmHandle(_felsoriaTransferDestination).Realm);
    QueryResult journal = LoginDatabase.PQuery("SELECT status,source_realm,destination_realm FROM felsoria_realm_purchases WHERE purchase_id=" UI64FMTD
        " AND account_id=%u AND guid=" UI64FMTD " AND server_token=%u", _felsoriaTransferPurchase, GetAccountId(), guid, _felsoriaTransferServerToken);
    if (!journal)
    {
        SendPurchaseUpdate(this, purchase, Battlepay::Error::PaymentFailed);
        return true;
    }
    uint8 status = journal->Fetch()[0].GetUInt8();
    if (status == 0 && (!capacity || capacity->Fetch()[0].GetUInt32() >= sWorld->getIntConfig(CONFIG_CHARACTERS_PER_REALM)))
    {
        CancelFelsoriaTransfer();
        SendPurchaseUpdate(this, purchase, Battlepay::Error::PurchaseDenied);
        return true;
    }
    if (status == 0)
    {
        Field* transferFields = journal->Fetch();
        Realm const* sourceRealm = sRealmList->GetRealm(::Battlenet::RealmHandle(
            realm.Id.Region, realm.Id.Site, transferFields[1].GetUInt32()));
        Realm const* destinationRealm = sRealmList->GetRealm(::Battlenet::RealmHandle(
            realm.Id.Region, realm.Id.Site, transferFields[2].GetUInt32()));
        if (!sourceRealm || !destinationRealm ||
            transferFields[2].GetUInt32() != ::Battlenet::RealmHandle(_felsoriaTransferDestination).Realm)
        {
            CancelFelsoriaTransfer();
            SendPurchaseUpdate(this, purchase, Battlepay::Error::PurchaseDenied);
            return true;
        }
        uint16 renameFlag = FelsoriaRealmPolicy::TransferRequiresRename(
            sourceRealm->Timezone, destinationRealm->Timezone) ? uint16(AT_LOGIN_RENAME) : 0;
        // One InnoDB statement: debit + realm + journal. Replaying it cannot debit twice.
        // Also lock/check the actual character row in the same MySQL instance.
        std::string sql = "UPDATE account a JOIN felsoria_realm_purchases p ON p.account_id=a.id "
            "JOIN felsoria_character_realms m ON m.guid=p.guid AND m.account_id=p.account_id JOIN " +
            sWorld->GetFelsoriaCharactersTable() + " c ON c.guid=p.guid AND c.account=p.account_id "
            "SET a.balans=a.balans-p.price,m.realm_id=p.destination_realm,p.status=1,p.completed_at=UNIX_TIMESTAMP(),"
            "c.at_login=(c.at_login | " + std::to_string(renameFlag) + ") "
            "WHERE p.purchase_id=" + std::to_string(_felsoriaTransferPurchase) +
            " AND p.account_id=" + std::to_string(GetAccountId()) +
            " AND p.server_token=" + std::to_string(_felsoriaTransferServerToken) +
            " AND p.status=0 AND p.price=1 AND a.balans>=1 AND m.active=1 AND m.realm_id=p.source_realm "
            "AND p.source_realm<>p.destination_realm " +
            (sConfigMgr->GetBoolDefault("Felsoria.VirtualRealms.EULegion.Enable", false) ?
                "AND EXISTS (SELECT 1 FROM felsoria_eu_legion_catalog v WHERE v.realm_id=p.destination_realm AND v.realm_id<>907 AND v.enabled=1 AND v.visible=1) " :
                "AND p.destination_realm BETWEEN 901 AND 906 ") +
            "AND p.created_at>=UNIX_TIMESTAMP()-300 AND c.online=0 AND (c.deleteDate IS NULL OR c.deleteDate=0)";
        LoginDatabase.DirectExecute(sql.c_str());
        journal = LoginDatabase.PQuery("SELECT status FROM felsoria_realm_purchases WHERE purchase_id=" UI64FMTD
            " AND account_id=%u AND server_token=%u", _felsoriaTransferPurchase, GetAccountId(), _felsoriaTransferServerToken);
        if (!journal)
        {
            // Ambiguous connection failure: keep the same ID, never create/charge another purchase.
            TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "FELSORIA_TRANSFER_VERIFY_REQUIRED purchase=" UI64FMTD, _felsoriaTransferPurchase);
            SendPurchaseUpdate(this, purchase, Battlepay::Error::PaymentFailed);
            return true;
        }
        status = journal->Fetch()[0].GetUInt8();
    }
    if (status != 1)
    {
        CancelFelsoriaTransfer();
        SendPurchaseUpdate(this, purchase, Battlepay::Error::PurchaseDenied);
        return true;
    }
    if (!sWorld->ReloadFelsoriaPlayerRealm(guid, GetAccountId()))
    {
        TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "FELSORIA_TRANSFER_VERIFY_REQUIRED purchase=" UI64FMTD, _felsoriaTransferPurchase);
        SendPurchaseUpdate(this, purchase, Battlepay::Error::PaymentFailed);
        return true;
    }
    if (QueryResult balance = LoginDatabase.PQuery("SELECT balans FROM account WHERE id=%u", GetAccountId()))
        ChangeBattlePayBalance(balance->Fetch()[0].GetInt64() - GetBattlePayBalance());
    GetBattlePayMgr()->SendPointsBalance();
    GetBattlePayMgr()->RegisterStartPurchase(purchase);
    SendPurchaseUpdate(this, purchase, Battlepay::Error::Other);
    WorldPackets::BattlePay::FelsoriaVasPurchaseComplete complete;
    complete.Character = _felsoriaTransferCharacter;
    complete.DestinationCharacter = _felsoriaTransferCharacter;
    complete.DestinationRealm = _felsoriaTransferDestination;
    SendPacket(complete.Write());
    TC_LOG_INFO(LOG_FILTER_WORLDSERVER, "FELSORIA_TRANSFER_COMPLETE account=%u guid=" UI64FMTD " realm=%u purchase=" UI64FMTD " price=1",
        GetAccountId(), guid, ::Battlenet::RealmHandle(_felsoriaTransferDestination).Realm, _felsoriaTransferPurchase);
    timeCharEnumOpcode = 0;
    SendCharacterEnum();
    return true;
}


void WorldSession::HandleGetPurchaseListQuery(WorldPackets::BattlePay::GetPurchaseListQuery& /*packet*/)
{
    WorldPackets::BattlePay::PurchaseListResponse packet;
    //uint32 Result = 0;
    //std::vector<WorldPackets::BattlePay::BattlePayPurchase> Purchase;
    SendPacket(packet.Write());
}

void WorldSession::HandleUpdateVasPurchaseStates(WorldPackets::BattlePay::UpdateVasPurchaseStates& /*packet*/)
{
}

void WorldSession::HandleBattlePayDistributionAssign(WorldPackets::BattlePay::DistributionAssignToTarget& packet)
{
    if (!GetBattlePayMgr()->IsAvailable())
        return;

    if (packet.ProductID == FelsoriaArtifactShop::ProductID &&
        (IsFelsoriaArtifactPurchase(GetBattlePayMgr()->GetPurchase()->PurchaseID) ||
         !sBattlePayDataStore->ProductExist(packet.ProductID)))
        return;
    GetBattlePayMgr()->AssignDistributionToCharacter(packet.TargetCharacter, packet.DistributionID, packet.ProductID, packet.SpecializationID, packet.ChoiceID);
}

void WorldSession::HandleGetProductList(WorldPackets::BattlePay::GetProductList& /*packet*/)
{
    if (!GetBattlePayMgr()->IsAvailable())
        return;

    GetBattlePayMgr()->SendProductList();
    GetBattlePayMgr()->SendPointsBalance();
}

auto MakePurchase = [](ObjectGuid targetCharacter, uint32 clientToken , uint32 productID, WorldSession* session) -> void
{
    if (!session || !session->GetBattlePayMgr()->IsAvailable())
        return;

    auto mgr = session->GetBattlePayMgr();

    auto player = session->GetPlayer();
    if (!player)
        return;

    auto accountID = session->GetAccountId();

    Battlepay::Purchase purchase;
    purchase.ProductID = productID;
    purchase.ClientToken = clientToken;
    purchase.TargetCharacter = targetCharacter;
    purchase.Status = Battlepay::UpdateStatus::Loading;
    purchase.DistributionId = mgr->GenerateNewDistributionId();

    auto characterInfo = sWorld->GetCharacterInfo(targetCharacter);
    if (!characterInfo)
    {
        SendStartPurchaseResponse(session, purchase, Battlepay::Error::PurchaseDenied);
        return;
    }

    if (characterInfo->AccountId != accountID)
    {
        SendStartPurchaseResponse(session, purchase, Battlepay::Error::PurchaseDenied);
        return;
    }

    if (!sBattlePayDataStore->ProductExist(productID))
    {
        SendStartPurchaseResponse(session, purchase, Battlepay::Error::PurchaseDenied);
        return;
    }

    auto const& product = sBattlePayDataStore->GetProduct(purchase.ProductID);
    purchase.CurrentPrice = product.CurrentPriceFixedPoint;

    mgr->RegisterStartPurchase(purchase);

    auto accountBalance = session->GetBattlePayBalance();
    auto purchaseData = mgr->GetPurchase();

    if (!accountBalance)
    {
        SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::InsufficientBalance);
        return;
    }

    if (accountBalance < static_cast<int64>(purchaseData->CurrentPrice))
    {
        SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::InsufficientBalance);
        return;
    }

    if (!product.Items.empty())
    {
        if (product.Items.size() > GetBagsFreeSlots(player))
        {
            std::ostringstream data;
            data << sObjectMgr->GetTrinityString(Battlepay::String::NotEnoughFreeBagSlots, session->GetSessionDbLocaleIndex());
            player->SendCustomMessage(GetCustomMessage(Battlepay::CustomMessage::StoreBuyFailed), data);
            SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::PurchaseDenied);
            return;
        }
    }

    if (!product.ScriptName.empty())
    {
        std::string reason;
        if (!sScriptMgr->BattlePayCanBuy(session, product, reason))
        {
            std::ostringstream data;
            data << reason;
            player->SendCustomMessage(GetCustomMessage(Battlepay::CustomMessage::StoreBuyFailed), data);
            SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::PurchaseDenied);
            return;
        }
    }

    for (auto itr : product.Items)
    {
        if (mgr->AlreadyOwnProduct(itr.ItemID))
        {
            std::ostringstream data;
            data << sObjectMgr->GetTrinityString(Battlepay::String::YouAlreadyOwnThat, session->GetSessionDbLocaleIndex());;
            player->SendCustomMessage(GetCustomMessage(Battlepay::CustomMessage::StoreBuyFailed), data);
            SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::PurchaseDenied);
            return;
        }
    }

    purchaseData->PurchaseID = mgr->GenerateNewPurchaseID();
    purchaseData->ServerToken = urand(0, 0xFFFFFFF);
    //purchaseData->Status = Battlepay::UpdateStatus::Ready; ?

    SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::Ok);
    SendPurchaseUpdate(session, *purchaseData, Battlepay::Error::Ok);

    WorldPackets::BattlePay::ConfirmPurchase confirmPurchase;
    confirmPurchase.PurchaseID = purchaseData->PurchaseID;
    confirmPurchase.ServerToken = purchaseData->ServerToken;
    session->SendPacket(confirmPurchase.Write());
};

void WorldSession::HandleBattlePayStartPurchase(WorldPackets::BattlePay::StartPurchase& packet)
{
    if (packet.NativeVasRealmRequest)
    {
        SendFelsoriaVasRealmList(packet.ClientToken);
        return;
    }
    CancelFelsoriaTransfer();
    if (!HandleFelsoriaArtifactStart(packet.TargetCharacter, packet.ClientToken, packet.ProductID))
        MakePurchase(packet.TargetCharacter, packet.ClientToken, packet.ProductID, this);
}

void WorldSession::HandleBattlePayPurchaseProduct(WorldPackets::BattlePay::PurchaseProduct& packet)
{
    CancelFelsoriaTransfer();
    if (!HandleFelsoriaArtifactStart(packet.TargetCharacter, packet.ClientToken, packet.ProductID))
        MakePurchase(packet.TargetCharacter, packet.ClientToken, packet.ProductID, this);
}

void WorldSession::HandleBattlePayConfirmPurchase(WorldPackets::BattlePay::ConfirmPurchaseResponse& packet)
{
    if (HandleFelsoriaArtifactConfirm(packet))
        return;
    if (HandleFelsoriaTransferConfirmation(packet))
        return;
    if (!GetBattlePayMgr()->IsAvailable())
        return;

    packet.ClientCurrentPriceFixedPoint /= Battlepay::g_CurrencyPrecision;

    auto purchase = GetBattlePayMgr()->GetPurchase();
    if (!purchase)
        return;

    if (purchase->Lock)
    {
        SendPurchaseUpdate(this, *purchase, Battlepay::Error::PurchaseDenied);
        return;
    }

    if (purchase->ServerToken != packet.ServerToken || !packet.ConfirmPurchase || purchase->CurrentPrice != packet.ClientCurrentPriceFixedPoint)
    {
        SendPurchaseUpdate(this, *purchase, Battlepay::Error::PurchaseDenied);
        return;
    }

    auto player = GetPlayer();
    if (!player)
    {
        SendPurchaseUpdate(this, *purchase, Battlepay::Error::PurchaseDenied);
        return;
    }

    auto accountBalance = GetBattlePayBalance();
    if (accountBalance < static_cast<int64>(purchase->CurrentPrice))
    {
        SendPurchaseUpdate(this, *purchase, Battlepay::Error::PurchaseDenied);
        return;
    }

    purchase->Lock = true;
    purchase->Status = Battlepay::UpdateStatus::Finish;

    auto const& product = sBattlePayDataStore->GetProduct(purchase->ProductID);
    if (!product.ScriptName.empty())
    {
        std::string reason;
        if (!sScriptMgr->BattlePayCanBuy(this, product, reason))
        {
            std::ostringstream data;
            data << reason;
            player->SendCustomMessage(GetCustomMessage(Battlepay::CustomMessage::StoreBuyFailed), data);
            SendPurchaseUpdate(this, *purchase, Battlepay::Error::PaymentFailed);
            return;
        }
    }

    if (!product.Items.empty())
    {
        if (product.Items.size() > GetBagsFreeSlots(player))
        {
            std::ostringstream data;
            data << sObjectMgr->GetTrinityString(Battlepay::String::NotEnoughFreeBagSlots, GetSessionDbLocaleIndex());
            player->SendCustomMessage(GetCustomMessage(Battlepay::CustomMessage::StoreBuyFailed), data);
            SendStartPurchaseResponse(this, *purchase, Battlepay::Error::PurchaseDenied);
            return;
        }
    }

    for (auto itr : product.Items)
    {
        if (GetBattlePayMgr()->AlreadyOwnProduct(itr.ItemID))
        {
            std::ostringstream data;
            data << sObjectMgr->GetTrinityString(Battlepay::String::YouAlreadyOwnThat, GetSessionDbLocaleIndex());
            player->SendCustomMessage(GetCustomMessage(Battlepay::CustomMessage::StoreBuyFailed), data);
            SendStartPurchaseResponse(this, *purchase, Battlepay::Error::PurchaseDenied);
            return;
        }
    }

    SendPurchaseUpdate(this, *purchase, Battlepay::Error::Other);

    int64 const purchasePrice =
        static_cast<int64>(purchase->CurrentPrice);

    if (player->ChangeDonateTokenCount(
            -purchasePrice,
            Battlepay::BattlepayCustomType::BattlePayShop,
            purchase->ProductID))
    {
        GetBattlePayMgr()->ProcessDelivery(purchase);
    }
}

void WorldSession::HandleBattlePayAckFailedResponse(WorldPackets::BattlePay::BattlePayAckFailedResponse& /*packet*/)
{
}

void WorldSession::HandleBattlePayQueryClassTrialResult(WorldPackets::BattlePay::BattlePayQueryClassTrialResult& /*packet*/)
{
}

void WorldSession::HandleBattlePayTrialBoostCharacter(WorldPackets::BattlePay::BattlePayTrialBoostCharacter& /*packet*/)
{
}

void WorldSession::HandleBattlePayPurchaseDetailsResponse(WorldPackets::BattlePay::BattlePayPurchaseDetailsResponse& packet)
{
    WorldPackets::BattlePay::BattlePayPurchaseUnk response;
    response.UnkInt = 0;
    response.Key = "";
    response.UnkByte = packet.UnkByte;
    SendPacket(response.Write());
}

void WorldSession::HandleBattlePayPurchaseUnkResponse(WorldPackets::BattlePay::BattlePayPurchaseUnkResponse& /*packet*/)
{
    auto purchaseData = GetBattlePayMgr()->GetPurchase();
    SendPurchaseUpdate(this, *purchaseData, Battlepay::Error::Ok);
}

void WorldSession::SendDisplayPromo(int32 promotionID /*= 0*/)
{
    SendPacket(WorldPackets::BattlePay::DisplayPromotion(promotionID).Write());

    if (!GetBattlePayMgr()->IsAvailable())
        return;

    WorldPackets::BattlePay::DistributionListResponse packet;
    packet.Result = Battlepay::Error::Ok;

    QueryResult entitlements = LoginDatabase.PQuery(
        "SELECT product_id, purchase_id, distribution_id "
        "FROM account_character_boost "
        "WHERE account_id = %u AND state = 0 "
        "ORDER BY id",
        GetAccountId());

    if (entitlements)
    {
        AddAuthFlag(AT_AUTH_FLAG_90_LVL_UP);

        do
        {
            Field* fields = entitlements->Fetch();

            uint32 const productId = fields[0].GetUInt32();
            uint64 const purchaseId = fields[1].GetUInt64();
            uint64 const distributionId = fields[2].GetUInt64();

            if (!sBattlePayDataStore->ProductExist(productId))
            {
                TC_LOG_ERROR(
                    LOG_FILTER_SQL,
                    "Character boost distribution skipped: product %u does not exist.",
                    productId);
                continue;
            }

            auto const& product =
                sBattlePayDataStore->GetProduct(productId);

            WorldPackets::BattlePay::BattlePayDistributionObject data;
            data.DistributionID = distributionId;
            data.PurchaseID = purchaseId;
            data.Status =
                Battlepay::DistributionStatus::BATTLE_PAY_DIST_STATUS_AVAILABLE;
            data.ProductID = productId;
            data.TargetVirtualRealm = 0;
            data.TargetNativeRealm = 0;
            data.Revoked = false;

            WorldPackets::BattlePay::BattlePayProduct productData;
            productData.ProductID = product.ProductID;
            productData.Flags = product.Flags;
            productData.Type = product.Type;
            productData.UnkInt1 = 0;
            productData.DisplayId = 0;
            productData.ItemId = 0;
            productData.UnkInt4 = 0;
            productData.UnkInt5 = 0;
            productData.UnkString = "";
            productData.UnkBit = false;

            for (auto const& item : product.Items)
            {
                WorldPackets::BattlePay::ProductItem productItem;
                productItem.ID = item.ID;
                productItem.ItemID =
                    product.Items.size() > 1 ? 0 : item.ItemID;
                productItem.Quantity = item.Quantity;
                productItem.HasPet =
                    GetBattlePayMgr()->AlreadyOwnProduct(item.ItemID);
                productItem.PetResult = item.PetResult;

                auto itemDisplay = GetBattlePayMgr()->WriteDisplayInfo(
                    item.DisplayInfoID,
                    GetSessionDbLocaleIndex());

                if (std::get<0>(itemDisplay))
                {
                    productItem.DisplayInfo = boost::in_place();
                    productItem.DisplayInfo = std::get<1>(itemDisplay);
                }

                productData.Items.emplace_back(productItem);
            }

            auto productDisplay = GetBattlePayMgr()->WriteDisplayInfo(
                product.DisplayInfoID,
                GetSessionDbLocaleIndex());

            if (std::get<0>(productDisplay))
            {
                productData.DisplayInfo = boost::in_place();
                productData.DisplayInfo = std::get<1>(productDisplay);
            }

            data.Product = boost::in_place();
            data.Product = std::move(productData);

            packet.DistributionObject.emplace_back(std::move(data));
        }
        while (entitlements->NextRow());
    }
    SendPacket(packet.Write());
    // Notify the glue screen that each unassigned boost is currently usable.
    for (auto const& distribution : packet.DistributionObject)
    {
        GetBattlePayMgr()->SendBattlePayDistribution(
            distribution.ProductID,
            Battlepay::DistributionStatus::BATTLE_PAY_DIST_STATUS_AVAILABLE,
            distribution.DistributionID);
    }
}

