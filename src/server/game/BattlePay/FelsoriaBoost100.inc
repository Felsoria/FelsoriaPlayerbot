// FELSORIA_LEVEL100_BOOST_P25. Included inside CharacterService's anonymous namespace.
// Product 100 is separate from the existing product 109 / level 110 service.
// FELSORIA_BOOST100_CLAIM_HOTFIX_V1
void FelsoriaBoost100Failure(Player* player, char const* message)
{
    player->FelsoriaBoost100InProgress = false;
    if (player->GetSession())
        ChatHandler(player->GetSession()).SendSysMessage(message);
}

struct FelsoriaBoostItem
{
    uint32 Entry;
    uint32 Count;
    bool QuestReward;
};

std::vector<uint32> FelsoriaDhStarterQuests()
{
    return {40077,39279,40378,40379,38759,39049,39050,38765,38766,
        38813,39262,39495,38727,38819,38725,40222,40051,39515,39516,
        39663,38728,38729,38668,38669,38672,38689,38690,38723,
        39682,39683,39684,39685,39686,39687,40373,39688,39694};
}

uint32 FelsoriaTrialLoadout(uint8 playerClass, uint32 specIndex)
{
    // IDs verified against the uploaded Legion CharacterLoadout.db2 (Purpose=4).
    static uint32 const ids[13][4] = {
        {0,0,0,0}, {653,654,655,0}, {674,675,676,0}, {659,660,661,0},
        {671,672,673,0}, {656,657,658,0}, {668,669,670,0}, {662,663,664,0},
        {647,648,649,0}, {650,651,652,0}, {665,666,667,0},
        {643,644,645,646}, {701,702,0,0}
    };
    return playerClass < 13 && specIndex < 4 ? ids[playerClass][specIndex] : 0;
}

bool FelsoriaAddBoostItem(std::vector<FelsoriaBoostItem>& items, uint32 entry, uint32 count, bool questReward=false)
{
    if (!entry || !count)
        return true;
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
    if (!proto)
        return false;
    if (proto->GetInventoryType() == INVTYPE_BAG)
        return true; // Exactly four 20-slot bags are granted separately.
    items.push_back({entry, count,questReward});
    return true;
}

bool FelsoriaBuildBoostItems(Player* player, uint32 specId, uint32 specIndex,
    std::vector<FelsoriaBoostItem>& items)
{
    if (player->getClass() != CLASS_DEMON_HUNTER)
    {
        uint32 const id = FelsoriaTrialLoadout(player->getClass(), specIndex);
        CharacterLoadoutEntry const* loadout = sCharacterLoadoutStore.LookupEntry(id);
        if (!loadout || loadout->Purpose != 4 || loadout->ChrClassID != player->getClass())
        {
            ChatHandler(player->GetSession()).PSendSysMessage("Boost 100: invalid trial loadout %u (class %u, spec index %u).", id, uint32(player->getClass()), specIndex);
            return false;
        }
        for (CharacterLoadoutItemEntry const* entry : sCharacterLoadoutItemStore)
            if (entry->CharacterLoadoutID == id &&
                !FelsoriaAddBoostItem(items, uint32(entry->ItemID), 1))
            {
                // FELSORIA_BOOST100_TRIAL_ITEM_FIX_V1
                // The uploaded Legion data references 141410 in every trial loadout,
                // but has no ItemSparse row for it. Item.db2 classifies it as Misc.
                // Omit only this unavailable auxiliary item; keep all equipment mandatory.
                // If a later data set supplies its template, the normal grant above succeeds.
                if (uint32(entry->ItemID) == 141410)
                {
                    ChatHandler(player->GetSession()).SendSysMessage("Boost 100: unavailable auxiliary trial item 141410 omitted; equipment validation continues.");
                    continue;
                }
                ChatHandler(player->GetSession()).PSendSysMessage("Boost 100: missing item template %u from trial loadout %u.", uint32(entry->ItemID), id);
                return false;
            }
        return !items.empty();
    }

    // DH receives its actual starting outfit and its starting quest packages.
    bool outfitFound = false;
    for (CharStartOutfitEntry const* outfit : sCharStartOutfitStore)
        if (outfit->ClassID == CLASS_DEMON_HUNTER && outfit->RaceID == player->getRace() &&
            outfit->SexID == player->getGender())
        {
            for (int32 entry : outfit->ItemID)
                if (entry > 0 && !FelsoriaAddBoostItem(items, uint32(entry), 1))
                    return false;
            outfitFound = true;
            break;
        }
    if (!outfitFound)
        return false;

    for (uint32 id : FelsoriaDhStarterQuests())
    {
        Quest const* quest = sQuestDataStore->GetQuestTemplate(id);
        if (!quest || (quest->AllowableRaces && !(quest->AllowableRaces & player->getRaceMask())))
            continue;
        for (uint32 i = 0; i < QUEST_ITEM_COUNT; ++i)
            if (!FelsoriaAddBoostItem(items, quest->RewardItemId[i], quest->RewardItemCount[i],true))
                return false;
        uint32 choice = 0;
        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(quest->RewardChoiceItemId[i]))
                if (proto->IsUsableBySpecialization(specId, 100) &&
                    (!choice || proto->GetId() < quest->RewardChoiceItemId[choice - 1]))
                    choice = i + 1;
        if (choice && !FelsoriaAddBoostItem(items, quest->RewardChoiceItemId[choice - 1],
            quest->RewardChoiceItemCount[choice - 1],true))
            return false;
        if (quest->PackageID)
        {
            auto packages = sDB2Manager.GetQuestPackageItems(quest->PackageID);
            if (!packages || packages->empty())
                return false;
            QuestPackageItemEntry const* selected = nullptr;
            for (QuestPackageItemEntry const* reward : *packages)
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(reward->ItemID))
                    if (proto->IsUsableBySpecialization(specId, 100) &&
                        (!selected || reward->ItemID < selected->ItemID))
                        selected = reward;
            if (!selected || !FelsoriaAddBoostItem(items, uint32(selected->ItemID),
                uint32(selected->ItemQuantity),true))
                return false;
        }
    }
    return !items.empty();
}

void FelsoriaMailBoostItem(Player* player, Item* item, SQLTransaction& trans, bool existing,
    std::vector<Item*>& preservedHeirlooms)
{
    if (existing)
    {
        player->MoveItemFromInventory(item, true);
        item->DeleteFromInventoryDB(trans);
        if (sDB2Manager.GetHeirloomByItemId(item->GetEntry()))
        {
            preservedHeirlooms.push_back(item);
            return;
        }
    }
    item->SaveToDB(trans);
    MailDraft("Felsoria Character Boost", "Your preserved equipment or boost rewards.")
        .AddItem(item).SendMailTo(trans, MailReceiver(player),
            MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM));
}

void FelsoriaBoostCapital(Player* player);

void FelsoriaConfirmBoost100(Player* player, uint32 accountId, uint64 guid,
    uint64 entitlementId, uint32 attempt)
{
    if (!player || !player->GetSession() || player->GetSession()->GetAccountId() != accountId ||
        player->GetGUID().GetCounter() != guid)
        return;
    QueryResult saved=CharacterDatabase.PQuery("SELECT entitlement_id FROM felsoria_boost100_completed "
        "WHERE entitlement_id=" UI64FMTD " AND guid=" UI64FMTD " AND account_id=%u",entitlementId,guid,accountId);
    if (!saved)
    {
        if (attempt < 120)
            player->AddDelayedEvent(500,[player,accountId,guid,entitlementId,attempt]()
            { FelsoriaConfirmBoost100(player,accountId,guid,entitlementId,attempt+1); });
        else
        {
            TC_LOG_ERROR(LOG_FILTER_CHARACTER,"FELSORIA_BOOST100_SAVE_NOT_CONFIRMED account=%u guid=" UI64FMTD,accountId,guid);
            ChatHandler(player->GetSession()).SendSysMessage("Boost 100: save not confirmed. Relog and report this message; do not purchase another boost.");
        }
        return;
    }
    LoginDatabase.PExecute("UPDATE account_character_boost SET state=2,completed_at=NOW() WHERE id=" UI64FMTD
        " AND account_id=%u AND product_id=100 AND state=1",entitlementId,accountId);
    FelsoriaBoostCapital(player);
    ChatHandler(player->GetSession()).SendSysMessage("Boost 100 completed and saved: level 100, equipment, four bags and 10,000 gold.");
    TC_LOG_INFO(LOG_FILTER_CHARACTER,"FELSORIA_BOOST100_COMPLETED account=%u guid=" UI64FMTD,accountId,guid);
}

void FelsoriaBoostCapital(Player* player)
{
    WorldLocation const destination = player->GetTeamId() == TEAM_ALLIANCE ?
        WorldLocation(0, -8543.563477f, 461.028870f, 104.730820f, 5.350924f) :
        WorldLocation(1, 1465.133179f, -4416.354492f, 25.454123f, 0.180618f);
    player->SetHomebind(destination, player->GetTeamId() == TEAM_ALLIANCE ? 5390 : 1637);
    player->TeleportTo(destination);
}

void ApplyFelsoriaLevel100Boost(Player* player, uint32 accountId, uint64 guid,
    uint64 entitlementId, uint32 specId, uint32 specIndex, uint32 attempt)
{
    if (!player || !player->GetSession() || player->GetSession()->IsBotSession() ||
        player->GetSession()->GetAccountId() != accountId || player->GetGUID().GetCounter() != guid)
        return;
    if (!player->IsInWorld() || player->IsBeingTeleported() || !player->IsSettingFinish() ||
        player->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CASTING))
    {
        if (attempt < 120)
            player->AddDelayedEvent(500, [player,accountId,guid,entitlementId,specId,specIndex,attempt]()
            { ApplyFelsoriaLevel100Boost(player,accountId,guid,entitlementId,specId,specIndex,attempt+1); });
        else FelsoriaBoost100Failure(player,"Boost 100: login/reset/teleport/cast did not finish in 60 seconds. Once ready, use .boost retry.");
        return;
    }
    if (!CharacterDatabase.Query("SELECT version FROM felsoria_boost100_schema WHERE id=1 AND version=1"))
    {
        FelsoriaBoost100Failure(player,"Boost 100: characters SQL/schema version 1 is missing. Purchase preserved.");
        TC_LOG_ERROR(LOG_FILTER_CHARACTER,"FELSORIA_BOOST100_CHARACTER_SQL_NOT_INSTALLED");
        return;
    }
    QueryResult completed = CharacterDatabase.PQuery(
        "SELECT entitlement_id FROM felsoria_boost100_completed WHERE entitlement_id=" UI64FMTD
        " AND guid=" UI64FMTD " AND account_id=%u", entitlementId,guid,accountId);
    if (completed)
    {
        LoginDatabase.PExecute("UPDATE account_character_boost SET state=2,completed_at=NOW() "
            "WHERE id=" UI64FMTD " AND account_id=%u AND product_id=100 AND state=1",entitlementId,accountId);
        FelsoriaBoostCapital(player);
        return; // Character rewards and completion marker were saved in one transaction.
    }
    if (player->getLevel() >= 100 || !player->isAlive() || player->isInCombat() || player->GetTransport() ||
        player->GetMap()->IsDungeon() || player->GetBattleground())
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Boost 100 eligibility: level=%u alive=%u combat=%u transport=%u dungeon=%u battleground=%u.", uint32(player->getLevel()), uint32(player->isAlive()), uint32(player->isInCombat()), uint32(player->GetTransport()!=nullptr), uint32(player->GetMap()->IsDungeon()), uint32(player->GetBattleground()!=nullptr));
        FelsoriaBoost100Failure(player,"Boost 100: not eligible at the current location/state. Purchase preserved. Once ready, use .boost retry.");
        TC_LOG_ERROR(LOG_FILTER_CHARACTER,"FELSORIA_BOOST100_NOT_ELIGIBLE account=%u guid=" UI64FMTD,accountId,guid);
        return;
    }
    ChrSpecializationEntry const* spec = sDB2Manager.GetChrSpecializationByIndex(player->getClass(),specIndex);
    ItemTemplate const* bag = sObjectMgr->GetItemTemplate(41599);
    uint32 const intro = player->getClass() == CLASS_DEMON_HUNTER ?
        (player->GetTeamId() == TEAM_ALLIANCE ? 39691 : 40976) :
        (player->GetTeamId() == TEAM_ALLIANCE ? 40519 : 43926);
    uint32 const effectiveIntro = intro;
    Quest const* introQuest = sQuestDataStore->GetQuestTemplate(effectiveIntro);
    uint32 const ground = player->getClass() == CLASS_DEMON_HUNTER ? 200175 :
        player->getClass() == CLASS_DEATH_KNIGHT ? 48778 :
        player->GetTeamId() == TEAM_ALLIANCE ? 23228 : 23250;
    uint32 const flight = player->GetTeamId() == TEAM_ALLIANCE ? 32235 : 32243;
    std::vector<uint32> const riding = {33388,33391,34090,90267,54197,115913,191645,ground,flight};
    if (!spec || spec->ID != specId || !bag || bag->GetContainerSlots() != 20 || !introQuest ||
        (introQuest->AllowableRaces && !(introQuest->AllowableRaces & player->getRaceMask())) ||
        player->GetMoney() > MAX_MONEY_AMOUNT - 10000ULL * GOLD)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Boost 100 data check: spec=%u valid=%u bag41599=%u slots=%u intro=%u exists=%u raceAllowed=%u goldSpace=%u.", specId, uint32(spec && spec->ID==specId), uint32(bag!=nullptr), bag ? uint32(bag->GetContainerSlots()) : 0u, effectiveIntro, uint32(introQuest!=nullptr), uint32(introQuest && (!introQuest->AllowableRaces || (introQuest->AllowableRaces & player->getRaceMask()))), uint32(player->GetMoney() <= MAX_MONEY_AMOUNT-10000ULL*GOLD));
        FelsoriaBoost100Failure(player,"Boost 100: required reward data check failed. Purchase preserved; report the preceding line.");
        TC_LOG_ERROR(LOG_FILTER_CHARACTER,"FELSORIA_BOOST100_PREFLIGHT_FAILED account=%u intro=%u spec=%u",accountId,effectiveIntro,specId);
        return;
    }
    for (uint32 spell : riding)
        if (!sSpellMgr->GetSpellInfo(spell))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("Boost 100: required riding/mount spell %u is missing.",spell);
            FelsoriaBoost100Failure(player,"Boost 100 stopped before rewards. Purchase preserved.");
            TC_LOG_ERROR(LOG_FILTER_CHARACTER,"FELSORIA_BOOST100_SPELL_MISSING spell=%u",spell);
            return;
        }
    if (player->getClass() == CLASS_DEMON_HUNTER &&
        (!sQuestDataStore->GetQuestTemplate(40374) || !sQuestDataStore->GetQuestTemplate(40375)))
    {
        FelsoriaBoost100Failure(player,"Boost 100: DH choice quest 40374/40375 is missing. Purchase preserved.");
        return;
    }
    if (!player->GetQuestRewardStatus(effectiveIntro) && player->GetQuestStatus(effectiveIntro) == QUEST_STATUS_NONE &&
        player->FindQuestSlot(0) >= MAX_QUEST_LOG_SIZE)
    {
        FelsoriaBoost100Failure(player,"Boost 100: quest log is full. Free one slot and use .boost retry.");
        return;
    }
    std::vector<FelsoriaBoostItem> grants;
    if (!FelsoriaBuildBoostItems(player,specId,specIndex,grants))
    {
        FelsoriaBoost100Failure(player,"Boost 100: trial/starter reward items are missing or invalid. Purchase preserved; report preceding messages.");
        TC_LOG_ERROR(LOG_FILTER_CHARACTER,"FELSORIA_BOOST100_ITEMS_MISSING account=%u guid=" UI64FMTD,accountId,guid);
        return;
    }
    uint32 heirloomCount=0;
    for (uint8 slot=EQUIPMENT_SLOT_START;slot<INVENTORY_SLOT_BAG_END;++slot)
        if (Item* old=player->GetItemByPos(INVENTORY_SLOT_BAG_0,slot))
        {
            if (sDB2Manager.GetHeirloomByItemId(old->GetEntry())) ++heirloomCount;
            if (Bag* oldBag=old->ToBag())
                for (uint32 inside=0;inside<oldBag->GetBagSize();++inside)
                    if (Item* content=oldBag->GetItemByPos(uint8(inside)))
                        if (sDB2Manager.GetHeirloomByItemId(content->GetEntry())) ++heirloomCount;
        }
    if (heirloomCount > 80)
    {
        FelsoriaBoost100Failure(player,"Boost 100: too many heirlooms to preserve. Purchase preserved.");
        return;
    }
    std::vector<Item*> created;
    for (FelsoriaBoostItem const& grant : grants)
        for (uint32 i=0;i<grant.Count;++i)
        {
            Item* item = Item::CreateItem(grant.Entry,1,player);
            if (!item) { for (Item* previous : created) delete previous; FelsoriaBoost100Failure(player,"Boost 100: equipment item creation failed. Purchase preserved."); return; }
            if (grant.QuestReward)
                for (uint32 bonus : sObjectMgr->GetItemBonusTree(grant.Entry,
                    player->GetMap()->GetDifficultyLootItemContext(),100))
                    item->AddBonuses(bonus);
            created.push_back(item);
        }
    for (uint32 i=0;i<4;++i)
    {
        Item* item = Item::CreateItem(41599,1,player);
        if (!item) { for (Item* previous : created) delete previous; FelsoriaBoost100Failure(player,"Boost 100: bag item creation failed. Purchase preserved."); return; }
        created.push_back(item);
    }

    // No mutation until all required data and all new item objects are available.
    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    std::vector<Item*> preservedHeirlooms;
    player->GiveLevel(100);
    player->ActivateTalentGroup(spec);
    player->SetUInt32Value(PLAYER_FIELD_XP,0);
    player->UpdateSkillsForLevel();
    player->LearnDefaultSkills();
    player->LearnDefaultSpells();
    player->InitTalentForLevel();
    LearnBoostClassSpells(player);
    for (uint32 spell : riding) player->learnSpell(spell,false);

    for (uint8 slot=EQUIPMENT_SLOT_START;slot<EQUIPMENT_SLOT_END;++slot)
        if (slot != EQUIPMENT_SLOT_BODY && slot != EQUIPMENT_SLOT_TABARD)
            if (Item* item=player->GetItemByPos(INVENTORY_SLOT_BAG_0,slot))
                FelsoriaMailBoostItem(player,item,trans,true,preservedHeirlooms);
    for (uint8 slot=INVENTORY_SLOT_BAG_START;slot<INVENTORY_SLOT_BAG_END;++slot)
        if (Bag* oldBag=player->GetBagByPos(slot))
        {
            for (uint32 inside=0;inside<oldBag->GetBagSize();++inside)
                if (Item* item=oldBag->GetItemByPos(uint8(inside)))
                    FelsoriaMailBoostItem(player,item,trans,true,preservedHeirlooms);
            FelsoriaMailBoostItem(player,oldBag,trans,true,preservedHeirlooms);
        }
    // Equip bags first, then the strongest starter reward for each equipment slot.
    std::stable_sort(created.begin(),created.end(),[](Item* a,Item* b)
    {
        if (a->IsBag() != b->IsBag()) return a->IsBag();
        return a->GetItemLevel(100) > b->GetItemLevel(100);
    });
    for (Item* item : created)
    {
        if (!item->IsBag() && !preservedHeirlooms.empty())
        {
            for (Item* old : preservedHeirlooms)
            {
                ItemPosCountVec dest;
                // Preflight bounds these to 80 objects; all four new bags are empty here.
                if (player->CanStoreItem(NULL_BAG,NULL_SLOT,dest,old,false) == EQUIP_ERR_OK)
                    player->StoreItem(dest,old,true);
            }
            preservedHeirlooms.clear();
        }
        uint16 dest=0;
        if (player->CanEquipItem(NULL_SLOT,dest,item,false) == EQUIP_ERR_OK)
            player->EquipItem(dest,item,true);
        else
        {
            ItemPosCountVec storage;
            if (player->CanStoreItem(NULL_BAG,NULL_SLOT,storage,item,false) == EQUIP_ERR_OK)
                player->StoreItem(storage,item,true);
            else FelsoriaMailBoostItem(player,item,trans,false,preservedHeirlooms);
        }
    }
    if (player->getClass() == CLASS_DEMON_HUNTER)
    {
        for (uint32 id : FelsoriaDhStarterQuests()) player->MarkFelsoriaBoost100Quest(id);
        player->SetIllidariChoice(40374,true);
        player->MarkFelsoriaBoost100Quest(player->GetTeamId() == TEAM_ALLIANCE ? 39689 : 39690);
    }
    else if (player->getClass() == CLASS_DEATH_KNIGHT)
    {
        for (auto const& entry : sQuestDataStore->GetQuestTemplates())
            if (entry.second->QuestSortID == -372 && entry.second->Level <= 60)
                player->MarkFelsoriaBoost100Quest(entry.first);
        player->setFactionForRace(player->getRace());
        std::vector<uint32> const factions=player->GetTeamId() == TEAM_ALLIANCE ?
            std::vector<uint32>{72,47,54,69,930,1134,1353} : std::vector<uint32>{76,68,81,530,911,1133,1352};
        for (uint32 id : factions)
            if (FactionEntry const* faction=sFactionStore.LookupEntry(id))
            {
                if (player->GetReputationMgr().GetReputation(id) < 3000)
                    player->GetReputationMgr().SetReputation(faction,3000);
                player->GetReputationMgr().SetAtWar(faction->ReputationIndex,false);
            }
    }
    if (!player->GetQuestRewardStatus(effectiveIntro) && player->GetQuestStatus(effectiveIntro) == QUEST_STATUS_NONE)
        player->AddQuest(introQuest,nullptr); // Leave incomplete: the player starts the Legion introduction.
    player->ModifyMoney(10000LL * GOLD);
    player->SendTalentsInfoData(false);
    player->UpdateAllStats();
    player->SetFullHealth();
    player->SaveToDB(false,entitlementId,trans);
    player->AddDelayedEvent(500,[player,accountId,guid,entitlementId]()
    { FelsoriaConfirmBoost100(player,accountId,guid,entitlementId,1); });
}
