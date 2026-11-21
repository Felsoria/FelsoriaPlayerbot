
//#include "G3D/stringutils.h"
#include "Guild.h"
#include "QuestDef.h"
#include "QuestData.h"
#include "QuestPackets.h"
#include "FelsoriaRegional.h"
#include "BotGroupAI.h"
#include "FelsoriaDHDummyLimit.h"
#include "Log.h"
#include "LFGMgr.h"
#include "MoveSplineInit.h"
#include "MoveSpline.h"
#include "PathGenerator.h"
#include "Map.h"
#include "GameObject.h"
#include <cmath>
#include "BotBGAIMovement.h"
#include "PlayerBotMgr.h"
#include "PlayerBotSession.h"
#include "Group.h"
#include "Spell.h"
#include "RdfRotationPolicy.h"
#include "Pet.h"
#include "ThreatManager.h"
#include "BotGroupClassAI.h"
#include "config.h"
#include "TradeData.h"
#include "ItemPackets.h"
#include "CharmInfo.h"
#include <corecrt_math_defines.h>

BotGroupAI* BotGroupAI::debugGroupAI = NULL;
bool BotGroupAI::PVE_MAX_DUNGEON = false;
bool BotGroupAI::PVE_DRIVING = true;
bool BotGroupAI::PVE_PULL = true;

BotGroupAI* BotGroupAI::CreateBotGroupAIByPlayerClass(Player* player)
{
	PlayerBotSetting::ClearUnknowMount(player);
	BotGroupAI* pAI = NULL;
	switch (player->getClass())
	{
	case CLASS_WARRIOR:
		pAI = new GroupWarriorAI(player);
		break;
	case CLASS_MAGE:
		pAI = new GroupMageAI(player);
		break;
	case CLASS_PRIEST:
		pAI = new GroupPriestAI(player);
		break;
	case CLASS_HUNTER:
		pAI = new GroupHunterAI(player);
		break;
	case CLASS_WARLOCK:
		pAI = new GroupWarlockAI(player);
		break;
	case CLASS_PALADIN:
		pAI = new GroupPaladinAI(player);
		break;
	case CLASS_ROGUE:
		pAI = new GroupRogueAI(player);
		break;
	case CLASS_SHAMAN:
		pAI = new GroupShamanAI(player);
		break;
	case CLASS_DRUID:
		pAI = new GroupDruidAI(player);
		break;
	case CLASS_DEATH_KNIGHT:
		pAI = new GroupDeathknightAI(player);
		break;
	}
	if (!pAI)
		pAI = new BotGroupAI(player);
	pAI->ResetBotAI();
	return pAI;
}

BotGroupAI::BotGroupAI(Player* player) :
PlayerAI(player),
m_UpdateTick(BOTAI_UPDATE_TICK),
m_DuelEmoteTick(0),
m_Movement(new BotBGAIMovement(player, this)),
m_VehicleMovement3D(new BotAIVehicleMovement3D(player)),
m_UseMountID(PlayerBotSetting::MountForPlayer(player)),
m_Guild(player),
pHorrorState(NULL),
m_CheckStoped(player),
m_Teleporting(player),
m_UseFood(player),
m_UsePotion(player),
m_FindLoot(player),
m_LootedItems(player),
m_AITrade(player),
m_DelayGiveXP(player),
m_Revive(player),
m_RevivePlayer(player),
m_Flee(player),
m_CruxMovement(player),
m_TankTargets(player),
m_Flying(player),
m_WishStore(player),
m_FliterCreatures(player),
m_WaitAuras(player),
m_CheckDuel(player),
m_FastAid(player),
m_MovetoUseGO(player),
m_GroupLeader(player),
m_MovetoHaltPos(player),
m_CruxHealTarget(ObjectGuid::Empty),
m_SeduceTarget(ObjectGuid::Empty),
m_MasterPlayer(NULL),
m_FullDispel(0),
m_MeleeFleeTick(0),
bothp(0),
m_ForceFlee(false),
m_StopFollow(false),
m_HasReset(false)
{
	if (!me->IsPvP())
	{
		BotUtility::PlayerBotTogglePVP(player, true);
	}
}

BotGroupAI::~BotGroupAI()
{
	if (m_VehicleMovement3D)
	{
		delete m_VehicleMovement3D;
		m_VehicleMovement3D = NULL;
	}
}

bool BotGroupAI::CanReciveCommand(std::string& cmd, std::string& param)
{
	if (cmd.empty())
		return false;
	if (cmd[0] == '@')
	{
		int32 firstEndIndex = cmd.find(' ');
		if (firstEndIndex <= 1)
			return false;
		std::string target = cmd.substr(1, firstEndIndex - 1);
		std::string realCmd = cmd.substr(firstEndIndex + 1);
		if (realCmd.empty())
			return false;
		if (target == "tank")
		{
			if (!IsTankBotAI())
				return false;
		}
		else if (target == "melee")
		{
			if (!IsMeleeBotAI() || IsHealerBotAI())
				return false;
		}
		else if (target == "ranged")
		{
			if (!IsRangeBotAI() || IsHealerBotAI())
				return false;
		}
		else if (target == "dps")
		{
			if (IsTankBotAI() || IsHealerBotAI())
				return false;
		}
		else if (target == "heal")
		{
			if (!IsHealerBotAI())
				return false;
		}
		else if (me->getClass() == Classes::CLASS_WARRIOR)
		{
			if (target != "zs")
				return false;
		}
		else if (me->getClass() == Classes::CLASS_PALADIN)
		{
			if (target != "qs")
				return false;
		}
		else if (me->getClass() == Classes::CLASS_HUNTER)
		{
			if (target != "lr")
				return false;
		}
		else if (me->getClass() == Classes::CLASS_ROGUE)
		{
			if (target != "dz")
				return false;
		}
		else if (me->getClass() == Classes::CLASS_PRIEST)
		{
			if (target != "ms")
				return false;
		}
		else if (me->getClass() == Classes::CLASS_DEATH_KNIGHT)
		{
			if (target != "dk")
				return false;
		}
		else if (me->getClass() == Classes::CLASS_SHAMAN)
		{
			if (target != "sm")
				return false;
		}
		else if (me->getClass() == Classes::CLASS_MAGE)
		{
			if (target != "fs")
				return false;
		}
		else if (me->getClass() == Classes::CLASS_WARLOCK)
		{
			if (target != "ss")
				return false;
		}
		else if (me->getClass() == Classes::CLASS_DRUID)
		{
			if (target != "xd")
				return false;
		}

		int32 secondEndIndex = realCmd.find(' ');
		if (secondEndIndex <= 0)
			cmd = realCmd;
		else
		{
			cmd = realCmd.substr(0, secondEndIndex);
			param = realCmd.substr(secondEndIndex + 1);
		}
	}
	else
	{
		int32 firstEndIndex = cmd.find(' ');
		if (firstEndIndex < 0)
			return true;
		std::string realCmd = cmd.substr(0, firstEndIndex);
		param = cmd.substr(firstEndIndex + 1);
		cmd = realCmd;
	}
	return true;
}

void BotGroupAI::ProcessSummonCommand()
{
	if (!m_MasterPlayer)
		return;
	m_ForceFlee = false;
	m_StopFollow = false;
	m_SeduceTarget = ObjectGuid::Empty;
	me->SetSelection(ObjectGuid::Empty);
	if (m_Teleporting.CanMovement() && !m_MasterPlayer->IsFlying())
		m_Teleporting.SetTeleport(m_MasterPlayer, me->GetMap() && me->GetMap()->IsDungeon() ? 0.0f : 2.5f);
}

void BotGroupAI::ProcessAttackCommand()
{
	Unit* pMasterTarget = m_MasterPlayer->GetSelectedUnit();
	if (!pMasterTarget || !pMasterTarget->isAlive())
		return;
	if (!me->IsValidAttackTarget(pMasterTarget))
		return;
    if (!FelsoriaDHDummyLimit::CanUse(me, pMasterTarget))
        return;
	me->SetSelection(pMasterTarget->GetGUID());
	m_ForceFlee = false;
	m_StopFollow = false;
	m_SeduceTarget = ObjectGuid::Empty;
}

void BotGroupAI::ProcessFleeCommand()
{
	me->SetSelection(ObjectGuid::Empty);
	m_SeduceTarget = ObjectGuid::Empty;
	m_ForceFlee = true;
}

void BotGroupAI::ProcessStopCommand()
{
	me->SetSelection(ObjectGuid::Empty);
	m_SeduceTarget = ObjectGuid::Empty;
	m_StopFollow = true;
}

void BotGroupAI::ProcessSetting()
{
	if (!m_MasterPlayer || !me->IsInWorld() || !me->isAlive() || me->isInCombat() || !m_Teleporting.CanMovement() || m_UseFood.HasFoodState())
		return;
	if (me->HasUnitState(UNIT_STATE_CASTING))
		return;
	if (!me->IsSettingFinish())
		return;
	PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(me->GetSession());
	if (!pSession)
		return;
	if (pSession->HasSchedules())
		return;
	BotGlobleSchedule schedule2(BotGlobleScheduleType::BGSType_Settting, 0);
	schedule2.parameter1 = m_MasterPlayer->getLevel();
	schedule2.parameter2 = m_MasterPlayer->getLevel();
	schedule2.parameter3 = PlayerBotSetting::FindPlayerTalentType(me) + 1;
	//schedule2.parameter4 = 1;
	pSession->PushScheduleToQueue(schedule2);
	me->ResetTalents(true);
	m_HasReset = false;
}

void BotGroupAI::ProcessListEquip(Player* srcPlayer)
{
	std::lock_guard<std::mutex> lock(m_ItemLock);
	if (!srcPlayer || srcPlayer->IsPlayerBot())
		return;
	WorldSession* pSession = srcPlayer->GetSession();
	std::list<std::string> needSendText;
	for (uint8 slot = InventoryPackSlots::INVENTORY_SLOT_ITEM_START; slot < InventoryPackSlots::INVENTORY_SLOT_ITEM_END; slot++)
	{
		Item* pItem = me->GetItemByPos(255, slot);
		if (!pItem)
			continue;
		const ItemTemplate* pTemplate = pItem->GetTemplate();
		if (!pTemplate)
			continue;
		std::string& itemLink = BotUtility::BuildItemLinkText(pTemplate);
		char outputText[256] = { 0 };
		sprintf_s(outputText, 255, "%s x %d", itemLink.c_str(), pItem->GetCount());
		//me->Whisper(std::string(outputText), Language::LANG_COMMON, srcPlayer);
		needSendText.push_back(std::string(outputText));

		//WorldPacket cmd(1);
		//cmd << pTemplate->GetId();
		//pSession->HandleItemQuerySingleOpcode(cmd);
	}
	for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
	{
		if (Bag* pBag = me->GetBagByPos(i))
		{
			for (uint32 j = 0; j < pBag->GetBagSize(); j++)
			{
				Item* pItem = pBag->GetItemByPos(uint8(j));
				if (!pItem)
					continue;
				const ItemTemplate* pTemplate = pItem->GetTemplate();
				if (!pTemplate)
					continue;
				std::string& itemLink = BotUtility::BuildItemLinkText(pTemplate);
				char outputText[256] = { 0 };
				sprintf_s(outputText, 255, "%s x %d", itemLink.c_str(), pItem->GetCount());
				//me->Whisper(std::string(outputText), Language::LANG_COMMON, srcPlayer);
				needSendText.push_back(std::string(outputText));

				//WorldPacket cmd(1);
				//cmd << pTemplate->ItemId;
				//pSession->HandleItemQuerySingleOpcode(cmd);
			}
		}
	}

	for (std::string& sendText : needSendText)
	{
		me->Whisper(sendText, Language::LANG_COMMON, srcPlayer->GetGUID());
	}
}

void BotGroupAI::ProcessUpequip(Player* srcPlayer, std::string& equipLink)
{
	std::lock_guard<std::mutex> lock(m_ItemLock);
	if (me->isInCombat())
		return;
	if (!srcPlayer || equipLink.empty())
		return;
	int firstIndex = equipLink.find(':');
	if (firstIndex < 0)
		return;
	uint32 entry = BotUtility::GetFirstNumberByString(equipLink.substr(firstIndex));
	if (entry == 0)
		return;
	uint8 bag = 255;
	uint8 index = 0;
	Item* pItem = BotUtility::FindItemFromAllBag(me, entry, bag, index);
	if (pItem == NULL)
		return;
	//if (pItem->GetTemplate()->GetInventoryType() == InventoryType::INVTYPE_AMMO)
	//	me->setAmmo(pItem->GetEntry());
	//else
	{
		uint16 dest;
		InventoryResult msg = me->CanEquipItem(NULL_SLOT, dest, pItem, !pItem->IsBag());
		if (msg != EQUIP_ERR_OK)
		{
			std::string outString;
			consoleToUtf8(std::string("Ę§°Ü×°±¸"), outString);
			me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
			return;
		}
		WorldPacket opcode(CMSG_AUTO_EQUIP_ITEM);
		WorldPackets::Item::AutoEquipItem packet(std::move(opcode));
		packet.PackSlot = bag;
		packet.Slot = index;
		WorldPackets::Item::InvUpdate::InvItem inv{ bag, index };
		packet.Inv.Items.resize(1);
		packet.Inv.Items.push_back(inv);
		me->GetSession()->HandleAutoEquipItem(packet);
        if (me->GetItemByPos(dest) && me->GetItemByPos(dest)->GetEntry()==entry)
            sRegionalBotChatMgr.FullControl(me,srcPlayer,"equip_command",entry);
	}
	std::string outString;
	consoleToUtf8(std::string("łÉą¦×°±¸"), outString);
	me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
}

void BotGroupAI::ProcessUnequip(Player* srcPlayer, std::string& equipLink)
{
	std::lock_guard<std::mutex> lock(m_ItemLock);
	if (me->isInCombat())
		return;
	if (!srcPlayer || equipLink.empty())
		return;
	int firstIndex = equipLink.find(':');
	if (firstIndex < 0)
		return;
	uint32 entry = BotUtility::GetFirstNumberByString(equipLink.substr(firstIndex));
	if (entry == 0)
		return;
	for (uint8 slot = EquipmentSlots::EQUIPMENT_SLOT_HEAD; slot < EquipmentSlots::EQUIPMENT_SLOT_END; slot++)
	{
		uint16 pos = (255 << 8) | slot;
		if (me->IsEquipmentPos(pos) || me->IsBagPos(pos))
		{
			InventoryResult msg = me->CanUnequipItem(pos, false);
			if (msg != EQUIP_ERR_OK)
				continue;
		}
		Item* pItem = me->GetItemByPos(255, slot);
		if (!pItem)
			continue;
		const ItemTemplate* pTemplate = pItem->GetTemplate();
		if (!pTemplate || pTemplate->GetId() != entry)
			continue;

		uint32 noSpaceForCount = 0;
		ItemPosCountVec dest;
		InventoryResult msg = me->CanStoreItem(NULL_BAG, NULL_SLOT, dest, pItem);
		if (msg != EQUIP_ERR_OK)
		{
			std::string outString;
			consoleToUtf8(std::string("ČˇĎÂĘ§°Ü"), outString);
			me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
			return;
		}
		me->RemoveItem(255, slot, true);
		me->StoreItem(dest, pItem, true);
        sRegionalBotChatMgr.FullControl(me,srcPlayer,"unequip_command",entry);
		std::string outString;
		consoleToUtf8(std::string("łÉą¦ČˇĎÂ"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
		return;
	}
	std::string outString;
	consoleToUtf8(std::string("ČˇĎÂĘ§°Ü"), outString);
	me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
}

void BotGroupAI::ProcessDestroyItem(Player* srcPlayer, std::string& equipLink)
{
	std::lock_guard<std::mutex> lock(m_ItemLock);
	if (me->isInCombat())
		return;
	if (!srcPlayer || equipLink.empty())
		return;
	int firstIndex = equipLink.find(':');
	if (firstIndex < 0)
		return;
	uint32 entry = BotUtility::GetFirstNumberByString(equipLink.substr(firstIndex));
	if (entry == 0)
		return;
	BotUtility::FindItemFromAllBag(me, entry, true);
	std::string outString;
	consoleToUtf8(std::string("łÉą¦¶Şµô"), outString);
	me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
}

void BotGroupAI::ProcessTradeItem(Player* srcPlayer, std::string& equipLink)
{
	std::lock_guard<std::mutex> lock(m_ItemLock);
	if (me->isInCombat())
		return;
	if (!srcPlayer || equipLink.empty())
		return;
	int firstIndex = equipLink.find(':');
	if (firstIndex < 0)
		return;
	uint32 entry = BotUtility::GetFirstNumberByString(equipLink.substr(firstIndex));
	if (entry == 0)
	{
		std::string outString;
		consoleToUtf8(std::string("ÎŇĂ»ÓĐŐâ¸öµŔľß"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
		return;
	}
	Item* pItem = BotUtility::FindItemFromAllBag(me, entry, false);
	if (!pItem)
	{
		std::string outString;
		consoleToUtf8(std::string("ÎŇĂ»ÓĐŐâ¸öµŔľß"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
		return;
	}
	TradeData* pTrade = me->GetTradeData();
	if (!pTrade)
	{
		std::string outString;
		consoleToUtf8(std::string("Ă»ÓĐżŞĘĽ˝»Ň×"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
		return;
	}
	Player* pTradePlayer = pTrade->GetTrader();
	if (!pTradePlayer)
	{
		std::string outString;
		consoleToUtf8(std::string("Ă»ÓĐżŞĘĽ˝»Ň×"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
		return;
	}
	TradeData* pTraderData = pTradePlayer->GetTradeData();
	if (!pTraderData)
	{
		me->TradeCancel(false);
		return;
	}
	if (pTrade->HasItem(pItem->GetGUID()))
	{
		std::string outString;
		consoleToUtf8(std::string("Őâ¸öµŔľßŇŃľ­·ĹÉĎČĄÁË"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
		return;
	}
	//if (!pItem->CanBeTraded(false, true))
	//{
	//	std::string outString;
	//	consoleToUtf8(std::string("ÎŢ·¨·ĹÉĎŐâ¸öµŔľß"), outString);
	//	me->Whisper(outString, Language::LANG_COMMON, srcPlayer);
	//	return;
	//}
	if (pTrade->SetItemAtNullSlot(pItem, true))
	{
		std::string outString;
		consoleToUtf8(std::string("·ĹÉĎČĄÁË"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
	}
	else
	{
		std::string outString;
		consoleToUtf8(std::string("Ă»·¨ÔŮ·Ĺ¶«Î÷ÁË"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
	}
}

void BotGroupAI::ProcessUseItem(Player* srcPlayer, std::string& equipLink)
{
	std::lock_guard<std::mutex> lock(m_ItemLock);
	if (!srcPlayer || equipLink.empty())
		return;
	int firstIndex = equipLink.find(':');
	if (firstIndex < 0)
		return;
	uint32 entry = BotUtility::GetFirstNumberByString(equipLink.substr(firstIndex));
    if (!entry || !sObjectMgr->GetItemTemplate(entry))
    {
        if (entry)
            sRegionalBotChatMgr.FullControl(me,srcPlayer,"use_command_invalid_item",entry);
        return;
    }
	Item* pItem = BotUtility::FindItemFromAllBag(me, entry);
    if (!pItem)
    {
        sRegionalBotChatMgr.FullControl(me,srcPlayer,"use_command_item_not_owned",entry);
        return;
    }
    for (ItemEffectEntry const* effect : pItem->GetTemplate()->Effects)
        if (effect && effect->SpellID && me->HasSpellCooldown(effect->SpellID))
        {
            sRegionalBotChatMgr.FullControl(me,srcPlayer,"use_command_item_cooldown",entry);
            return;
        }
    uint32 count=me->GetItemCount(entry);

	m_Movement->ClearMovement();
	SpellCastTargets targets;
	targets.SetTargetMask(0);
	if (!me->CastItemUseSpell(pItem, targets, 0, ObjectGuid::Empty))
	{
        sRegionalBotChatMgr.FullControl(me,srcPlayer,"use_command_item_error",entry);
		std::string outString;
		consoleToUtf8(std::string("Ę§°ÜĘąÓĂ"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
		return;
	}
    uint32 available=me->GetItemCount(entry);
    sRegionalBotChatMgr.FullControl(me,srcPlayer,"use_command",entry,0,{},"command_target_self",
        available<count ? int32(available) : -1);
	std::string outString;
	consoleToUtf8(std::string("łÉą¦ĘąÓĂ"), outString);
	me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
}

void BotGroupAI::ProcessTalent(Player* srcPlayer, std::string& talentText)
{
	std::lock_guard<std::mutex> lock(m_ItemLock);
	if (!srcPlayer)
		return;
	if (!me->IsInWorld() || me->isInCombat() || me->HasUnitState(UNIT_STATE_CASTING) || !me->IsSettingFinish())
		return;
	uint32 talentType = talentText.empty() ? 3 : uint32(atoi(talentText.c_str()));
	if (talentType > 3)
		talentType = 3;
	talentType = me->SwitchTalent(talentType);
	OnLevelUp(talentType);
	m_HasReset = false;

	std::string outString;
	consoleToUtf8(std::string("ÇĐ»»Ěě¸łÍęłÉ"), outString);
	me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());

}

void BotGroupAI::ProcessSummonRiteSpell(Player* srcPlayer)
{
	if (!srcPlayer || !srcPlayer->IsInWorld() || HasTeleport() || me->GetMap() != srcPlayer->GetMap())
		return;
	if (srcPlayer->GetDistance(me->GetPosition()) > BOTAI_RANGESPELL_DISTANCE)
		return;

	if (!m_MovetoUseGO.CanCastSummonRite())
	{
		std::string outString;
		consoleToUtf8(std::string("ÄżÇ°ÎŢ·¨żŞĘĽŐŮ»˝ŇÇĘ˝Łˇ"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
		return;
	}
	uint32 castSpellID = TryCastSummonRiteSpell();
	if (castSpellID)
	{
		m_MovetoUseGO.StartSummonRite(castSpellID);
        sRegionalBotChatMgr.FullControl(me,srcPlayer,"cast_spell_command_summon",0,castSpellID,srcPlayer->GetName());
		std::string outString;
		consoleToUtf8(std::string("ŐŮ»˝ŇÇĘ˝Ćô¶ŻŁˇ"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
	}
	else
	{
		std::string outString;
		consoleToUtf8(std::string("ÄżÇ°ÎŢ·¨żŞĘĽŐŮ»˝ŇÇĘ˝Łˇ"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
	}
}

void BotGroupAI::FullExecuteTextOrder(Player* requester, std::string const& order)
{
    Group* group=me->GetGroup();
    if (!requester || !group || !group->IsLeader(requester->GetGUID()) ||
        requester->GetMap()!=me->GetMap() || !me->InSamePhase(requester) ||
        !me->IsInWorld() || !me->isAlive() || me->IsBeingTeleported())
        return;

    if (order=="follow" || order=="stop" || order=="flee" || order=="attack" ||
        order=="summon" || order=="rite" || order.compare(0,2,"u ")==0 ||
        order.compare(0,2,"e ")==0 || order.compare(0,3,"ue ")==0)
    {
        if (order=="summon")
        {
            if (me->isInCombat() || requester->isInCombat())
                return sRegionalBotChatMgr.FullControl(me,requester,"cast_spell_command_summon_error_combat");
            if (requester->IsInWater() && !me->IsInWater())
                return sRegionalBotChatMgr.FullControl(me,requester,"error_water");
            if (!m_MasterPlayer || m_MasterPlayer!=requester)
                return sRegionalBotChatMgr.FullControl(me,requester,"cast_spell_command_summon_error_target");
            if (requester->IsFlying() || !m_Teleporting.CanMovement())
                return sRegionalBotChatMgr.FullControl(me,requester,"error_cant_go");
        }
        if (order=="rite")
        {
            if (me->isInCombat())
                return sRegionalBotChatMgr.FullControl(me,requester,"cast_spell_command_summon_error_combat");
            if (me->GetDistance(requester)>BOTAI_RANGESPELL_DISTANCE)
                return sRegionalBotChatMgr.FullControl(me,requester,"error_far");
            unsigned count=0;
            for (GroupReference* ref=group->GetFirstMember();ref;ref=ref->next())
                if (Player* member=ref->getSource())
                    if (member->IsInWorld() && member->GetMap()==me->GetMap() && member->isAlive() &&
                        me->InSamePhase(member) && me->GetDistance(member)<30.f)
                        ++count;
            if (count<3)
                return sRegionalBotChatMgr.FullControl(me,requester,"cast_spell_command_summon_error_members");
        }
        ProcessBotCommand(requester,order);
        return;
    }
    auto linkedId=[](std::string const& text, char const* tag, size_t& offset) -> uint32
    {
        std::string marker=std::string("|H")+tag+":";
        auto hit=text.find(marker,offset);
        if (hit==std::string::npos)
            return 0;
        offset=hit+marker.size();
        uint32 id=0;
        while (offset<text.size() && text[offset]>='0' && text[offset]<='9')
        {
            if (id>10000000)
                return 0;
            id=id*10+uint32(text[offset++]-'0');
        }
        return offset<text.size() && (text[offset]==':' || text[offset]=='|') ? id : 0;
    };

    if (!me->isInCombat() && order.compare(0,7,"socket ")==0)
    {
        size_t offset=7;
        uint32 target=linkedId(order,"item",offset);
        uint32 gem=linkedId(order,"item",offset);
        Item* item=target ? BotUtility::FindItemFromAllBag(me,target) : nullptr;
        Item* stone=gem ? BotUtility::FindItemFromAllBag(me,gem) : nullptr;
        if (!item || !stone || item==stone)
            return sRegionalBotChatMgr.FullControl(me,requester,"use_command_socket_error");
        int socket=-1;
        for (uint8 i=0;i<MAX_GEM_SOCKETS;++i)
            if (item->GetSocketColor(i) && !item->GetGem(i))
            {
                socket=i;
                break;
            }
        if (socket<0)
            return sRegionalBotChatMgr.FullControl(me,requester,"use_command_socket_error");
        WorldPacket raw(CMSG_SOCKET_GEMS);
        WorldPackets::Item::SocketGems packet(std::move(raw));
        packet.ItemGuid=item->GetGUID();
        packet.GemItem.fill(ObjectGuid::Empty);
        packet.GemItem[socket]=stone->GetGUID();
        me->GetSession()->HandleSocketGems(packet);
        return;
    }
    if (!me->isInCombat() && order.compare(0,5,"bank ")==0)
    {
        size_t offset=5;
        uint32 entry=linkedId(order,"item",offset);
        Guild* guild=me->GetGuild();
        if (!guild || guild->GetId()!=requester->GetGuildId())
            return sRegionalBotChatMgr.FullControl(me,requester,"error_guild");
        GameObject* bank=me->FindNearestGameObjectOfType(GAMEOBJECT_TYPE_GUILD_BANK,5.f);
        if (!bank || !me->GetGameObjectIfCanInteractWith(bank->GetGUID(),GAMEOBJECT_TYPE_GUILD_BANK))
            return sRegionalBotChatMgr.FullControl(me,requester,"error_gbank_found");
        uint8 bag=INVENTORY_SLOT_BAG_0, slot=0;
        Item* item=entry ? BotUtility::FindItemFromAllBag(me,entry,bag,slot) : nullptr;
        if (!item)
            return sRegionalBotChatMgr.FullControl(me,requester,"use_command_item_not_owned",entry);
        guild->SwapItemsWithInventory(me,false,0,NULL_SLOT,bag,slot,0);
        return;
    }
    if (!me->isInCombat() && (order.compare(0,7,"accept ")==0 || order.compare(0,7,"reward ")==0 ||
           order.compare(0,8,"abandon ")==0))
    {
        size_t offset=0;
        uint32 id=linkedId(order,"quest",offset);
        Quest const* quest=sQuestDataStore->GetQuestTemplate(id);
        if (!quest)
            return;
        if (order.compare(0,8,"abandon ")==0)
        {
            uint16 slot=me->FindQuestSlot(id);
            if (slot>=MAX_QUEST_LOG_SIZE)
                return;
            WorldPacket raw(CMSG_QUEST_LOG_REMOVE_QUEST);
            WorldPackets::Quest::QuestLogRemoveQuest packet(std::move(raw));
            packet.Entry=uint8(slot);
            me->GetSession()->HandleQuestLogRemoveQuest(packet);
            return;
        }

        Unit* giver=requester->GetSelectedUnit();
        bool accept=order.compare(0,7,"accept ")==0;
        if (!giver || !giver->ToCreature() || !me->CanInteractWithQuestGiver(giver))
        {
            sRegionalBotChatMgr.FullControl(me,requester,"quest_error_talk");
            return;
        }
        if (accept && giver->hasQuest(id))
        {
            WorldPacket raw(CMSG_QUEST_GIVER_ACCEPT_QUEST);
            WorldPackets::Quest::QuestGiverAcceptQuest packet(std::move(raw));
            packet.QuestGiverGUID=giver->GetGUID();
            packet.QuestID=id;
            me->GetSession()->HandleQuestGiverAcceptQuest(packet);
        }
        else if (!accept && giver->hasInvolvedQuest(id) && me->GetQuestStatus(id)==QUEST_STATUS_COMPLETE)
        {
            uint32 choice=linkedId(order,"item",offset);
            if (quest->m_rewChoiceItemsCount)
            {
                if (!choice && quest->m_rewChoiceItemsCount==1)
                    choice=quest->RewardChoiceItemId[0];
                if (!choice || std::find(std::begin(quest->RewardChoiceItemId),std::end(quest->RewardChoiceItemId),choice)==std::end(quest->RewardChoiceItemId))
                {
                    FelsoriaRegional::FullTextFacts facts;
                    facts.quest=id;
                    facts.rewards=true;
                    sRegionalBotChatMgr.FullEvent(me,"quest_status_complete_pick_reward",5,facts,requester);
                    return;
                }
            }
            else
                choice=0;
            WorldPacket raw(CMSG_QUEST_GIVER_CHOOSE_REWARD);
            WorldPackets::Quest::QuestGiverChooseReward packet(std::move(raw));
            packet.QuestGiverGUID=giver->GetGUID();
            packet.QuestID=id;
            packet.ItemChoiceID=choice;
            me->GetSession()->HandleQuestGiverChooseReward(packet);
        }
        return;
    }
    if (order.compare(0,5,"cast ")!=0)
        return;
    std::string idText=order.substr(5);
    if (idText.empty() || idText.size()>7 ||
        !std::all_of(idText.begin(),idText.end(),[](char digit){return digit>='0' && digit<='9';}))
        return;
    uint32 id=uint32(std::stoul(idText));
    SpellInfo const* spell=sSpellMgr->GetSpellInfo(id);
    if (!spell || !me->HasSpell(id))
        return sRegionalBotChatMgr.FullControl(me,requester,"cast_spell_command_error_unknown_spell",0,id);
    if (me->HasUnitState(UNIT_STATE_CASTING) || me->HasSpellCooldown(id))
        return sRegionalBotChatMgr.FullControl(me,requester,"cast_spell_command_error",0,id);
    Unit* target=requester->GetSelectedUnit();
    if (!target || target->GetMap()!=me->GetMap() || !target->InSamePhase(me))
        target=me;
    SpellCastResult result=TryCastSpell(id,target);
    if (result!=SPELL_CAST_OK)
        return sRegionalBotChatMgr.FullControl(me,requester,"cast_spell_command_error_failed",0,id);

    char const* key=spell->HasEffect(SPELL_EFFECT_CREATE_ITEM) || spell->HasEffect(SPELL_EFFECT_CREATE_ITEM_2) ?
        "cast_spell_command_craft" : "cast_spell_command_spell";
    sRegionalBotChatMgr.FullControl(me,requester,key,0,id,target->GetName(),
        target==me ? "command_target_self" : "command_target_unit");
}

char const* BotGroupAI::FullMovementText()
{
    if (m_ForceFlee)
        return m_MasterPlayer && m_MasterPlayer->GetMap()==me->GetMap() &&
            me->GetDistance(m_MasterPlayer)>60.f ? "fleeing_far" : "fleeing";
    if (m_StopFollow)
        return me->isInCombat() ? "guarding" : "staying";
    if (me->getVictim())
        return "attacking";
    return m_MasterPlayer ? "following" : "free_moving";
}

void BotGroupAI::ProcessBotCommand(Player* srcPlayer, std::string cmd)
{
	if (!m_MasterPlayer || !srcPlayer)
		return;
	std::string param;
	if (!CanReciveCommand(cmd, param))
		return;
    if (cmd=="summon")
    {
        bool canSummon=m_MasterPlayer && m_Teleporting.CanMovement() && !m_MasterPlayer->IsFlying();
        ProcessSummonCommand();
        if (canSummon)
            sRegionalBotChatMgr.FullControl(me,srcPlayer,"cast_spell_command_summon",0,0,m_MasterPlayer->GetName());
    }
    else if (cmd=="attack")
    {
        ProcessAttackCommand();
        if (me->GetSelectedUnit() && me->GetSelectedUnit()==m_MasterPlayer->GetSelectedUnit() &&
            me->IsValidAttackTarget(me->GetSelectedUnit()))
            sRegionalBotChatMgr.FullControl(me,srcPlayer,"attacking");
    }
	else if (cmd == "follow")
	{
		me->SetSelection(ObjectGuid::Empty);
		m_ForceFlee = false;
		m_StopFollow = false;
        sRegionalBotChatMgr.FullControl(me,srcPlayer,urand(0,1) ? "following" : "hello_follow");
	}
    else if (cmd=="flee")
    {
        ProcessFleeCommand();
        sRegionalBotChatMgr.FullControl(me,srcPlayer,FullMovementText());
    }
    else if (cmd=="stop")
    {
        ProcessStopCommand();
        sRegionalBotChatMgr.FullControl(me,srcPlayer,urand(0,1) ? "staying" : FullMovementText());
    }
	else if (cmd == "setting")
		ProcessSetting();
	else if (cmd == "c")
		ProcessListEquip(srcPlayer);
	else if (cmd == "e")
		ProcessUpequip(srcPlayer, param);
	else if (cmd == "ue")
		ProcessUnequip(srcPlayer, param);
	else if (cmd == "destroy")
		ProcessDestroyItem(srcPlayer, param);
	else if (cmd == "s")
		ProcessTradeItem(srcPlayer, param);
	else if (cmd == "u")
		ProcessUseItem(srcPlayer, param);
	else if (cmd == "talent")
		ProcessTalent(srcPlayer, param);
	else if (cmd == "rite")
		ProcessSummonRiteSpell(srcPlayer);
   	else if (cmd !="")
{
        
        uint32 spellId=(int32)atoi(cmd.c_str());
//TC_LOG_ERROR("botai", "spellid %u ", spellId);
//if (me->HasSpell(spellID))
//me->CastSpell(me,spellId , true);
if (spellId>0 && spellId<=80000 && !me->HasUnitState(UNIT_STATE_CASTING))
{
	Unit* ownerTarget = NULL;
        if (me->getVictim())
{
    //40827
if (me->HasSpell(spellId))
{
    
    if (TryCastSpell(spellId, me->getVictim()) == SpellCastResult::SPELL_CAST_OK)
1==1;
else    
{
    if (ownerTarget = srcPlayer->GetSelectedUnit())
    TryCastSpell(spellId, ownerTarget);
}
//else
	  //me->CastSpell(me->getVictim(),spellId , true);
  //  TryCastSpell(spellId, me->getVictim());
 }   

}
else
{
    //40733
    if (me->HasSpell(spellId))
{

if (ownerTarget = srcPlayer->GetSelectedUnit())
{
    if (TryCastSpell(spellId, ownerTarget) == SpellCastResult::SPELL_CAST_OK)
1==1;
else
    //if (irand(0,1)==0)
    TryCastSpell(spellId, me);
}
else TryCastSpell(spellId, me);
    //else
    //TryCastSpell(spellId, srcPlayer);
    //me->CastSpell(me,spellId , true);
    //me->CastSpell(srcPlayer,spellId , true);
}
}
}	
}	
		
}

void BotGroupAI::DamageDealt(Unit* victim, uint32& damage, DamageEffectType damageType)
{
	if (!victim || !me->IsInWorld() || damage == 0)
		return;
	if (!me->GetMap()->IsDungeon() || me->InBattleground())
		return;
	Group* pGroup = me->GetGroup();
	if (!pGroup)// || pGroup->GetMembersCount() <= 5)
		return;
	if (IsTankBotAI())
	{
		//damage = damage / 2;
		return;
	}

	float result = float(damage);
	switch (damageType)
	{
	case DamageEffectType::DIRECT_DAMAGE:
	case DamageEffectType::SPELL_DIRECT_DAMAGE:
		if (!victim->ToPlayer())
		{
			//float param = 1.2f;
			float rate = BotUtility::DungeonBotDamageModify;// *param;
			result *= rate;
		}
		break;
	case DamageEffectType::DOT:
		if (!victim->ToPlayer())
		{
			//float param = 1.1f;
			float rate = BotUtility::DungeonBotDamageModify;// *param;
			result *= rate;
		}
		break;
	//case DamageEffectType::HEAL:
	//	{
	//		float rate = BotUtility::DungeonBotDamageModify;
	//		result *= rate;
	//	}
	//	break;
	default:
		return;
	}
	damage = uint32(result);
}

void BotGroupAI::DamageEndure(Unit* attacker, uint32& damage, DamageEffectType damageType)
{
	if (!attacker || !me->IsInWorld() || damage == 0)
		return;
	if (!me->GetMap()->IsDungeon() || me->InBattleground())
		return;
	Group* pGroup = me->GetGroup();
	if (!pGroup)// || pGroup->GetMembersCount() <= 5)
		return;

	float result = float(damage);
	switch (damageType)
	{
	case DamageEffectType::DIRECT_DAMAGE:
	case DamageEffectType::SPELL_DIRECT_DAMAGE:
	case DamageEffectType::DOT:
		if (!attacker->ToPlayer())
		{
			float param = (IsTankBotAI()) ? 1.1f : 1.0f;
			float rate = 1.0f / (BotUtility::DungeonBotEndureModify * param);
			result *= rate;
		}
		break;
	case DamageEffectType::HEAL:
		return;
	//{
	//	float rate = BotUtility::DungeonBotAnnulModify * 1.8f;
	//	result *= rate;
	//}
	//break;
	default:
		return;
	}
	damage = uint32(result);
}

void BotGroupAI::UpdateAI(uint32 diff)
{
    // FELSORIA_DH_DUMMY_LIMIT_V1: release surplus trainers before cast/idle returns.
    if (FelsoriaDHDummyLimit::Enforce(me, m_Movement))
    {
        m_CruxMovement.ClearMovement();
    }
    // FELSORIA_ACTIVITY_V1: orphan casting flags must not strand AI before rotation.
    if(me->IsInWorld() && me->isAlive() && me->GetMap() && me->GetMap()->IsDungeon() && me->GetGroup()){
        bool liveCast=false;
        for(uint8 slot:{uint8(CURRENT_GENERIC_SPELL),uint8(CURRENT_CHANNELED_SPELL),uint8(CURRENT_AUTOREPEAT_SPELL)})
            if(Spell* spell=me->GetCurrentSpell(slot))
                if(spell->getState()!=SPELL_STATE_FINISHED)liveCast=true;
        if(me->HasUnitState(UNIT_STATE_CASTING) && !liveCast){
            me->ClearUnitState(UNIT_STATE_CASTING);
            TC_LOG_INFO(LOG_FILTER_LFG,"RDF_ACTIVITY_RECOVER account=%u reason=orphan_cast_flag",me->GetSession()->GetAccountId());
        }
        uint32 activityNow=getMSTime();
        if(!m_RdfStillSince || me->GetDistance(m_RdfStillPosition)>0.75f){
            m_RdfStillPosition=me->GetPosition();m_RdfStillSince=activityNow;
        }
        bool distantLeader=m_MasterPlayer && m_MasterPlayer->IsInWorld() &&
            m_MasterPlayer->GetMap()==me->GetMap() && me->GetDistance(m_MasterPlayer)>12.0f;
        if(uint32(activityNow-m_RdfStillSince)>=8000 &&
           uint32(activityNow-m_RdfStillLog)>=10000 &&
           (me->isInCombat() || distantLeader || m_MechanicOwned)){
            m_RdfStillLog=activityNow;
            Unit* selected=me->GetSelectedUnit();
            TC_LOG_INFO(LOG_FILTER_LFG,"RDF_ACTIVITY_STATIONARY account=%u spec=%u map=%u instance=%u target=%u combat=%u casting=%u moving=%u mechanic=%u mechanicMoving=%u stopFollow=%u flee=%u teleport=%u pos=(%.2f,%.2f,%.2f)",
                me->GetSession()->GetAccountId(),me->GetSpecializationId(),me->GetMapId(),me->GetInstanceId(),
                selected?selected->GetEntry():0,uint32(me->isInCombat()),uint32(liveCast),uint32(me->isMoving()),
                uint32(m_MechanicOwned),uint32(m_MechanicMoving),uint32(m_StopFollow),uint32(m_ForceFlee),
                uint32(!m_Teleporting.CanMovement()),me->GetPositionX(),me->GetPositionY(),me->GetPositionZ());
        }
    }else m_RdfStillSince=0;
    // FELSORIA_VAULT_BRIDGES_V11: the watchdog survives legacy bridge/dodge returns.
    // Pending rescue observation never consumes the update needed for teleport ACKs.
    if (me->IsInWorld() && me->isAlive() &&
        me->GetMapId()==1493 && me->GetGroup() && UpdateMasterPlayer() &&
        ProcessVaultBridgeWatchdog())return;
    // FELSORIA_BRIDGE_DODGE_V6: finish bot teleport ACKs before mechanics can return.
    if (!m_Teleporting.CanMovement()) {
        m_UpdateTick -= diff;
        if (m_UpdateTick > 0) return;
        m_UpdateTick = BOTAI_UPDATE_TICK;
        UpdateTeleport(BOTAI_UPDATE_TICK);
        return;
    }
    // FELSORIA_DODGE_LEASH_V10: distance recovery cannot be starved by dodge returns.
    if (me->IsInWorld() && me->isAlive() && me->IsSettingFinish() &&
        me->GetMap() && me->GetMap()->IsDungeon() && me->GetGroup() &&
        UpdateMasterPlayer() && ProcessDungeonLeash())return;
    // Both surveyed Vault crossings own movement before dodge and formation.
    if (me->IsInWorld() && me->isAlive() && me->IsSettingFinish() &&
        me->GetMapId()==1493 && me->GetGroup() && UpdateMasterPlayer() &&
        ProcessRdfVaultBridges()) {
        m_MechanicOwned=false;
        m_MechanicMoving=false;
        m_RdfAvoidMoving=false;
        return;
    }
    // FELSORIA_MECHANICS_V1: survival owns movement before all lower-priority AI.
    bool const mechanic=ProcessPredictiveMechanics();
    if(mechanic && m_MechanicMoving)return;
    if(!mechanic){
        if(ProcessBossPullSafety())return;
        if(ProcessMountFollow())return;
        // V12: formation is issued once by the ordinary follow/formation owner.
    }

	m_UpdateTick -= diff;
	if (m_UpdateTick > 0)
		return;
	m_UpdateTick = BOTAI_UPDATE_TICK;
	if (!me->isAlive())
		if (PlayerBotSession* session = dynamic_cast<PlayerBotSession*>(me->GetSession()))
			if (session->HandlesDynamicWorldRevive(me))
				return;
    m_CombatCastIssued = false;
    if(mechanic){
        // In a real shelter keep healing/casting, with repositioning disabled.
        Unit* enemy=GetCombatTarget();
        ProcessRdfLegionRotation(enemy,true);
        return;
    }

    // This legacy NPC conjure can leave bots waiting indefinitely. Do not touch
    // other combat casts/channels; permit a single attempt per gem-use cycle.
    if (me->IsInWorld() && me->GetGroup() && me->GetGroup()->isLFGGroup())
    {
        auto attempt = m_UtilityAttempts.find(std::make_pair(uint32(36883), me->GetGUID()));
        if (attempt != m_UtilityAttempts.end() &&
            (uint32(getMSTime() - attempt->second) >= 15000 || !me->GetGroup()->AllGroupNotCombat()))
        {
            Spell* cast = me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (cast && cast->GetSpellInfo()->Id == 36883)
            {
                me->InterruptSpell(CURRENT_GENERIC_SPELL, true, true, 36883);
                TC_LOG_INFO(LOG_FILTER_LFG, "RDF_CONJURE_ABORT account=%u spell=36883",
                    me->GetSession()->GetAccountId());
            }
        }
    }

    if (!me->isInCombat())
        m_UsePotion.TryUsePotion(); // Reset the per-combat latch even during preparation.
    // Opt-in catalog from this server's loaded spell data and learned spellbook.
    if (me->IsInWorld() && me->GetGroup() && me->GetGroup()->isLFGGroup() &&
        m_SpellAuditSpec != me->GetSpecializationId() &&
        sConfigMgr->GetIntDefault("PlayerBot.CombatSpellAudit", 0) != 0)
    {
        m_SpellAuditSpec = me->GetSpecializationId();
        PlayerSpellMap const& spells = me->GetSpellMap();
        for (auto itr = spells.begin(); itr != spells.end(); ++itr)
        {
            if (itr->second.state == PLAYERSPELL_REMOVED || itr->second.disabled)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(itr->first);
            if (!info || info->IsPassive())
                continue;
            TC_LOG_INFO(LOG_FILTER_LFG,
                "RDF_SPELLBOOK account=%u class=%u spec=%u spell=%u positive=%u name=%s",
                me->GetSession()->GetAccountId(), uint32(me->getClass()), m_SpellAuditSpec,
                itr->first, uint32(info->IsPositive()), info->SpellName ? info->SpellName : "?");
        }
    }

    bool rdfLog = m_RdfTraceTick <= BOTAI_UPDATE_TICK;
    m_RdfTraceTick = rdfLog ? 10000 : m_RdfTraceTick - BOTAI_UPDATE_TICK;
    auto rdfTrace = [&](char const* gate)
    {
        if (!rdfLog || !me->GetGroup()) return;
        bool rdfTraceGroup = me->GetGroup()->isLFGGroup();
        if (!rdfTraceGroup && sConfigMgr->GetIntDefault("PlayerBot.CombatSpellAudit", 0) == 0) return;
        TC_LOG_INFO(LOG_FILTER_LFG,
            "%s account=%u name=%s gate=%s alive=%u setting=%u casting=%u combat=%u customTeleport=%u stopFollow=%u forceFlee=%u master=%u",
            rdfTraceGroup ? "RDF_AI_GATE" : "PARTY_AI_GATE",
            me->GetSession()->GetAccountId(), me->GetName(), gate,
            uint32(me->isAlive()), uint32(me->IsSettingFinish()),
            uint32(me->HasUnitState(UNIT_STATE_CASTING)), uint32(me->isInCombat()),
            uint32(!m_Teleporting.CanMovement()), uint32(m_StopFollow),
            uint32(m_ForceFlee), uint32(m_MasterPlayer != nullptr));
    };

    // RDF_REVIVE_PRIORITY_FIX: death handling must precede following.
    if (!me->isAlive() && me->GetGroup() && me->GetGroup()->isLFGGroup())
    {
        if (!me->IsInWorld() || me->IsBeingTeleported() || me->IsChangeMap())
            { rdfTrace("exit_809"); return; }

        // Do not automatically revive while a party member is fighting.
        for (GroupReference* ref = me->GetGroup()->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->getSource())
                if (member->isAlive() && member->IsInWorld() && member->GetMap() == me->GetMap() && member->isInCombat())
                    {
                        m_Revive = BotAIRevive(me); // Require a continuous quiet interval.
                        rdfTrace("revive_wait_party_combat"); return;
                    }

        m_Revive.UpdateRevive(BOTAI_UPDATE_TICK);
        if (me->isAlive())
        {
            m_RdfAvoidMoving=false; m_RdfFormationMoving=false;
            m_Movement->ClearMovement();
            TC_LOG_INFO(LOG_FILTER_LFG,"RDF_REVIVE_DONE account=%u map=%u instance=%u",me->GetSession()->GetAccountId(),me->GetMapId(),me->GetInstanceId());
        }
        { rdfTrace("revive_timer"); return; }
    }

	if (!me->IsSettingFinish())
		{ rdfTrace("exit_822"); return; }
	UpdateTeleport(BOTAI_UPDATE_TICK);
	if (!m_Teleporting.CanMovement())
		{ rdfTrace("exit_825"); return; }
	me->UpdateObjectVisibility(false);

	if (!UpdateMasterPlayer())
	{
		m_Flying.CancelFly();
        // A human is temporarily unavailable during any world/map transfer.
        // Preserve actual membership, including offline human ownership.
        if (me->GetGroup() && (me->GetGroup()->isLFGGroup() ||
            (sPlayerBotMgr->DynamicBotsEnabled() && sPlayerBotMgr->IsDynamicBotPartyProtected(me))))
            { rdfTrace("exit_833"); return; }
		me->RemoveFromGroup(RemoveMethod::GROUP_REMOVEMETHOD_LEAVE);
		{ rdfTrace("exit_835"); return; }
	}
    // Vault crossing is handled before predictive mechanics above.
    if (ProcessRdfTravelRegroup())
        return;
	if (TrySettingToMaster())
		{ rdfTrace("exit_838"); return; }
	if (m_MasterPlayer && m_MasterPlayer->GetMap() == me->GetMap() &&
		m_VehicleMovement3D->UpdateVehicleMovement3D())
		{ rdfTrace("exit_841"); return; }
	if (TryTeleportToMaster())
		{ rdfTrace("exit_843"); return; }
	if (m_MasterPlayer && m_MasterPlayer->duel && !(m_MasterPlayer->duel->state == DuelState::DUEL_COMPLETED))
	{
		if (!me->duel)
		{
			me->SetInFront(m_MasterPlayer);
			me->SetFacingToObject(m_MasterPlayer);
			if (m_DuelEmoteTick == 0)
				m_DuelEmoteTick = getMSTime();
			else if (m_DuelEmoteTick + urand(1, 2) * 500 < getMSTime())
			{
				m_DuelEmoteTick = getMSTime();
				//me->HandleEmoteCommand(Emote::EMOTE_ONESHOT_APPLAUD);
				me->HandleEmoteCommand(Emote::EMOTE_ONESHOT_CHEER);
			}
			{ rdfTrace("exit_858"); return; }
		}
	}
	if (!m_HasReset)
		ResetBotAI();
	BotUtility::UpdatePlayerBotRoll(me);



	if (me->isAlive())
	{
		
if (bothp==0)
{
int32 isok = sConfigMgr->GetIntDefault("pbot_hp", 1);
if (isok >1)
{
        
           int pct = ((int) (me->GetMaxHealth()) + me->getLevel()*100);

        pct=pct*(isok-1);
        if ((int) me->GetMaxHealth() < pct && (int) me->GetMaxHealth() <100000)
  {
        bothp=1;
        me->SetCreateHealth(pct);
        me->SetMaxHealth(pct);
        me->SetHealth(pct);
  }



  
}	
} 

if (!UseRdfLegionRotation() && !UsePartyLegionRotation(me->GetSelectedUnit(),true) &&
    !PvpLegionRotation::Applicable(me, me->GetSelectedUnit()))
{
if (me->GetPower(POWER_RAGE) < me->GetMaxPower(POWER_RAGE)/5)
me->SetPower(POWER_RAGE, (me->GetMaxPower(POWER_RAGE)));

if (me->GetPower(POWER_ENERGY) < me->GetMaxPower(POWER_ENERGY)/5)
me->SetPower(POWER_ENERGY, (me->GetMaxPower(POWER_ENERGY)));

if (me->GetPower(POWER_MANA) < me->GetMaxPower(POWER_MANA)/5)
me->SetPower(POWER_MANA, (me->GetMaxPower(POWER_MANA)));
}
		
		m_CheckStoped.UpdatePosition(diff);
		if (!PvpLegionRotation::Applicable(me, me->GetSelectedUnit())) ClearMechanicAura();
		if (!IsNotMovement())
			ProcessHorror(diff);
        if(me->HasUnitState(UNIT_STATE_CASTING) && !m_ForceFlee && !m_CruxMovement.HasCruxMovement() &&
            (UseRdfLegionRotation() || UsePartyLegionRotation(me->GetSelectedUnit(),true) ||
             PvpLegionRotation::Applicable(me,me->GetSelectedUnit())))
            BotUtility::TryCombatInterrupt(me,me->GetSelectedUnit(),m_Movement);
		if (NeedWaitSpecialSpell(BOTAI_UPDATE_TICK))
			{ rdfTrace("exit_907"); return; }
        // V12: predictive mechanics were already processed once before combat.
		if (ExecuteSeduceCommand())
			{ rdfTrace("exit_909"); return; }
        if (me->getClass() == CLASS_HUNTER && me->GetSummonedBattlePet())
            me->UnsummonCurrentBattlePetIfAny(false);
        if (!UseRdfLegionRotation() && m_PvpRotation.SelfHeal(me, m_Movement)) return;
		if (!UseRdfLegionRotation() && (!m_MasterPlayer || !m_MasterPlayer->isInCombat()) && !me->HasUnitState(UNIT_STATE_CASTING) && !IsTankBotAI() && !IsHealerBotAI())
		{
			if (m_FastAid.TryDoingFastAidForMe())
				{ rdfTrace("exit_913"); return; }
		}

        // RDF_COMBAT_PRIORITY_V1: assist actual party combat before preparation.
        bool rdfPartyCombat = false;
        Unit* rdfTarget = nullptr;
        Group* rdfGroup = me->GetGroup();
        if (rdfGroup && !rdfGroup->isLFGGroup() && !me->InBattleground())
        {
            Unit* encounter = BotUtility::FindWorldPvPEnemy(me, BOTAI_SEARCH_RANGE);
            Unit* assist = encounter ? encounter : GetHumanPartyCombatTarget();
            if (assist)
            {
                rdfPartyCombat = true; rdfTarget = assist;
                if (me->GetSelectedUnit() != assist)
                {
                    me->AttackStop(); me->SetSelection(assist->GetGUID());
                    m_Movement->ClearMovement();
                    if (sConfigMgr->GetIntDefault("PlayerBot.CombatSpellAudit", 0) != 0)
                        TC_LOG_INFO(LOG_FILTER_LFG, "PARTY_ASSIST_ENGAGE account=%u spec=%u target=%u",
                            me->GetSession()->GetAccountId(), me->GetSpecializationId(), assist->GetEntry());
                }
                m_CruxMovement.ClearMovement();
                m_UseFood.ClearFoodState(); Dismount();
                if (Spell* preparation = me->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                {
                    SpellInfo const* info = preparation->GetSpellInfo();
                    std::string name = info && info->SpellName ? info->SpellName : "";
                    if (name.find("Conjure Mana") == 0 || name.find("Call Pet") == 0)
                        me->InterruptSpell(CURRENT_GENERIC_SPELL, false);
                }
                if (Spell* bandage = me->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                {
                    uint32 id = bandage->GetSpellInfo()->Id;
                    if (id == 746 || id == 3267 || id == 7926 || id == 10838 ||
                        id == 18608 || id == 27030 || id == 45543 || id == 45544)
                        me->InterruptSpell(CURRENT_CHANNELED_SPELL, false);
                }
            }
        }
        if (rdfGroup && rdfGroup->isLFGGroup() && me->IsInWorld() &&
            !me->IsBeingTeleported() && !me->IsChangeMap() && me->GetMap()->IsDungeon())
        {
            std::vector<Player*> rdfMembers;
            for (GroupReference* ref = rdfGroup->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->getSource();
                if (!member || !member->IsInWorld() || !member->isAlive() ||
                    member->GetMap() != me->GetMap() || !me->InSamePhase(member))
                    continue;
                rdfMembers.push_back(member);
                rdfPartyCombat = rdfPartyCombat || member->isInCombat();
            }
            if (rdfPartyCombat)
            {
                NearCreatureVec rdfCreatures;
                SearchCreatureListFromRange(me, rdfCreatures, BOTAI_SEARCH_RANGE, false);
                if(IsTankBotAI())
                    for(Player* member:rdfMembers)
                        if(member!=me && me->GetDistance(member)<=60.0f)
                            SearchCreatureListFromRange(member,rdfCreatures,BOTAI_SEARCH_RANGE,false);
                std::set<Creature*> rdfSeen;
                float bestScore = -1.0f;
                for (Creature* enemy : rdfCreatures)
                {
                    if(!rdfSeen.insert(enemy).second)continue;
                    if (!enemy || !enemy->IsInWorld() || !enemy->isAlive() ||
                        enemy->GetMap() != me->GetMap() || !me->InSamePhase(enemy) ||
                        !me->IsValidAttackTarget(enemy) || enemy->IsInEvadeMode() ||
                        IsInvincible(enemy) || TargetIsControl(enemy) ||
                        m_FliterCreatures.IsFliterCreature(enemy))
                        continue;
                    bool engaged = false;
                    bool threatensOther = false;
                    for (Player* member : rdfMembers)
                    {
                        if (member->getVictim() == enemy)
                            engaged = true;
                        if(enemy->isInCombat() && enemy->getThreatManager().getThreat(member)>0.0f)
                            engaged=true;
                        UnitSet* attackers = member->getAttackers();
                        for (UnitSet::iterator itr = attackers->begin(); itr != attackers->end(); ++itr)
                            if (*itr == enemy)
                            {
                                engaged = true;
                                break;
                            }
                        if (Unit* victim = enemy->getVictim())
                            if (victim->GetCharmerOrOwnerPlayerOrPlayerItself() == member)
                            {
                                engaged = true;
                                if (member != me && !member->IsTankPlayer())
                                    threatensOther = true;
                            }
                        if (Pet* pet = member->GetPet())
                            if (pet->getVictim() == enemy)
                                engaged = true;
                    }
                    if (!engaged)
                        continue;
                    float score = 100.0f - me->GetDistance(enemy);
                    if (me->GetSelectedUnit() == enemy) score += 20.0f;
                    if (rdfGroup->GetGroupTankTarget() == enemy) score += 40.0f;
                    if (IsTankBotAI() && threatensOther) {
                        score += 200.0f; // Recovery outranks the current tank focus.
                        if(Unit* victim=enemy->getVictim())
                            if(Player* owner=victim->GetCharmerOrOwnerPlayerOrPlayerItself())
                                if(RdfRotationPolicy::healer(owner->GetSpecializationId()))score+=100.0f;
                    }
                    if (score > bestScore)
                    {
                        bestScore = score;
                        rdfTarget = enemy;
                    }
                }
                // Recover direct party victims even if the nearby creature scan missed them.
                if (!rdfTarget)
                    for (Player* member : rdfMembers) {
                        Unit* candidate = member->getVictim();
                        if (!candidate && member->GetPet()) candidate=member->GetPet()->getVictim();
                        if (!candidate || !candidate->IsInWorld() || !candidate->isAlive() ||
                            candidate->GetMap()!=me->GetMap() || !me->InSamePhase(candidate) ||
                            !me->IsValidAttackTarget(candidate) || IsInvincible(candidate) || TargetIsControl(candidate))continue;
                        if (candidate->ToCreature() && candidate->ToCreature()->IsInEvadeMode())continue;
                        rdfTarget=candidate;break;
                    }
                m_UseFood.UpdateBotFood(BOTAI_UPDATE_TICK, m_UseMountID);
                // Preparation must not hold the group in combat indefinitely.
                if (Spell* preparation = me->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                    if (preparation->GetSpellInfo()->Id == 36883)
                        me->InterruptSpell(CURRENT_GENERIC_SPELL, false, true, 36883);
                if (rdfTarget)
                {
                    if (me->GetSelectedUnit() != rdfTarget)
                    {
                        me->AttackStop();
                        me->SetSelection(rdfTarget->GetGUID());
                        TC_LOG_INFO(LOG_FILTER_LFG,
                            "RDF_ENGAGE account=%u name=%s target=%s entry=%u tank=%u healer=%u",
                            me->GetSession()->GetAccountId(), me->GetName(),
                            rdfTarget->GetName(), rdfTarget->GetEntry(),
                            uint32(IsTankBotAI()), uint32(IsHealerBotAI()));
                    }
                }
                if (rdfLog)
                    TC_LOG_INFO(LOG_FILTER_LFG,
                        "RDF_COMBAT_PRIORITY account=%u target=%u casting=%u flee=%u",
                        me->GetSession()->GetAccountId(), rdfTarget ? rdfTarget->GetEntry() : 0,
                        uint32(me->HasUnitState(UNIT_STATE_CASTING)), uint32(m_ForceFlee));
            }
        }

        if (!m_ForceFlee && !m_CruxMovement.HasCruxMovement() &&
            ProcessLegionPvp(PvpLegionRotation::Applicable(me, rdfTarget) ? rdfTarget : me->GetSelectedUnit())) return;
		if (ProcessRdfLegionRotation(rdfTarget, rdfPartyCombat))
			return;

		if (!rdfPartyCombat && NonCombatProcess())
			{ rdfTrace("exit_917"); return; }
		if (!m_CruxMovement.HasCruxMovement() && me->HasUnitState(UNIT_STATE_CASTING))
			{ rdfTrace("exit_919"); return; }

		if (!me->isInCombat() && !rdfPartyCombat)
		{
			m_MeleeFleeTick = 0;
			m_FullDispel = 0;
			m_MovetoHaltPos.ClearMoveto();
			if (ProcessNormalSpell())
				{ rdfTrace("exit_927"); return; }
		}
		m_Movement->SyncPosition(me->GetPosition());
		if (!rdfPartyCombat && TryUpMount())
			{ rdfTrace("exit_931"); return; }
		if (!me->HasAura(m_UseMountID) && !me->HasUnitState(UNIT_STATE_CASTING))
			m_UsePotion.TryUsePotion();
		if (me->isInCombat())
			if (!PvpLegionRotation::Applicable(me, me->GetSelectedUnit())) UpEnergy();
		Unit* pTarget = rdfPartyCombat ? rdfTarget : GetBotAIValidSelectedUnit();
		if (m_ForceFlee)
		{
			me->AttackStop();
			me->SetSelection(ObjectGuid::Empty);
			ProcessFollowToMaster();
			ProcessFullDispel();
		}
		else if (m_CruxMovement.HasCruxMovement())
		{
			m_CruxMovement.UpdateCruxMovement(m_Movement);
			ProcessFullDispel();
		}
		else if (IsTankBotAI() && ProcessTank(pTarget))
			{ rdfTrace("exit_950"); return; }
		else if (pTarget && pTarget->isAlive() && !IsInvincible(pTarget))
		{
			if (ProcessFullDispel())
				{ rdfTrace("exit_954"); return; }
			float distance = me->GetDistance(pTarget->GetPosition());
			if (m_MovetoHaltPos.ProcessMovetoPosition(m_Movement))
			{
				if (IsHealerBotAI() && me->getLevel() >= 10)
					ProcessHealth(false);
				else
					ProcessHalt(pTarget);
			}
			else if (distance < BOTAI_SEARCH_RANGE)
			{
				if (IsHealerBotAI() && me->getLevel() >= 10)
					ProcessHealth(true);
				else
					ProcessCombat(pTarget);
			}
			else if (distance > BOTAI_SEARCH_RANGE * 4.0f || me->GetMap() != pTarget->GetMap())
			{
				me->AttackStop();
				//me->StopMoving();
				me->SetSelection(ObjectGuid::Empty);
			}
			else
			{
				m_Movement->MovementToTarget();
			}
		}
		else if (!rdfPartyCombat && (pTarget = GetCombatTarget()))
		{
			me->AttackStop();
			me->SetSelection(pTarget->GetGUID());
			ProcessFullDispel();
		}
		else
		{
			me->SetSelection(ObjectGuid::Empty);
			ClearTankTarget();
			if (m_CruxHealTarget != ObjectGuid::Empty)
			{
				if (IsHealerBotAI())
					ProcessHealth(true);
				else
					m_CruxHealTarget = ObjectGuid::Empty;
			}
			else
			{
				ProcessFollowToMaster();
				ProcessFullDispel();
			}
		}
	}
	else
	{
		m_MeleeFleeTick = 0;
		m_FullDispel = 0;
		m_CruxHealTarget = ObjectGuid::Empty;
		m_MovetoUseGO.ClearUseGO();
		m_MovetoHaltPos.ClearMoveto();
		if (m_SeduceTarget != ObjectGuid::Empty)
		{
			m_SeduceTarget = ObjectGuid::Empty;
			if (Group* pGroup = me->GetGroup())
				pGroup->ClearAllGroupForceFleeState();
		}
		m_WishStore.ClearStores();
		ClearCruxMovement();
		if (!me->isInCombat())
			ClearTankTarget();
		if (Group* pGroup = me->GetGroup())
		{
			if (!BotUtility::BotCanForceRevive)
			{
				if (!pGroup->AllGroupNotCombat())
					{ rdfTrace("exit_1027"); return; }
			}
		}
		if (me->GetDistance(m_MasterPlayer->GetPosition()) >= NEEDFLEE_CHECKRANGE)
		{
			m_Teleporting.SetTeleport(m_MasterPlayer, 2.0f);
		}
		else
		{
			//m_Teleporting.ClearTeleport();
			me->SetSelection(ObjectGuid::Empty);
			m_Revive.UpdateRevive(BOTAI_UPDATE_TICK);
		}
	}
    rdfTrace("completed");
}

void BotGroupAI::ResetBotAI()
{
	PlayerBotSetting::ClearUnknowMount(me);
	m_Movement->ClearMovement();
	m_Flying.RandomFlyMount();
	m_FastAid.CheckPlayerFastAid();

	if (m_UseMountID != 0)
	{
		if (!me->HasSpell(m_UseMountID))
			me->learnSpell(m_UseMountID, false);
	}
	m_IsMeleeBot = IsMeleeBotAI();
	m_IsRangeBot = IsRangeBotAI();
	m_IsHealerBot = IsHealerBotAI();
	m_HasReset = true;

	m_CruxHealTarget = ObjectGuid::Empty;
	m_SeduceTarget = ObjectGuid::Empty;
	m_Flee.Clear();
	m_MeleeFleeTick = 0;
}

void BotGroupAI::StartFullDispel()
{
	if (IsTankBotAI())
		return;
	m_FullDispel = getMSTime() + 2000;
}

void BotGroupAI::ClearCruxMovement()
{
	if (m_CruxMovement.HasCruxMovement())
	{
		m_CruxMovement.ClearMovement();
		m_Movement->ClearMovement();
	}
}

void BotGroupAI::AddTankTarget(Creature* pCreature)
{
	if (!pCreature || !pCreature->isAlive() || me->GetMap() != pCreature->GetMap() || !me->IsValidAttackTarget(pCreature))
		return;
	if (!IsTankBotAI())
		return;
	m_TankTargets.AddTarget(pCreature);
}

bool BotGroupAI::IsNotSelect(Unit* pTarget)
{
    if (!FelsoriaDHDummyLimit::CanUse(me, pTarget))
        return true;
	if (!pTarget || !pTarget->isAlive())
		return true;
	if (pTarget->HasAura(27827)) // (27827 ľČĘęÖ®»ę ÉńÄÁËŔÍöşó)
		return true;
	if (pTarget->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE))
		return true;
	if (pTarget->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE))
		return true;
	if (pTarget->ToCreature() && pTarget->ToCreature()->IsInEvadeMode())
		return true;
	return false;
}

bool BotGroupAI::IsIDLEBot()
{
	if (me->HasUnitState(UNIT_STATE_CASTING))
		return false;
	if (me->isInCombat() || !me->isAlive())
		return false;
	if (!m_Teleporting.CanMovement())
		return false;
	if (m_UseFood.HasFoodState())
		return false;
	if (m_FindLoot.HasLoot())
		return false;

	return true;
}

bool BotGroupAI::CanExecuteSeduce()
{
	if (m_SeduceTarget != ObjectGuid::Empty)
		return false;
	if (me->HasUnitState(UNIT_STATE_CASTING))
		return false;
	if (!me->isAlive() || me->GetTargetGUID() != ObjectGuid::Empty)
		return false;
	if (!m_Teleporting.CanMovement() || me->GetHealthPct() < 75)
		return false;
	if (m_UseFood.HasFoodState())
		return false;
	if (m_FindLoot.HasLoot())
		return false;
	return true;
}

void BotGroupAI::OnLevelUp(uint32 talentType)
{
	m_HasReset = false;
	m_WishStore.ClearWishs();
}

void BotGroupAI::OnLootedItem(uint32 entry)
{
	m_LootedItems.AddLootedItem(entry);
}

bool BotGroupAI::TryUpMount()
{
    return ProcessMountFollow();
}

void BotGroupAI::Dismount()
{
	if (!me->HasAura(m_UseMountID))
		return;
	me->RemoveOwnedAura(m_UseMountID, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
}

void BotGroupAI::SetVehicle3DNextMoveGap(float gap)
{
	m_VehicleMovement3D->SetStopMoveGap(gap);
}

void BotGroupAI::SetVehicle3DMoveTarget(Unit* pTarget, float offset)
{
	m_VehicleMovement3D->AddMovement(pTarget, offset);
}

void BotGroupAI::ProcessHorror(uint32 diff)
{
	if (HasAuraMechanic(me, Mechanics::MECHANIC_HORROR) ||
		HasAuraMechanic(me, Mechanics::MECHANIC_DISORIENTED) ||
		HasAuraMechanic(me, Mechanics::MECHANIC_FEAR))
	{
		if (!pHorrorState)
		{
			pHorrorState = new BotAIHorrorState(me);
			me->GetMotionMaster()->Clear();
			m_Movement->ClearMovement();
			me->UpdatePosition(me->GetPosition(), true);
			m_Movement->SyncPosition(me->GetPosition(), true);
			me->SetSelection(ObjectGuid::Empty);
		}
		pHorrorState->UpdateHorror(diff, m_Movement);
	}
	else if (pHorrorState)
	{
		delete pHorrorState;
		pHorrorState = NULL;
		m_Movement->ClearMovement();
	}
}

bool BotGroupAI::TargetIsNotDiminishingByType(Unit* pTarget, DiminishingGroup diType)
{
	if (!pTarget)
		return false;
	if (diType < 1 || diType > 20)
		return false;
	DiminishingLevels diLevel = pTarget->GetDiminishing(diType);
	return diLevel < 1;
}

bool BotGroupAI::TargetIsNotDiminishingByType2(Unit* pTarget, DiminishingGroup diType)
{
	if (!pTarget)
		return false;
	if (diType < 1 || diType > 20)
		return false;
	DiminishingLevels diLevel = pTarget->GetDiminishing(diType);
	return diLevel < 2;
}

bool BotGroupAI::HasAuraMechanic(Unit* pTarget, Mechanics mask)
{
	if (!pTarget)
		return false;
	return (pTarget->HasAuraWithMechanic(1 << mask));
}

bool BotGroupAI::IsNotMovement()
{
	if (HasAuraMechanic(me, Mechanics::MECHANIC_ROOT))
	{
		me->StopMoving();
		return true;
	}
	if (IsNotSelect(me))
	{
		me->StopMoving();
		return true;
	}
	//if (me->IsStopped())
	return false;
	//return true;
}

bool BotGroupAI::IsInvincible(Unit* pTarget)
{
	if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_BANISH))
	{
		return true;
	}
	//if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_IMMUNE_SHIELD))
	//{
	//	return true;
	//}
	return false;
}

// FELSORIA_PARTY_ASSIST_V3: actual human victims and cast targets, including casters.
Unit* BotGroupAI::GetHumanPartyCombatTarget()
{
    Group* group = me->GetGroup();
    if (!group || group->isLFGGroup() || me->InBattleground()) return nullptr;
    auto legal = [this](Unit* enemy)
    {
        return enemy && enemy != me && enemy->IsInWorld() && enemy->isAlive() &&
            enemy->GetMap() == me->GetMap() && me->InSamePhase(enemy) &&
            me->IsValidAttackTarget(enemy) && me->canSeeOrDetect(enemy) &&
            me->GetDistance(enemy) <= BOTAI_SEARCH_RANGE * 2.5f &&
            !IsInvincible(enemy) && !TargetIsControl(enemy);
    };
    auto humanTarget = [&](Player* human) -> Unit*
    {
        if (!human || !human->GetSession() || human->GetSession()->IsBotSession() ||
            !human->IsInWorld() || !human->isAlive() || !group->IsMember(human->GetGUID()) ||
            human->GetMap() != me->GetMap() || !me->InSamePhase(human)) return nullptr;
        for (uint8 slot : {uint8(CURRENT_GENERIC_SPELL), uint8(CURRENT_CHANNELED_SPELL), uint8(CURRENT_AUTOREPEAT_SPELL)})
            if (Spell* cast = human->GetCurrentSpell(slot))
                if (cast->getState() != SPELL_STATE_FINISHED && !cast->GetSpellInfo()->IsPositive())
                    if (Unit* enemy = cast->m_targets.GetUnitTarget())
                        if (legal(enemy)) return enemy;
        if (Unit* victim = human->getVictim()) if (legal(victim)) return victim;
        if (Pet* pet = human->GetPet())
            if (Unit* victim = pet->getVictim()) if (legal(victim)) return victim;
        if (human->isInCombat())
            if (Unit* selected = human->GetSelectedUnit()) if (legal(selected)) return selected;
        return nullptr;
    };
    if (Unit* enemy = humanTarget(m_MasterPlayer)) return enemy;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (ref->getSource() != m_MasterPlayer)
            if (Unit* enemy = humanTarget(ref->getSource())) return enemy;
    return nullptr;
}

bool BotGroupAI::UsePartyLegionRotation(Unit* enemy, bool partyCombat) const
{
    Group* group = me->GetGroup();
    return group && !group->isLFGGroup() && !me->InBattleground() && !me->InArena() &&
        m_MasterPlayer && m_MasterPlayer->GetSession() && !m_MasterPlayer->GetSession()->IsBotSession() &&
        group->IsMember(m_MasterPlayer->GetGUID()) && partyCombat &&
        (!enemy || !enemy->GetCharmerOrOwnerPlayerOrPlayerItself()) &&
        RdfRotationPolicy::supported(me->GetSpecializationId()) &&
        sConfigMgr->GetIntDefault("PlayerBot.Party.LegionRotations", 1) != 0;
}

bool BotGroupAI::UpdateMasterPlayer()
{
	Group* pGroup = me->GetGroup();
	if (!pGroup)
	{
		m_Flying.CancelFly();
		sLFGMgr->LeaveLfg(me->GetGUID());
		PlayerBotMgr::SwitchPlayerBotAI(me, PlayerBotAIType::PBAIT_FIELD, true);
		return false;
	}
    if (pGroup->isLFGGroup())
    {
        // Do not require an LFG leader to be a human, and do not use
        // FindPlayer here: a member remains in the group while off-world.
        m_MasterPlayer = nullptr;
        for (GroupReference* ref = pGroup->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (!member || !member->GetSession() || member->GetSession()->IsBotSession())
                continue;
            if (!m_MasterPlayer || member->GetGUID() == pGroup->GetLeaderGUID())
                m_MasterPlayer = member;
        }
        return m_MasterPlayer != nullptr;
    }
	if (!pGroup->GroupExistRealPlayer())
	{
		m_Flying.CancelFly();
		sLFGMgr->LeaveLfg(me->GetGUID());
		return false;
	}

    m_GroupLeader.ProcessGroupLeader();
    m_MasterPlayer = nullptr;
    for (GroupReference* ref = pGroup->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->getSource();
        if (!member || !member->GetSession() || member->GetSession()->IsBotSession() || !member->IsInWorld()) continue;
        if (!m_MasterPlayer || member->GetGUID() == pGroup->GetLeaderGUID()) m_MasterPlayer = member;
    }
    return m_MasterPlayer != nullptr;
}

bool BotGroupAI::TrySettingToMaster()
{
    if (!BotUtility::BotCanSettingToMaster)
        return false;

    if (!me->isAlive() ||
        me->isInCombat() ||
        m_MasterPlayer == NULL)
    {
        return false;
    }

    if (me->HasUnitState(UNIT_STATE_CASTING))
        return false;

    if (!me->IsSettingFinish())
        return true;

    PlayerBotSession* pSession =
        dynamic_cast<PlayerBotSession*>(me->GetSession());

    if (!pSession)
        return false;

    if (pSession->HasSchedules())
        return true;

    if (pSession->IsAccountBotSession())
        return false;

    int32 masterLV = PlayerBotSetting::CheckMaxLevel(
        m_MasterPlayer->getLevel());

    int32 meLV = me->getLevel();

    // A party soha ne csökkentse a bot szintjét.
    if (meLV >= masterLV)
        return false;

    BotGlobleSchedule schedule2(
        BotGlobleScheduleType::BGSType_Settting,
        0);

    schedule2.parameter1 = masterLV;
    schedule2.parameter2 = masterLV;
    schedule2.parameter3 =
        PlayerBotSetting::FindPlayerTalentType(me) + 1;

    pSession->PushScheduleToQueue(schedule2);
    m_HasReset = false;

    return true;
}

bool BotGroupAI::TryTeleportToMaster()
{
	if (m_MasterPlayer == NULL)
		return false;
	if (!m_MasterPlayer->IsInWorld())
		return true;
	if (!m_MasterPlayer->m_taxi.empty())
		return true;
	if (m_StopFollow)
		return true;
	if (m_MasterPlayer->IsFlying() && !m_Flying.HasFlying())
		return false;
    if (me->GetGroup() && me->GetGroup()->isLFGGroup())
    {
        // Let the native LFG transfer complete; do not start a competing
        // BotTeleport transfer to the master's coordinates.
        if (me->IsBeingTeleported() || m_MasterPlayer->IsBeingTeleported())
            return true;
        if (m_MasterPlayer->GetMap() != me->GetMap())
        {
            auto dungeon = sLFGMgr->GetLFGDungeon(sLFGMgr->GetDungeon(me->GetGroup()->GetGUID()));
            if (dungeon && m_MasterPlayer->GetMapId() == uint32(dungeon->map) &&
                me->GetMapId() != uint32(dungeon->map))
            {
                uint32 now = getMSTime();
                if (!m_RdfTeleportAttempted || uint32(now - m_RdfTeleportAttempt) >= 5000)
                {
                    m_RdfTeleportAttempted = true;
                    m_RdfTeleportAttempt = now;
                    sLFGMgr->TeleportPlayer(me, false);
                }
            }
            return true;
        }
    }
	if (m_MasterPlayer->GetMap() != me->GetMap())
	{
		if (!m_MasterPlayer->InBattleground())
			m_Teleporting.SetTeleport(m_MasterPlayer, me->GetMap() && me->GetMap()->IsDungeon() ? 0.0f : 2.5f);
		me->SetSelection(ObjectGuid::Empty);
		m_Movement->ClearMovement();
		m_Flee.Clear();
		return true;
	}
	else if (me->GetDistance(m_MasterPlayer->GetPosition()) > BOTAI_SEARCH_RANGE * 5)
	{
        // V10 owns same-dungeon recovery, including unsafe-landing retries.
        // Do not bypass its checks with the legacy long-distance teleport.
        if (me->GetMap() && me->GetMap()->IsDungeon())return false;
		m_Teleporting.SetTeleport(m_MasterPlayer, me->GetMap() && me->GetMap()->IsDungeon() ? 0.0f : 2.5f);
		me->SetSelection(ObjectGuid::Empty);
		m_Movement->ClearMovement();
		m_Flee.Clear();
		return true;
	}

	return false;
}

Unit* BotGroupAI::GetCombatTarget(float range)
{
	Group* pGroup = me->GetGroup();
	if (!pGroup)
		return NULL;
	if (!IsTankBotAI())
	{
		Unit* pTankTarget = pGroup->GetGroupTankTarget();
		if (pTankTarget && FelsoriaDHDummyLimit::CanUse(me, pTankTarget) &&
            pTankTarget->isAlive() && me->IsValidAttackTarget(pTankTarget) &&
			pTankTarget->IsVisible() && !ShouldBlockBossPull(pTankTarget) && !m_FliterCreatures.IsFliterCreature(pTankTarget->ToCreature()))
			return pTankTarget;
	}
	NearUnitVec validTarget;
	NearPlayerList playersNearby;
	me->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() != me->GetTeamId())
		{
			if (!pVisionPlayer->IsPvP())
				continue;
			if (TargetIsStealth(pVisionPlayer))
				continue;
			if (TargetIsControl(pVisionPlayer))
				continue;
			Player* pTarget = pVisionPlayer->GetSelectedPlayer();
			if (pTarget && pGroup->IsMember(pTarget->GetGUID()))
				validTarget.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(me, creatures, range, false);
	for (Creature* pCreature : creatures)
	{
		if (!pCreature->isInCombat())
			continue;
		if (pCreature->GetPhaseMask() != me->GetPhaseMask())
			continue;
		if (TargetIsControl(pCreature))
			continue;
        ObjectGuid& guid = pCreature->GetTargetGUID();
        if (guid == ObjectGuid::Empty)
			continue;
		if(!pGroup->IsMember(guid))
			continue;
		validTarget.push_back(pCreature);
	}
	if (validTarget.empty())
		return NULL;
	return validTarget[urand(0, validTarget.size() - 1)];
}

void BotGroupAI::ProcessFollowToMaster()
{
    if(m_MechanicOwned)return;
    if(ProcessTankLeadFormation())return;

	if (m_MasterPlayer == NULL)
		return;
	if (m_StopFollow || m_MasterPlayer->IsFlying())
	{
		m_Movement->ClearMovement();
		return;
	}
	if (m_MasterPlayer->duel && !(m_MasterPlayer->duel->state == DuelState::DUEL_COMPLETED))
	{
		m_Movement->ClearMovement();
		return;
	}
    // Outdoor party formation: stable rear slots, rather than the owner's GPS.
    if (me->GetMap() && !me->GetMap()->IsDungeon() && !me->InBattleground() && !m_MasterPlayer->IsPlayerBot())
    {
        uint32 slot = me->GetGUID().GetCounter() % 5;
        float angle = 2.35f + float(slot) * 0.4f;
        float distance = 2.5f + float(slot % 2) * 0.5f;
        Position follow;
        m_MasterPlayer->GetFirstCollisionPosition(follow, distance, angle);
        if (me->GetDistance(follow) > 1.25f)
            m_Movement->MovementTo(follow.GetPositionX(), follow.GetPositionY(), follow.GetPositionZ(), 0);
        else
        {
            m_Movement->ClearMovement();
            if (!me->IsStopped()) me->StopMoving();
        }
        return;
    }
    if(UseRdfLegionRotation() && me->GetMap()->IsDungeon()) {
        if(IsNotMovement() || !m_MasterPlayer->IsInWorld() ||
           me->GetMap()!=m_MasterPlayer->GetMap() || !me->InSamePhase(m_MasterPlayer))return;
        if(me->GetDistance(m_MasterPlayer)<=2.0f){m_Movement->ClearMovement();return;}
        PathGenerator path(me);
        if(path.CalculatePath(m_MasterPlayer->GetPositionX(),m_MasterPlayer->GetPositionY(),m_MasterPlayer->GetPositionZ(),false,false,false)) {
            auto const& pts=path.GetPath();
            if(!pts.empty()) {
                float x=pts.back().x-m_MasterPlayer->GetPositionX(),y=pts.back().y-m_MasterPlayer->GetPositionY();
                if(x*x+y*y<=2.25f && std::fabs(pts.back().z-m_MasterPlayer->GetPositionZ())<=2.0f && LaunchRdfValidatedPath(path))return;
            }
        }
        if(m_RdfTraceTick==10000)TC_LOG_INFO(LOG_FILTER_LFG,"RDF_FOLLOW_BLOCKED account=%u",me->GetSession()->GetAccountId());
        return;
    }
	Position& targetPos = BotUtility::GetPositionFromGroup(m_MasterPlayer, me->GetGUID(), me->GetGroup());
	m_Movement->MovementTo(targetPos.GetPositionX(), targetPos.GetPositionY(), targetPos.GetPositionZ(), 0);
	//float distance = me->GetDistance(m_MasterPlayer->GetPosition());
	//if (distance <= NEEDFLEE_CHECKRANGE && distance > 0.1f)
	//{
	//	me->GetMotionMaster()->Clear();
	//	me->GetMotionMaster()->MoveFollow(m_MasterPlayer, 1.0f, m_MasterPlayer->GetOrientation());
	//	return;
	//}

	//if (me->IsWithinLOSInMap(m_MasterPlayer))
	//{
	//	me->GetMotionMaster()->Clear();
	//	me->GetMotionMaster()->MoveFollow(m_MasterPlayer, 1.0f, m_MasterPlayer->GetOrientation());
	//}
	//else
	//	m_Movement->MovementTo(m_MasterPlayer->GetPositionX(), m_MasterPlayer->GetPositionY(), m_MasterPlayer->GetPositionZ(), 1);
}

bool BotGroupAI::NonCombatProcess()
{
	m_DelayGiveXP.ProcessGiveXP(m_MasterPlayer->getLevel());
	{
		std::lock_guard<std::mutex> lock(m_ItemLock);
		if (m_CheckDuel.CheckDuel())
			sPlayerBotMgr->SwitchPlayerBotAI(me, PlayerBotAIType::PBAIT_DUEL, true);
		m_Guild.UpdateGuildProcess();
		if (m_AITrade.ProcessTrade())
			{
                if (m_RdfTraceTick == 10000)
                    TC_LOG_INFO(LOG_FILTER_LFG,
                        "RDF_NONCOMBAT account=%u name=%s reason=trade health=%.1f",
                        me->GetSession()->GetAccountId(), me->GetName(),
                        double(me->GetHealthPct()));
                return /* diagnostic */ true;
            }
		m_Flying.UpdateFly(m_MasterPlayer, m_UseMountID, m_Movement);
		if (m_Flying.HasFlying())
			{
                if (m_RdfTraceTick == 10000)
                    TC_LOG_INFO(LOG_FILTER_LFG,
                        "RDF_NONCOMBAT account=%u name=%s reason=flying health=%.1f",
                        me->GetSession()->GetAccountId(), me->GetName(),
                        double(me->GetHealthPct()));
                return /* diagnostic */ true;
            }
		m_LootedItems.LookupLootedItems(BOTAI_UPDATE_TICK);
		if (m_UseFood.UpdateBotFood(BOTAI_UPDATE_TICK, m_UseMountID))
			{
                if (m_RdfTraceTick == 10000)
                    TC_LOG_INFO(LOG_FILTER_LFG,
                        "RDF_NONCOMBAT account=%u name=%s reason=food health=%.1f",
                        me->GetSession()->GetAccountId(), me->GetName(),
                        double(me->GetHealthPct()));
                return /* diagnostic */ true;
            }
		if (m_FindLoot.DoFindLoot(BOTAI_UPDATE_TICK, m_Movement, m_UseMountID))
			{
                if (m_RdfTraceTick == 10000)
                    TC_LOG_INFO(LOG_FILTER_LFG,
                        "RDF_NONCOMBAT account=%u name=%s reason=loot health=%.1f",
                        me->GetSession()->GetAccountId(), me->GetName(),
                        double(me->GetHealthPct()));
                return /* diagnostic */ true;
            }
		if (m_MovetoUseGO.ProcessMovetoUseGO(m_Movement))
			{
                if (m_RdfTraceTick == 10000)
                    TC_LOG_INFO(LOG_FILTER_LFG,
                        "RDF_NONCOMBAT account=%u name=%s reason=gameobject health=%.1f",
                        me->GetSession()->GetAccountId(), me->GetName(),
                        double(me->GetHealthPct()));
                return /* diagnostic */ true;
            }
		if (ProcessRevivePlayer())
			{
                if (m_RdfTraceTick == 10000)
                    TC_LOG_INFO(LOG_FILTER_LFG,
                        "RDF_NONCOMBAT account=%u name=%s reason=resurrect health=%.1f",
                        me->GetSession()->GetAccountId(), me->GetName(),
                        double(me->GetHealthPct()));
                return /* diagnostic */ true;
            }
		m_WishStore.UpdateWishStore();
		BotUtility::TryTeleportPlayerPet(me);
	}
	return false;
}

bool BotGroupAI::ProcessRevivePlayer()
{
	uint32 reviveSpellID = GetReviveSpell();
	if (reviveSpellID == 0)
		return false;
	if (me->HasUnitState(UNIT_STATE_CASTING))
		return false;
	ObjectGuid& guid = m_RevivePlayer.SearchNeedRevive(BOTAI_UPDATE_TICK);
	if (guid.IsEmpty())
		return false;
	Player* pRevive = ObjectAccessor::FindPlayer(guid);
	if (!pRevive || pRevive->isAlive())
		return false;
	if (!me->IsWithinLOSInMap(pRevive) || me->GetDistance(pRevive->GetPosition()) > 15)
	{
		m_Movement->MovementTo(pRevive->GetPositionX(), pRevive->GetPositionY(), pRevive->GetPositionZ());
		return true;
	}
	m_Movement->ClearMovement();
	if (TryCastSpell(reviveSpellID, pRevive) == SpellCastResult::SPELL_CAST_OK)
		return true;
	return false;
}

bool BotGroupAI::DoFaceToTarget(Unit* pTarget)
{
	float relative = me->GetRelativeAngle(pTarget->GetPositionX(), pTarget->GetPositionY());
	if (relative >= M_PI_2 && !IsNotMovement())// (fabsf(selfAngle) > M_PI_4)
	{
		//me->SetInFront(pTarget);
		//me->SetFacingToObject(pTarget);
		Movement::MoveSplineInit init(*me);
		init.MoveTo(me->GetPositionX(), me->GetPositionY(), me->GetPositionZMinusOffset());
		init.SetFacing(pTarget);
		init.SetOrientationFixed(true);
		init.Launch();
		return true;
	}
	return false;
}

SpellCastResult BotGroupAI::TryCastSpell(uint32 spellID, Unit* pTarget, bool force, bool dismount)
{
	if (!spellID || !me->HasSpell(spellID))
	{
		return SpellCastResult::SPELL_FAILED_SPELL_LEARNED;
	}
	SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
	if (!spellInfo || spellInfo->IsPassive() || m_Flying.HasFlying())
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
    if (!pTarget || !me->IsInWorld() || !pTarget->IsInWorld() || me->GetMap() != pTarget->GetMap())
        return SpellCastResult::SPELL_FAILED_UNKNOWN;
    if (m_CombatCastIssued || me->HasUnitState(UNIT_STATE_CASTING))
        return SpellCastResult::SPELL_FAILED_UNKNOWN;
    bool const utility = !me->isInCombat() &&
        ((spellInfo->IsPositive() && spellInfo->HasEffect(SPELL_EFFECT_APPLY_AURA)) ||
         spellInfo->HasEffect(SPELL_EFFECT_CREATE_ITEM) || spellInfo->HasEffect(SPELL_EFFECT_CREATE_ITEM_2));
    uint32 const now = getMSTime();
    auto const key = std::make_pair(spellID, pTarget->GetGUID());
    if (utility)
    {
        if (pTarget->HasAura(spellID))
            return SpellCastResult::SPELL_FAILED_UNKNOWN;
        auto itr = m_UtilityAttempts.find(key);
        if (itr != m_UtilityAttempts.end() && uint32(now - itr->second) < 30000)
            return SpellCastResult::SPELL_FAILED_UNKNOWN;
        // Includes unsuccessful preparation; failure must not block follow every tick.
        m_UtilityAttempts[key] = now;
    }
	if (!m_WishStore.CanWishStore(spellID, pTarget))
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
    // Native combat pets only: never use the old companion summon (23498).
    if (spellID == 23498) return SPELL_FAILED_UNKNOWN;
    std::string optionalName = spellInfo->SpellName ? spellInfo->SpellName : "";
    bool petPreparation = spellID == 883 || spellID == 982 || optionalName.find("Call Pet") == 0 || optionalName == "Revive Pet";
    bool gemPreparation = spellID == 36883 || optionalName.find("Conjure Mana") == 0;
    if ((petPreparation || gemPreparation) &&
        !m_OptionalPreparation.Begin(spellID, getMSTime(), 15000, gemPreparation))
        return SPELL_FAILED_NOT_READY;
    SpellCastResult readiness = BotUtility::PrepareBotCast(me, spellInfo, pTarget, m_Movement);
    if (readiness != SPELL_CAST_OK) return readiness;
	TriggerCastData data;
	data.triggerFlags = force ? TriggerCastFlags(TriggerCastFlags::TRIGGERED_IGNORE_POWER_AND_REAGENT_COST | TriggerCastFlags::TRIGGERED_IGNORE_CAST_ITEM) : TriggerCastFlags::TRIGGERED_NONE;
	Spell* spell = new Spell(me, spellInfo, data);
	spell->m_CastItem = NULL;
	SpellCastTargets targets;
	targets.SetUnitTarget(pTarget);
	if (dismount)
		Dismount();
	SpellCastResult castResult = spell->prepare(&targets);
	if (castResult != SpellCastResult::SPELL_CAST_OK)
	{
		if (castResult == SpellCastResult::SPELL_FAILED_NOT_MOUNTED)
			PlayerBotSetting::ClearUnknowMount(me);
		return castResult;
	}
	m_CombatCastIssued = true;
	m_WishStore.TryWishStore(spellID, pTarget);
    if (gemPreparation) m_OptionalPreparation.Complete(spellID);
	return SpellCastResult::SPELL_CAST_OK;
}

SpellCastResult BotGroupAI::TryCastSpellByLifePCTInterrupt(uint32 spellID, Unit* pTarget, uint32 interruptByLife)
{
	if (!spellID || !me->HasSpell(spellID))
	{
		if (spellID)
			m_HasReset = false;
		return SpellCastResult::SPELL_FAILED_SPELL_LEARNED;
	}
	SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
	if (!spellInfo || spellInfo->IsPassive() || m_Flying.HasFlying())
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
	if (interruptByLife < 1 || interruptByLife > 100)
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
	if (!m_WishStore.CanWishStore(spellID, pTarget))
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
	TriggerCastData data;
	Spell* spell = new Spell(me, spellInfo, data);
	spell->m_CastItem = NULL;
	SpellCastTargets targets;
	targets.SetUnitTarget(pTarget);
	Dismount();
	SpellCastResult castResult = spell->prepare(&targets);
	if (castResult != SpellCastResult::SPELL_CAST_OK)
	{
		if (castResult == SpellCastResult::SPELL_FAILED_NOT_MOUNTED)
			PlayerBotSetting::ClearUnknowMount(me);
		return castResult;
	}
	m_WishStore.TryWishStore(spellID, pTarget);
	//if (pTarget && pTarget->GetGUID() != m_CruxHealTarget)
	//	spell->SetInterruptConditionByLifePCT(pTarget->GetGUID(), interruptByLife);
	return SpellCastResult::SPELL_CAST_OK;
}

SpellCastResult BotGroupAI::TryCastPullSpell(uint32 spellID, Unit* pTarget)
{
	if (!spellID || !me->HasSpell(spellID))
		return SpellCastResult::SPELL_FAILED_SPELL_LEARNED;
	SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
	if (!spellInfo || spellInfo->IsPassive() || m_Flying.HasFlying())
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
	TriggerCastData data;
	data.triggerFlags = TriggerCastFlags(TriggerCastFlags::TRIGGERED_IGNORE_POWER_AND_REAGENT_COST | TriggerCastFlags::TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD);
	Spell* spell = new Spell(me, spellInfo, data);
	spell->m_CastItem = NULL;
	SpellCastTargets targets;
	targets.SetUnitTarget(pTarget);
	//spell->InitExplicitTargets(targets);
	//SpellCastResult castResult = spell->CheckCast(true);
	//if (castResult != SpellCastResult::SPELL_CAST_OK)
	//{
	//	spell->finish(false);
	//	delete spell;
	//	return castResult;
	//}
	Dismount();
	SpellCastResult castResult = spell->prepare(&targets);
	if (castResult != SpellCastResult::SPELL_CAST_OK)
	{
		//spell->finish(false);
		//delete spell;
		return castResult;
	}
	return SpellCastResult::SPELL_CAST_OK;
}

SpellCastResult BotGroupAI::PetTryCastSpell(uint32 spellID, Unit* pTarget, bool force)
{
	Pet* pPet = me->GetPet();
	if (!pPet || !pPet->isAlive())
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
	if (!spellID || !pPet->HasSpell(spellID))
		return SpellCastResult::SPELL_FAILED_SPELL_LEARNED;
	SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
	if (!spellInfo || spellInfo->IsPassive())
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
	Spell* spell = new Spell(pPet, spellInfo, TriggerCastData{});
	pTarget = pTarget ? pTarget : pPet;
	SpellCastResult castResult = spell->CheckPetCast(pTarget);
	if (castResult == SPELL_FAILED_UNIT_NOT_INFRONT && !pPet->isPossessed() && !pPet->IsVehicle())
	{
		if (pTarget && pTarget != pPet)
		{
			pPet->SetInFront(pTarget);
			if (Player* player = pTarget->ToPlayer())
				pPet->SendUpdateToPlayer(player);
		}
		else if (Unit* unit_target2 = spell->m_targets.GetUnitTarget())
		{
			pPet->SetInFront(unit_target2);
			if (Player* player = unit_target2->ToPlayer())
				pPet->SendUpdateToPlayer(player);
		}

		if (Unit* powner = pPet->GetCharmerOrOwner())
			if (Player* player = powner->ToPlayer())
				pPet->SendUpdateToPlayer(player);

		castResult = SPELL_CAST_OK;
	}
	if (castResult == SPELL_CAST_OK)
	{
		pTarget = spell->m_targets.GetUnitTarget();

		//10% chance to play special pet attack talk, else growl
		//actually this only seems to happen on special spells, fire shield for imp, torment for voidwalker, but it's stupid to check every spell
		if (pPet->isPet() && (((Pet*)pPet)->getPetType() == SUMMON_PET) && (pPet != pTarget) && (urand(0, 100) < 10))
			pPet->SendPetTalk((uint32)PET_TALK_SPECIAL_SPELL);
		else
		{
			pPet->SendPetAIReaction(me->GetPetGUID());
		}

		if (pTarget && !pPet->isPossessed() && !pPet->IsVehicle())
		{
			// This is true if pet has no target or has target but targets differs.
			if (pPet->getVictim() != pTarget)
			{
				if (pPet->getVictim())
					pPet->AttackStop();
				pPet->GetMotionMaster()->Clear();
				if (pPet->ToCreature()->IsAIEnabled)
					pPet->ToCreature()->AI()->AttackStart(pTarget);
			}
		}

		return spell->prepare(&(spell->m_targets));
	}
	else
	{
		spell->SendCastResult(me, spellInfo, castResult);

		if (!pPet->HasSpellCooldown(spellID))
			pPet->RemoveCreatureSpellCooldown(spellID);

		spell->finish(false);
		delete spell;

		// reset specific flags in case of spell fail. AI will reset other flags
		if (pPet->GetCharmInfo())
			pPet->GetCharmInfo()->SetIsCommandAttack(false);
	}

	return castResult;
}

void BotGroupAI::SettingPetAutoCastSpell(uint32 spellID, bool autoCast)
{
	Pet* pPet = me->GetPet();
	if (!pPet || !pPet->isAlive())
		return;
	if (!spellID || !pPet->HasSpell(spellID))
		return;
	SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
	if (!spellInfo || spellInfo->IsPassive() || !spellInfo->IsAutocastable())
		return;
	CharmInfo* charmInfo = pPet->GetCharmInfo();
	if (!charmInfo)
		return;
	if (pPet->isPet())
		pPet->ToggleAutocast(spellInfo, autoCast);
	else
		pPet->GetCharmInfo()->ToggleCreatureAutocast(spellInfo, autoCast);

	charmInfo->SetSpellAutocast(spellInfo, autoCast);
}

bool BotGroupAI::NeedWaitSpecialSpell(uint32 diff)
{
	if (IsNotSelect(me))
	{
		me->StopMoving();
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_CHARM))
	{
		me->StopMoving();
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_DISORIENTED))
	{
		me->StopMoving();
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_FEAR))
	{
		//me->StopMoving();
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_SLEEP))
	{
		me->StopMoving();
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_STUN))
	{
		me->StopMoving();
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_FREEZE))
	{
		me->StopMoving();
		return true;
	}
	//if (HasAuraMechanic(me, Mechanics::MECHANIC_KNOCKOUT))
	//{
	//	me->StopMoving();
	//	return true;
	//}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_POLYMORPH))
	{
		me->StopMoving();
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_BANISH))
	{
		me->StopMoving();
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_HORROR))
	{
		//me->StopMoving();
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_SAPPED))
	{
		me->StopMoving();
		return true;
	}

	return m_WaitAuras.HasNeedWaitAura();
}

bool BotGroupAI::NeedFlee()
{
	if (m_MeleeFleeTick > 0)
	{
		if (m_MeleeFleeTick > getMSTime())
			return true;
		else
		{
			m_MeleeFleeTick = 0;
			if (me->GetHealthPct() < 25)
			{
				m_MeleeFleeTick = getMSTime() + 2500;
				return true;
			}
			else
			{
				m_Flee.Clear();
				return false;
			}
		}
	}
	if (m_Flee.Fleeing())
	{
		if (me->GetHealthPct() > 25)
		{
			m_Flee.Clear();
			return false;
		}
		return true;
	}
	if (IsMeleeBotAI())
	{
		if (me->GetHealthPct() < 25)
		{
			m_MeleeFleeTick = getMSTime() + 2500;
			return true;
		}
	}
	return false;
}

void BotGroupAI::UpdateTeleport(uint32 diff)
{
    bool pending=!m_Teleporting.CanMovement();
    m_Teleporting.Update(diff,m_Movement);
    if(pending && m_Teleporting.CanMovement() && me->IsInWorld() &&
       !me->IsBeingTeleported() && !me->IsChangeMap() && me->GetMap() &&
       me->GetMap()->IsDungeon())ResetDungeonMovementAfterTeleport();
}

void BotGroupAI::ResetDungeonMovementAfterTeleport()
{
    // V12: only transient AI movement; native roots/fear and explicit stop remain intact.
    m_Movement->ClearMovement();me->GetMotionMaster()->Clear();me->StopMoving();
    m_MechanicOwned=false;m_MechanicMoving=false;m_MechanicTick=0;
    m_MechanicBlockedUntil=0;m_MechanicSearchOffset=0;m_MechanicProgressTime=0;
    m_RdfAvoidMoving=false;m_RdfAvoidScan=0;m_RdfFormationMoving=false;
    m_RdfFormationTick=0;m_RdfFormationProgressTime=0;m_RdfFormationRecoveryAttempt=0;
    m_RdfPositionTick=0;m_RdfPositionAttempt=0;m_RdfRepositionRequested=false;
    m_RdfPathProgressTime=0;m_RdfPathProgressPosition=me->GetPosition();
    m_VaultBridgeLink=-1;m_VaultBridgeStage=-1;m_VaultBridgeIssued=false;
    m_VaultBridgeFallbackLink=-1;m_VaultBridgeFallbackStart=0;
    m_DungeonLeashClock.reset();m_DungeonLeashTick=0;m_RdfStillSince=0;
    m_Flee.Clear();m_MeleeFleeTick=0;m_UpdateTick=0;
    m_TankLeadTick=0;m_TankLeadHandled=false;m_TankLeadAnchor.Clear();
    TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_MOVEMENT_RESUME_V12 account=%u combat=%u stop_follow=%u",me->GetSession()->GetAccountId(),uint32(me->isInCombat()),uint32(m_StopFollow));
}

bool BotGroupAI::SuppressDungeonTankFlee()
{
    if(!m_ForceFlee && me->GetMap() && me->GetMap()->IsDungeon() &&
       RdfRotationPolicy::tank(me->GetSpecializationId())){
        m_Flee.Clear();m_MeleeFleeTick=0;return true;
    }
    return false;
}

void BotGroupAI::FleeMovement()
{
    if(SuppressDungeonTankFlee())return;
	if (/*me->IsStopped() && */!IsNotMovement())
	{
		NearUnitVec& enemys = RangeEnemyListByTargetIsMe(NEEDFLEE_CHECKRANGE);
		Unit* selectEnemy = NULL;
		if (enemys.empty())
		{
			selectEnemy = me->GetSelectedUnit();
			if (!selectEnemy || me->GetDistance(selectEnemy->GetPosition()) > BOTAI_FLEE_JUDGE + 5.0f)
			{
				m_Flee.Clear();
				return;
			}
		}
		if (!selectEnemy && !enemys.empty())
		{
			float nearDist = 999999;
			for (Unit* pEnemy : enemys)
			{
				float dist = me->GetDistance(pEnemy->GetPosition());
				if (dist < nearDist)
				{
					nearDist = dist;
					selectEnemy = pEnemy;
				}
			}
			if (!selectEnemy)
			{
				m_Flee.Clear();
				return;
			}
		}
		m_Flee.UpdateFleeMovementByPVE(m_MasterPlayer, selectEnemy, m_Movement);
	}
	else
		m_Flee.Clear();
}

void BotGroupAI::ProcessFlee()
{
	FleeMovement();
}

bool BotGroupAI::ExecuteSeduceCommand()
{
	if (m_SeduceTarget == ObjectGuid::Empty || !m_MasterPlayer)
		return false;
	Group* pGroup = me->GetGroup();
	Unit* pTarget = ObjectAccessor::GetCreature(*me, m_SeduceTarget);
	if (!pTarget || !pTarget->isAlive() || m_MasterPlayer->isInCombat() ||
		!pTarget->ToCreature() || pTarget->ToCreature()->IsInEvadeMode() ||
		!me->IsInWorld() || me->GetMap() != pTarget->GetMap() ||
		pTarget->ToPlayer() || pTarget->isTotem())
	{
		m_SeduceTarget = ObjectGuid::Empty;
		if (pGroup)
			pGroup->ClearAllGroupForceFleeState();
		return false;
	}
	if (m_MasterPlayer)
	{
		NearCreatureVec nearCreatures;
		SearchCreatureListFromRange(m_MasterPlayer, nearCreatures, BOTAI_RANGESPELL_DISTANCE, false);
		if (!nearCreatures.empty())
		{
			for (Creature* pCreature : nearCreatures)
			{
				if (m_MasterPlayer->IsWithinLOSInMap(pCreature))
				{
					m_SeduceTarget = ObjectGuid::Empty;
					if (pGroup)
						pGroup->ClearAllGroupForceFleeState();
					return false;
				}
			}
		}
		if (m_MasterPlayer->IsWithinLOSInMap(pTarget) && m_MasterPlayer->GetDistance(pTarget) <= BOTAI_RANGESPELL_DISTANCE &&
			pTarget->isInCombat() && pTarget->GetTargetGUID() != ObjectGuid::Empty)
		{
			m_SeduceTarget = ObjectGuid::Empty;
			if (pGroup)
				pGroup->ClearAllGroupForceFleeState();
			return false;
		}
	}

	NearCreatureVec nearCreatures;
	SearchCreatureListFromRange(me, nearCreatures, BOTAI_RANGESPELL_DISTANCE, false);
	if (!nearCreatures.empty())
	{
		float minDist = 9999;
		Creature* nearCreature = NULL;
		for (Creature* pCreature : nearCreatures)
		{
			if (pCreature->IsInEvadeMode() || pCreature->getLevel() <= 1)
				continue;
			if (!pCreature->isInCombat() || pCreature->GetTargetGUID() == ObjectGuid::Empty || pCreature->isTotem())
				continue;
			float dist = me->GetDistance(pCreature->GetPosition());
			if (dist < minDist || nearCreature == NULL)
			{
				minDist = dist;
				nearCreature = pCreature;
			}
		}
		if (nearCreature && nearCreature->GetGUID() != m_SeduceTarget)
		{
			m_SeduceTarget = nearCreature->GetGUID();
			me->SetSelection(m_SeduceTarget);
			return true;
		}
	}

	if (Creature* pCreature = pTarget->ToCreature())
	{
		if (pCreature->isPet() || !me->IsValidAttackTarget(pTarget) || pCreature->IsInEvadeMode())
		{
			m_SeduceTarget = ObjectGuid::Empty;
			if (pGroup)
				pGroup->ClearAllGroupForceFleeState();
			return false;
		}
	}
	if (/*me->IsWithinLOSInMap(pTarget) && */me->GetDistance(pTarget) <= BOTAI_RANGESPELL_DISTANCE &&
		pTarget->isInCombat() && pTarget->GetTargetGUID() != ObjectGuid::Empty)
	{
		if (m_IsRangeBot)
		{
			Unit* pVictim = me->getVictim();
			if (!pVictim || pVictim != pTarget)
				me->Attack(pTarget, false);
			DoRangedAttackIfReady();
		}
		else
		{
			Unit* pVictim = me->getVictim();
			if (!pVictim || pVictim != pTarget)
				me->Attack(pTarget, true);
		}
		ProcessFollowToMaster();
	}
	else if (!IsNotMovement())
		m_Movement->MovementTo(pTarget->GetGUID(), 2.0f);
	if (me->IsWithinLOSInMap(pTarget) && me->GetDistance(pTarget) <= BOTAI_RANGESPELL_DISTANCE)
		ProcessSeduceSpell(pTarget);
	return true;
}

void BotGroupAI::ProcessPrepareHealth()
{
	if (!me->isInCombat())
	{
		ProcessCombat(me->GetSelectedUnit());
		return;
	}
	std::vector<Unit*> preparaTargets;
	NearUnitVec& friends = SearchFriend();
	for (Unit* pUnit : friends)
	{
		if (!pUnit->isInCombat())
			continue;
		if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(pUnit->GetAI()))
		{
			if (pGroupAI->IsTankBotAI())
				preparaTargets.push_back(pUnit);
		}
		else
			continue;
	}
	if (preparaTargets.empty())
	{
		ProcessCombat(me->GetSelectedUnit());
		return;
	}
	Unit* prepareUnit = preparaTargets[urand(0, preparaTargets.size() - 1)];
	if (me->IsWithinLOSInMap(prepareUnit))
	{
		if (me->GetDistance(prepareUnit) > BOTAI_RANGESPELL_DISTANCE - 3)
		{
			if (!IsNotMovement())
				m_Movement->MovementTo(prepareUnit->GetGUID());
			return;
		}
		else
		{
			m_Movement->ClearMovement();
			me->SetInFront(prepareUnit);
			//me->SetFacingToObject(healthPlayer);
			ProcessPrepareHealthSpell(prepareUnit);
			return;
		}
	}
	else if (!IsNotMovement())
	{
		m_Movement->MovementTo(prepareUnit->GetGUID());
		return;
	}
	ProcessCombat(me->GetSelectedUnit());
}

void BotGroupAI::ProcessHealth(bool canMove)
{
    if (ProcessLegionPvp(me->GetSelectedUnit())) return;
	if (me->HasUnitState(UNIT_STATE_CASTING))
		return;
	Unit* cruxHeal = (m_CruxHealTarget != ObjectGuid::Empty) ? ObjectAccessor::GetUnit(*me, m_CruxHealTarget) : NULL;
	if (!cruxHeal || cruxHeal->IsFullHealth() ||
		!cruxHeal->isAlive() || me->GetMap() != cruxHeal->GetMap() ||
		me->GetDistance(cruxHeal) > BOTAI_SEARCH_RANGE * 2)
	{
		m_CruxHealTarget = ObjectGuid::Empty;
		cruxHeal = NULL;
	}
	NearUnitVec& needHealth = SearchNeedHealth(BOTAI_SEARCH_RANGE * 1.6f);
	if (needHealth.empty() && cruxHeal == NULL)
	{
		ProcessPrepareHealth();
		//ProcessCombat(me->GetSelectedUnit());
		return;
	}
	uint32 cruxRate = (cruxHeal == NULL) ? 0 : urand(0, 99);
	Unit* healthPlayer = (cruxRate > 60 || needHealth.empty()) ? cruxHeal : NULL;
	if (needHealth.size() > 0 && (!healthPlayer || cruxRate <= 60))
		healthPlayer = needHealth[urand(0, needHealth.size() - 1)];
	if (!healthPlayer)
	{
		ProcessPrepareHealth();
		//ProcessCombat(me->GetSelectedUnit());
		return;
	}
	bool inView = me->IsWithinLOSInMap(healthPlayer);
	if (inView)
	{
		if (me->GetDistance(healthPlayer) > BOTAI_RANGESPELL_DISTANCE - 3)
		{
			if (canMove && !IsNotMovement())
				m_Movement->MovementTo(healthPlayer->GetPositionX(), healthPlayer->GetPositionY(), healthPlayer->GetPositionZ());
			return;
		}
		else
		{
			if (canMove)
			{
				m_Movement->ClearMovement();
				me->SetInFront(healthPlayer);
				//me->SetFacingToObject(healthPlayer);
			}
			ProcessHealthSpell(healthPlayer);
			return;
		}
	}
	else if (canMove && !IsNotMovement())
	{
		m_Movement->MovementTo(healthPlayer->GetGUID());
		return;
	}
	if (canMove && !SuppressDungeonTankFlee() && NeedFlee())
	{
		ProcessFlee();
		return;
	}
}

void BotGroupAI::ProcessCombat(Unit* pTarget)
{
    if (!FelsoriaDHDummyLimit::CanUse(me, pTarget))
    {
        FelsoriaDHDummyLimit::Enforce(me, m_Movement);
        return;
    }
    if (ProcessLegionPvp(pTarget)) return;
	if (!pTarget || !pTarget->isAlive())
		return;
	if (TargetIsStealth(pTarget->ToPlayer()))
	{
		me->SetSelection(ObjectGuid::Empty);
		return;
	}
	bool inView = me->IsWithinLOSInMap(pTarget);
	if (inView)
	{
		if (pTarget->ToPlayer() && !me->IsPvP())
			BotUtility::PlayerBotTogglePVP(me, true);
		if (me->HasUnitState(UNIT_STATE_CASTING))
			return;
		if (m_IsRangeBot)
		{
			if (!SuppressDungeonTankFlee() && NeedFlee())
			{
				Unit* pVictim = me->getVictim();
				if (!pVictim || pVictim != pTarget || !me->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
					me->Attack(pTarget, true);
				ProcessFlee();
				if (!IsNotSelect(pTarget))
					ProcessMeleeSpell(pTarget);
			}
			else if (me->GetDistance(pTarget) <= BOTAI_RANGESPELL_DISTANCE)
			{
				Unit* pVictim = me->getVictim();
				if (!pVictim || pVictim != pTarget)
					me->Attack(pTarget, false);
				if (!IsNotSelect(pTarget))// && !DoFaceToTarget(pTarget))
				{
					me->SetInFront(pTarget);
					me->SetFacingToObject(pTarget);
					if (me->getClass() != Classes::CLASS_HUNTER)
						m_Movement->ClearMovement();
					ChaseTarget(pTarget, false, BOTAI_RANGESPELL_DISTANCE);
					ProcessRangeSpell(pTarget);
					if (!me->HasUnitState(UNIT_STATE_CASTING))
						DoRangedAttackIfReady();
				}
			}
			else if (!IsNotMovement())
			{
				m_Movement->MovementToTarget();
			}
		}
		else
		{
			Unit* pVictim = me->getVictim();
			if (!pVictim || pVictim != pTarget)
				me->Attack(pTarget, true);
			if (!SuppressDungeonTankFlee() && NeedFlee())
			{
				ProcessFlee();
				if (!IsNotSelect(pTarget))
				{
					if (IsTankBotAI() || me->IsWithinMeleeRange(pTarget))
						ProcessMeleeSpell(pTarget);
					else if (me->GetDistance(pTarget) < BOTAI_RANGESPELL_DISTANCE)
						ProcessRangeSpell(pTarget);
				}
			}
			else if (me->IsWithinMeleeRange(pTarget))
			{
				me->SetInFront(pTarget);
				me->SetFacingToObject(pTarget);
				//if (!DoFaceToTarget(pTarget))
				{
					if (!IsNotMovement())
						ChaseTarget(pTarget, true);
					if (!IsNotSelect(pTarget))
					{
						ProcessMeleeSpell(pTarget);
						DoMeleeAttackIfReady();
					}
				}
			}
			else
			{
				if (!IsNotMovement())
					m_Movement->MovementToTarget();
				if (me->GetDistance(pTarget) < BOTAI_RANGESPELL_DISTANCE)
				{
					ProcessRangeSpell(pTarget);
				}
			}
		}
	}
	else if (!IsNotMovement())
		m_Movement->MovementToTarget();
}

void BotGroupAI::ProcessHalt(Unit* pTarget)
{
	if (!pTarget || !pTarget->isAlive())
		return;
	if (TargetIsStealth(pTarget->ToPlayer()) || IsNotSelect(pTarget))
	{
		me->SetSelection(ObjectGuid::Empty);
		return;
	}
	if (me->HasUnitState(UNIT_STATE_CASTING))
		return;
	bool inView = me->IsWithinLOSInMap(pTarget);
	if (inView)
	{
		Unit* pVictim = me->getVictim();
		if (!pVictim || pVictim != pTarget)
			me->Attack(pTarget, false);
		if (m_IsRangeBot)
		{
			//ChaseTarget(pTarget, false, me->GetDistance(pTarget->GetPosition()));
			ProcessRangeSpell(pTarget);
			DoRangedAttackIfReady();
		}
		else if (me->IsWithinMeleeRange(pTarget))
		{
			//ChaseTarget(pTarget, false, me->GetDistance(pTarget->GetPosition()));
			ProcessMeleeSpell(pTarget);
			DoMeleeAttackIfReady();
		}
		else
		{
			if (me->GetDistance(pTarget) < BOTAI_RANGESPELL_DISTANCE)
			{
				ProcessRangeSpell(pTarget);
			}
		}
	}
}

bool BotGroupAI::ProcessTank(Unit* pTarget)
{
    if(m_MechanicOwned || ShouldBlockBossPull(pTarget))return true;
	Unit* pUnit = NULL;
	pUnit = m_TankTargets.GetNeedPullTarget();
	if (!pUnit)
		pUnit = SearchTankTargetEnemy();
	if (!pUnit)
	{
		if (pTarget && pTarget->isAlive())
			pUnit = pTarget;
		else
		{
			me->SetSelection(ObjectGuid::Empty);
		}
	}
	else
		me->SetSelection(pUnit->GetGUID());
	if (!pUnit)
		return false;
	if (m_TankTargets.UpdateTankTarget(m_Movement))
		return true;
	float distance = me->GetDistance(pUnit->GetPosition());
	if (distance <= 5.0f)
	{
		if (!HasAuraMechanic(pUnit, Mechanics::MECHANIC_FEAR) && !HasAuraMechanic(pUnit, Mechanics::MECHANIC_HORROR) && !HasAuraMechanic(pUnit, Mechanics::MECHANIC_STUN))
		{
			ObjectGuid& guid = pUnit->GetTargetGUID();
			if (guid.IsPlayer())
			{
				Player* pUnitTarget = ObjectAccessor::FindPlayer(guid);
				if (pUnitTarget && me != pUnitTarget)
				{
					if (!pUnitTarget->IsTankPlayer() || pUnitTarget->GetTargetGUID() != pUnit->GetGUID())
						ProcessPullSpell(pUnit);
				}
			}
			else
				ProcessPullSpell(pUnit);
		}
		ProcessCombat(pUnit);
	}
	else
	{
		if (distance >= NEEDFLEE_CHECKRANGE && distance < BOTAI_RANGESPELL_DISTANCE)
			ProcessRangeSpell(pUnit);
		m_Movement->MovementToTarget();
	}
	return true;
}

void BotGroupAI::ChaseTarget(Unit* pTarget, bool isMelee, float range)
{
	if (IsNotSelect(pTarget))
		return;
	if (isMelee && me->InBattleground())
	{
		if (me->IsStopped())
		{
			Position& targetPos = pTarget->GetPosition();
			float rndOffset = frand(-float(M_PI_4) * 0.75f, float(M_PI_4) * 0.75f);
			Position& pos = me->GetFirstCollisionPosition(me->GetDistance(targetPos) + range, me->GetRelativeAngle(&targetPos) + rndOffset);
			m_Movement->MovementTo(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
			//me->GetMotionMaster()->MovePoint(0, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
		}
	}
	else
	{
		//if (me->isInBack(pTarget) && !IsNotMovement())// (fabsf(selfAngle) > M_PI_4)
		//{
		//	Movement::MoveSplineInit init(me);
		//	init.MoveTo(me->GetPositionX(), me->GetPositionY(), me->GetPositionZMinusOffset());
		//	init.SetFacing(pTarget);
		//	init.SetOrientationFixed(true);
		//	init.Launch();
		//}
		//else// if (!IsNotMovement())
		if (me->GetDistance(pTarget->GetPosition()) > range)
		{
			me->GetMotionMaster()->Clear();
			me->GetMotionMaster()->MoveChase(pTarget, range);
		}
		else
		{
			m_Movement->ClearMovement();
		}
	}
}

void BotGroupAI::SearchCreatureListFromRange(Unit* center, NearCreatureVec& nearCreatures, float range, bool selfFaction)
{
	NearCreatureList nearCreature;
	Trinity::AllWorldObjectsInRange checker(center, range);
	Trinity::CreatureListSearcher<Trinity::AllWorldObjectsInRange> searcher(center, nearCreature, checker);
	center->VisitNearbyGridObject(range, searcher);
	for (Creature* pCreature : nearCreature)
	{
        if (!selfFaction && !FelsoriaDHDummyLimit::CanUse(me, pCreature))
            continue;
		if (!pCreature->isAlive() || !pCreature->IsVisible() || pCreature->getLevel() <= 1 ||
			m_FliterCreatures.IsFliterCreature(pCreature) || pCreature->isPet())// || pCreature->IsTotem())
			continue;
		if (pCreature->IsInEvadeMode())
			continue;
		if (selfFaction && me->IsValidAttackTarget(pCreature))
			continue;
		if (!selfFaction && !me->IsValidAttackTarget(pCreature))
			continue;
		nearCreatures.push_back(pCreature);
	}
}

void BotGroupAI::ToggleFliterCreature(Creature* pCreature, bool fliter)
{
	if (!pCreature)
		return;
	if (fliter)
		m_FliterCreatures.UpdateFliterCreature(pCreature);
	else
		m_FliterCreatures.RemoveFliterCreature(pCreature);
}

NearUnitVec BotGroupAI::SearchFriend(float range)
{
	NearPlayerList playersNearby;
	NearUnitVec friendNearby;
	me->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() == me->GetTeamId())
		{
			friendNearby.push_back(pVisionPlayer);
		}
	}
	//NearCreatureVec creatures;
	//SearchCreatureListFromRange(me, creatures, range, true);
	//for (Creature* pCreature : creatures)
	//	friendNearby.push_back(pCreature);
	return friendNearby;
}


NearUnitVec BotGroupAI::SearchFriendTargetIsTarget(Unit* pTarget, float range)
{
	NearPlayerList playersNearby;
	NearUnitVec friendNearby;
	pTarget->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!pVisionPlayer->IsPlayerBot())
			continue;
		if (pVisionPlayer->GetTargetGUID() != pTarget->GetGUID())
			continue;
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() == me->GetTeamId())
		{
			friendNearby.push_back(pVisionPlayer);
		}
	}
	return friendNearby;
}

NearPlayerVec BotGroupAI::SearchFarFriend(float minRange, float maxRange, bool isIDLE)
{
	NearPlayerList playersNearby;
	NearPlayerVec friendNearby;
	me->GetPlayerListInGrid(playersNearby, maxRange);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() == me->GetTeamId())
		{
			if (me->GetDistance(pVisionPlayer->GetPosition()) > minRange)
			{
				if (isIDLE)
				{
					if (pVisionPlayer->GetSelectedUnit() == NULL || !pVisionPlayer->isInCombat())
						friendNearby.push_back(pVisionPlayer);
				}
				else
					friendNearby.push_back(pVisionPlayer);
			}
		}
	}
	return friendNearby;
}

NearPlayerVec BotGroupAI::ExistFriendAttacker(float range /* = BOTAI_RANGESPELL_DISTANCE */)
{
	NearPlayerList playersNearby;
	NearPlayerVec friendNearby;
	me->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() == me->GetTeamId() && IsAttacker())
		{
			friendNearby.push_back(pVisionPlayer);
		}
	}

	return friendNearby;
}

NearUnitVec BotGroupAI::SearchNeedHealth(float range /* = BOTAI_SEARCH_RANGE */)
{
	//NearPlayerList playersNearby;
	NearUnitVec lifeTo25, life25To55, life55To80;
	//me->GetPlayerListInGrid(playersNearby, range);
	Group* pGroup = me->GetGroup();
	if (!pGroup || pGroup->isBGGroup())
	{
		float healthPct = me->GetHealthPct();
		if (healthPct < 25)
			lifeTo25.push_back(me);
		else if (healthPct >= 25 && healthPct < 55)
			life25To55.push_back(me);
		else if (healthPct >= 55 && healthPct < 80)
			life55To80.push_back(me);
	}
	else
	{
		Group::MemberSlotList const& memList = pGroup->GetMemberSlots();
		for (Group::MemberSlot const& slot : memList)
		{
			Player* player = ObjectAccessor::FindPlayer(slot.Guid);
			if (!player || IsNotSelect(player) || (me->GetMap() != player->GetMap()))
				continue;
			if(me->GetDistance(player->GetPosition()) > range)
				continue;
			float healthPct = player->GetHealthPct();
			if (healthPct < 25)
				lifeTo25.push_back(player);
			else if (healthPct >= 25 && healthPct < 55)
				life25To55.push_back(player);
			else if (healthPct >= 55 && healthPct < 80)
				life55To80.push_back(player);
		}
	}
	//for (Player* pVisionPlayer : playersNearby)
	//{
	//	if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() == me->GetTeamId())
	//	{
	//		float healthPct = pVisionPlayer->GetHealthPct();
	//		if (healthPct < 25)
	//			lifeTo25.push_back(pVisionPlayer);
	//		else if (healthPct >= 25 && healthPct < 55)
	//			life25To55.push_back(pVisionPlayer);
	//		else if (healthPct >= 55 && healthPct < 80)
	//			life55To80.push_back(pVisionPlayer);
	//	}
	//}
	//NearCreatureVec creatures;
	//SearchCreatureListFromRange(me, creatures, range, true);
	//for (Creature* pCreature : creatures)
	//{
	//	float healthPct = pCreature->GetHealthPct();
	//	if (healthPct < 25)
	//		lifeTo25.push_back(pCreature);
	//	else if (healthPct >= 25 && healthPct < 55)
	//		life25To55.push_back(pCreature);
	//	else if (healthPct >= 55 && healthPct < 80)
	//		life55To80.push_back(pCreature);
	//}

	uint32 rate = urand(0, 99);
	if (rate >= 85 && life55To80.size() > 0)
		return life55To80;
	else if (rate < 85 && rate >= 55 && life25To55.size() > 0)
		return life25To55;
	else if (lifeTo25.size() > 0)
		return lifeTo25;
	else if (life25To55.size() > 0)
		return life25To55;
	return life55To80;
}

NearUnitVec BotGroupAI::SearchLifePctByFriendRange(Unit* pTarget, float lifePct, float range /* = NEEDFLEE_CHECKRANGE */)
{
	NearPlayerList playersNearby;
	NearUnitVec lifePctPlayers;
	pTarget->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() == me->GetTeamId())
		{
			float healthPct = pVisionPlayer->GetHealthPct();
			if (healthPct <= lifePct)
				lifePctPlayers.push_back(pVisionPlayer);
		}
	}
	//NearCreatureVec creatures;
	//SearchCreatureListFromRange(me, creatures, range, true);
	//for (Creature* pCreature : creatures)
	//{
	//	float healthPct = pCreature->GetHealthPct();
	//	if (healthPct <= lifePct)
	//		lifePctPlayers.push_back(pCreature);
	//}

	return lifePctPlayers;
}

Unit* BotGroupAI::RandomRangeEnemyByCasting(float range)
{
	NearUnitVec enemyPlayers;
	NearPlayerList playersNearby;
	me->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() != me->GetTeamId())
		{
			if (pVisionPlayer->HasUnitState(UNIT_STATE_CASTING))
				enemyPlayers.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(me, creatures, range, false);
	for (Creature* pCreature : creatures)
	{
		if (pCreature->HasUnitState(UNIT_STATE_CASTING))
			enemyPlayers.push_back(pCreature);
	}
	if (!enemyPlayers.empty())
	{
		uint32 index = urand(0, enemyPlayers.size() - 1);
		return enemyPlayers[index];
	}
	return NULL;
}

Unit* BotGroupAI::SearchTankTargetEnemy(float range)
{
	if (!me->IsInWorld())
		return NULL;
	NearUnitVec& enemys = RangeEnemyListByHasAura(0, range);
	if (enemys.empty())
		return NULL;
	Group* pGroup = me->GetGroup();
	if (!pGroup)
		return NULL;
	std::map<Player*, std::vector<Unit*> > otherTankPullTargets;
	float minDistance = 9999;
	Unit* needUnit = NULL;
	float targetMeDist = 9999;
	Unit* targetMeUnit = NULL;
	for (Unit* pUnit : enemys)
	{
		if (!pUnit->isInCombat())
			continue;
		if (TargetIsControl(pUnit))
			continue;
		if (TargetIsStealth(pUnit->ToPlayer()))
			continue;
		ObjectGuid& guid = pUnit->GetTargetGUID();
		if (guid == ObjectGuid::Empty)
			continue;
		if (guid == me->GetGUID())
		{
			float meDist = me->GetDistance(pUnit->GetPosition());
			if (meDist < targetMeDist)
			{
				targetMeDist = meDist;
				targetMeUnit = pUnit;
			}
			continue;
		}
		if (guid.IsPlayer())
		{
			if (!pGroup->IsMember(guid))
				continue;
			Player* player = ObjectAccessor::FindPlayer(guid);
			if (!player)
				continue;
			if (pUnit->GetDistance(player->GetPosition()) > BOTAI_FLEE_JUDGE)
				continue;
			BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI());
			if (pGroupAI && pGroupAI->IsTankBotAI() && !pGroupAI->IsFleeing())
			{
				std::vector<Unit*>& targets = otherTankPullTargets[player];
				targets.push_back(pUnit);
				continue;
			}
			//if (player && player->IsTankPlayer())
			//	continue;
		}
		if(IsNotSelect(pUnit) || IsInvincible(pUnit))
			continue;
		float dist = me->GetDistance(pUnit->GetPosition());
		if (dist < minDistance)
		{
			minDistance = dist;
			needUnit = pUnit;
		}
	}

	if (BotGroupAI::PVE_DRIVING && !me->GetMap()->IsDungeon())
	{
		if (pGroup->AllGroupIsIDLE())
		{
			minDistance = 9999;
			needUnit = NULL;
			if (!enemys.empty() && needUnit == NULL && targetMeUnit == NULL)
			{
				for (Unit* pUnit : enemys)
				{
					if (IsNotSelect(pUnit) || IsInvincible(pUnit) || TargetIsControl(pUnit))
						continue;
					ObjectGuid& guid = pUnit->GetTargetGUID();
					if (guid != ObjectGuid::Empty && guid.IsPlayer())
					{
						if (!pGroup->IsMember(guid))
							continue;
						Player* player = ObjectAccessor::FindPlayer(guid);
						if (player && player->IsTankPlayer())
							continue;
					}
					float dist = me->GetDistance(pUnit->GetPosition());
					if (dist < minDistance)
					{
						minDistance = dist;
						needUnit = pUnit;
					}
				}
			}
		}
	}
	if (needUnit)
		return needUnit;
	else if (targetMeUnit)
		return targetMeUnit;
	else if (otherTankPullTargets.size() > 0)
	{
		uint32 maxTargets = 0;
		Unit* needTarget = NULL;
		for (std::map<Player*, std::vector<Unit*> >::iterator itTargets = otherTankPullTargets.begin();
			itTargets != otherTankPullTargets.end();
			itTargets++)
		{
			std::vector<Unit*>& targets = itTargets->second;
			uint32 count = targets.size();
			if (count <= 1)
				continue;
			if (count > maxTargets || !needTarget)
			{
				maxTargets = count;
				needTarget = targets[urand(0, count - 1)];
			}
		}
		return needTarget;
	}
	return NULL;
}

NearUnitVec BotGroupAI::RangeEnemyListByHasAura(uint32 aura, float range)
{
	NearUnitVec enemyPlayers;
	NearPlayerList playersNearby;
	me->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() != me->GetTeamId())
		{
			if (TargetIsStealth(pVisionPlayer))
				continue;
			if (aura == 0 || pVisionPlayer->HasAura(aura))
				enemyPlayers.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(me, creatures, range, false);
	for (Creature* pCreature : creatures)
	{
		if (aura == 0 || pCreature->HasAura(aura))
			enemyPlayers.push_back(pCreature);
	}
	return enemyPlayers;
}

NearUnitVec BotGroupAI::RangeEnemyListByNonAura(uint32 aura, float range)
{
	NearUnitVec enemyPlayers;
	if (aura == 0)
		return enemyPlayers;
	NearPlayerList playersNearby;
	me->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() != me->GetTeamId())
		{
			if (TargetIsStealth(pVisionPlayer))
				continue;
			if (!pVisionPlayer->HasAura(aura))
				enemyPlayers.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(me, creatures, range, false);
	for (Creature* pCreature : creatures)
	{
		if (!pCreature->HasAura(aura))
			enemyPlayers.push_back(pCreature);
	}
	return enemyPlayers;
}

NearUnitVec BotGroupAI::RangeEnemyListByTargetIsMe(float range)
{
	NearUnitVec enemyPlayers;
	NearPlayerList playersNearby;
	me->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() != me->GetTeamId())
		{
			if (TargetIsStealth(pVisionPlayer))
				continue;
			Unit* pUnit = pVisionPlayer->GetSelectedUnit();
			if (pUnit && pUnit->GetGUID() == me->GetGUID())
				enemyPlayers.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(me, creatures, range, false);
	for (Creature* pCreature : creatures)
	{
		if (pCreature->GetTargetGUID() == me->GetGUID())
			enemyPlayers.push_back(pCreature);
	}
	return enemyPlayers;
}

NearUnitVec BotGroupAI::RangeListByTargetIsTarget(Unit* pTarget, float range)
{
	NearUnitVec enemyPlayers;
	NearPlayerList playersNearby;
	pTarget->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() == me->GetTeamId())
		{
			if (TargetIsStealth(pVisionPlayer))
				continue;
			if (pVisionPlayer->GetSelectedUnit() == pTarget)
				enemyPlayers.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(pTarget, creatures, range, false);
	for (Creature* pCreature : creatures)
	{
		if (pCreature->GetTargetGUID() == pTarget->GetGUID())
			enemyPlayers.push_back(pCreature);
	}
	return enemyPlayers;
}

NearUnitVec BotGroupAI::RangeEnemyListByTargetRange(Unit* pTarget, float range)
{
	NearUnitVec enemyPlayers;
	NearPlayerList playersNearby;
	pTarget->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() != me->GetTeamId())
		{
			if (TargetIsStealth(pVisionPlayer))
				continue;
			enemyPlayers.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(pTarget, creatures, range, false);
	for (Creature* pCreature : creatures)
		enemyPlayers.push_back(pCreature);
	return enemyPlayers;
}

NearUnitVec BotGroupAI::SearchFarEnemy(float minRange, float maxRange)
{
	NearUnitVec enemyNearby;
	NearCreatureVec creatures;
	SearchCreatureListFromRange(me, creatures, maxRange, false);
	for (Creature* pCreature : creatures)
	{
		if (me->GetDistance(pCreature->GetPosition()) > minRange)
			enemyNearby.push_back(pCreature);
	}
	return enemyNearby;
}

bool BotGroupAI::IsMeleeBotAI()
{
	switch (me->getClass())
	{
	case CLASS_WARRIOR:
	case CLASS_PALADIN:
	case CLASS_ROGUE:
	case CLASS_DEATH_KNIGHT:
	case CLASS_SHAMAN:
	case CLASS_DRUID:
		return true;
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_PRIEST:
	case CLASS_HUNTER:
		return false;
	}
	return true;
}

bool BotGroupAI::IsRangeBotAI()
{
	switch (me->getClass())
	{
	case CLASS_WARRIOR:
	case CLASS_PALADIN:
	case CLASS_ROGUE:
	case CLASS_DEATH_KNIGHT:
		return false;
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_PRIEST:
	case CLASS_HUNTER:
	case CLASS_SHAMAN:
	case CLASS_DRUID:
		return true;
	}
	return false;
}

bool BotGroupAI::IsHealerBotAI()
{
	switch (me->getClass())
	{
	case CLASS_WARRIOR:
	case CLASS_ROGUE:
	case CLASS_DEATH_KNIGHT:
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_HUNTER:
		return false;
	case CLASS_PALADIN:
	case CLASS_PRIEST:
	case CLASS_SHAMAN:
	case CLASS_DRUID:
		return true;
	}
	return false;
}

Unit* BotGroupAI::GetBotAIValidSelectedUnit()
{
    if(ShouldBlockBossPull(me->GetSelectedUnit())){me->SetSelection(ObjectGuid::Empty);return nullptr;}
	Unit* pTarget = me->GetSelectedUnit();
	bool isValid = true;
	if (!pTarget)
		isValid = false;
	if (isValid && !pTarget->IsVisible())
		isValid = false;
	if (isValid && !me->InSamePhase(pTarget->GetPhaseMask()))
		isValid = false;
	if (isValid && IsNotSelect(pTarget))
		isValid = false;
	if (isValid && TargetIsControl(pTarget))
		isValid = false;
	if (isValid && m_FliterCreatures.IsFliterCreature(pTarget->ToCreature()))
		isValid = false;
	if (!isValid)
	{
		me->AttackStop();
		me->SetSelection(ObjectGuid::Empty);
		return NULL;
	}
	return pTarget;
}

bool BotGroupAI::TargetIsControl(Unit* pTarget)
{
	if (!pTarget)
		return false;
	Creature* pCreature = pTarget->ToCreature();
	if (pCreature && pCreature->IsInEvadeMode())
		return true;
	if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_CHARM) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_DISORIENTED) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_DISTRACT) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_SLEEP) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_POLYMORPH) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_BANISH)/* ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_IMMUNE_SHIELD)*/)
		return true;
	return false;
}

bool BotGroupAI::TargetIsMelee(Player* pTarget)
{
	if (!pTarget)
		return false;
	switch (pTarget->getClass())
	{
	case CLASS_PALADIN:
	case CLASS_WARRIOR:
	case CLASS_ROGUE:
	case CLASS_DEATH_KNIGHT:
		return true;
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_HUNTER:
	case CLASS_PRIEST:
	case CLASS_DRUID:
	case CLASS_SHAMAN:
		return false;
	}
	return false;
}

bool BotGroupAI::TargetIsRange(Player* pTarget)
{
	if (!pTarget)
		return false;
	switch (pTarget->getClass())
	{
	case CLASS_WARRIOR:
	case CLASS_PALADIN:
	case CLASS_ROGUE:
	case CLASS_DEATH_KNIGHT:
		return false;
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_HUNTER:
	case CLASS_PRIEST:
	case CLASS_SHAMAN:
	case CLASS_DRUID:
		return true;
	}
	return false;
}

bool BotGroupAI::TargetIsMagic(Player* pTarget)
{
	if (!pTarget)
		return false;
	switch (pTarget->getClass())
	{
	case CLASS_WARRIOR:
	case CLASS_ROGUE:
	case CLASS_DEATH_KNIGHT:
		return false;
	case CLASS_PALADIN:
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_HUNTER:
	case CLASS_PRIEST:
	case CLASS_SHAMAN:
	case CLASS_DRUID:
		return true;
	}
	return false;
}

bool BotGroupAI::TargetIsCastMagic(Player* pTarget)
{
	if (!pTarget)
		return false;
	switch (pTarget->getClass())
	{
	case CLASS_WARRIOR:
	case CLASS_ROGUE:
	case CLASS_DEATH_KNIGHT:
	case CLASS_PALADIN:
	case CLASS_HUNTER:
		return false;
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_PRIEST:
	case CLASS_SHAMAN:
	case CLASS_DRUID:
		return true;
	}
	return false;
}

bool BotGroupAI::TargetIsStealth(Player* pTarget)
{
	if (!pTarget)
		return false;
	// (1784 µÁÔôÇ±ĐĐ || 5215 µÂÂłŇÁÇ±ĐĐ || 66 ·¨Ę¦ŇţĐÎ || 58984 °µŇąŇţ¶Ý)
	if (pTarget->HasAura(1784) || pTarget->HasAura(5215) ||
		pTarget->HasAura(66) || pTarget->HasAura(58984))
	{
		if (!me->canSeeOrDetect(pTarget, false, true)) // Őě˛âÇ±ĐĐ
			return true;
	}
	return false;
}



#include "RdfLegionRotation.inc"
#include "RdfAvoidance.inc"
#include "RdfCombatPosition.inc"

#include "RdfTravelRegroup.inc"

// FELSORIA_VAULT_RETURN_ACK_V1: platform regions + actual walking bridge corridors.
namespace FelsoriaVaultBridge
{
    struct Point { float x, y, z; };
    struct Link {
        Point entrance, arena, center;
        float minX,maxX,minY,maxY,radius;
    };
    static Link const links[] = {
        // Glazer: observed platform lips, each extended 3 yards inland.
        {{4451.3467f,-612.7415f,117.1718f},{4450.7944f,-628.8228f,117.1496f},
            {4451.21f,-673.26f,117.16f},4420.0f,4485.0f,-620.0f,-570.0f,56.0f},
        // Ashgolm: X4291.582520/Y-451.607849 and X4274.137695/Y-450.952881.
        // Aprons extend 3 yards inland; the boss script provides arena center.
        {{4294.5804f,-451.7204f,105.5185f},{4271.1398f,-450.8403f,106.3993f},
            {4239.14f,-451.332f,105.947f},4284.0f,4350.0f,-494.0f,-409.0f,46.0f}
    };
    static float Distance2(Point const& a,Point const& b)
    {
        return (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y);
    }
    static float Progress(Link const& link,Point const& p)
    {
        float dx=link.arena.x-link.entrance.x,dy=link.arena.y-link.entrance.y;
        return ((p.x-link.entrance.x)*dx+(p.y-link.entrance.y)*dy)/(dx*dx+dy*dy);
    }
    static bool OnFloor(Link const& link,Point const& p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
            std::fabs(p.z-link.center.z)<8.0f;
    }
    static bool EntrancePlatform(Link const& link,Point const& p)
    {
        return OnFloor(link,p) && p.x>=link.minX && p.x<=link.maxX &&
            p.y>=link.minY && p.y<=link.maxY && Progress(link,p)<0.45f;
    }
    // FELSORIA_VAULT_RESCUE_V4: the leader may keep walking off the apron.
    static bool EntranceSide(Link const& link,Point const& p)
    {
        return EntrancePlatform(link,p) || (OnFloor(link,p) &&
            Progress(link,p)<=0.05f && Distance2(p,link.entrance)<=90.0f*90.0f);
    }
    static bool BossPlatform(Link const& link,Point const& p)
    {
        return OnFloor(link,p) && Distance2(p,link.center)<=link.radius*link.radius && Progress(link,p)>0.55f;
    }
    static bool Corridor(Link const& link,Point const& p)
    {
        float dx=link.arena.x-link.entrance.x,dy=link.arena.y-link.entrance.y;
        float lateral=(p.x-link.entrance.x)*dy-(p.y-link.entrance.y)*dx;
        float t=Progress(link,p);
        return OnFloor(link,p) && std::fabs(p.z-link.center.z)<2.5f &&
            t>=-0.06f && t<=1.06f && lateral*lateral<=1.75f*1.75f*(dx*dx+dy*dy);
    }
}

// FELSORIA_VAULT_BRIDGES_V11: independent per-bot/per-bridge rescue state.
bool BotGroupAI::VaultBridgeObservedLanding(Position const& destination,uint32 now) const
{
    using namespace FelsoriaVaultBridge;
    if (!m_VaultGroundedObserved || uint32(now-m_VaultGroundedSince)<1000 ||
        !VaultBridgeLandingAllowed(destination))return false;
    Point p={destination.GetPositionX(),destination.GetPositionY(),destination.GetPositionZ()};
    Link const& link=links[m_VaultRescueIndex];
    if (!OnFloor(link,p))return false;
    float dx=p.x-m_VaultGroundedPosition.GetPositionX(),dy=p.y-m_VaultGroundedPosition.GetPositionY(),dz=p.z-m_VaultGroundedPosition.GetPositionZ();
    return dx*dx+dy*dy+dz*dz<=0.0625f;
}

bool BotGroupAI::VaultBridgeLandingAllowed(Position const& destination) const
{
    using namespace FelsoriaVaultBridge;
    if (m_VaultRescueIndex<0 || m_VaultRescueIndex>=2)return false;
    Point p={destination.GetPositionX(),destination.GetPositionY(),destination.GetPositionZ()};
    Link const& link=links[m_VaultRescueIndex];
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))return false;
    return m_VaultCrossingWatch[m_VaultRescueIndex].reverse ? Progress(link,p)<=0.05f : Progress(link,p)>=0.95f;
}

bool BotGroupAI::ProcessVaultBridgeWatchdog()
{
    using namespace FelsoriaVaultBridge;
    auto resetAll=[&](){for(auto& clock:m_VaultCrossingWatch)clock.reset();m_VaultRescueIndex=-1;m_VaultGroundedObserved=false;};
    Player* leader=m_MasterPlayer;
    if (!me->IsInWorld() || !me->isAlive() || me->GetMapId()!=1493 || !me->GetGroup() ||
        !me->GetSession() || !me->GetSession()->IsBotSession() ||
        !leader || !leader->GetSession() || leader->GetSession()->IsBotSession() ||
        leader->GetGroup()!=me->GetGroup()) {resetAll();return false;}
    // Temporary player transfer/airborne movement must not erase a crossing.
    if (!leader->IsInWorld() || !leader->isAlive() || !leader->GetMap())return false;
    if (leader->GetMap()!=me->GetMap() || !me->InSamePhase(leader)) {resetAll();return false;}
    if (m_VaultWatchLeader!=leader->GetGUID() || m_VaultWatchInstance!=me->GetInstanceId()) {
        resetAll();m_VaultWatchLeader=leader->GetGUID();m_VaultWatchInstance=me->GetInstanceId();
    }
    uint32 const now=getMSTime();
    Point bot={me->GetPositionX(),me->GetPositionY(),me->GetPositionZ()};
    Point human={leader->GetPositionX(),leader->GetPositionY(),leader->GetPositionZ()};
    bool const grounded=!leader->IsBeingTeleported() && !leader->IsChangeMap() &&
        !leader->IsFlying() && !leader->isMoving() && !leader->GetTransport() &&
        !leader->GetVehicleBase() && !leader->isInFlight() &&
        !leader->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
    if (!grounded)m_VaultGroundedObserved=false;
    else {
        float dx=human.x-m_VaultGroundedPosition.GetPositionX(),dy=human.y-m_VaultGroundedPosition.GetPositionY(),dz=human.z-m_VaultGroundedPosition.GetPositionZ();
        if (!m_VaultGroundedObserved || dx*dx+dy*dy+dz*dz>0.0625f) {
            m_VaultGroundedObserved=true;m_VaultGroundedSince=now;m_VaultGroundedPosition=leader->GetPosition();
        }
    }
    for(unsigned i=0;i<2;++i) {
        auto& clock=m_VaultCrossingWatch[i];Link const& link=links[i];
        float const bp=Progress(link,bot),hp=Progress(link,human);
        if (clock.pending) {
            if (!m_Teleporting.CanMovement() || me->IsBeingTeleported() || me->IsChangeMap()) {
                if (uint32(now-m_VaultWatchLog[i])>=5000) {
                    m_VaultWatchLog[i]=now;
                    TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_VAULT_RESCUE_ACK_WAIT_V11 account=%u bridge=%u reverse=%u",me->GetSession()->GetAccountId(),i+1,uint32(clock.reverse));
                }
                continue; // The normal teleport state machine must keep receiving updates.
            }
            Position const& goal=m_VaultWatchLanding[i];
            float dx=bot.x-goal.GetPositionX(),dy=bot.y-goal.GetPositionY(),dz=bot.z-goal.GetPositionZ();
            bool const correctSide=clock.reverse?bp<=0.05f:bp>=0.95f;
            if (dx*dx+dy*dy+dz*dz<=1.0f && correctSide) {
                TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_VAULT_RESCUE_DONE_V11 account=%u bridge=%u reverse=%u",me->GetSession()->GetAccountId(),i+1,uint32(clock.reverse));
                clock.reset();m_VaultRescueIndex=-1;
                continue;
            }
            clock.pending=false;
            TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_VAULT_RESCUE_RETRY_V11 account=%u bridge=%u reverse=%u reason=arrival_not_confirmed",me->GetSession()->GetAccountId(),i+1,uint32(clock.reverse));
        }
        if (!clock.active) {
            // Wider aprons catch bots stranded beside the narrow walking corridor.
            bool const forwardSource=OnFloor(link,bot) && bp<0.95f && Distance2(bot,link.entrance)<=90.0f*90.0f;
            bool const reverseSource=OnFloor(link,bot) && bp>0.05f && Distance2(bot,link.center)<=(link.radius+15.0f)*(link.radius+15.0f);
            bool const forwardGoal=OnFloor(link,human) && hp>=0.95f && Distance2(human,link.arena)<=180.0f*180.0f;
            bool const reverseGoal=OnFloor(link,human) && hp<=0.05f && Distance2(human,link.entrance)<=180.0f*180.0f;
            if (!(forwardSource && forwardGoal) && !(reverseSource && reverseGoal))continue;
            clock.begin(reverseSource && reverseGoal,bp,now);
            TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_VAULT_WATCH_START_V11 account=%u bridge=%u reverse=%u",me->GetSession()->GetAccountId(),i+1,uint32(clock.reverse));
        }
        // Only actual arrival on the target side completes an ordinary walk.
        bool const arrived=OnFloor(link,bot) && (clock.reverse?bp<=0.05f:bp>=0.95f);
        if (arrived) {clock.reset();continue;}
        // A human intentionally returning to the source side cancels that direction.
        if (OnFloor(link,human) && (clock.reverse ? BossPlatform(link,human) : EntranceSide(link,human))) {
            clock.reset();continue;
        }
        clock.observe(bp,now); // Lateral jitter, path retries and dodge cannot renew this deadline.
        if (!clock.due(now) || !clock.canAttempt(now))continue;
        if (me->IsBeingTeleported() || me->IsChangeMap() || !m_Teleporting.CanMovement() ||
            leader->IsBeingTeleported() || leader->IsChangeMap() ||
            me->GetTransport() || me->GetVehicleBase() || me->isInFlight() ||
            leader->GetTransport() || leader->GetVehicleBase() || leader->isInFlight())continue;
        // The selected landing must be past this bridge, even if the human kept walking.
        m_VaultRescueIndex=int(i);
        clock.attempt(now);
        if (ProcessDungeonLeash(true)) {
            clock.pending=true;m_VaultWatchLanding[i]=m_DungeonLeashDestination;
            TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_VAULT_RESCUE_QUEUED_V11 account=%u bridge=%u reverse=%u waited=%u",me->GetSession()->GetAccountId(),i+1,uint32(clock.reverse),uint32(now-clock.started));
            return true;
        }
        if (uint32(now-m_VaultWatchLog[i])>=5000) {
            m_VaultWatchLog[i]=now;
            TC_LOG_INFO(LOG_FILTER_LFG,"FELSORIA_VAULT_RESCUE_WAIT_V11 account=%u bridge=%u reverse=%u reason=no_safe_destination",me->GetSession()->GetAccountId(),i+1,uint32(clock.reverse));
        }
    }
    return false;
}

bool BotGroupAI::ProcessRdfVaultBridges()
{
    using namespace FelsoriaVaultBridge;
    uint32 const now=getMSTime();
    Player* leader=m_MasterPlayer;
    // FELSORIA_VAULT_BRIDGE_DIAGNOSTIC_V1: record why a crossing was rejected.
    char const* failure="unknown";
    Point probe={me->GetPositionX(),me->GetPositionY(),me->GetPositionZ()};
    float measuredFloor=0.0f;
    auto cancel=[&](bool failed) {
        if (m_VaultBridgeStage>=0) {
            if (failed)
                TC_LOG_INFO(LOG_FILTER_LFG,"RDF_VAULT_BRIDGE_WAIT account=%u bridge=%u reverse=%u leg=%u difficulty=%u reason=%s bot=(%.3f,%.3f,%.3f) probe=(%.3f,%.3f,%.3f) floor=%.3f",
                    me->GetSession()->GetAccountId(),uint32(m_VaultBridgeLink+1),uint32(m_VaultBridgeReverse),uint32(m_VaultBridgeStage),
                    uint32(me->GetMap()->GetDifficultyID()),failure,
                    me->GetPositionX(),me->GetPositionY(),me->GetPositionZ(),
                    probe.x,probe.y,probe.z,measuredFloor);
            // Raw walking splines have no PointMovementGenerator finalizer that
            // could relocate a player to an unvisited endpoint on cancellation.
            me->StopMoving();
            m_VaultBridgeStage=-1;
            m_VaultBridgeLink=-1;
            m_VaultBridgeIssued=false;
            m_RdfFormationMoving=false;
            m_RdfFormationTick=0;
            m_VaultBridgeRetry=failed ? now : 0;
        }
        if (!failed) {
            m_VaultBridgeFallbackLink=-1;
            m_VaultBridgeFallbackStart=0;
            m_VaultBridgeProgressTime=0;
        }
    };
    if (me->GetMapId()!=1493 || !me->IsInWorld() || !me->isAlive() ||
        !me->GetGroup() || !leader || !leader->IsInWorld() || !leader->isAlive() ||
        !leader->GetSession() || leader->GetSession()->IsBotSession() ||
        leader->GetGroup()!=me->GetGroup() || leader->GetMap()!=me->GetMap() ||
        !me->InSamePhase(leader) || me->IsBeingTeleported() || leader->IsBeingTeleported() ||
        me->IsChangeMap() || leader->IsChangeMap() || me->GetTransport() || leader->GetTransport() ||
        me->GetVehicleBase() || leader->GetVehicleBase() || me->isInFlight() || leader->isInFlight() ||
        me->IsFlying() || leader->IsFlying() || m_StopFollow || m_ForceFlee ||
        me->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING) ||
        me->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR) ||
        leader->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR)) {
        cancel(false); return false;
    }
    Point bot={me->GetPositionX(),me->GetPositionY(),me->GetPositionZ()};
    Point human={leader->GetPositionX(),leader->GetPositionY(),leader->GetPositionZ()};
    if (m_VaultBridgeStage<0) {
        bool found=false;
        for (int i=0;i<2;++i) {
            Link const& link=links[i];
            bool forward=BossPlatform(link,human) &&
                (EntranceSide(link,bot) || (Corridor(link,bot) && Progress(link,bot)<0.95f));
            bool reverse=EntranceSide(link,human) &&
                (BossPlatform(link,bot) || (Corridor(link,bot) && Progress(link,bot)>0.05f));
            if (!forward && !reverse) continue;
            m_VaultBridgeLink=i;
            m_VaultBridgeReverse=reverse;
            found=true;
            break;
        }
        if (!found) {cancel(false); return false;}
        // FELSORIA_VAULT_BRIDGE_TELEPORT_V3: keep the total wait across path retries.
        if (m_VaultBridgeFallbackLink!=m_VaultBridgeLink ||
            m_VaultBridgeFallbackReverse!=m_VaultBridgeReverse) {
            m_VaultBridgeFallbackLink=m_VaultBridgeLink;
            m_VaultBridgeFallbackReverse=m_VaultBridgeReverse;
            m_VaultBridgeFallbackStart=now;
            m_VaultBridgeProgressTime=now;
            m_VaultBridgeProgressPosition=me->GetPosition();
        }
        // Claim the crossing while waiting for walking or the bounded fallback.
        Link const& link=links[m_VaultBridgeLink];
        float t=Progress(link,bot);
        m_VaultBridgeStage=Corridor(link,bot) && t>0.05f && t<0.95f ? 1 : 0;
        m_VaultBridgeIssued=false;
        m_VaultBridgeStart=now;
        m_VaultBridgeRetry=0;
    }
    if (m_VaultBridgeLink<0 || m_VaultBridgeLink>=2) {cancel(false); return false;}
    Link const& link=links[m_VaultBridgeLink];
    if (!OnFloor(link,bot) || !OnFloor(link,human)) {cancel(false); return false;}
    if (uint32(now-m_VaultBridgeStart)>45000) {failure="timeout"; cancel(true); return true;}
    if ((!m_VaultBridgeReverse && EntranceSide(link,human)) ||
        (m_VaultBridgeReverse && BossPlatform(link,human)) ||
        (!EntranceSide(link,human) && !BossPlatform(link,human) && !Corridor(link,human))) {
        cancel(false); return false;
    }
    // V11 watchdog owns rescue independently of this legacy walking state.
    // GO extension remains a walking check; an already-crossed human can be
    // followed safely even if the bridge GO retracts or disappears afterwards.
    uint32 const bridgeEntry=m_VaultBridgeLink==0 ? 246727u : 246057u;
    GameObject* bridge=me->FindNearestGameObject(bridgeEntry,100.0f);
    bool const bridgeExtended=bridge && bridge->IsInWorld() && bridge->isSpawned() &&
        me->InSamePhase(bridge) && bridge->GetGoState()==GO_STATE_READY;
    // Preserve the rescue clock while a walking retry or root blocks the path.
    if (m_VaultBridgeRetry && uint32(now-m_VaultBridgeRetry)<2000) return true;
    if (IsNotMovement()) return true;
    Point const entry=m_VaultBridgeReverse ? link.arena : link.entrance;
    Point const exit=m_VaultBridgeReverse ? link.entrance : link.arena;
    Point target=m_VaultBridgeStage==0 ? entry : exit;
    if (Distance2(bot,target)<0.64f && std::fabs(bot.z-target.z)<1.5f) {
        if (m_VaultBridgeStage==1) {
            TC_LOG_INFO(LOG_FILTER_LFG,"RDF_VAULT_BRIDGE_DONE account=%u bridge=%u reverse=%u difficulty=%u",
                me->GetSession()->GetAccountId(),uint32(m_VaultBridgeLink+1),uint32(m_VaultBridgeReverse),uint32(me->GetMap()->GetDifficultyID()));
            cancel(false); return false;
        }
        m_VaultBridgeStage=1;
        m_VaultBridgeIssued=false;
        target=exit;
    }
    if (m_VaultBridgeIssued && !me->IsSplineFinished()) return true;
    m_VaultBridgeIssued=false;

    // FELSORIA_VAULT_BRIDGE_HEIGHT_V2: missing height data is allowed only
    // inside the surveyed bridge span, with its real extended GO present.
    // Reuse the open-bridge check made before the rescue branch.
    bool heightFallback=false;
    auto supported=[&](Point const& a,Point const& b,bool crossing) {
        probe=a;
        if (!OnFloor(link,a) || !OnFloor(link,b)) {failure="floor_band"; return false;}
        float length=std::sqrt(Distance2(a,b));
        unsigned steps=unsigned(std::ceil(length/0.5f));
        if (steps<1) steps=1;
        if (steps>200) {failure="segment_length"; return false;}
        if (!me->GetMap()->isInLineOfSight(a.x,a.y,a.z+1.0f,
            b.x,b.y,b.z+1.0f,me->GetPhases())) {failure="los"; return false;}
        for (unsigned n=0;n<=steps;++n) {
            float t=float(n)/float(steps);
            Point p={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
            probe=p;
            if (crossing && !Corridor(link,p)) {failure="corridor"; return false;}
            if (!crossing && (m_VaultBridgeReverse ? Progress(link,p)<0.50f : Progress(link,p)>0.50f)) {failure="approach_half"; return false;}
            float floor=me->GetHeight(p.x,p.y,p.z+2.0f,true,6.0f);
            measuredFloor=floor;
            if (!std::isfinite(floor)) {failure="floor_nonfinite"; return false;}
            if (std::fabs(floor-p.z)>1.5f)
            {
                float const span=std::sqrt(Distance2(link.entrance,link.arena));
                float const along=Progress(link,p)*span;
                // Keep real height checks on both platform aprons. Never accept
                // a nonzero wrong floor, NaN, or an arbitrary path outside the bridge.
                if (crossing && bridgeExtended && std::fabs(floor)<0.01f &&
                    Corridor(link,p) && along>=2.0f && along<=span-2.0f)
                    heightFallback=true;
                else
                {
                    failure=(crossing && std::fabs(floor)<0.01f && !bridgeExtended) ?
                        "bridge_missing_or_retracted" : "floor_delta";
                    return false;
                }
            }
        }
        return true;
    };
    Movement::PointsArray points;
    if (m_VaultBridgeStage==0) {
        PathGenerator path(me);
        if (path.CalculatePath(target.x,target.y,target.z) &&
            (path.GetPathType() & PATHFIND_NORMAL) &&
            !(path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_SHORTCUT | PATHFIND_NOT_USING_PATH | PATHFIND_SHORT))) {
            points=path.GetPath();
            if (points.size()<2) points.clear();
            if (!points.empty()) {
                Point last={points.back().x,points.back().y,points.back().z};
                if (Distance2(last,target)>0.64f || std::fabs(last.z-target.z)>1.5f) points.clear();
            }
        }
        // Local platform fallback is still checked against real floor and LOS.
        if (points.empty() && supported(bot,target,false)) {
            points.push_back(G3D::Vector3(bot.x,bot.y,bot.z));
            points.push_back(G3D::Vector3(target.x,target.y,target.z));
        }
        if (points.size()<2) {cancel(true); return true;}
        points.front()=G3D::Vector3(bot.x,bot.y,bot.z);
        for (size_t i=1;i<points.size();++i) {
            Point a={points[i-1].x,points[i-1].y,points[i-1].z};
            Point b={points[i].x,points[i].y,points[i].z};
            if (!supported(a,b,false)) {cancel(true); return true;}
        }
    } else {
        if (!supported(bot,target,true)) {cancel(true); return true;}
        points.push_back(G3D::Vector3(bot.x,bot.y,bot.z));
        points.push_back(G3D::Vector3(target.x,target.y,target.z));
    }
    me->InterruptSpell(CURRENT_GENERIC_SPELL,false);
    me->InterruptSpell(CURRENT_CHANNELED_SPELL,false);
    m_Movement->ClearMovement();
    m_CruxMovement.ClearMovement();
    me->GetMotionMaster()->Clear();
    Movement::MoveSplineInit init(*me);
    init.MovebyPath(points);
    init.SetWalk(false);
    if (init.Launch()<=0) {failure="spline_launch"; cancel(true); return true;}
    if (heightFallback)
        TC_LOG_INFO(LOG_FILTER_LFG,
            "RDF_VAULT_BRIDGE_HEIGHT_FALLBACK account=%u bridge=%u reverse=%u difficulty=%u",
            me->GetSession()->GetAccountId(),uint32(m_VaultBridgeLink+1),
            uint32(m_VaultBridgeReverse),uint32(me->GetMap()->GetDifficultyID()));
    m_VaultBridgeIssued=true;
    TC_LOG_INFO(LOG_FILTER_LFG,"RDF_VAULT_BRIDGE_WALK account=%u bridge=%u reverse=%u leg=%u difficulty=%u points=%u",
        me->GetSession()->GetAccountId(),uint32(m_VaultBridgeLink+1),uint32(m_VaultBridgeReverse),uint32(m_VaultBridgeStage),
        uint32(me->GetMap()->GetDifficultyID()),uint32(points.size()));
    return true;
}


// FELSORIA_PVP_LEGION_ROTATIONS_V1
bool BotGroupAI::ProcessLegionPvp(Unit* target)
{
    if (!PvpLegionRotation::Applicable(me, target)) return false;
    if (m_UsePotion.TryUsePotion()) return true;
    auto nearby = PvpLegionRotation::Nearby(me);
    bool handled = m_PvpRotation.Run(me, target, m_Movement, nearby, nearby);
    if (handled && !me->HasUnitState(UNIT_STATE_CASTING) &&
        !BotUtility::IsCombatControl(target) && target->isAlive() && me->InSamePhase(target) &&
        target->GetMap() == me->GetMap() && me->IsWithinLOSInMap(target))
    {
        if (PvpRotationPolicy::melee(me->GetSpecializationId()) && me->IsWithinMeleeRange(target)) DoMeleeAttackIfReady();
        else if (me->getClass() == CLASS_HUNTER) DoRangedAttackIfReady();
        if (!PvpRotationPolicy::melee(me->GetSpecializationId()) && me->GetHealthPct() < 65.0f &&
            me->GetDistance(target) < 9.0f && !IsNotMovement())
            m_Flee.UpdateFleeMovementByPVP(target, m_Movement);
    }
    return handled;
}

#include "FelsoriaTankLead.inc"

#include "FelsoriaPredictiveMechanics.inc"
