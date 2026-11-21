#include "FelsoriaDynamicBotMetrics.h"

#include "PlayerBotSetting.h"
#include "FelsoriaBotArtifactSkins.h"
#include <mutex>
#include <tuple>
#include "DB2Stores.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "WorldSession.h"
#include "PlayerBotSession.h"
#include "OnlineMgr.h"
#include "MapManager.h"
#include "BotAITool.h"
#include "ConditionMgr.h"
#include "SpellAuraEffects.h"

uint32 PlayerBotSetting::classesTrainersGUID[MAX_CLASSES][2];
std::set<BotTalentEntry> PlayerBotSetting::classesTalents[MAX_CLASSES][3] = { std::set<BotTalentEntry>() };
std::map<uint32, ItemsForLevel> PlayerBotSetting::classesEquips[MAX_CLASSES][InventoryType::INVTYPE_RELIC+1] = { std::map<uint32, ItemsForLevel>() };
std::map<uint32, ItemsForLevel> PlayerBotSetting::starterEquips[MAX_CLASSES][InventoryType::INVTYPE_RELIC+1] = { std::map<uint32, ItemsForLevel>() };
std::list<uint32> PlayerBotSetting::classesCommonSpells[MAX_CLASSES] = { std::list<uint32>() };
std::vector<uint32> PlayerBotSetting::beastCreatureEntrys = std::vector<uint32>();
std::vector<uint32> PlayerBotSetting::normalMountSpells = std::vector<uint32>();
std::vector<uint32> PlayerBotSetting::fastMountSpells = std::vector<uint32>();
std::set<uint32> PlayerBotSetting::botFlyMountEntrys = std::set<uint32>();

bool ItemsForLevel::IsTenacityItem(const ItemTemplate* itemTemplate)
{
	if (!itemTemplate || itemTemplate->ItemLevel < 240)
		return false;
	for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; i++)
	{
		auto type = itemTemplate->ExtendedData->StatModifierBonusStat[i];
		if (type == ItemModType::ITEM_MOD_CRIT_TAKEN_RATING ||
			type == ItemModType::ITEM_MOD_RESILIENCE_RATING)
		{
			if (itemTemplate->GetQuality() < ItemQualities::ITEM_QUALITY_EPIC)
				continue;
			if (type >= 40)
				return true;
			if (itemTemplate->GetInventoryType() == InventoryType::INVTYPE_THROWN ||
				itemTemplate->GetInventoryType() == InventoryType::INVTYPE_RANGED ||
				itemTemplate->GetInventoryType() == InventoryType::INVTYPE_RANGEDRIGHT)
			{
				if (type >= 20)
					return true;
			}
			if (itemTemplate->GetInventoryType() == InventoryType::INVTYPE_TRINKET)
				return true;
		}
	}
	return false;
}

void ItemsForLevel::AddItem(const ItemTemplate* pItem)
{
	if (!pItem || pItem->AllowableClass == 0)
		return;
	if (IsTenacityItem(pItem))
	{
		bool exist = false;
		for (BotItems::iterator itItem = m_TenacityItems.begin(); itItem != m_TenacityItems.end(); itItem++)
		{
			if ((*itItem) == pItem)
			{
				exist = true;
				break;
			}
		}
		if (!exist)
		{
			m_TenacityItems.push_back(pItem);
			return;
		}
	}
	bool exist = false;
	for (BotItems::iterator itItem = m_Items.begin(); itItem != m_Items.end(); itItem++)
	{
		if ((*itItem) == pItem)
		{
			exist = true;
			break;
		}
	}
	if (!exist)
		m_Items.push_back(pItem);
}

const ItemTemplate* ItemsForLevel::RandomItem()
{
	if (m_Items.size() <= 0)
		return NULL;
	uint32 index = irand(0, m_Items.size() - 1);
	return m_Items[index];
}

const ItemTemplate* ItemsForLevel::RandomTenacityItem()
{
	if (m_TenacityItems.size() <= 0)
		return NULL;
	uint32 index = irand(0, m_TenacityItems.size() - 1);
	return m_TenacityItems[index];
}

bool BotTalentEntry::operator < (const BotTalentEntry &tal) const
{
	if (!talentEntry || !tal.talentEntry)
		return false;
	//if (talentEntry->Row == tal.talentEntry->Row)
	{
		return talentEntry->ColumnIndex < tal.talentEntry->ColumnIndex;
	}
	//else
	//	return talentEntry->Row < tal.talentEntry->Row;
	return false;
}

bool PlayerBotSetting::IsEquipByClasses(uint32 cls, const ItemTemplate* itemTemplate)
{
	if (!itemTemplate || itemTemplate->AllowableClass == 0)
		return false;
	if (itemTemplate->AllowableClass > 0)
	{
		if (!(itemTemplate->AllowableClass & (1 << (cls - 1))))
			return false;
	}
	if (cls == 1 || cls == 6 || cls == 3 || cls == 4)
	{
		if (!IsOnlyPhysicsAttributeEquip(itemTemplate, (cls == 3) ? false : true))
			return false;
	}
	if (IsCommonEquip(itemTemplate))
		return true;
	switch (cls)
	{
	case 1:
		return IsWarriorEquip(itemTemplate);
	case 2:
		return IsPaladinEquip(itemTemplate);
	case 3:
		return IsHunterEquip(itemTemplate);
	case 4:
		return IsRogueEquip(itemTemplate);
	case 5:
		return IsPriestEquip(itemTemplate);
	case 6:
		return IsDeathKightEquip(itemTemplate);
	case 7:
		return IsShamanEquip(itemTemplate);
	case 8:
		return IsMageEquip(itemTemplate);
	case 9:
		return IsWarlockEquip(itemTemplate);
	    case CLASS_MONK:
    case CLASS_DEMON_HUNTER:
        return IsRogueEquip(itemTemplate);case 11:
		return IsDruidEquip(itemTemplate);
	default:
		return false;
	}
	return false;
}

bool PlayerBotSetting::IsEquipByClsAndTal(uint32 cls, uint32 tal, const ItemTemplate* itemTemplate, int32 rndPropID)
{
	if (tal > 2 || !itemTemplate || cls < 1 || cls > CLASS_DEMON_HUNTER)
		return false;
	if (!itemTemplate || itemTemplate->AllowableClass == 0)
		return false;
	if (itemTemplate->AllowableClass > 0)
	{
		uint32 clsMask = 1 << (cls - 1);
		if (!(itemTemplate->AllowableClass & clsMask))
			return false;
	}

	if (IsTrinketEquip(itemTemplate))
		return true;
	std::list<uint32> enchants;
	GetRandomPropEnchantments(rndPropID, enchants);
	switch (cls)
	{
	case 1:
		if (tal == 2)
		{
			if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON && itemTemplate->GetInventoryType() == INVTYPE_2HWEAPON)
				return false;
			//if (!IsTankAttributeEquip(itemTemplate) && !IsTankRandomAttributeByEquip(enchants))
			//	return false;
			if (!IsOnlyPhysicsRandomAttributeByEquip(enchants, true) && !IsOnlyPhysicsAttributeEquip(itemTemplate, true))
				return false;
		}
		else
		{
			if (IsTankRandomAttributeByEquip(enchants) || IsTankAttributeEquip(itemTemplate))
				return false;
			if (!IsOnlyPhysicsRandomAttributeByEquip(enchants, true) && !IsOnlyPhysicsAttributeEquip(itemTemplate, true))
				return false;
		}
		if (IsCommonEquip(itemTemplate))
			return true;
		return IsWarriorEquip(itemTemplate);
	case 2:
		if (tal == 1)
		{
			if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON && itemTemplate->GetInventoryType() == INVTYPE_2HWEAPON)
				return false;
			//if (!IsTankRandomAttributeByEquip(enchants) && !IsTankAttributeEquip(itemTemplate))
			//	return false;
		}
		else
		{
			if (IsTankRandomAttributeByEquip(enchants) || IsTankAttributeEquip(itemTemplate))
				return false;
		}
		if (IsCommonEquip(itemTemplate))
			return true;
		return IsPaladinEquip(itemTemplate);
	case 3:
		if (IsTankRandomAttributeByEquip(enchants) || IsTankAttributeEquip(itemTemplate))
			return false;
		if (!IsOnlyPhysicsRandomAttributeByEquip(enchants, false) && !IsOnlyPhysicsAttributeEquip(itemTemplate, false))
			return false;
		if (IsCommonEquip(itemTemplate))
			return true;
		return IsHunterEquip(itemTemplate);
	case 4:
		if (IsTankRandomAttributeByEquip(enchants) || IsTankAttributeEquip(itemTemplate))
			return false;
		if (!IsOnlyPhysicsRandomAttributeByEquip(enchants, true) && !IsOnlyPhysicsAttributeEquip(itemTemplate, true))
			return false;
		if (IsCommonEquip(itemTemplate))
			return true;
		return IsRogueEquip(itemTemplate);
	case 5:
		if (IsTankRandomAttributeByEquip(enchants) || IsTankAttributeEquip(itemTemplate))
			return false;
		if (!IsOnlyMagicRandomAttributeByEquip(enchants) && !IsOnlyMagicAttributeEquip(itemTemplate))
			return false;
		if (IsCommonEquip(itemTemplate))
			return true;
		return IsPriestEquip(itemTemplate);
	case 6:
		if (tal != 1)
		{
			if (IsTankRandomAttributeByEquip(enchants) || IsTankAttributeEquip(itemTemplate))
				return false;
		}
		if (!IsOnlyPhysicsRandomAttributeByEquip(enchants, true) && !IsOnlyPhysicsAttributeEquip(itemTemplate, true))
			return false;
		if (IsCommonEquip(itemTemplate))
			return true;
		return IsDeathKightEquip(itemTemplate);
	case 7:
		if (IsTankRandomAttributeByEquip(enchants) || IsTankAttributeEquip(itemTemplate))
			return false;
		if (tal == 1)
		{
			if (!IsOnlyPhysicsRandomAttributeByEquip(enchants, false) && !IsOnlyPhysicsAttributeEquip(itemTemplate, false))
				return false;
		}
		else
		{
			if (!IsOnlyMagicRandomAttributeByEquip(enchants) && !IsOnlyMagicAttributeEquip(itemTemplate))
				return false;
		}
		if (IsCommonEquip(itemTemplate))
			return true;
		return IsShamanEquip(itemTemplate);
	case 8:
		if (IsTankRandomAttributeByEquip(enchants) || IsTankAttributeEquip(itemTemplate))
			return false;
		if (!IsOnlyMagicRandomAttributeByEquip(enchants) && !IsOnlyMagicAttributeEquip(itemTemplate))
			return false;
		if (IsCommonEquip(itemTemplate))
			return true;
		return IsMageEquip(itemTemplate);
	case 9:
		if (IsTankRandomAttributeByEquip(enchants) || IsTankAttributeEquip(itemTemplate))
			return false;
		if (!IsOnlyMagicRandomAttributeByEquip(enchants) && !IsOnlyMagicAttributeEquip(itemTemplate))
			return false;
		if (IsCommonEquip(itemTemplate))
			return true;
		return IsWarlockEquip(itemTemplate);
	    case CLASS_MONK:
        if (tal != 0)
        {
            if (IsTankRandomAttributeByEquip(enchants) ||
                IsTankAttributeEquip(itemTemplate))
            {
                return false;
            }
        }

        if (tal == 2)
        {
            if (!IsOnlyMagicRandomAttributeByEquip(enchants) &&
                !IsOnlyMagicAttributeEquip(itemTemplate))
            {
                return false;
            }
        }
        else
        {
            if (!IsOnlyPhysicsRandomAttributeByEquip(enchants, true) &&
                !IsOnlyPhysicsAttributeEquip(itemTemplate, true))
            {
                return false;
            }
        }

        if (IsCommonEquip(itemTemplate))
            return true;

        return IsRogueEquip(itemTemplate);

    case CLASS_DEMON_HUNTER:
        if (tal != 1)
        {
            if (IsTankRandomAttributeByEquip(enchants) ||
                IsTankAttributeEquip(itemTemplate))
            {
                return false;
            }
        }

        if (!IsOnlyPhysicsRandomAttributeByEquip(enchants, true) &&
            !IsOnlyPhysicsAttributeEquip(itemTemplate, true))
        {
            return false;
        }

        if (IsCommonEquip(itemTemplate))
            return true;

        return IsRogueEquip(itemTemplate);
case 11:
		if (IsTankRandomAttributeByEquip(enchants) || IsTankAttributeEquip(itemTemplate))
			return false;
		if (tal == 1)
		{
			if (!IsOnlyPhysicsRandomAttributeByEquip(enchants, true) && !IsOnlyPhysicsAttributeEquip(itemTemplate, true))
				return false;
		}
		else
		{
			if (!IsOnlyMagicRandomAttributeByEquip(enchants) && !IsOnlyMagicAttributeEquip(itemTemplate))
				return false;
		}
		if (IsCommonEquip(itemTemplate))
			return true;
		return IsDruidEquip(itemTemplate);
	default:
		return false;
	}
	return false;
}

bool PlayerBotSetting::MatchEquipmentSlot(uint8 pos, const ItemTemplate* itemTemplate)
{
	EquipmentSlots slot = EquipmentSlots(pos);
	InventoryType type = InventoryType(itemTemplate->GetInventoryType());
	switch (type)
	{
	case InventoryType::INVTYPE_HEAD:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_HEAD)
			return true;
		break;
	case InventoryType::INVTYPE_NECK:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_NECK)
			return true;
		break;
	case InventoryType::INVTYPE_SHOULDERS:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_SHOULDERS)
			return true;
		break;
	case InventoryType::INVTYPE_CHEST:
	case InventoryType::INVTYPE_ROBE:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_CHEST)
			return true;
		break;
	case InventoryType::INVTYPE_WAIST:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_WAIST)
			return true;
		break;
	case InventoryType::INVTYPE_LEGS:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_LEGS)
			return true;
		break;
	case InventoryType::INVTYPE_FEET:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_FEET)
			return true;
		break;
	case InventoryType::INVTYPE_WRISTS:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_WRISTS)
			return true;
		break;
	case InventoryType::INVTYPE_HANDS:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_HANDS)
			return true;
		break;
	case InventoryType::INVTYPE_FINGER:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_FINGER1 || slot == EquipmentSlots::EQUIPMENT_SLOT_FINGER2)
			return true;
		break;
	case InventoryType::INVTYPE_TRINKET:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_TRINKET1 || slot == EquipmentSlots::EQUIPMENT_SLOT_TRINKET2)
			return true;
		break;
	case InventoryType::INVTYPE_CLOAK:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_BACK)
			return true;
		break;
	case InventoryType::INVTYPE_WEAPON:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_MAINHAND || slot == EquipmentSlots::EQUIPMENT_SLOT_OFFHAND)
			return true;
		break;
	case InventoryType::INVTYPE_2HWEAPON:
	case InventoryType::INVTYPE_WEAPONMAINHAND:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_MAINHAND)
			return true;
		break;
	case InventoryType::INVTYPE_SHIELD:
	case InventoryType::INVTYPE_WEAPONOFFHAND:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_OFFHAND)
			return true;
		break;
	case InventoryType::INVTYPE_RANGED:
	case InventoryType::INVTYPE_RANGEDRIGHT:
	case InventoryType::INVTYPE_HOLDABLE:
	case InventoryType::INVTYPE_THROWN:
	case InventoryType::INVTYPE_RELIC:
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_RANGED)
			return true;
		break;
	}

	return false;
}

uint32 PlayerBotSetting::GetItemLevelByAI(const ItemTemplate* item)
{
	if (!item)
		return 0;
	uint32 level = item->ItemLevel;
	uint32 quality = item->GetQuality();
	if (quality <= 1)
		return level;
	if (quality == ITEM_QUALITY_HEIRLOOM)
		return 999;
	level += uint32(float(level) * (float(quality) * 0.3f));
	return level;
}

bool PlayerBotSetting::IsBetterEquip(Player* player, const ItemTemplate* itemTemplate, int32 rndPropID)
{
	if (!itemTemplate || itemTemplate->GetBaseRequiredLevel() > player->getLevel())
		return false;
	if (player->getLevel() < 110 && !IsLevelAppropriateEquip(player->getLevel(), itemTemplate))
		return false;
	bool const suitable = player->getLevel() < 110 && player->getClass() == CLASS_MONK
		? IsLowLevelSpecEquip(player->getClass(), player->FindTalentType(), itemTemplate)
		: IsEquipByClsAndTal(player->getClass(), player->FindTalentType(), itemTemplate, rndPropID);
	if (!suitable)
		return false;
	uint16 eDest;
	InventoryResult msg = player->CanEquipNewItem(NULL_SLOT, eDest, itemTemplate->GetId(), true);
	if (msg != EQUIP_ERR_OK)
		return false;
	bool mainhandIsTwo = false;
	for (uint8 slot = EquipmentSlots::EQUIPMENT_SLOT_HEAD; slot < EquipmentSlots::EQUIPMENT_SLOT_END; slot++)
	{
		if (!MatchEquipmentSlot(slot, itemTemplate))
			continue;
		//uint16 pos = (255 << 8) | slot;
		//if (!player->IsEquipmentPos(pos))
		//	continue;

		Item* pItem = player->GetItemByPos(255, slot);
		if (!pItem)
		{
			if (slot == EquipmentSlots::EQUIPMENT_SLOT_OFFHAND && mainhandIsTwo)
				return false;
			return true;
		}
		const ItemTemplate* selfTemplate = pItem->GetTemplate();
		if (!selfTemplate)
			continue;
		if (slot == EquipmentSlots::EQUIPMENT_SLOT_MAINHAND)
		{
			if (selfTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON && selfTemplate->GetInventoryType() == INVTYPE_2HWEAPON)
				mainhandIsTwo = true;
			else
				mainhandIsTwo = false;
		}
		if (selfTemplate->GetClass() != itemTemplate->GetClass())
			continue;
		if (selfTemplate->GetSubClass() != itemTemplate->GetSubClass())
		{
			if (selfTemplate->GetClass() != ItemClass::ITEM_CLASS_WEAPON || itemTemplate->GetClass() != ItemClass::ITEM_CLASS_WEAPON)
				continue;
			if ((selfTemplate->GetInventoryType() != INVTYPE_2HWEAPON && selfTemplate->GetInventoryType() != INVTYPE_WEAPON && selfTemplate->GetInventoryType() != INVTYPE_WEAPONMAINHAND) ||
				(itemTemplate->GetInventoryType() != INVTYPE_2HWEAPON && itemTemplate->GetInventoryType() != INVTYPE_WEAPON && itemTemplate->GetInventoryType() != INVTYPE_WEAPONMAINHAND))
				continue;
		}
		if (GetItemLevelByAI(selfTemplate) >= GetItemLevelByAI(itemTemplate))
		{
			//if (IsTrinketEquip(itemTemplate) || IsFingerEquip(itemTemplate))
			//	continue;
			return false;
		}
		return true;
	}

	return false;
}

void PlayerBotSetting::ClearUnknowMount(Player* player)
{
	for (uint32 mountID : normalMountSpells)
	{
		if (player->HasAura(mountID))
			player->RemoveOwnedAura(mountID, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
	}
	for (uint32 mountID : fastMountSpells)
	{
		if (player->HasAura(mountID))
			player->RemoveOwnedAura(mountID, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
	}
	if (player->IsMounted())// && AURA_EFFECT_HANDLE_REAL
	{
		player->Dismount();
		player->RemoveAurasByType(SPELL_AURA_MOUNTED);
	}
}

uint32 PlayerBotSetting::CheckMaxLevel(uint32 level)
{
	uint32 worldMaxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
	if (level > worldMaxLevel)
	{
		level = worldMaxLevel;
	}
	if (level == 0)
		level = 1;
	return level;
}

bool PlayerBotSetting::IsStarterEquipByClasses(uint32 cls, const ItemTemplate* item)
{
	if (!item || cls < 1 || cls >= MAX_CLASSES || item->AllowableClass == 0 ||
		!(item->AllowableClass & (1 << (cls - 1))))
		return false;
	if (IsCommonEquip(item))
		return true;
	switch (cls)
	{
	case CLASS_WARRIOR: return IsWarriorEquip(item);
	case CLASS_PALADIN: return IsPaladinEquip(item);
	case CLASS_HUNTER: return IsHunterEquip(item);
	case CLASS_ROGUE: return IsRogueEquip(item);
	case CLASS_PRIEST: return IsPriestEquip(item);
	case CLASS_DEATH_KNIGHT: return IsDeathKightEquip(item);
	case CLASS_SHAMAN: return IsShamanEquip(item);
	case CLASS_MAGE: return IsMageEquip(item);
	case CLASS_WARLOCK: return IsWarlockEquip(item);
	case CLASS_MONK:
		// Mistweavers can use staves; the legacy rogue predicate excludes them.
		return (item->GetClass() == ITEM_CLASS_WEAPON &&
			item->GetSubClass() == ITEM_SUBCLASS_WEAPON_STAFF) || IsRogueEquip(item);
	case CLASS_DEMON_HUNTER: return IsRogueEquip(item);
	case CLASS_DRUID: return IsDruidEquip(item);
	default: return false;
	}
}

bool PlayerBotSetting::IsLowLevelSpecEquip(uint32 cls, uint32 tal, const ItemTemplate* item)
{
	if (!IsStarterEquipByClasses(cls, item))
		return false;
	if (item->GetClass() == ITEM_CLASS_WEAPON && item->GetInventoryType() == INVTYPE_2HWEAPON &&
		((cls == CLASS_WARRIOR && tal == 2) || (cls == CLASS_PALADIN && tal <= 1)))
		return false;
	// The old max-level predicate requires stats, which many real starter
	// templates do not have. Apply the same class and spec restrictions while
	// admitting statless equipment at the levels where it is useful.
	if (!item->HasStats())
		return true;
	bool const caster = cls == CLASS_PRIEST || cls == CLASS_MAGE ||
		cls == CLASS_WARLOCK || (cls == CLASS_PALADIN && tal == 0) ||
		(cls == CLASS_SHAMAN && tal != 1) ||
		(cls == CLASS_DRUID && (tal == 0 || tal == 3)) ||
		(cls == CLASS_MONK && tal == 1);
	if (caster)
		return IsOnlyMagicAttributeEquip(item);
	if (!IsOnlyPhysicsAttributeEquip(item, true))
		return false;
	bool const strength = cls == CLASS_WARRIOR || cls == CLASS_PALADIN ||
		cls == CLASS_DEATH_KNIGHT;
	for (uint32 index = 0; index < MAX_ITEM_PROTO_STATS; ++index)
	{
		auto const stat = item->ExtendedData->StatModifierBonusStat[index];
		if ((strength && stat == ItemModType::ITEM_MOD_AGILITY) ||
			(!strength && stat == ItemModType::ITEM_MOD_STRENGTH))
			return false;
	}
	return true;
}

bool PlayerBotSetting::IsLevelAppropriateEquip(uint32 level, const ItemTemplate* item)
{
	if (!item || level >= 110 || item->GetArtifactID())
		return false;
	int32 const required = item->GetBaseRequiredLevel();
	if (required > int32(level))
		return false;
	uint32 const quality = item->GetQuality();
	if (quality > (level < 20 ? uint32(ITEM_QUALITY_UNCOMMON) :
		level < 60 ? uint32(ITEM_QUALITY_RARE) : uint32(ITEM_QUALITY_EPIC)))
		return false;
	// A required level of zero occurs on starter items but also on scaled,
	// heirloom and endgame templates. It cannot establish an item's era.
	uint32 const maxItemLevel = required <= 0 ? level + 8 :
		level < 60 ? level + 10 :
		level < 80 ? level * 3 :
		level < 100 ? level * 7 : 750;
	return item->ItemLevel <= maxItemLevel;
}

uint32 PlayerBotSetting::CheckBotLevel(Player const* player, uint32 level)
{
	level = std::min<uint32>(110, CheckMaxLevel(level));
	if (!player)
		return level;

	uint32 minimum = 1;
	if (player->getClass() == CLASS_DEMON_HUNTER)
		minimum = 98;
	else if (player->getClass() == CLASS_DEATH_KNIGHT)
		minimum = 55;
	else
	{
		switch (player->getRace())
		{
		case RACE_NIGHTBORNE:
		case RACE_HIGHMOUNTAIN_TAUREN:
		case RACE_VOID_ELF:
		case RACE_LIGHTFORGED_DRAENEI:
			minimum = 20;
			break;
		default:
			break;
		}
	}
	return std::max(level, minimum);
}

bool PlayerBotSetting::IsBotFlyMountAura(uint32 aura)
{
	if (!aura)
		return false;
	return botFlyMountEntrys.find(aura) != botFlyMountEntrys.end();
}

bool PlayerBotSetting::IsOnlyPhysicsAttributeEquip(const ItemTemplate* itemTemplate, bool coverIntellect)
{
	if (!itemTemplate->HasStats())
		return false;
	for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; i++)
	{
		auto type = itemTemplate->ExtendedData->StatModifierBonusStat[i];
		switch (type)
		{
		case ItemModType::ITEM_MOD_MANA:
		case ItemModType::ITEM_MOD_SPIRIT:
		case ItemModType::ITEM_MOD_HIT_SPELL_RATING:
		case ItemModType::ITEM_MOD_CRIT_SPELL_RATING:
		case ItemModType::ITEM_MOD_HIT_TAKEN_SPELL_RATING:
		case ItemModType::ITEM_MOD_CRIT_TAKEN_SPELL_RATING:
		case ItemModType::ITEM_MOD_HASTE_SPELL_RATING:
		case ItemModType::ITEM_MOD_SPELL_HEALING_DONE:
		case ItemModType::ITEM_MOD_SPELL_DAMAGE_DONE:
		case ItemModType::ITEM_MOD_MANA_REGENERATION:
		case ItemModType::ITEM_MOD_SPELL_POWER:
		case ItemModType::ITEM_MOD_SPELL_PENETRATION:
			return false;
		case ItemModType::ITEM_MOD_INTELLECT:
			if (coverIntellect)
				return false;
		}
	}
	return true;
}

bool PlayerBotSetting::IsOnlyMagicAttributeEquip(const ItemTemplate* itemTemplate)
{
	if (!itemTemplate->HasStats())
		return false;
	for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; i++)
	{
		auto type = itemTemplate->ExtendedData->StatModifierBonusStat[i];
		switch (type)
		{
		case ItemModType::ITEM_MOD_AGILITY:
		case ItemModType::ITEM_MOD_STRENGTH:
		case ItemModType::ITEM_MOD_HIT_MELEE_RATING:
		case ItemModType::ITEM_MOD_CRIT_MELEE_RATING:
		case ItemModType::ITEM_MOD_HIT_TAKEN_MELEE_RATING:
		case ItemModType::ITEM_MOD_CRIT_TAKEN_MELEE_RATING:
		case ItemModType::ITEM_MOD_HASTE_MELEE_RATING:
		case ItemModType::ITEM_MOD_ATTACK_POWER:
		case ItemModType::ITEM_MOD_RANGED_ATTACK_POWER:
		case ItemModType::ITEM_MOD_ARMOR_PENETRATION_RATING:
			return false;
		}
	}
	return true;
}

bool PlayerBotSetting::IsTankAttributeEquip(const ItemTemplate* itemTemplate)
{
	if (!itemTemplate->HasStats())
		return false;
	for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; i++)
	{
		auto type = itemTemplate->ExtendedData->StatModifierBonusStat[i];
		switch (type)
		{
		case ItemModType::ITEM_MOD_DEFENSE_SKILL_RATING:
		case ItemModType::ITEM_MOD_DODGE_RATING:
		case ItemModType::ITEM_MOD_PARRY_RATING:
		case ItemModType::ITEM_MOD_BLOCK_RATING:
		case ItemModType::ITEM_MOD_HIT_TAKEN_MELEE_RATING:
		case ItemModType::ITEM_MOD_HIT_TAKEN_RANGED_RATING:
		case ItemModType::ITEM_MOD_HIT_TAKEN_SPELL_RATING:
		case ItemModType::ITEM_MOD_HIT_TAKEN_RATING:
		case ItemModType::ITEM_MOD_BLOCK_VALUE:
			return true;
		}
	}
	return false;
}

bool PlayerBotSetting::IsOnlyPhysicsRandomAttributeByEquip(std::list<uint32>& enchants, bool coverIntellect)
{
	if (enchants.empty())
		return false;
	bool have = false;
	for (uint32 enchant_id : enchants)
	{
		SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
		if (!pEnchant)
			continue;
		for (int s = 0; s < MAX_ITEM_ENCHANTMENT_EFFECTS; ++s)
		{
			uint32 enchant_spell_id = pEnchant->Effect[s];
			if (enchant_spell_id == 0)
				continue;
			switch (enchant_spell_id)
			{
			case ItemModType::ITEM_MOD_MANA:
			case ItemModType::ITEM_MOD_SPIRIT:
			case ItemModType::ITEM_MOD_HIT_SPELL_RATING:
			case ItemModType::ITEM_MOD_CRIT_SPELL_RATING:
			case ItemModType::ITEM_MOD_HIT_TAKEN_SPELL_RATING:
			case ItemModType::ITEM_MOD_CRIT_TAKEN_SPELL_RATING:
			case ItemModType::ITEM_MOD_HASTE_SPELL_RATING:
			case ItemModType::ITEM_MOD_SPELL_HEALING_DONE:
			case ItemModType::ITEM_MOD_SPELL_DAMAGE_DONE:
			case ItemModType::ITEM_MOD_MANA_REGENERATION:
			case ItemModType::ITEM_MOD_SPELL_POWER:
			case ItemModType::ITEM_MOD_SPELL_PENETRATION:
				return false;
			case ItemModType::ITEM_MOD_INTELLECT:
				if (coverIntellect)
					return false;
				have = true;
				break;
			default:
				have = true;
			}
		}
	}
	return have;
}

bool PlayerBotSetting::IsOnlyMagicRandomAttributeByEquip(std::list<uint32>& enchants)
{
	if (enchants.empty())
		return false;
	bool have = false;
	for (uint32 enchant_id : enchants)
	{
		SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
		if (!pEnchant)
			continue;
		for (int s = 0; s < MAX_ITEM_ENCHANTMENT_EFFECTS; ++s)
		{
			uint32 enchant_spell_id = pEnchant->Effect[s];
			if (enchant_spell_id == 0)
				continue;
			switch (enchant_spell_id)
			{
			case ItemModType::ITEM_MOD_AGILITY:
			case ItemModType::ITEM_MOD_STRENGTH:
			case ItemModType::ITEM_MOD_HIT_MELEE_RATING:
			case ItemModType::ITEM_MOD_CRIT_MELEE_RATING:
			case ItemModType::ITEM_MOD_HIT_TAKEN_MELEE_RATING:
			case ItemModType::ITEM_MOD_CRIT_TAKEN_MELEE_RATING:
			case ItemModType::ITEM_MOD_HASTE_MELEE_RATING:
			case ItemModType::ITEM_MOD_ATTACK_POWER:
			case ItemModType::ITEM_MOD_RANGED_ATTACK_POWER:
			case ItemModType::ITEM_MOD_ARMOR_PENETRATION_RATING:
				return false;
			default:
				have = true;
			}
		}
	}
	return have;
}

bool PlayerBotSetting::IsTankRandomAttributeByEquip(std::list<uint32>& enchants)
{
	if (enchants.empty())
		return false;
	for (uint32 enchant_id : enchants)
	{
		SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
		if (!pEnchant)
			continue;
		for (int s = 0; s < MAX_ITEM_ENCHANTMENT_EFFECTS; ++s)
		{
			uint32 enchant_spell_id = pEnchant->Effect[s];
			if (enchant_spell_id == 0)
				continue;
			switch (enchant_spell_id)
			{
			case ItemModType::ITEM_MOD_DEFENSE_SKILL_RATING:
			case ItemModType::ITEM_MOD_DODGE_RATING:
			case ItemModType::ITEM_MOD_PARRY_RATING:
			case ItemModType::ITEM_MOD_BLOCK_RATING:
			case ItemModType::ITEM_MOD_HIT_TAKEN_MELEE_RATING:
			case ItemModType::ITEM_MOD_HIT_TAKEN_RANGED_RATING:
			case ItemModType::ITEM_MOD_HIT_TAKEN_SPELL_RATING:
			case ItemModType::ITEM_MOD_HIT_TAKEN_RATING:
			case ItemModType::ITEM_MOD_BLOCK_VALUE:
				return true;
			}
		}
	}
	return false;
}

void PlayerBotSetting::GetRandomPropEnchantments(int32 rndPropID, std::list<uint32>& enchants)
{
	enchants.clear();
	if (rndPropID == 0)
		return;
	if (rndPropID > 0)
	{
		if (ItemRandomPropertiesEntry const* item_rand = sItemRandomPropertiesStore.LookupEntry(rndPropID))
		{
			for (uint32 i = PROP_ENCHANTMENT_SLOT_2; i < PROP_ENCHANTMENT_SLOT_2 + 3; ++i)
			{
				uint32 enchant_id = item_rand->Enchantment[i - PROP_ENCHANTMENT_SLOT_2];
				if (enchant_id > 0)
					enchants.push_back(enchant_id);
			}
		}
	}
	if (rndPropID < 0)
	{
		if (ItemRandomSuffixEntry const* item_rand = sItemRandomSuffixStore.LookupEntry(-rndPropID))
		{
			for (uint32 i = PROP_ENCHANTMENT_SLOT_0; i < PROP_ENCHANTMENT_SLOT_0 + 3; ++i)
			{
				uint32 enchant_id = item_rand->Enchantment[i - PROP_ENCHANTMENT_SLOT_0];
				if (enchant_id > 0)
					enchants.push_back(enchant_id);
			}
		}
	}
}

//bool PlayerBotSetting::MatchEquipmentSlotsByWeapon(EquipmentSlots slot, InventoryType iType)
//{
//	if (slot == EquipmentSlots::EQUIPMENT_SLOT_MAINHAND)
//		return (iType == InventoryType::INVTYPE_2HWEAPON || iType == InventoryType::INVTYPE_WEAPON || iType == InventoryType::INVTYPE_WEAPONMAINHAND);
//	if (slot == EquipmentSlots::EQUIPMENT_SLOT_OFFHAND)
//		return (iType == InventoryType::INVTYPE_WEAPON || iType == InventoryType::INVTYPE_WEAPONOFFHAND);
//
//	return false;
//}
//
//bool PlayerBotSetting::MatchEquipmentSlotsByArmor(EquipmentSlots slot, InventoryType iType)
//{
//	if (slot == EquipmentSlots::EQUIPMENT_SLOT_BODY)
//		return (iType == InventoryType::INVTYPE_BODY || iType == InventoryType::INVTYPE_ROBE);
//	if (slot >= EquipmentSlots::EQUIPMENT_SLOT_HEAD && slot <= EquipmentSlots::EQUIPMENT_SLOT_HANDS)
//		return (uint8(iType) - 1 == slot);
//	if (slot == EquipmentSlots::EQUIPMENT_SLOT_FINGER1 || slot == EquipmentSlots::EQUIPMENT_SLOT_FINGER2)
//		return (iType == InventoryType::INVTYPE_FINGER);
//	if (slot == EquipmentSlots::EQUIPMENT_SLOT_TRINKET1 || slot == EquipmentSlots::EQUIPMENT_SLOT_TRINKET2)
//		return (iType == InventoryType::INVTYPE_TRINKET);
//	if (slot == EquipmentSlots::EQUIPMENT_SLOT_BACK)
//		return (iType == InventoryType::INVTYPE_CLOAK);
//	return false;
//}
//
//bool PlayerBotSetting::MatchRangeEquipmentSlots(uint32 cls, const ItemTemplate* itemTemplate)
//{
//	return false;
//}

bool PlayerBotSetting::IsCommonEquip(const ItemTemplate* itemTemplate)
{
	switch (itemTemplate->GetInventoryType())
	{
	case INVTYPE_NECK:
	case INVTYPE_CLOAK:
	case INVTYPE_FINGER:
	case INVTYPE_TRINKET:
		return true;
	default:
		break;
	}
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR /*&& itemTemplate->GetSubClass() == ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC */&& itemTemplate->GetInventoryType() == INVTYPE_TRINKET)
		return true;
	return false;
}

bool PlayerBotSetting::IsFingerEquip(const ItemTemplate* itemTemplate)
{
	switch (itemTemplate->GetInventoryType())
	{
	case INVTYPE_FINGER:
		return true;
	default:
		break;
	}
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR /*&& itemTemplate->GetSubClass() == ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC*/ && itemTemplate->GetInventoryType() == INVTYPE_FINGER)
		return true;
	return false;
}

bool PlayerBotSetting::IsTrinketEquip(const ItemTemplate* itemTemplate)
{
	switch (itemTemplate->GetInventoryType())
	{
	case INVTYPE_TRINKET:
		return true;
	default:
		break;
	}
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR/* && itemTemplate->GetSubClass() == ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC*/ && itemTemplate->GetInventoryType() == INVTYPE_TRINKET)
		return true;
	return false;
}

bool PlayerBotSetting::IsWarriorEquip(const ItemTemplate* itemTemplate)
{
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_obsolete:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_STAFF:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SPEAR:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MISC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_DAGGER:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_WAND:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FISHING_POLE:
			return false;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_CLOTH:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LEATHER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LIBRAM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_IDOL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_TOTEM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SIGIL:
			return false;
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MAIL:
			if (itemTemplate->GetBaseRequiredLevel() >= 40)
				return false;
			break;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_PROJECTILE)
	{
		if (itemTemplate->GetSubClass() == ItemSubclassProjectile::ITEM_SUBCLASS_ARROW || itemTemplate->GetSubClass() == ItemSubclassProjectile::ITEM_SUBCLASS_BULLET)
			return true;
	}

	return false;
}

bool PlayerBotSetting::IsPaladinEquip(const ItemTemplate* itemTemplate)
{
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (itemTemplate->GetSubClass())
		{
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_THROWN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_STAFF:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SPEAR:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MISC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_DAGGER:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_WAND:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FISHING_POLE:
			return false;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_CLOTH:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LEATHER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_IDOL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_TOTEM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SIGIL:
			return false;
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MAIL:
			if (itemTemplate->GetBaseRequiredLevel() >= 40)
				return false;
			break;
		default:
			break;
		}
		return true;
	}

	return false;
}

bool PlayerBotSetting::IsDeathKightEquip(const ItemTemplate* itemTemplate)
{
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_obsolete:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FIST:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_THROWN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_STAFF:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_POLEARM:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SPEAR:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MISC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_DAGGER:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_WAND:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FISHING_POLE:
			return false;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_CLOTH:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LEATHER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LIBRAM:
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_BUCKLER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SHIELD:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_IDOL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_TOTEM:
			return false;
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MAIL:
			if (itemTemplate->GetBaseRequiredLevel() >= 40)
				return false;
			break;
		default:
			break;
		}
		return true;
	}

	return false;
}

bool PlayerBotSetting::IsRogueEquip(const ItemTemplate* itemTemplate)
{
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_obsolete:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_POLEARM:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SPEAR:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_STAFF:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC2:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MISC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_WAND:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FISHING_POLE:
			return false;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_CLOTH:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MAIL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_PLATE:
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_BUCKLER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SHIELD:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LIBRAM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_IDOL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_TOTEM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SIGIL:
			return false;
		default:
			break;
		}
		return true;
	}

	return false;
}

bool PlayerBotSetting::IsDruidEquip(const ItemTemplate* itemTemplate)
{
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_obsolete:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_POLEARM:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SPEAR:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC2:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MISC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_WAND:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FISHING_POLE:
			return false;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_CLOTH:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MAIL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_PLATE:
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_BUCKLER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SHIELD:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LIBRAM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_TOTEM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SIGIL:
			return false;
		default:
			break;
		}
		return true;
	}

	return false;
}

bool PlayerBotSetting::IsHunterEquip(const ItemTemplate* itemTemplate)
{
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_obsolete:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_STAFF:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FIST:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_DAGGER:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_THROWN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SPEAR:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MISC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_WAND:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FISHING_POLE:
			return false;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_CLOTH:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_PLATE:
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_BUCKLER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SHIELD:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LIBRAM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_IDOL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_TOTEM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SIGIL:
			return false;
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LEATHER:
			if (itemTemplate->GetBaseRequiredLevel() >= 40)
				return false;
			break;
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MAIL:
			if (itemTemplate->GetBaseRequiredLevel() < 40)
				return false;
			break;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_PROJECTILE)
	{
		if (itemTemplate->GetSubClass() == ItemSubclassProjectile::ITEM_SUBCLASS_ARROW || itemTemplate->GetSubClass() == ItemSubclassProjectile::ITEM_SUBCLASS_BULLET)
			return true;
	}

	return false;
}

bool PlayerBotSetting::IsShamanEquip(const ItemTemplate* itemTemplate)
{
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_obsolete:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_STAFF:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD2:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FIST:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_DAGGER:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_THROWN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_POLEARM:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SPEAR:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC2:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MISC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_WAND:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FISHING_POLE:
			return false;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_CLOTH:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_PLATE:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LIBRAM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_IDOL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SIGIL:
			return false;
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LEATHER:
			if (itemTemplate->GetBaseRequiredLevel() >= 40)
				return false;
			break;
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MAIL:
			if (itemTemplate->GetBaseRequiredLevel() < 40)
				return false;
			break;
		default:
			break;
		}
		return true;
	}

	return false;
}

bool PlayerBotSetting::IsMageEquip(const ItemTemplate* itemTemplate)
{
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_obsolete:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FIST:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_THROWN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_POLEARM:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SPEAR:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC2:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MISC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FISHING_POLE:
			return false;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR)
	{
		switch (itemTemplate->GetSubClass())
		{
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LEATHER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MAIL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_PLATE:
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_BUCKLER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SHIELD:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LIBRAM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_IDOL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_TOTEM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SIGIL:
			return false;
		default:
			break;
		}
		return true;
	}

	return false;
}

bool PlayerBotSetting::IsWarlockEquip(const ItemTemplate* itemTemplate)
{
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_obsolete:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FIST:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_THROWN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_POLEARM:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SPEAR:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC2:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MISC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FISHING_POLE:
			return false;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR)
	{
		switch (itemTemplate->GetSubClass())
		{
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LEATHER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MAIL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_PLATE:
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_BUCKLER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SHIELD:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LIBRAM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_IDOL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_TOTEM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SIGIL:
			return false;
		default:
			break;
		}
		return true;
	}

	return false;
}

bool PlayerBotSetting::IsPriestEquip(const ItemTemplate* itemTemplate)
{
	if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (itemTemplate->GetSubClass())
		{
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_obsolete:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_AXE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MACE2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SWORD2:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FIST:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_THROWN:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_POLEARM:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_SPEAR:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_EXOTIC2:
		//case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_MISC:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_FISHING_POLE:
			return false;
		default:
			break;
		}
		return true;
	}
	else if (itemTemplate->GetClass() == ItemClass::ITEM_CLASS_ARMOR)
	{
		switch (itemTemplate->GetSubClass())
		{
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LEATHER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MAIL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_PLATE:
		//case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_BUCKLER:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SHIELD:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_LIBRAM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_IDOL:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_TOTEM:
		case ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_SIGIL:
			return false;
		default:
			break;
		}
		return true;
	}

	return false;
}

bool PlayerBotSetting::CheckHunterPet(Player* player)
{
	if (!player || player->getClass() != Classes::CLASS_HUNTER)
		return false;
	if (player->GetPet() == NULL && beastCreatureEntrys.size() > 0)
	{
		uint32 rndEntry = urand(0, beastCreatureEntrys.size() - 1);
		Pet* pet = player->CreateTamedPetFrom(beastCreatureEntrys[rndEntry], 1515);
		if (pet)
		{
			player->GetMap()->AddToMap(pet->ToCreature());
			player->SetMinion(pet, true);
			pet->InitPetCreateSpells();
			pet->ToCreature()->SetReactState(ReactStates(1));
			pet->SettingAllSpellAutocast(true);
			pet->SavePetToDB();
			player->PetSpellInitialize();
			return true;
		}
	}
	else if (Pet* pet = player->GetPet())
	{
		pet->SetLevel(player->getLevel());
		pet->InitPetCreateSpells();
		//pet->resetTalents();
		pet->ToCreature()->SetReactState(ReactStates(1));
		//pet->FlushTalentsByPoints();
		pet->SettingAllSpellAutocast(true);
		player->PetSpellInitialize();
		return true;
	}
	return false;
}

uint32 PlayerBotSetting::FindPlayerTalentType(Player* player)
{
    if (!player)
        return 0;
    ChrSpecializationEntry const* spec = sChrSpecializationStore.LookupEntry(
        player->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID));
    if (!spec || spec->ClassID != player->getClass())
        return 0;
    // Legacy druid AI: 0=balance, 1=feral/guardian, 2=restoration.
    if (player->getClass() == CLASS_DRUID)
        return spec->OrderIndex == 3 ? 2 : (spec->OrderIndex == 2 ? 1 : spec->OrderIndex);
    return spec->OrderIndex;
}

uint32 PlayerBotSetting::RandomMountByLevel(uint32 level)
{
	//if (level < 40)
	//{
	//	return 6777;
	//}
	//else
	{
		uint32 count = fastMountSpells.size();
		if (count == 0)
			return 6777;
		uint32 rnd = urand(0, count - 1);
		return fastMountSpells[rnd];
	}

	return 6777;
}

uint32 PlayerBotSetting::MountForPlayer(Player const* player)
{
    if (!player)
        return 0;
    // Hero classes never fall back to random mounts, in any AI mode.
    if (player->getClass() == CLASS_DEATH_KNIGHT)
        return IsMountAllowed(player, 48778) ? 48778 : 0; // Acherus Deathcharger
    if (player->getClass() == CLASS_DEMON_HUNTER)
        return IsMountAllowed(player, 200175) ? 200175 : 0; // Felsaber

    std::vector<uint32> eligible;
    for (uint32 spellId : fastMountSpells)
        if (IsMountAllowed(player, spellId))
            eligible.push_back(spellId);
    if (!eligible.empty())
        return eligible[urand(0, eligible.size() - 1)];
    for (uint32 spellId : normalMountSpells)
        if (IsMountAllowed(player, spellId))
            return spellId;
    return 0;
}

bool PlayerBotSetting::IsMountAllowed(Player const* player, uint32 spellId)
{
    if (!player || !spellId)
        return false;
    bool const isBot = player->GetSession() && player->GetSession()->IsBotSession();
    if (isBot &&
        ((player->getClass() == CLASS_DEATH_KNIGHT && spellId != 48778) ||
            (player->getClass() == CLASS_DEMON_HUNTER && spellId != 200175)))
        return false;
    if (spellId == 48778 && player->getClass() != CLASS_DEATH_KNIGHT)
        return false;
    if (spellId == 200175 && player->getClass() != CLASS_DEMON_HUNTER)
        return false;

    MountEntry const* mount = sDB2Manager.GetMount(spellId);
    if (!mount)
        return false;

    // Mount.db2's faction marker is not itself a faction identifier. Its
    // PlayerCondition supplies the actual race/class/faction requirements.
    // A missing condition on a faction-marked mount must not allow both teams.
    constexpr uint16 factionSpecificMount = 0x0004;
    bool const classMount = spellId == 48778 || spellId == 200175;
    // Generated bots have no personal hero quest progression. Their own class
    // mount is available at the legal starting level; human unlocks stay native.
    if (classMount && isBot)
        return player->getLevel() >= (spellId == 48778 ? 55 : 98);
    if (!classMount && (mount->Flags & factionSpecificMount) && !mount->PlayerConditionID)
        return false;
    if (mount->PlayerConditionID &&
        (!sPlayerConditionStore.LookupEntry(mount->PlayerConditionID) ||
         !ConditionMgr::IsPlayerMeetingCondition(const_cast<Player*>(player), mount->PlayerConditionID)))
        return false;
    return true;
}

void PlayerBotSetting::RemoveForbiddenMount(Player* player)
{
    if (!player || !player->IsMounted())
        return;

    std::vector<uint32> forbidden;
    if (Unit::AuraEffectList const* auras = player->GetAuraEffectsByType(SPELL_AURA_MOUNTED))
        for (AuraEffect const* aura : *auras)
        {
            uint32 const spellId = aura->GetId();
            if (!IsMountAllowed(player, spellId) ||
                (player->getClass() == CLASS_DEATH_KNIGHT && spellId != 48778) ||
                (player->getClass() == CLASS_DEMON_HUNTER && spellId != 200175))
                forbidden.push_back(spellId);
        }

    for (uint32 spellId : forbidden)
        player->RemoveOwnedAura(spellId, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
}

void PlayerBotSetting::Initialize()
{
	for (int i = 0; i < MAX_CLASSES; i++)
	{
		classesTrainersGUID[i][0] = 0;
		classesTrainersGUID[i][1] = 0;

		for (int j = 0; j < 3; j++)
			classesTalents[i][j].clear();

		for (int j = 0; j <= InventoryType::INVTYPE_RELIC; j++)
		{
			classesEquips[i][j].clear();
			starterEquips[i][j].clear();
		}

		classesCommonSpells[i].clear();
	}
	beastCreatureEntrys.clear();
	normalMountSpells.clear();
	fastMountSpells.clear();
	botFlyMountEntrys.clear();

	classesTrainersGUID[Classes::CLASS_WARRIOR][0] = 5113;
	classesTrainersGUID[Classes::CLASS_WARRIOR][1] = 4593;

	classesTrainersGUID[Classes::CLASS_PALADIN][0] = 5147;
	classesTrainersGUID[Classes::CLASS_PALADIN][1] = 23128;

	classesTrainersGUID[Classes::CLASS_DEATH_KNIGHT][0] = 28474;
	classesTrainersGUID[Classes::CLASS_DEATH_KNIGHT][1] = 28474;

	classesTrainersGUID[Classes::CLASS_ROGUE][0] = 5165;
	classesTrainersGUID[Classes::CLASS_ROGUE][1] = 4584;

	classesTrainersGUID[Classes::CLASS_DRUID][0] = 4217;
	classesTrainersGUID[Classes::CLASS_DRUID][1] = 3034;

	classesTrainersGUID[Classes::CLASS_HUNTER][0] = 5115;
	classesTrainersGUID[Classes::CLASS_HUNTER][1] = 3352;

	classesTrainersGUID[Classes::CLASS_SHAMAN][0] = 20407;
	classesTrainersGUID[Classes::CLASS_SHAMAN][1] = 3344;

	classesTrainersGUID[Classes::CLASS_MAGE][0] = 5146;
	classesTrainersGUID[Classes::CLASS_MAGE][1] = 4566;

	classesTrainersGUID[Classes::CLASS_WARLOCK][0] = 5172;
	classesTrainersGUID[Classes::CLASS_WARLOCK][1] = 4563;

	classesTrainersGUID[Classes::CLASS_PRIEST][0] = 5141;
	classesTrainersGUID[Classes::CLASS_PRIEST][1] = 4606;

	for (uint32 talentId = 0; talentId < sTalentStore.GetNumRows(); ++talentId)
	{
		TalentEntry const* talentInfo = sTalentStore.LookupEntry(talentId);

		if (!talentInfo)
			continue;

		//TalentTabEntry const* talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);

		//if (!talentTabInfo || talentTabInfo->tabpage < 0 || talentTabInfo->tabpage > 2)
		//	continue;
		//if ((talentTabInfo->GetClass()Mask & CLASSMASK_ALL_PLAYABLE) == 0)
		//	continue;
		//for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
		//{
		//	if (talentTabInfo->GetClass()Mask & (1 << (cls - 1)))
		//	{
		//		BotTalentPage& botPage = classesTalents[cls][talentTabInfo->tabpage];
		//		botPage.insert(BotTalentEntry(cls, talentTabInfo->tabpage, talentInfo));
		//	}
		//}
	}

	ItemTemplateContainer const* its = sObjectMgr->GetItemTemplateStore();
	for (ItemTemplateContainer::const_iterator itr = its->begin(); itr != its->end(); ++itr)
	{
		const ItemTemplate& item = itr->second;
		if (item.GetRequiredSkill() > 0 /*|| item.RequiredSpell > 0*/)// || item.AllowableRace != -1)
			continue;
		//if (item.RequiredReputationFaction != 0 || item.Area != 0 || item.Map != 0)
		//	continue;
		//if (item.Class != ItemClass::ITEM_CLASS_WEAPON && item.Class != ItemClass::ITEM_CLASS_ARMOR && item.Class != ItemClass::ITEM_CLASS_PROJECTILE)
		//	continue;
		
	//hxsd
		//if (item.ItemId	>=56806)
		//	continue;
		
		//if (item.Flags2 > 0)
		//	continue;
		if (item.ExtendedData->InventoryType <= InventoryType::INVTYPE_NON_EQUIP || item.ExtendedData->InventoryType > InventoryType::INVTYPE_RELIC)
			continue;
		// Only ordinary level-zero items belong in the starter pool;
		// do not change the established equipment pool used at 110.
		bool const starter = item.GetBaseRequiredLevel() <= 0 ||
			(item.GetBaseRequiredLevel() < 20 && item.GetQuality() <= 1 &&
			 item.ExtendedData->InventoryType != InventoryType::INVTYPE_AMMO);
		for (int i = 1; i < MAX_CLASSES; i++)
		{
			bool const normal = IsEquipByClasses(i, &item);
			bool const lowLevel = ((starter &&
				(item.GetBaseRequiredLevel() > 0 || item.GetQuality() <= 1)) ||
				(i == CLASS_MONK && item.GetBaseRequiredLevel() <= 109 &&
				 item.GetClass() == ITEM_CLASS_WEAPON &&
				 item.GetSubClass() == ITEM_SUBCLASS_WEAPON_STAFF)) &&
				IsStarterEquipByClasses(i, &item);
			if (!normal && !lowLevel)
				continue;
			//uint32 classMask = 1 << (i - 1);
			//if (item.AllowableClass & classMask)
			{
				InventoryType iType = InventoryType((item.ExtendedData->InventoryType == InventoryType::INVTYPE_ROBE) ? (InventoryType::INVTYPE_CHEST) : item.ExtendedData->InventoryType);
				uint32 const requiredLevel = std::max<int32>(0, item.GetBaseRequiredLevel());
				if (normal)
					classesEquips[i][iType][requiredLevel].AddItem(&item);
				if (lowLevel)
					starterEquips[i][iType][requiredLevel].AddItem(&item);
			}
		}
	}

	auto const* ctc = sObjectMgr->GetCreatureTemplates();
	for (auto itr = ctc->begin(); itr != ctc->end(); ++itr)
	{
		const CreatureTemplate& creature = itr->second;
		if (creature.Type == CreatureType::CREATURE_TYPE_BEAST && (creature.TypeFlags[0] & CreatureTypeFlags::CREATURE_TYPEFLAGS_TAMEABLE))
			beastCreatureEntrys.push_back(creature.Entry);
	}

	classesCommonSpells[1].push_back(33388);
	classesCommonSpells[1].push_back(33391);
	classesCommonSpells[1].push_back(196);
	classesCommonSpells[1].push_back(197);
	classesCommonSpells[1].push_back(201);
	classesCommonSpells[1].push_back(202);
	classesCommonSpells[1].push_back(198);
	classesCommonSpells[1].push_back(199);
	classesCommonSpells[1].push_back(264);
	classesCommonSpells[1].push_back(5011);
	classesCommonSpells[1].push_back(266);
	classesCommonSpells[1].push_back(200);
	classesCommonSpells[1].push_back(2567);
	classesCommonSpells[1].push_back(71);
	classesCommonSpells[1].push_back(355);
	classesCommonSpells[1].push_back(2458);
	classesCommonSpells[1].push_back(7386);

	classesCommonSpells[2].push_back(33388);
	classesCommonSpells[2].push_back(33391);
	classesCommonSpells[2].push_back(201);
	classesCommonSpells[2].push_back(202);
	classesCommonSpells[2].push_back(198);
	classesCommonSpells[2].push_back(199);
	classesCommonSpells[2].push_back(200);
	classesCommonSpells[2].push_back(7328);

	classesCommonSpells[3].push_back(33388);
	classesCommonSpells[3].push_back(33391);
	classesCommonSpells[3].push_back(1515);
	classesCommonSpells[3].push_back(6991);
	classesCommonSpells[3].push_back(883);
	classesCommonSpells[3].push_back(982);
	classesCommonSpells[3].push_back(2641);
	classesCommonSpells[3].push_back(197);
	classesCommonSpells[3].push_back(202);
	classesCommonSpells[3].push_back(200);
	classesCommonSpells[3].push_back(264);
	classesCommonSpells[3].push_back(5011);
	classesCommonSpells[3].push_back(266);

	classesCommonSpells[4].push_back(33388);
	classesCommonSpells[4].push_back(33391);
	classesCommonSpells[4].push_back(196);
	classesCommonSpells[4].push_back(201);
	classesCommonSpells[4].push_back(198);
	classesCommonSpells[4].push_back(1180);
	classesCommonSpells[4].push_back(15590);
	classesCommonSpells[4].push_back(2567);

	classesCommonSpells[5].push_back(33388);
	classesCommonSpells[5].push_back(33391);
	classesCommonSpells[5].push_back(1180);
	classesCommonSpells[5].push_back(5009);
	classesCommonSpells[5].push_back(227);
	classesCommonSpells[5].push_back(198);

	classesCommonSpells[6].push_back(33388);
	classesCommonSpells[6].push_back(33391);
	classesCommonSpells[6].push_back(197);
	classesCommonSpells[6].push_back(202);
	classesCommonSpells[6].push_back(199);
	classesCommonSpells[6].push_back(200);

	classesCommonSpells[7].push_back(33388);
	classesCommonSpells[7].push_back(33391);
	classesCommonSpells[7].push_back(196);
	classesCommonSpells[7].push_back(197);
	classesCommonSpells[7].push_back(198);
	classesCommonSpells[7].push_back(199);

	classesCommonSpells[8].push_back(33388);
	classesCommonSpells[8].push_back(33391);
	classesCommonSpells[8].push_back(1180);
	classesCommonSpells[8].push_back(5009);
	classesCommonSpells[8].push_back(227);
	classesCommonSpells[8].push_back(201);

	classesCommonSpells[9].push_back(33388);
	classesCommonSpells[9].push_back(33391);
	classesCommonSpells[9].push_back(1180);
	classesCommonSpells[9].push_back(5009);
	classesCommonSpells[9].push_back(227);
	classesCommonSpells[9].push_back(201);
	classesCommonSpells[9].push_back(712);
	classesCommonSpells[9].push_back(697);
	classesCommonSpells[9].push_back(691);

	classesCommonSpells[11].push_back(33388);
	classesCommonSpells[11].push_back(33391);
	classesCommonSpells[11].push_back(1180);
	classesCommonSpells[11].push_back(227);
	classesCommonSpells[11].push_back(198);
	classesCommonSpells[11].push_back(199);

	normalMountSpells.push_back(6777);
	fastMountSpells.push_back(23240);
	fastMountSpells.push_back(22717);
	fastMountSpells.push_back(22720);
	fastMountSpells.push_back(22719);
	fastMountSpells.push_back(22723);
	fastMountSpells.push_back(48027);
	fastMountSpells.push_back(42777);
	fastMountSpells.push_back(74918);
	fastMountSpells.push_back(73313);
	fastMountSpells.push_back(51412);
	fastMountSpells.push_back(65644);
	fastMountSpells.push_back(64659);
	fastMountSpells.push_back(65646);
	fastMountSpells.push_back(66091);
	fastMountSpells.push_back(65641);
	fastMountSpells.push_back(68188);
	fastMountSpells.push_back(66090);
	fastMountSpells.push_back(41252);
	fastMountSpells.push_back(68057);

	botFlyMountEntrys.insert(69395);
	botFlyMountEntrys.insert(67336);
	botFlyMountEntrys.insert(65439);
	botFlyMountEntrys.insert(32345);
	botFlyMountEntrys.insert(46199);
	botFlyMountEntrys.insert(75596);

	TC_LOG_INFO(LOG_FILTER_GENERAL, "Initialize all classes trainer talent and spell and equip.");
}

bool PlayerBotSetting::BindingPlayerHomePosition(Player* player)
{
	if (!player || !player->IsInWorld() || player->GetMap()->IsDungeon())
		return false;
	if (!MapManager::IsValidMapCoord(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation()))
		return false;

	uint32 bindspell = 3286;

	// send spell for homebinding (3286)
	player->CastSpell(player, bindspell, true);
	return true;
}

PlayerBotSetting::PlayerBotSetting(Player* player) :
m_Finish(true),
m_TenacitySetting(false),
m_ResetStep(0),
m_Player(player),
m_ActiveTalentType(3)
{
}

PlayerBotSetting::~PlayerBotSetting()
{
}

uint32 PlayerBotSetting::UpdateTalentType()
{
	if (m_Finish)
		m_ActiveTalentType = FindPlayerTalentType(m_Player);
	if (m_Player->IsPlayerBot())
	{
		uint32 lv = (m_Player->getLevel() < 9) ? 9 : m_Player->getLevel();

	}
	if (m_ActiveTalentType >= 3)
	{
		m_ActiveTalentType = PlayerBotSetting::FindPlayerTalentType(m_Player);
	}
	return m_ActiveTalentType;
}

uint32 PlayerBotSetting::GetTalentType()
{

	return m_ActiveTalentType;
}

bool PlayerBotSetting::ResetPlayerToLevel(uint32 level, uint32 talent, bool tenacity)
{
	if (!m_Player)
		return false;
	if (m_Player->IsPlayerBot())
		level = CheckBotLevel(m_Player, level);
	if (!m_Finish && !m_RepairFailed)
		return false;
    bool const existingMaxLevelBot = m_Player->IsPlayerBot() &&
        m_Player->getLevel() == 110 && level == 110;
    Item const* existingWeapon = m_Player->GetItemByPos(
        INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    m_PreserveExistingEquipment = existingMaxLevelBot;
    // Keep incomplete armor sets too. Only a missing/nonartifact starter weapon
    // needs initialization, otherwise the mandatory artifact gate never completes.
    m_InitializeArtifactEquipment = existingMaxLevelBot && !EquipIsTidiness() &&
        (!existingWeapon || !existingWeapon->GetTemplate() ||
            !existingWeapon->GetTemplate()->GetArtifactID());
    if (existingMaxLevelBot && talent == 3)
        talent = FindPlayerTalentType(m_Player);
    m_PreserveExistingTalents = existingMaxLevelBot &&
        talent == FindPlayerTalentType(m_Player);
	m_RepairOnly = false;
	m_RepairFailed = false;
	m_RepairAttempts = 0;
	if (talent <= 2 || ((talent == 4 || talent == 5) && m_Player->getClass() == CLASS_DRUID))
		m_ActiveTalentType = talent;
	else
		m_ActiveTalentType = 3;
	//TC_LOG_INFO("server.reset", ">> Reset player level to %d !", level);
	if (m_Player->getLevel() != level)
	{
		m_Player->GiveLevel(level);
		m_Player->SetUInt32Value(PLAYER_FIELD_XP, 0);
	}
	m_ResetStep = 1;
	m_Finish = false;
	m_TenacitySetting = tenacity;
	return true;
}

uint32 PlayerBotSetting::SwitchPlayerTalent(uint32 talent)
{
	return m_ActiveTalentType;
}

void PlayerBotSetting::SupplementAmmo()
{
	
}

void PlayerBotSetting::UpdateReset()
{
	if (m_Finish || m_RepairFailed)
		return;
	if (!m_Player || !m_Player->IsInWorld() || !m_Player->isAlive() ||
		m_Player->IsBeingTeleported())
		return;
    FelsoriaDynamicMetrics::Scope diagnosticSettings(FelsoriaDynamicMetrics::SettingsStep);
	if (m_RepairOnly)
	{
		if (m_Player->isInCombat())
			return;
		m_Player->LearnSpecializationSpells();
		LearnTalents();
		bool const artifactOK = EnsureArtifact();
        bool const talentsOK = HasCompleteTalents();
		if (talentsOK && artifactOK)
		{
			m_RepairOnly = false;
			m_Finish = true;
			m_TenacitySetting = false;
			m_Player->SendTalentsInfoData(false);
			m_Player->UpdateAllStats();
			m_Player->SaveToDB();
		}
		else if (++m_RepairAttempts >= 5)
		{
			m_RepairFailed = true;
            // FELSORIA_RDF_PREPARE_FAILURE_V2: report the failed readiness condition.
            TC_LOG_ERROR(LOG_FILTER_LFG,
                "BOT_PREPARATION_REPAIR_FAILED guid=%u class=%u spec=%u talents=%u artifact=%u attempts=%u",
                m_Player->GetGUIDLow(), uint32(m_Player->getClass()),
                m_Player->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID),
                uint32(talentsOK), uint32(artifactOK), m_RepairAttempts);
		}
		return;
	}

	if (m_Player->isInCombat())
		m_Player->CombatStop(true);
	switch (m_ResetStep)
	{
	case 1:
		if (m_Player->IsPlayerBot())
		{
			std::vector<ChrSpecializationEntry const*> specs;
			for (uint32 index = 0; index < MAX_SPECIALIZATIONS; ++index)
				if (ChrSpecializationEntry const* spec = sDB2Manager.GetChrSpecializationByIndex(m_Player->getClass(), index))
					specs.push_back(spec);
			ChrSpecializationEntry const* selected = nullptr;
			if (!specs.empty() && m_ActiveTalentType == 3)
				selected = specs[urand(0, uint32(specs.size() - 1))];
			else
			{
				uint32 index = m_ActiveTalentType;
				if (m_Player->getClass() == CLASS_DRUID && (index == 2 || index == 4))
					index = 3;
				else if (m_Player->getClass() == CLASS_DRUID && index == 5)
					index = 2;
				selected = sDB2Manager.GetChrSpecializationByIndex(m_Player->getClass(), index);
			}
			if (!selected)
			{
				m_RepairFailed = true;
				TC_LOG_ERROR(LOG_FILTER_GENERAL, "BOT_SPEC_SELECTION_FAILED guid=%u", m_Player->GetGUIDLow());
				return;
			}
			if (!m_PreserveExistingTalents)
				m_Player->ActivateTalentGroup(selected);
			m_ActiveTalentType = FindPlayerTalentType(m_Player);
		}
		if (!m_PreserveExistingTalents)
			m_Player->ResetTalents(true);
		++m_ResetStep;
		break;
	case 2:
		// Teach talents after trainer spells and specialization spells.
		++m_ResetStep;
		break;
	case 3:
		RemoveSpells();
		++m_ResetStep;
		break;
	case 4:
		LearnCommonSpells();
		++m_ResetStep;
		break;
	case 5:
		LearnSpells();
		m_Player->LearnSpecializationSpells();
		LearnTalents();
		++m_ResetStep;
		break;
	case 6:
	        // Real character boosts receive their equipment later from
	        // EquipCharacterBoostGear. Skip PlayerBot item generation.
	        if (!m_Player->IsPlayerBot())
	        {
	                m_ResetStep = 11;
	                break;
	        }

	        m_Player->UpdateSkillsForLevel();
	        UnequipFromAll();
	        ++m_ResetStep;
	        break;
	case 7:
		CheckInventroy();
		++m_ResetStep;
		break;
	case 8:
		AddEquipFromAll();
		++m_ResetStep;
		break;
	case 9:
		UpequipFromAll();
		++m_ResetStep;
		break;
	case 10:
		SupplementOtherItems();
		++m_ResetStep;
		break;
	case 11:
		if (!m_Player->IsPlayerBot())
			m_Player->SendTalentsInfoData(false);
		++m_ResetStep;
		break;
	case 12:
		if (m_Player->IsPlayerBot())
			CheckHunterPet(m_Player);
		++m_ResetStep;
		break;
	case 13:
        if (m_Player->IsPlayerBot() && m_Player->getLevel() < 110)
        {
            FillMissingEquipmentSlots();
            if (!EquipIsTidiness())
            {
                m_RepairFailed = true;
                TC_LOG_ERROR(LOG_FILTER_GENERAL,
                    "DYNAMIC_BOT_GEAR_FAILED guid=%u class=%u level=%u; readiness blocked",
                    m_Player->GetGUIDLow(), uint32(m_Player->getClass()), uint32(m_Player->getLevel()));
                return;
            }
        }
		if (m_Player->IsPlayerBot() && (!HasCompleteTalents() || !EnsureArtifact()))
		{
			m_RepairOnly = true;
			m_RepairAttempts = 0;
			return;
		}
		PlayerBotSetting::ClearUnknowMount(m_Player);
		m_Player->SetFullHealth();
		m_Player->UpdateSkillsForLevel();
		m_Player->UpdateAllStats();
		m_Player->SaveToDB();
		++m_ResetStep;
		break;
	}

	m_Finish = (m_ResetStep >= 14);
	if (m_Finish && m_Player->IsPlayerBot())
	{
		BotGlobleSchedule schedule(BotGlobleScheduleType::BGSType_DelayLevelup, m_Player->GetGUID());
		PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(m_Player->GetSession());
		if (pSession)
			pSession->PushScheduleToQueue(schedule);
	}
	if (m_Finish)
		m_TenacitySetting = false;
}

void PlayerBotSetting::LearnTalents()
{
    (void)0; // FELSORIA_P11_QUIET_TALENT_START
    if (!m_Player || m_Player->getLevel() < 15)
        return;

    uint32 const playerClass = m_Player->getClass();
    uint32 const activeSpec = m_Player->GetUInt32Value(
        PLAYER_FIELD_CURRENT_SPEC_ID);
    uint32 const playerSeed = m_Player->GetGUIDLow();

    for (uint32 tier = 0; tier < MAX_TALENT_TIERS; ++tier)
    {
        if (tier >= m_Player->GetUInt32Value(PLAYER_FIELD_MAX_TALENT_TIERS))
            break;
        uint32 const preferredColumn =
            (playerSeed + tier + m_ActiveTalentType) %
            MAX_TALENT_COLUMNS;

        TalentEntry const* selectedTalent = nullptr;

        for (uint32 columnOffset = 0;
             columnOffset < MAX_TALENT_COLUMNS &&
             !selectedTalent;
             ++columnOffset)
        {
            uint32 const column =
                (preferredColumn + columnOffset) %
                MAX_TALENT_COLUMNS;

            TalentEntry const* genericTalent = nullptr;

            for (TalentEntry const* talentInfo :
                 sDB2Manager._talentByPos
                     [playerClass][tier][column])
            {
                if (!talentInfo || !talentInfo->SpellID)
                    continue;

                if (talentInfo->SpecID == activeSpec)
                {
                    selectedTalent = talentInfo;
                    break;
                }

                if (talentInfo->SpecID == 0)
                    genericTalent = talentInfo;
            }

            if (!selectedTalent)
                selectedTalent = genericTalent;
        }

        if (selectedTalent)
        {
			// Do not replace a learned talent outside a resting area.
			bool tierComplete = false;
			for (TalentEntry const* existing : sTalentStore)
				if (existing && existing->ClassID == playerClass && existing->TierID == tier &&
					(!existing->SpecID || existing->SpecID == activeSpec) &&
					m_Player->HasTalent(existing->ID, m_Player->GetActiveTalentGroup()) &&
					m_Player->HasSpell(existing->SpellID))
					tierComplete = true;
			if (tierComplete)
				continue;
            bool const learned =
                m_Player->LearnTalent(selectedTalent->ID);

            (void)learned; // FELSORIA_P11_QUIET_TALENT_RESULT
        }
        else
        {
            TC_LOG_ERROR(
                LOG_FILTER_GENERAL,
                "PlayerBot talent missing: guid=%u class=%u spec=%u tier=%u",
                m_Player->GetGUIDLow(),
                playerClass,
                activeSpec,
                tier);
        }
    }

    m_Player->SendTalentsInfoData(false);
}
void PlayerBotSetting::LearnCommonSpells()
{
	uint8 cls = m_Player->getClass();
	if (cls <= 0 || cls >= MAX_CLASSES)
		return;
    if (m_Player->getLevel() >= 20)
        m_Player->learnSpell(33388, false);
    if (m_Player->getLevel() >= 40)
        m_Player->learnSpell(33391, false);
    if (cls == CLASS_DEMON_HUNTER)
        m_Player->learnSpell(200175, false);
    else if (cls == CLASS_DEATH_KNIGHT)
        m_Player->learnSpell(48778, false);
	BotCommonSpells& commonSpells = classesCommonSpells[cls];
	for (BotCommonSpells::iterator itSpell = commonSpells.begin();
		itSpell != commonSpells.end();
		itSpell++)
	{
		uint32 spellID = *itSpell;
		if (m_Player->HasSpell(spellID))
			continue;
		m_Player->learnSpell(spellID, false);
	}

	for (BeastCreatureEntrys::iterator itMount = normalMountSpells.begin();
		itMount != normalMountSpells.end();
		itMount++)
	{
		uint32 spellID = *itMount;
		if (!IsMountAllowed(m_Player, spellID) || m_Player->HasSpell(spellID))
			continue;
		m_Player->learnSpell(spellID, false);
	}

	if (m_Player->IsPlayerBot())
	{
		for (BeastCreatureEntrys::iterator itMount = fastMountSpells.begin();
			itMount != fastMountSpells.end();
			itMount++)
		{
			uint32 spellID = *itMount;
			if (!IsMountAllowed(m_Player, spellID) || m_Player->HasSpell(spellID))
				continue;
			m_Player->learnSpell(spellID, false);
		}
	}
}

void PlayerBotSetting::RemoveSpells()
{
    if (m_PreserveExistingTalents)
        return;
	const TrainerSpellData* spellData = sObjectMgr->GetNpcTrainerSpells(classesTrainersGUID[m_Player->getClass()][(m_Player->GetTeamId() == TeamId::TEAM_ALLIANCE) ? 0 : 1]);
	if (!spellData)
		return;
	for (TrainerSpellMap::const_iterator itBTS = spellData->spellList.begin();
		itBTS != spellData->spellList.end();
		itBTS++)
	{
		const TrainerSpell& tSpell = itBTS->second;
		uint32 spellID = tSpell.spell;
		if (m_Player->HasSpell(spellID))
		{
			m_Player->removeSpell(spellID, false, false);
		}
	}
}

void PlayerBotSetting::LearnSpells()
{
	uint8 level = m_Player->getLevel();
	const TrainerSpellData* spellData = sObjectMgr->GetNpcTrainerSpells(classesTrainersGUID[m_Player->getClass()][(m_Player->GetTeamId() == TeamId::TEAM_ALLIANCE) ? 0 : 1]);
	if (!spellData)
		return;
	for (TrainerSpellMap::const_iterator itBTS = spellData->spellList.begin();
		itBTS != spellData->spellList.end();
		itBTS++)
	{
		const TrainerSpell& tSpell = itBTS->second;
		uint32 spellID = tSpell.spell;
		if (tSpell.reqLevel > level)
		{
			continue;
		}
		if (m_Player->HasSpell(spellID))
			continue;
		if (tSpell.IsCastable())
		{
			m_Player->CastSpell(m_Player, spellID, true);
			//TC_LOG_WARN("PlayerBotSetting", "Player %s Learn spell %d, spell is castable, do cast.", m_Player->GetName().c_str(), spellID);
		}
		else
			m_Player->learnSpell(spellID, false);
	}
}

void PlayerBotSetting::CheckInventroy()
{
	if (!m_Player->GetItemByPos(255, InventorySlots::INVENTORY_SLOT_BAG_START) ||
		!m_Player->GetItemByPos(255, InventorySlots::INVENTORY_SLOT_BAG_START + 1) ||
		!m_Player->GetItemByPos(255, InventorySlots::INVENTORY_SLOT_BAG_START + 2))
	{
		const ItemTemplate* item = sObjectMgr->GetItemTemplate(21876);
		uint32 count = 1;
		uint32 noSpaceForCount = 0;
		ItemPosCountVec dest;
		InventoryResult msg = m_Player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item->GetId(), count, &noSpaceForCount);
		if (msg != EQUIP_ERR_OK)
			count -= noSpaceForCount;
		if (count <= 0 || dest.empty())
			return;
		Item* itemInst = m_Player->StoreNewItem(dest, item->GetId(), true, Item::GenerateItemRandomPropertyId(item->GetId()));
		if (itemInst)
			EquipItem(itemInst);
	}
}

void PlayerBotSetting::UnequipFromAll()
{
    if (m_PreserveExistingEquipment)
        return;
	bool isBot = m_Player->IsPlayerBot();
	for (uint8 slot = EquipmentSlots::EQUIPMENT_SLOT_HEAD; slot < EquipmentSlots::EQUIPMENT_SLOT_END; slot++)
	{
		uint16 pos = (255 << 8) | slot;
		if (m_Player->IsEquipmentPos(pos) || m_Player->IsBagPos(pos))
		{
			InventoryResult msg = m_Player->CanUnequipItem(pos, false);
			if (msg != EQUIP_ERR_OK)
				continue;
		}
		Item* pItem = m_Player->GetItemByPos(255, slot);
		if (!pItem)
			continue;
		m_Player->DestroyItem(255, slot, true);
	}

	for (uint8 slot = InventoryPackSlots::INVENTORY_SLOT_ITEM_START+1; slot < InventoryPackSlots::INVENTORY_SLOT_ITEM_END; slot++)
	{
		Item* pItem = m_Player->GetItemByPos(255, slot);
		if (!pItem)
			continue;
		if (!isBot && pItem->GetEntry() == 6948)
			continue;
		m_Player->DestroyItem(255, slot, true);
	}

	for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
	{
		if (Bag* pBag = m_Player->GetBagByPos(i))
		{
			for (uint32 j = 0; j < pBag->GetBagSize(); j++)
			{
				Item* pItem = pBag->GetItemByPos(uint8(j));
				if (!pItem)
					continue;
				if (!isBot && pItem->GetEntry() == 6948)
					continue;
				m_Player->DestroyItem(i, uint8(j), true);
			}
		}
	}

}

void PlayerBotSetting::AddEquipFromAll()
{
	m_NeedEquips.clear();
    if (m_PreserveExistingEquipment)
        return; // UpequipFromAll fills only missing slots; EnsureArtifact repairs weapons.
	uint32 level = m_Player->getLevel();
	uint8 prof = m_Player->getClass();
	if (prof <= 0 || prof >= MAX_CLASSES)
		return;
	const ItemTemplate* firstFinger = NULL;
	const ItemTemplate* firstTrinket = NULL;
	for (int i = 0; i < InventoryType::INVTYPE_RELIC; i++)
	{
		//BotEquips& equips = classesEquips[prof][i];
		//BotEquips::iterator itEquip = equips.find(level);
		//if (itEquip == equips.end())
		//	continue;
		if (i != InventoryType::INVTYPE_CLOAK && i > InventoryType::INVTYPE_TRINKET)
			continue;
		if (i == InventoryType::INVTYPE_FINGER && !firstFinger)
		{
			const ItemTemplate* item = GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_FINGER, level, firstFinger);
			firstFinger = item;
			AddOnceEquip(item);
		}
		else if (i == InventoryType::INVTYPE_TRINKET && !firstTrinket)
		{
			const ItemTemplate* item = GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_TRINKET, level, firstFinger);
			firstTrinket = item;
			AddOnceEquip(item);
		}
		else
		{
			const ItemTemplate* item = GetRandomItemFromLoopLV(prof, (InventoryType)i, level);
			AddOnceEquip(item);
		}
	}
	if (firstFinger)
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_FINGER, level, firstFinger));
	if (firstTrinket)
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_TRINKET, level, firstTrinket));

	if (level == 110 && AddArtifactWeaponForCurrentSpec())
		return;

	switch (prof)
	{
	case 1:
		RandomWeaponByWarrior();
		break;
	case 2:
		RandomWeaponByPaladin();
		break;
	case 3:
		RandomWeaponByHunter();
		break;
	case 4:
		RandomWeaponByRogue();
		break;
	case 5:
		RandomWeaponByPriest();
		break;
	case 6:
		RandomWeaponByDeathKight();
		break;
	case 7:
		RandomWeaponByShaman();
		break;
	case 8:
		RandomWeaponByMage();
		break;
	case 9:
		RandomWeaponByWarlock();
		break;
	case CLASS_MONK:
	case CLASS_DEMON_HUNTER:
		if (level < 110)
		{
			// Mistweaver is specialization index 1. Prefer its staff over
			// a physical one-handed weapon.
			if (prof == CLASS_MONK && m_ActiveTalentType == 1)
			{
				ItemTemplate const* staff = GetRandomItemFromLoopLV(prof, INVTYPE_2HWEAPON, level);
				if (staff)
				{
					AddOnceEquip(staff);
					break;
				}
			}
			ItemTemplate const* main = GetRandomItemFromLoopLV(prof, INVTYPE_WEAPON, level);
			if (!main)
				main = GetRandomItemFromLoopLV(prof, INVTYPE_WEAPONMAINHAND, level);
			if (main)
			{
				AddOnceEquip(main);
				ItemTemplate const* offhand = GetRandomItemFromLoopLV(prof, INVTYPE_WEAPON, level, main);
				if (!offhand)
					offhand = GetRandomItemFromLoopLV(prof, INVTYPE_WEAPONOFFHAND, level, main);
				AddOnceEquip(offhand);
			}
			else if (prof == CLASS_MONK)
				AddOnceEquip(GetRandomItemFromLoopLV(prof, INVTYPE_2HWEAPON, level));
		}
		break;
	case 11:
		RandomWeaponByDruid();
		break;
	default:
		break;
	}
}

void PlayerBotSetting::FillMissingEquipmentSlots()
{
    if (m_Player->getLevel() < 110)
    {
        // The max-level fallback intentionally looks for the strongest template.
        // Low-level bots instead use the same progression guard as their pools.
        static uint8 const slots[] =
        {
            EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_NECK, EQUIPMENT_SLOT_SHOULDERS,
            EQUIPMENT_SLOT_CHEST, EQUIPMENT_SLOT_WAIST, EQUIPMENT_SLOT_LEGS,
            EQUIPMENT_SLOT_FEET, EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_HANDS,
            EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2,
            EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2,
            EQUIPMENT_SLOT_BACK, EQUIPMENT_SLOT_MAINHAND,
            EQUIPMENT_SLOT_OFFHAND
        };
        uint32 const level = m_Player->getLevel();
        uint32 const cls = m_Player->getClass();
        uint32 const spec = m_ActiveTalentType;
        ItemTemplate const* best[sizeof(slots) / sizeof(slots[0])] = {};
        using GearKey = std::tuple<uint32, uint32, uint32>;
        static std::mutex catalogLock;
        static std::map<GearKey, std::vector<ItemTemplate const*>> catalogs;
        std::vector<ItemTemplate const*> candidates;
        {
            std::lock_guard<std::mutex> lock(catalogLock);
            GearKey const key(level, cls, spec);
            auto found = catalogs.find(key);
            if (found == catalogs.end())
            {
                if (catalogs.size() >= 128) catalogs.erase(catalogs.begin());
                auto& catalog = catalogs[key];
                for (ItemSparseEntry const* sparse : sItemSparseStore)
                {
                    if (!sparse) continue;
                    ItemTemplate const* item = sObjectMgr->GetItemTemplate(sparse->ID);
                    if (IsLevelAppropriateEquip(level, item) && IsLowLevelSpecEquip(cls, spec, item))
                        catalog.push_back(item);
                }
                candidates = catalog;
            }
            else
                candidates = found->second;
        }
        for (ItemTemplate const* candidate : candidates)
        {
            if (m_Player->CanUseItem(candidate) != EQUIP_ERR_OK)
                continue;
            for (size_t index = 0; index < sizeof(slots) / sizeof(slots[0]); ++index)
            {
                uint8 const slot = slots[index];
                if (m_Player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot) ||
                    !MatchEquipmentSlot(slot, candidate))
                    continue;
                ItemTemplate const* current = best[index];
                if (current && candidate->GetBaseRequiredLevel() < current->GetBaseRequiredLevel())
                    continue;
                if (current && candidate->GetBaseRequiredLevel() == current->GetBaseRequiredLevel() &&
                    candidate->ItemLevel <= current->ItemLevel)
                    continue;
                uint16 dest = 0;
                if (m_Player->CanEquipNewItem(slot, dest, candidate->GetId(), false) != EQUIP_ERR_OK ||
                    uint8(dest) != slot)
                    continue;
                best[index] = candidate;
            }
        }
        for (size_t index = 0; index < sizeof(slots) / sizeof(slots[0]); ++index)
        {
            uint8 const slot = slots[index];
            ItemTemplate const* candidate = best[index];
            if (!candidate || m_Player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                continue;
            // A selected main hand may now forbid an offhand.
            uint16 dest = 0;
            if (m_Player->CanEquipNewItem(slot, dest, candidate->GetId(), false) != EQUIP_ERR_OK ||
                uint8(dest) != slot)
                continue;
            size_t const previousSize = m_NeedEquips.size();
            AddOnceEquip(candidate);
            if (m_NeedEquips.size() > previousSize)
            {
                EquipItem(m_NeedEquips.back());
                m_NeedEquips.clear();
            }
        }
        return;
    }

    static uint8 const requiredSlots[] =
    {
        EquipmentSlots::EQUIPMENT_SLOT_HEAD,
        EquipmentSlots::EQUIPMENT_SLOT_NECK,
        EquipmentSlots::EQUIPMENT_SLOT_SHOULDERS,
        EquipmentSlots::EQUIPMENT_SLOT_CHEST,
        EquipmentSlots::EQUIPMENT_SLOT_WAIST,
        EquipmentSlots::EQUIPMENT_SLOT_LEGS,
        EquipmentSlots::EQUIPMENT_SLOT_FEET,
        EquipmentSlots::EQUIPMENT_SLOT_WRISTS,
        EquipmentSlots::EQUIPMENT_SLOT_HANDS,
        EquipmentSlots::EQUIPMENT_SLOT_FINGER1,
        EquipmentSlots::EQUIPMENT_SLOT_FINGER2,
        EquipmentSlots::EQUIPMENT_SLOT_TRINKET1,
        EquipmentSlots::EQUIPMENT_SLOT_TRINKET2,
        EquipmentSlots::EQUIPMENT_SLOT_BACK,
        EquipmentSlots::EQUIPMENT_SLOT_MAINHAND
    };

    for (uint8 slot : requiredSlots)
    {
        if (m_Player->GetItemByPos(
            INVENTORY_SLOT_BAG_0,
            slot))
        {
            continue;
        }

        ItemTemplate const* bestItem = NULL;
        uint32 bestLevel = 0;

        for (ItemSparseEntry const* sparse : sItemSparseStore)
        {
            if (!sparse)
                continue;

            ItemTemplate const* itemTemplate =
                sObjectMgr->GetItemTemplate(sparse->ID);

            if (!itemTemplate)
                continue;

            if (itemTemplate->GetArtifactID())
                continue;

            if (!MatchEquipmentSlot(slot, itemTemplate))
                continue;

            if (itemTemplate->GetBaseRequiredLevel() >
                m_Player->getLevel())
            {
                continue;
            }

            if (!IsEquipByClsAndTal(
                m_Player->getClass(),
                m_Player->FindTalentType(),
                itemTemplate,
                0))
            {
                continue;
            }

            if (m_Player->CanUseItem(itemTemplate) !=
                EQUIP_ERR_OK)
            {
                continue;
            }

            uint16 equipDest = 0;

            if (m_Player->CanEquipNewItem(
                NULL_SLOT,
                equipDest,
                itemTemplate->GetId(),
                true) != EQUIP_ERR_OK)
            {
                continue;
            }

            uint32 itemLevel =
                GetItemLevelByAI(itemTemplate);

            if (!bestItem || itemLevel > bestLevel)
            {
                bestItem = itemTemplate;
                bestLevel = itemLevel;
            }
        }

        // Ä‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚ÂltalÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬Ă„â€¦Ă„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ä‚â€ąĂ˘â‚¬Ë‡nos mÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬Ă„â€¦Ă„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ä‚â€ąĂ˘â‚¬Ë‡sodik keresÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Â©si kÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Â¶r:
        // megfelelÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ˘â‚¬ĹľÄ‚ËĂ˘â€šÂ¬Ă‚Â¦Ä‚â€žĂ˘â‚¬ĹˇÄ‚â€ąĂ‚ÂĂ„â€šĂ‹ÂÄ‚ËĂ˘â€šÂ¬ÄąË‡Ä‚â€šĂ‚Â¬Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Â armor vagy kÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Â¶zÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Â¶s Ä‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Â©kszer/kÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Â¶peny statszÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ˘â‚¬ĹľÄ‚ËĂ˘â€šÂ¬Ă‚Â¦Ä‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Â±rÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Â©s nÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Â©lkÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ˘â‚¬Ä…Ă„Ä…Ă„â€žl.
        if (!bestItem)
        {
            for (ItemSparseEntry const* sparse : sItemSparseStore)
            {
                if (!sparse)
                    continue;

                ItemTemplate const* itemTemplate =
                    sObjectMgr->GetItemTemplate(sparse->ID);

                if (!itemTemplate ||
                    itemTemplate->GetArtifactID())
                {
                    continue;
                }

                if (!MatchEquipmentSlot(slot, itemTemplate))
                    continue;

                if (itemTemplate->GetBaseRequiredLevel() >
                    m_Player->getLevel())
                {
                    continue;
                }

                if (m_Player->CanUseItem(itemTemplate) !=
                    EQUIP_ERR_OK)
                {
                    continue;
                }

                bool correctItemType =
                    IsCommonEquip(itemTemplate);

                if (!correctItemType)
                {
                    switch (m_Player->getClass())
                    {
                    case CLASS_WARRIOR:
                        correctItemType =
                            IsWarriorEquip(itemTemplate);
                        break;
                    case CLASS_PALADIN:
                        correctItemType =
                            IsPaladinEquip(itemTemplate);
                        break;
                    case CLASS_HUNTER:
                        correctItemType =
                            IsHunterEquip(itemTemplate);
                        break;
                    case CLASS_ROGUE:
                    case CLASS_MONK:
                    case CLASS_DEMON_HUNTER:
                        correctItemType =
                            IsRogueEquip(itemTemplate);
                        break;
                    case CLASS_PRIEST:
                        correctItemType =
                            IsPriestEquip(itemTemplate);
                        break;
                    case CLASS_DEATH_KNIGHT:
                        correctItemType =
                            IsDeathKightEquip(itemTemplate);
                        break;
                    case CLASS_SHAMAN:
                        correctItemType =
                            IsShamanEquip(itemTemplate);
                        break;
                    case CLASS_MAGE:
                        correctItemType =
                            IsMageEquip(itemTemplate);
                        break;
                    case CLASS_WARLOCK:
                        correctItemType =
                            IsWarlockEquip(itemTemplate);
                        break;
                    case CLASS_DRUID:
                        correctItemType =
                            IsDruidEquip(itemTemplate);
                        break;
                    default:
                        break;
                    }
                }

                if (!correctItemType)
                    continue;

                uint16 equipDest = 0;

                if (m_Player->CanEquipNewItem(
                    NULL_SLOT,
                    equipDest,
                    itemTemplate->GetId(),
                    true) != EQUIP_ERR_OK)
                {
                    continue;
                }

                uint32 itemLevel =
                    GetItemLevelByAI(itemTemplate);

                if (!bestItem || itemLevel > bestLevel)
                {
                    bestItem = itemTemplate;
                    bestLevel = itemLevel;
                }
            }
        }

        if (!bestItem)
            continue;

        AddOnceEquip(bestItem);

        if (!m_NeedEquips.empty())
        {
            Item* newItem = m_NeedEquips.back();
            EquipItem(newItem);
            m_NeedEquips.clear();
        }
    }
}

bool PlayerBotSetting::EquipCharacterBoostGear()
{
    if (!m_Player || m_Player->getLevel() != 110)
        return false;

    struct BoostGearChoice
    {
        uint8 Slot;
        ItemTemplate const* Item;
    };

    static uint8 const armorSlots[] =
    {
        EQUIPMENT_SLOT_HEAD,
        EQUIPMENT_SLOT_NECK,
        EQUIPMENT_SLOT_SHOULDERS,
        EQUIPMENT_SLOT_CHEST,
        EQUIPMENT_SLOT_WAIST,
        EQUIPMENT_SLOT_LEGS,
        EQUIPMENT_SLOT_FEET,
        EQUIPMENT_SLOT_WRISTS,
        EQUIPMENT_SLOT_HANDS,
        EQUIPMENT_SLOT_FINGER1,
        EQUIPMENT_SLOT_FINGER2,
        EQUIPMENT_SLOT_TRINKET1,
        EQUIPMENT_SLOT_TRINKET2,
        EQUIPMENT_SLOT_BACK
    };

    std::vector<BoostGearChoice> armorChoices;
    std::set<uint32> selectedEntries;

    // ElÄąâ€szÄ‚Â¶r minden 850-es armor/Ä‚Â©kszer tÄ‚Ë‡rgyat megkeresÄ‚Ä˝nk.
    // A meglÄ‚Â©vÄąâ€ tÄ‚Ë‡rgyakat csak teljes talÄ‚Ë‡lat utÄ‚Ë‡n tÄ‚Â¶rÄ‚Â¶ljÄ‚Ä˝k.
    for (uint8 slot : armorSlots)
    {
        ItemTemplate const* selected = nullptr;

        for (ItemSparseEntry const* sparse : sItemSparseStore)
        {
            if (!sparse)
                continue;

            ItemTemplate const* item =
                sObjectMgr->GetItemTemplate(sparse->ID);

            if (!item || item->GetArtifactID())
                continue;

            if (item->ItemLevel != 850)
                continue;

            if (item->GetBaseRequiredLevel() > 110)
                continue;

            if (!MatchEquipmentSlot(slot, item))
                continue;

            if (!IsEquipByClsAndTal(
                    m_Player->getClass(),
                    m_ActiveTalentType,
                    item,
                    0))
            {
                continue;
            }

            if (m_Player->CanUseItem(item) != EQUIP_ERR_OK)
                continue;

            if ((slot == EQUIPMENT_SLOT_FINGER2 ||
                 slot == EQUIPMENT_SLOT_TRINKET2) &&
                selectedEntries.count(item->GetId()))
            {
                continue;
            }

            if (!selected || item->GetId() < selected->GetId())
                selected = item;
        }

        if (!selected)
        {
            TC_LOG_ERROR(
                LOG_FILTER_GENERAL,
                "CHARACTER_BOOST_ARMOR_NOT_FOUND guid=%u class=%u talent=%u slot=%u itemLevel=850",
                m_Player->GetGUIDLow(),
                uint32(m_Player->getClass()),
                m_ActiveTalentType,
                uint32(slot));

            return false;
        }

        armorChoices.push_back({ slot, selected });
        selectedEntries.insert(selected->GetId());
    }

    // MegkeressÄ‚Ä˝k a specnek megfelelÄąâ€ normÄ‚Ë‡l fÄąâ€fegyvert.
    ItemTemplate const* mainWeapon = nullptr;

    for (ItemSparseEntry const* sparse : sItemSparseStore)
    {
        if (!sparse)
            continue;

        ItemTemplate const* item =
            sObjectMgr->GetItemTemplate(sparse->ID);

        if (!item ||
            item->GetArtifactID() ||
            sDB2Manager.IsChildItem(item->GetId()))
        {
            continue;
        }

        if (item->GetClass() != ITEM_CLASS_WEAPON)
            continue;

        // FELSORIA_HUNTER_BOOST_BOW_V1: boost hunters receive a bow.
        if (m_Player->getClass() == CLASS_HUNTER &&
            item->GetSubClass() != ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW)
        {
            continue;
        }

        uint32 const itemLevel = item->ItemLevel;

        if (itemLevel != 700)
            continue;

        if (item->GetBaseRequiredLevel() > 110)
            continue;

        InventoryType const inventoryType =
            item->GetInventoryType();

        if (inventoryType != INVTYPE_WEAPON &&
            inventoryType != INVTYPE_2HWEAPON &&
            inventoryType != INVTYPE_WEAPONMAINHAND &&
            inventoryType != INVTYPE_RANGED &&
            inventoryType != INVTYPE_RANGEDRIGHT &&
            inventoryType != INVTYPE_THROWN)
        {
            continue;
        }

        if (!IsEquipByClsAndTal(
                m_Player->getClass(),
                m_ActiveTalentType,
                item,
                0))
        {
            continue;
        }

        if (m_Player->CanUseItem(item) != EQUIP_ERR_OK)
            continue;

        if (!mainWeapon ||
            itemLevel > mainWeapon->ItemLevel ||
            (itemLevel == mainWeapon->ItemLevel &&
             item->GetId() < mainWeapon->GetId()))
        {
            mainWeapon = item;
        }
    }

    if (!mainWeapon)
    {
        TC_LOG_ERROR(
            LOG_FILTER_GENERAL,
            "CHARACTER_BOOST_MAIN_WEAPON_NOT_FOUND guid=%u class=%u talent=%u",
            m_Player->GetGUIDLow(),
            uint32(m_Player->getClass()),
            m_ActiveTalentType);

        return false;
    }

    // Minden szÄ‚Ä˝ksÄ‚Â©ges tÄ‚Ë‡rgy megvan, eltÄ‚Ë‡volÄ‚Â­thatÄ‚Ĺ‚ a reset Ä‚Ë‡ltal
    // lÄ‚Â©trehozott ideiglenes felszerelÄ‚Â©s Ä‚Â©s artifact.
    for (BoostGearChoice const& choice : armorChoices)
    {
        if (m_Player->GetItemByPos(
                INVENTORY_SLOT_BAG_0,
                choice.Slot))
        {
            m_Player->DestroyItem(
                INVENTORY_SLOT_BAG_0,
                choice.Slot,
                true);
        }
    }

    static uint8 const weaponSlots[] =
    {
        EQUIPMENT_SLOT_MAINHAND,
        EQUIPMENT_SLOT_OFFHAND,
        EQUIPMENT_SLOT_RANGED
    };

    for (uint8 slot : weaponSlots)
    {
        if (m_Player->GetItemByPos(
                INVENTORY_SLOT_BAG_0,
                slot))
        {
            m_Player->DestroyItem(
                INVENTORY_SLOT_BAG_0,
                slot,
                true);
        }
    }

    auto createAndEquip =
        [this](ItemTemplate const* item) -> bool
    {
        if (!item)
            return false;

        ItemPosCountVec destination;

        if (m_Player->CanStoreNewItem(
                NULL_BAG,
                NULL_SLOT,
                destination,
                item->GetId(),
                1) != EQUIP_ERR_OK ||
            destination.empty())
        {
            return false;
        }

        Item* newItem = m_Player->StoreNewItem(
            destination,
            item->GetId(),
            true,
            Item::GenerateItemRandomPropertyId(
                item->GetId(),
                m_Player->GetLootSpecID()));

        return newItem && EquipItem(newItem);
    };

    for (BoostGearChoice const& choice : armorChoices)
    {
        if (!createAndEquip(choice.Item))
        {
            TC_LOG_ERROR(
                LOG_FILTER_GENERAL,
                "CHARACTER_BOOST_ARMOR_EQUIP_FAILED guid=%u item=%u slot=%u",
                m_Player->GetGUIDLow(),
                choice.Item->GetId(),
                uint32(choice.Slot));

            return false;
        }
    }

    if (!createAndEquip(mainWeapon))
    {
        TC_LOG_ERROR(
            LOG_FILTER_GENERAL,
            "CHARACTER_BOOST_MAIN_WEAPON_EQUIP_FAILED guid=%u item=%u",
            m_Player->GetGUIDLow(),
            mainWeapon->GetId());

        return false;
    }

    // Ha a spec enged mÄ‚Ë‡sodik fegyvert, pajzsot vagy offhandet,
    // megkeressÄ‚Ä˝k Ä‚Â©s felszereljÄ‚Ä˝k. KÄ‚Â©tkezes/ranged speceknÄ‚Â©l
    // termÄ‚Â©szetesen nem kÄ‚Â¶telezÄąâ€.
    ItemTemplate const* offhand = nullptr;

    for (ItemSparseEntry const* sparse : sItemSparseStore)
    {
        // A hunter boost bow occupies the weapon setup; do not add melee gear.
        if (m_Player->getClass() == CLASS_HUNTER)
            break;
        if (!sparse)
            continue;

        ItemTemplate const* item =
            sObjectMgr->GetItemTemplate(sparse->ID);

        if (!item ||
            item->GetArtifactID() ||
            sDB2Manager.IsChildItem(item->GetId()) ||
            item->GetId() == mainWeapon->GetId())
        {
            continue;
        }

        uint32 const itemLevel = item->ItemLevel;

        if (itemLevel != 700)
            continue;

        if (item->GetBaseRequiredLevel() > 110)
            continue;

        InventoryType const inventoryType =
            item->GetInventoryType();

        if (inventoryType != INVTYPE_WEAPON &&
            inventoryType != INVTYPE_2HWEAPON &&
            inventoryType != INVTYPE_WEAPONOFFHAND &&
            inventoryType != INVTYPE_SHIELD &&
            inventoryType != INVTYPE_HOLDABLE)
        {
            continue;
        }

        if (!IsEquipByClsAndTal(
                m_Player->getClass(),
                m_ActiveTalentType,
                item,
                0))
        {
            continue;
        }

        if (m_Player->CanUseItem(item) != EQUIP_ERR_OK)
            continue;

        uint16 destination = 0;

        if (m_Player->CanEquipNewItem(
                EQUIPMENT_SLOT_OFFHAND,
                destination,
                item->GetId(),
                false) != EQUIP_ERR_OK)
        {
            continue;
        }

        if ((destination & 0xFF) != EQUIPMENT_SLOT_OFFHAND)
            continue;

        if (!offhand ||
            itemLevel > offhand->ItemLevel ||
            (itemLevel == offhand->ItemLevel &&
             item->GetId() < offhand->GetId()))
        {
            offhand = item;
        }
    }

    if (offhand && !createAndEquip(offhand))
    {
        TC_LOG_ERROR(
            LOG_FILTER_GENERAL,
            "CHARACTER_BOOST_OFFHAND_EQUIP_FAILED guid=%u item=%u",
            m_Player->GetGUIDLow(),
            offhand->GetId());

        return false;
    }

    // EllenÄąâ€rzÄ‚Â©s: sem fÄąâ€-, sem offhandben nem maradhat artifact.
    for (uint8 slot : weaponSlots)
    {
        Item* equipped = m_Player->GetItemByPos(
            INVENTORY_SLOT_BAG_0,
            slot);

        if (!equipped || !equipped->GetTemplate())
            continue;

        ItemTemplate const* item = equipped->GetTemplate();

        if (item->GetArtifactID())
            return false;

        if (item->GetClass() == ITEM_CLASS_WEAPON ||
            item->GetInventoryType() == INVTYPE_SHIELD ||
            item->GetInventoryType() == INVTYPE_HOLDABLE)
        {
            if (item->ItemLevel < 700 ||
                item->ItemLevel > 750)
            {
                return false;
            }
        }
    }

    for (BoostGearChoice const& choice : armorChoices)
    {
        Item* equipped = m_Player->GetItemByPos(
            INVENTORY_SLOT_BAG_0,
            choice.Slot);

        if (!equipped ||
            !equipped->GetTemplate() ||
            equipped->GetTemplate()->ItemLevel != 850)
        {
            return false;
        }
    }

    m_Player->UpdateAllStats();
    m_Player->SaveToDB();

    TC_LOG_INFO(
        LOG_FILTER_GENERAL,
        "CHARACTER_BOOST_GEAR_COMPLETE guid=%u spec=%u armorItemLevel=850 mainWeapon=%u mainWeaponItemLevel=%u offhand=%u",
        m_Player->GetGUIDLow(),
        m_Player->GetUInt32Value(
            PLAYER_FIELD_CURRENT_SPEC_ID),
        mainWeapon->GetId(),
        mainWeapon->ItemLevel,
        offhand ? offhand->GetId() : 0);

    return true;
}
void PlayerBotSetting::UpequipFromAll()
{
	for (BotNeedEquips::iterator itNeed = m_NeedEquips.begin();
		itNeed != m_NeedEquips.end();
		itNeed++)
	{
		Item* itemInst = (*itNeed);
		if (itemInst->GetTemplate()->GetInventoryType() == InventoryType::INVTYPE_AMMO)
		{
			//m_Player->SetAmmo(itemInst->GetEntry());
		}
		else
			EquipItem(itemInst);
	}
	m_NeedEquips.clear();
	FillMissingEquipmentSlots();
}

bool PlayerBotSetting::EquipItem(Item* pItem)
{
        uint8 const parentBag = pItem->GetBagSlot();
        uint8 const parentSlot = pItem->GetSlot();
        bool const equipChildItem =
                m_Player->CanEquipChildItem(pItem) == EQUIP_ERR_OK;
	uint16 dest;
	InventoryResult msg = m_Player->CanEquipItem(NULL_SLOT, dest, pItem, !pItem->IsBag());
	if (msg != EQUIP_ERR_OK)
	{
        if (pItem->GetTemplate()->GetArtifactID())
            TC_LOG_ERROR(LOG_FILTER_LFG,
                "BOT_ARTIFACT_SLOT_REJECTED guid=%u item=%u spec=%u error=%u",
                m_Player->GetGUIDLow(), pItem->GetEntry(),
                m_Player->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID), uint32(msg));
		return false;
	}

	uint16 src = pItem->GetPos();
	if (dest == src)                                           // prevent equip in same slot, only at cheat
		return false;

	Item* pDstItem = m_Player->GetItemByPos(dest);
    if (pDstItem && m_PreserveExistingEquipment)
    {
        // Ordinary repairs retain equipped items. A deliberate role change may
        // replace only a weapon whose artifact belongs to another specialization.
        ArtifactEntry const* desired = sArtifactStore.LookupEntry(pItem->GetTemplate()->GetArtifactID());
        ArtifactEntry const* existing = sArtifactStore.LookupEntry(pDstItem->GetTemplate()->GetArtifactID());
        // FELSORIA_SPEC_ARTIFACT_V3: repair a wrong/missing artifact even when
        // preserving talents. Only the current spec's artifact parent may replace a weapon.
        bool const correctArtifact = desired && !sDB2Manager.IsChildItem(pItem->GetEntry()) &&
            desired->ChrSpecializationID == m_Player->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID) &&
            (uint8(dest) == EQUIPMENT_SLOT_MAINHAND || uint8(dest) == EQUIPMENT_SLOT_OFFHAND);
        if (!correctArtifact || (existing && existing->ChrSpecializationID == desired->ChrSpecializationID))
            return false;
    }
	if (!pDstItem)                                         // empty slot, simple case
	{
		m_Player->RemoveItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
		m_Player->EquipItem(dest, pItem, true);
		m_Player->AutoUnequipOffhandIfNeed();
	}
	else                                                    // have currently equipped item, not simple case
	{
		uint8 dstbag = pDstItem->GetBagSlot();
		uint8 dstslot = pDstItem->GetSlot();

		msg = m_Player->CanUnequipItem(dest, !pItem->IsBag());
		if (msg != EQUIP_ERR_OK)
		{
			return false;
		}

		// check dest->src move possibility
		ItemPosCountVec sSrc;
		uint16 eSrc = 0;
		if (m_Player->IsInventoryPos(src))
		{
			msg = m_Player->CanStoreItem(pItem->GetBagSlot(), pItem->GetSlot(), sSrc, pDstItem, true);
			if (msg != EQUIP_ERR_OK)
				msg = m_Player->CanStoreItem(pItem->GetBagSlot(), NULL_SLOT, sSrc, pDstItem, true);
			if (msg != EQUIP_ERR_OK)
				msg = m_Player->CanStoreItem(NULL_BAG, NULL_SLOT, sSrc, pDstItem, true);
		}
		else if (m_Player->IsBankPos(src))
		{
			msg = m_Player->CanBankItem(pItem->GetBagSlot(), pItem->GetSlot(), sSrc, pDstItem, true);
			if (msg != EQUIP_ERR_OK)
				msg = m_Player->CanBankItem(pItem->GetBagSlot(), NULL_SLOT, sSrc, pDstItem, true);
			if (msg != EQUIP_ERR_OK)
				msg = m_Player->CanBankItem(NULL_BAG, NULL_SLOT, sSrc, pDstItem, true);
		}
		else if (m_Player->IsEquipmentPos(src))
		{
			msg = m_Player->CanEquipItem(pItem->GetSlot(), eSrc, pDstItem, true);
			if (msg == EQUIP_ERR_OK)
				msg = m_Player->CanUnequipItem(eSrc, true);
		}

		if (msg != EQUIP_ERR_OK)
		{
			return false;
		}

		// now do moves, remove...
		m_Player->RemoveItem(dstbag, dstslot, false);
		m_Player->RemoveItem(pItem->GetBagSlot(), pItem->GetSlot(), false);

		// add to dest
		m_Player->EquipItem(dest, pItem, true);

		// add to src
		if (m_Player->IsInventoryPos(src))
			m_Player->StoreItem(sSrc, pDstItem, true);
		else if (m_Player->IsBankPos(src))
			m_Player->BankItem(sSrc, pDstItem, true);
		else if (m_Player->IsEquipmentPos(src))
			m_Player->EquipItem(eSrc, pDstItem, true);

		m_Player->AutoUnequipOffhandIfNeed();
	}
        if (equipChildItem &&
                sDB2Manager.GetItemChildEquipment(pItem->GetEntry()))
        {
                m_Player->EquipChildItem(
                        parentBag,
                        parentSlot,
                        pItem);
        }
	return true;
}

void PlayerBotSetting::SupplementOtherItems()
{
	uint8 prof = m_Player->getClass();
	switch (prof)
	{
	case 1:
		SupplementItemByWarrior();
		break;
	case 2:
		SupplementItemByPaladin();
		break;
	}
}

const ItemTemplate* PlayerBotSetting::GetRandomAmmoByType(ItemSubclassProjectile iType, uint32 startLV)
{
	BotEquips& equips = classesEquips[3][InventoryType::INVTYPE_AMMO];
	for (int i = startLV; i > 0; i--)
	{
		BotEquips::iterator itEquip = equips.find(i);
		if (itEquip == equips.end() || itEquip->second.m_Items.size() <= 0)
			continue;
		for (int j = 0; j < 3; j++)
		{
			const ItemTemplate* item = itEquip->second.RandomItem();
			if (item && item->GetSubClass() == iType)
				return item;
		}
	}
	return NULL;
}

const ItemTemplate* PlayerBotSetting::GetRandomItemFromLoopLV(uint32 prof, InventoryType iType, uint32 startLV, const ItemTemplate* filter)
{
	BotEquips& equips = classesEquips[prof][iType];
	if (startLV < 110 && m_Player)
	{
		// Search normal equipment before starter items, preserving the
		// original max-level selection pools.
		for (int pass = 0; pass < 2; ++pass)
		{
			BotEquips& pool = pass == 0 ? equips : starterEquips[prof][iType];
			for (int level = int(startLV); level >= 0; --level)
			{
				auto bucket = pool.find(level);
				if (bucket == pool.end())
					continue;
				BotItems const& items = bucket->second.m_Items;
				if (items.empty())
					continue;
				size_t const first = urand(0, uint32(items.size() - 1));
				for (size_t offset = 0; offset < items.size(); ++offset)
				{
					ItemTemplate const* item = items[(first + offset) % items.size()];
					if (!item || item == filter || !IsLevelAppropriateEquip(startLV, item) ||
						m_Player->CanUseItem(item) != EQUIP_ERR_OK)
						continue;
					if (IsLowLevelSpecEquip(prof, m_ActiveTalentType, item))
						return item;
				}
			}
		}
		return nullptr;
	}
	for (int i = startLV; i > 0; i--)
	{
		BotEquips::iterator itEquip = equips.find(i);
		if (itEquip == equips.end())
			continue;
		if (m_TenacitySetting)
		{
			for (int j = 0; j < 2; j++)
			{
				const ItemTemplate* item = itEquip->second.RandomTenacityItem();
				if (item && item != filter)
					return item;
			}
		}
		for (int j = 0; j < 2; j++)
		{
			const ItemTemplate* item = itEquip->second.RandomItem();
			if (item && item != filter)
				return item;
		}
	}
	return NULL;
}

bool PlayerBotSetting::IsTenacityEquipSlot(uint8 slot)
{
	switch (EquipmentSlots(slot))
	{
	case EquipmentSlots::EQUIPMENT_SLOT_HEAD:
	case EquipmentSlots::EQUIPMENT_SLOT_NECK:
	case EquipmentSlots::EQUIPMENT_SLOT_SHOULDERS:
	case EquipmentSlots::EQUIPMENT_SLOT_BODY:
	case EquipmentSlots::EQUIPMENT_SLOT_CHEST:
	case EquipmentSlots::EQUIPMENT_SLOT_WAIST:
	case EquipmentSlots::EQUIPMENT_SLOT_LEGS:
	case EquipmentSlots::EQUIPMENT_SLOT_FEET:
	case EquipmentSlots::EQUIPMENT_SLOT_WRISTS:
	case EquipmentSlots::EQUIPMENT_SLOT_HANDS:
	case EquipmentSlots::EQUIPMENT_SLOT_FINGER1:
	case EquipmentSlots::EQUIPMENT_SLOT_FINGER2:
	case EquipmentSlots::EQUIPMENT_SLOT_BACK:
	case EquipmentSlots::EQUIPMENT_SLOT_MAINHAND:
	case EquipmentSlots::EQUIPMENT_SLOT_OFFHAND:
	case EquipmentSlots::EQUIPMENT_SLOT_RANGED:
		return true;
	}
	return false;
}

bool PlayerBotSetting::EquipIsTidiness()
{
    if (!m_Player)
        return false;

    if (m_Player->getLevel() < 110)
    {
        // Slots unavailable at this level may remain empty, but reject any
        // old overpowered set so the regular reset can replace it.
        for (uint8 slot = EQUIPMENT_SLOT_HEAD; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item const* equipped = m_Player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (equipped && !IsLevelAppropriateEquip(m_Player->getLevel(), equipped->GetTemplate()))
                return false;
        }
        Item const* weapon = m_Player->GetItemByPos(
            INVENTORY_SLOT_BAG_0, EquipmentSlots::EQUIPMENT_SLOT_MAINHAND);
        if (!weapon)
            return false;
        // Starter weapons alone must not classify an otherwise naked bot as ready.
        static uint8 const armorSlots[] = { EQUIPMENT_SLOT_CHEST,
            EQUIPMENT_SLOT_LEGS, EQUIPMENT_SLOT_FEET };
        for (uint8 slot : armorSlots)
            if (!m_Player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                return false;
        return true;
    }

    static uint8 const requiredSlots[] =
    {
        EquipmentSlots::EQUIPMENT_SLOT_HEAD,
        EquipmentSlots::EQUIPMENT_SLOT_NECK,
        EquipmentSlots::EQUIPMENT_SLOT_SHOULDERS,
        EquipmentSlots::EQUIPMENT_SLOT_CHEST,
        EquipmentSlots::EQUIPMENT_SLOT_WAIST,
        EquipmentSlots::EQUIPMENT_SLOT_LEGS,
        EquipmentSlots::EQUIPMENT_SLOT_FEET,
        EquipmentSlots::EQUIPMENT_SLOT_WRISTS,
        EquipmentSlots::EQUIPMENT_SLOT_HANDS,
        EquipmentSlots::EQUIPMENT_SLOT_FINGER1,
        EquipmentSlots::EQUIPMENT_SLOT_FINGER2,
        EquipmentSlots::EQUIPMENT_SLOT_TRINKET1,
        EquipmentSlots::EQUIPMENT_SLOT_TRINKET2,
        EquipmentSlots::EQUIPMENT_SLOT_BACK,
        EquipmentSlots::EQUIPMENT_SLOT_MAINHAND
    };

    for (uint8 slot : requiredSlots)
    {
        if (!m_Player->GetItemByPos(
            INVENTORY_SLOT_BAG_0,
            slot))
        {
            return false;
        }
    }

    return HasEquippedSpecArtifact();
}

bool PlayerBotSetting::CheckNeedTenacityFlush()
{
	uint32 noMatchEquipCount = 0;
	for (uint8 slot = EquipmentSlots::EQUIPMENT_SLOT_HEAD; slot < EquipmentSlots::EQUIPMENT_SLOT_END; slot++)
	{
		if (!IsTenacityEquipSlot(slot))
			continue;
		//uint16 pos = (255 << 8) | slot;
		//if (m_Player->IsEquipmentPos(pos) || m_Player->IsBagPos(pos))
		//{
		//	InventoryResult msg = m_Player->CanUnequipItem(pos, false);
		//	if (msg != EQUIP_ERR_OK)
		//		continue;
		//}
		Item* pItem = m_Player->GetItemByPos(255, slot);
		if (!pItem)
		{
			++noMatchEquipCount;
			if (noMatchEquipCount > 2)
				return true;
			continue;
		}
		const ItemTemplate* pTemplate = pItem->GetTemplate();
		if (!ItemsForLevel::IsTenacityItem(pTemplate))
		{
			++noMatchEquipCount;
			if (noMatchEquipCount > 2)
				return true;
		}
	}
	return false;
}

bool PlayerBotSetting::IsTenacityInventoryType(InventoryType iType)
{
	if (iType == InventoryType::INVTYPE_NON_EQUIP || iType == InventoryType::INVTYPE_TRINKET || iType == InventoryType::INVTYPE_BAG ||
		iType == InventoryType::INVTYPE_TABARD || iType == InventoryType::INVTYPE_HOLDABLE || iType == InventoryType::INVTYPE_AMMO ||
		/*iType == InventoryType::INVTYPE_QUIVER||*/ iType == InventoryType::INVTYPE_RELIC)
		return false;
	return true;
}

void PlayerBotSetting::AddOnceEquip(const ItemTemplate* item)
{
	if (!item)
		return;
	//m_Player->AddItem(item->GetId(), 1);
	uint32 count = 1;
	if (item->GetClass() == ItemClass::ITEM_CLASS_PROJECTILE && item->GetInventoryType() == InventoryType::INVTYPE_AMMO)
		count = 1000;
	uint32 noSpaceForCount = 0;
	ItemPosCountVec dest;
	InventoryResult msg = m_Player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item->GetId(), count, &noSpaceForCount);
	if (msg != EQUIP_ERR_OK)
		count -= noSpaceForCount;
	if (count <= 0 || dest.empty())
		return;
	Item* itemInst = m_Player->StoreNewItem(dest, item->GetId(), true, Item::GenerateItemRandomPropertyId(item->GetId()));
	if (itemInst)
		m_NeedEquips.push_back(itemInst);
}

void PlayerBotSetting::OnSpecializationChanged()
{
    if (!m_Player || !m_Player->IsPlayerBot())
        return;
    m_ActiveTalentType = FindPlayerTalentType(m_Player);
    // An ongoing full reset will teach and equip the new spec itself.
    if (!m_Finish && !m_RepairOnly && !m_RepairFailed)
        return;
    m_RepairOnly = true;
    m_RepairFailed = false;
    m_RepairAttempts = 0;
    m_Finish = false;
}

bool PlayerBotSetting::HasCompleteTalents() const
{
    uint32 const specID = m_Player->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID);
    uint32 const tiers = std::min(uint32(MAX_TALENT_TIERS),
        m_Player->GetUInt32Value(PLAYER_FIELD_MAX_TALENT_TIERS));
    if (m_Player->getLevel() == 110 && tiers != MAX_TALENT_TIERS)
        return false;
    for (uint32 tier = 0; tier < tiers; ++tier)
    {
        bool found = false;
        for (TalentEntry const* talent : sTalentStore)
            if (talent && talent->ClassID == m_Player->getClass() && talent->TierID == tier &&
                (!talent->SpecID || talent->SpecID == specID) &&
                m_Player->HasTalent(talent->ID, m_Player->GetActiveTalentGroup()) &&
                m_Player->HasSpell(talent->SpellID))
                found = true;
        if (!found)
            return false;
    }
    return true;
}

bool PlayerBotSetting::HasEquippedSpecArtifact() const
{
    if (!m_Player)
        return false;
    uint32 const specID = m_Player->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID);
    if (!specID)
        return false;
    // The artifact parent may occupy either weapon slot. Validate its actual
    // linked child, rather than treating every main-hand item as a parent.
    uint8 const weaponSlots[] = { EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND };
    for (uint8 slot : weaponSlots)
    {
        Item* parent = m_Player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!parent || !parent->GetTemplate() || sDB2Manager.IsChildItem(parent->GetEntry()))
            continue;
        ArtifactEntry const* art = sArtifactStore.LookupEntry(parent->GetTemplate()->GetArtifactID());
        if (!art || art->ChrSpecializationID != specID)
            continue;
        if (ItemChildEquipmentEntry const* link = sDB2Manager.GetItemChildEquipment(parent->GetEntry()))
        {
            Item* child = m_Player->GetChildItemByGuid(parent->GetChildItem());
            if (!child || child->GetEntry() != link->ChildItemID ||
                m_Player->GetItemByPos(INVENTORY_SLOT_BAG_0, link->ChildItemEquipSlot) != child)
                continue;
        }
        return true;
    }
    return false;
}

bool PlayerBotSetting::EnsureArtifact()
{
    if (m_Player->getLevel() != 110)
        return true;
    uint32 const specID = m_Player->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID);
    // A DH's native Dual Wield proficiency is required before its offhand can
    // remain equipped. Reapply its learned spell effect after specialization setup.
    if(m_Player->IsPlayerBot() && m_Player->getClass()==CLASS_DEMON_HUNTER && !m_Player->CanDualWield()){
        m_Player->learnSpell(674,false);
        if(!m_Player->CanDualWield())m_Player->CastSpell(m_Player,674,true);
    }
    if (HasEquippedSpecArtifact())
    {
        FelsoriaBotArtifactSkins::Apply(m_Player);
        return true;
    }
    // FELSORIA_SPEC_ARTIFACT_V4: reuse equipped parents before searching bags.
    auto report = [&](char const* stage, uint32 entry, uint32 error) {
        TC_LOG_ERROR(LOG_FILTER_LFG,
            "BOT_ARTIFACT_REPAIR_BLOCKED guid=%u spec=%u item=%u stage=%s error=%u",
            m_Player->GetGUIDLow(), specID, entry, stage, error);
    };
    for (ItemSparseEntry const* sparse : sItemSparseStore)
    {
        if (!sparse)
            continue;
        ItemTemplate const* item = sObjectMgr->GetItemTemplate(sparse->ID);
        if (!item || !item->GetArtifactID() || sDB2Manager.IsChildItem(item->GetId()))
            continue;
        ArtifactEntry const* art = sArtifactStore.LookupEntry(item->GetArtifactID());
        if (!art || art->ChrSpecializationID != specID)
            continue;
        InventoryResult usable = m_Player->CanUseItem(item);
        if (usable != EQUIP_ERR_OK) {
            report("use",item->GetId(),uint32(usable));
            continue;
        }
        Item* weapon = nullptr;
        uint8 const weaponSlots[] = { EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND };
        for (uint8 slot : weaponSlots)
            if (Item* equipped = m_Player->GetItemByPos(INVENTORY_SLOT_BAG_0,slot))
                if (equipped->GetEntry() == item->GetId()) {
                    weapon = equipped;
                    break;
                }
        if (!weapon)
            weapon = BotUtility::FindItemFromAllBag(m_Player,item->GetId());
        if (!weapon)
        {
            ItemPosCountVec dest;
            InventoryResult stored = m_Player->CanStoreNewItem(NULL_BAG,NULL_SLOT,dest,item->GetId(),1);
            if (stored != EQUIP_ERR_OK || dest.empty()) {
                report("create_storage",item->GetId(),uint32(stored));
                continue;
            }
            weapon = m_Player->StoreNewItem(dest,item->GetId(),true,
                Item::GenerateItemRandomPropertyId(item->GetId()));
        }
        if (!weapon) {
            report("create",item->GetId(),0);
            continue;
        }
        // FELSORIA_BG_ARTIFACT_PAIR_V1_2: clear only the linked child's target
        // slot through validated native storage. A shield parent must not be
        // auto-unequipped while its child sword is replacing an old two-hander.
        ItemChildEquipmentEntry const* link=sDB2Manager.GetItemChildEquipment(weapon->GetEntry());
        Item* linkedChild=link?m_Player->GetChildItemByGuid(weapon->GetChildItem()):nullptr;
        if(link){
            if(!linkedChild || linkedChild->GetEntry()!=link->ChildItemID){
                report("linked_child_missing",weapon->GetEntry(),0);continue;
            }
            Item* occupant=m_Player->GetItemByPos(INVENTORY_SLOT_BAG_0,link->ChildItemEquipSlot);
            if(occupant && occupant!=linkedChild){
                ItemPosCountVec storage;
                InventoryResult moved=m_Player->CanUnequipItem(occupant->GetPos(),false);
                if(moved==EQUIP_ERR_OK)moved=m_Player->CanStoreItem(NULL_BAG,NULL_SLOT,storage,occupant,false);
                if(moved!=EQUIP_ERR_OK || storage.empty()){
                    report("child_target_storage",weapon->GetEntry(),uint32(moved));continue;
                }
                m_Player->RemoveItem(occupant,true);
                if(!m_Player->StoreItem(storage,occupant,true)){
                    report("child_target_store",weapon->GetEntry(),0);continue;
                }
            }
        }
        if(!weapon->IsEquipped()){
            // When the linked child is already in place, there is no child swap
            // to validate. The native helper otherwise tries to store it again.
            bool const childInPlace=link &&
                m_Player->GetItemByPos(INVENTORY_SLOT_BAG_0,link->ChildItemEquipSlot)==linkedChild;
            InventoryResult child=childInPlace?EQUIP_ERR_OK:m_Player->CanEquipChildItem(weapon);
            if(child!=EQUIP_ERR_OK){report("child_swap",weapon->GetEntry(),uint32(child));continue;}
            if(!EquipItem(weapon)){report("equip",weapon->GetEntry(),0);continue;}
        }
        if(link && weapon->IsEquipped() &&
           m_Player->GetItemByPos(INVENTORY_SLOT_BAG_0,link->ChildItemEquipSlot)!=linkedChild)
            m_Player->EquipChildItem(weapon->GetBagSlot(),weapon->GetSlot(),weapon);
        if (HasEquippedSpecArtifact()) {
            FelsoriaBotArtifactSkins::Apply(m_Player);
            TC_LOG_INFO(LOG_FILTER_LFG,"BOT_ARTIFACT_REPAIR_READY guid=%u spec=%u item=%u",
                m_Player->GetGUIDLow(),specID,weapon->GetEntry());
            return true;
        }
        report("pair_validation",weapon->GetEntry(),0);
    }
    return false;
}

bool PlayerBotSetting::AddArtifactWeaponForCurrentSpec()
{
    if (!m_Player || m_Player->getLevel() != 110)
        return false;

    uint32 specID =
        m_Player->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID);

    if (!specID)
        return false;

    for (ItemSparseEntry const* sparse : sItemSparseStore)
    {
        if (!sparse)
            continue;

        ItemTemplate const* itemTemplate =
            sObjectMgr->GetItemTemplate(sparse->ID);

        if (!itemTemplate)
            continue;

        uint32 artifactID = itemTemplate->GetArtifactID();

        if (!artifactID)
            continue;

        if (sDB2Manager.IsChildItem(itemTemplate->GetId()))
            continue;

        ArtifactEntry const* artifact =
            sArtifactStore.LookupEntry(artifactID);

        if (!artifact)
            continue;

        if (artifact->ChrSpecializationID != specID)
            continue;

        if (m_Player->CanUseItem(itemTemplate) != EQUIP_ERR_OK)
            continue;

        size_t const previousSize = m_NeedEquips.size();
        AddOnceEquip(itemTemplate);
        if (m_NeedEquips.size() > previousSize)
            return true;
    }

    return false;
}

void PlayerBotSetting::RandomWeaponByWarrior()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 1;
	switch (m_ActiveTalentType)
	{
	case 0:
	{
		AddOnceEquip(GetRandomItemFromLoopLV(1, InventoryType::INVTYPE_2HWEAPON, level));
	}
	break;
	case 1:
	{
		ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level);
		if (!item1)
			item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
		AddOnceEquip(item1);
		ItemTemplate* item2 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level, item1);
		if (!item2)
			item2 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONOFFHAND, level);
		AddOnceEquip(item2);
	}
	break;
	case 2:
	{
		ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level);
		if (!item1)
			item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
		AddOnceEquip(item1);
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_SHIELD, level));
	}
	break;
	default:
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
		break;
	}
	AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_THROWN, level));
}

void PlayerBotSetting::RandomWeaponByPaladin()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 2;
	switch (m_ActiveTalentType)
	{
	case 0:
	{
		ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level);
		if (!item1)
			item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
		AddOnceEquip(item1);
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_SHIELD, level));
	}
	break;
	case 1:
	{
		ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level);
		if (!item1)
			item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
		AddOnceEquip(item1);
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_SHIELD, level));
	}
	break;
	case 2:
	{
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
	}
	break;
	default:
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
		break;
	}
	AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_RELIC, level));
}

void PlayerBotSetting::RandomWeaponByDeathKight()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 6;
	switch (m_ActiveTalentType)
	{
	case 0:
	{
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
	}
	break;
	case 1:
	{
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
	}
	break;
	case 2:
	{
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
	}
	break;
	default:
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
		break;
	}
}

void PlayerBotSetting::RandomWeaponByRogue()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 4;
	ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level);
	if (!item1)
		item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
	AddOnceEquip(item1);
	ItemTemplate* item2 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level, item1);
	if (!item2)
		item2 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONOFFHAND, level);
	AddOnceEquip(item2);
	AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_THROWN, level));
}

void PlayerBotSetting::RandomWeaponByDruid()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 11;
	AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
	AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_RELIC, level));
}

void PlayerBotSetting::RandomWeaponByHunter()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 3;
	AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
	InventoryType iType = (irand(0, 99) < 50) ? InventoryType::INVTYPE_RANGED : InventoryType::INVTYPE_RANGEDRIGHT;
	const ItemTemplate* item = GetRandomItemFromLoopLV(prof, iType, level);
	AddOnceEquip(item);
	if (item && item->GetClass() == ItemClass::ITEM_CLASS_WEAPON)
	{
		switch (item->GetSubClass())
		{
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW:
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW:
			AddOnceEquip(GetRandomAmmoByType(ItemSubclassProjectile::ITEM_SUBCLASS_ARROW, level));
			break;
		case ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN:
			AddOnceEquip(GetRandomAmmoByType(ItemSubclassProjectile::ITEM_SUBCLASS_BULLET, level));
			break;
		default:
			break;
		}
	}
}

void PlayerBotSetting::RandomWeaponByShaman()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 7;
	switch (m_ActiveTalentType)
	{
	case 0:
	{
		ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level);
		if (!item1)
			item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
		AddOnceEquip(item1);
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_SHIELD, level));
	}
	break;
	case 1:
	{
		if (m_Player->HasSpell(30798)) // talent two weapon
		{
			ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level);
			if (!item1)
				item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
			AddOnceEquip(item1);
			ItemTemplate* item2 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level, item1);
			if (!item2)
				item2 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONOFFHAND, level);
			AddOnceEquip(item2);
		}
		else
			AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
	}
	break;
	case 2:
	{
		ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level);
		if (!item1)
			item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
		AddOnceEquip(item1);
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_SHIELD, level));
	}
	break;
	default:
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
		break;
	}
	AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_RELIC, level));
}

void PlayerBotSetting::RandomWeaponByMage()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 8;
	InventoryType iType = (irand(0, 99) < 50) ? InventoryType::INVTYPE_2HWEAPON : InventoryType::INVTYPE_WEAPON;
	ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, iType, level);
	if (!item1)
		item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
	AddOnceEquip(item1);
	if (iType == InventoryType::INVTYPE_WEAPON)
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_HOLDABLE, level, item1));
	AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_RANGEDRIGHT, level));
}

void PlayerBotSetting::RandomWeaponByWarlock()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 9;
	InventoryType iType = (irand(0, 99) < 50) ? InventoryType::INVTYPE_2HWEAPON : InventoryType::INVTYPE_WEAPON;
	ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, iType, level);
	if (!item1)
		item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
	AddOnceEquip(item1);
	if (iType == InventoryType::INVTYPE_WEAPON)
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_HOLDABLE, level, item1));
	AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_RANGEDRIGHT, level));
}

void PlayerBotSetting::RandomWeaponByPriest()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 5;
	InventoryType iType = (irand(0, 99) < 50) ? InventoryType::INVTYPE_2HWEAPON : InventoryType::INVTYPE_WEAPON;
	ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, iType, level);
	if (!item1)
		item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
	AddOnceEquip(item1);
	if (iType == InventoryType::INVTYPE_WEAPON)
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_HOLDABLE, level, item1));
	AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_RANGEDRIGHT, level));
}

void PlayerBotSetting::SupplementItemByWarrior()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 1;
	switch (m_ActiveTalentType)
	{
	case 0:
	{
		ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level);
		if (!item1)
			item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
		AddOnceEquip(item1);
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_SHIELD, level));
	}
	break;
	case 1:
	{
		AddOnceEquip(GetRandomItemFromLoopLV(1, InventoryType::INVTYPE_2HWEAPON, level));
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_SHIELD, level));
	}
	break;
	case 2:
	{
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level));
		AddOnceEquip(GetRandomItemFromLoopLV(1, InventoryType::INVTYPE_2HWEAPON, level));
	}
	break;
	}
	m_NeedEquips.clear();
}

void PlayerBotSetting::SupplementItemByPaladin()
{
	uint32 level = m_Player->getLevel();
	uint32 prof = 2;
	switch (m_ActiveTalentType)
	{
	case 0:
	case 1:
	{
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_2HWEAPON, level));
	}
	break;
	case 2:
	{
		ItemTemplate* item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPON, level);
		if (!item1)
			item1 = (ItemTemplate*)GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_WEAPONMAINHAND, level);
		AddOnceEquip(item1);
		AddOnceEquip(GetRandomItemFromLoopLV(prof, InventoryType::INVTYPE_SHIELD, level));
	}
	break;
	}
	m_NeedEquips.clear();
}







