
#ifndef __PLAYERBOTSETTING_H__
#define __PLAYERBOTSETTING_H__

#include "Log.h"
#include "Common.h"
#include "SharedDefines.h"
#include "DatabaseEnv.h"
#include "Player.h"

typedef std::vector<const ItemTemplate*> BotItems;
class ItemsForLevel
{
public:
	static bool IsTenacityItem(const ItemTemplate* itemTemplate);

public:
	ItemsForLevel() {}
	void AddItem(const ItemTemplate* pItem);
	const ItemTemplate* RandomItem();
	const ItemTemplate* RandomTenacityItem();

	BotItems m_Items;
	BotItems m_TenacityItems;
};

struct TalentEntry;
class BotTalentEntry
{
public:
	BotTalentEntry(uint32 cls, uint32 index, const TalentEntry* entry) :
		prof(cls), pageIndex(index), talentEntry(entry)
	{
	}

	bool operator < (const BotTalentEntry &tal) const;

public:
	uint32 prof;
	uint32 pageIndex;
	const TalentEntry* talentEntry;
};

class PlayerBotSetting
{
public:
	typedef std::set<uint32> SetEntrys;
	typedef std::set<BotTalentEntry> BotTalentPage;
	typedef std::map<uint32, ItemsForLevel> BotEquips;
	typedef std::list<Item*> BotNeedEquips;
	typedef std::list<uint32> BotCommonSpells;
	typedef std::vector<uint32> BeastCreatureEntrys;

	static void Initialize();
	static bool BindingPlayerHomePosition(Player* player);
	static bool CheckHunterPet(Player* player);
	static uint32 FindPlayerTalentType(Player* player);
	static uint32 RandomMountByLevel(uint32 level);
	static uint32 MountForPlayer(Player const* player);
	static bool IsMountAllowed(Player const* player, uint32 spellId);
	static void RemoveForbiddenMount(Player* player);
	static bool MatchEquipmentSlot(uint8 pos, const ItemTemplate* itemTemplate);
	static uint32 GetItemLevelByAI(const ItemTemplate* item);
	static bool IsBetterEquip(Player* player, const ItemTemplate* itemTemplate, int32 rndPropID);
	static void ClearUnknowMount(Player* player);
	static uint32 CheckMaxLevel(uint32 level);
	static uint32 CheckBotLevel(Player const* player, uint32 level);
	static bool IsBotFlyMountAura(uint32 aura);

private:
	//static bool MatchEquipmentSlotsByWeapon(EquipmentSlots slot, InventoryType iType);
	//static bool MatchEquipmentSlotsByArmor(EquipmentSlots slot, InventoryType iType);
	//static bool MatchRangeEquipmentSlots(uint32 cls, const ItemTemplate* itemTemplate);
	static bool IsCommonEquip(const ItemTemplate* itemTemplate);
	static bool IsFingerEquip(const ItemTemplate* itemTemplate);
	static bool IsTrinketEquip(const ItemTemplate* itemTemplate);
	static bool IsWarriorEquip(const ItemTemplate* itemTemplate);
	static bool IsPaladinEquip(const ItemTemplate* itemTemplate);
	static bool IsDeathKightEquip(const ItemTemplate* itemTemplate);
	static bool IsRogueEquip(const ItemTemplate* itemTemplate);
	static bool IsDruidEquip(const ItemTemplate* itemTemplate);
	static bool IsHunterEquip(const ItemTemplate* itemTemplate);
	static bool IsShamanEquip(const ItemTemplate* itemTemplate);
	static bool IsMageEquip(const ItemTemplate* itemTemplate);
	static bool IsWarlockEquip(const ItemTemplate* itemTemplate);
	static bool IsPriestEquip(const ItemTemplate* itemTemplate);
	static bool IsEquipByClasses(uint32 cls, const ItemTemplate* itemTemplate);
	static bool IsStarterEquipByClasses(uint32 cls, const ItemTemplate* itemTemplate);
	static bool IsLowLevelSpecEquip(uint32 cls, uint32 tal, const ItemTemplate* itemTemplate);
	static bool IsLevelAppropriateEquip(uint32 level, const ItemTemplate* itemTemplate);
	static bool IsEquipByClsAndTal(uint32 cls, uint32 tal, const ItemTemplate* itemTemplate, int32 rndPropID);
	static bool IsOnlyPhysicsAttributeEquip(const ItemTemplate* itemTemplate, bool coverIntellect);
	static bool IsOnlyMagicAttributeEquip(const ItemTemplate* itemTemplate);
	static bool IsTankAttributeEquip(const ItemTemplate* itemTemplate);
	static bool IsOnlyPhysicsRandomAttributeByEquip(std::list<uint32>& enchants, bool coverIntellect);
	static bool IsOnlyMagicRandomAttributeByEquip(std::list<uint32>& enchants);
	static bool IsTankRandomAttributeByEquip(std::list<uint32>& enchants);
	static void GetRandomPropEnchantments(int32 rndPropID, std::list<uint32>& enchants);

public:
	PlayerBotSetting(Player* player);
	~PlayerBotSetting();

	bool EquipIsTidiness();
	bool CheckNeedTenacityFlush();
	uint32 UpdateTalentType();
	uint32 GetTalentType();
	bool IsFinish() { return m_Finish; }
    // FELSORIA_RDF_PREPARE_FAILURE_V2: expose state without changing readiness.
    bool IsPreparationFailed() const { return m_RepairFailed; }
    uint32 GetPreparationStep() const { return m_ResetStep; }
    uint32 GetPreparationAttempts() const { return m_RepairAttempts; }
    bool IsPreparationRepair() const { return m_RepairOnly; }
	bool ResetPlayerToLevel(uint32 level, uint32 talent, bool tenacity = false);
	uint32 SwitchPlayerTalent(uint32 talent);
	void SupplementAmmo();
	void UpdateReset();
	void LearnSpells();
	void LearnTalents();
	void OnSpecializationChanged();
	bool EquipItem(Item* pItem);
	bool EquipCharacterBoostGear();

private:
	void RemoveSpells();
	bool HasCompleteTalents() const;
	bool EnsureArtifact();
    bool HasEquippedSpecArtifact() const; // FELSORIA_SPEC_ARTIFACT_V3
	bool m_PreserveExistingEquipment = false;
    bool m_InitializeArtifactEquipment = false;
	bool m_PreserveExistingTalents = false;
	bool m_RepairOnly = false;
	uint32 m_RepairAttempts = 0;
	bool m_RepairFailed = false;
	void LearnCommonSpells();
	void CheckInventroy();
	void UnequipFromAll();
	void AddEquipFromAll();
	void UpequipFromAll();
	void SupplementOtherItems();

	bool IsTenacityEquipSlot(uint8 slot);
	bool IsTenacityInventoryType(InventoryType iType);
	void AddOnceEquip(const ItemTemplate* item);
	bool AddArtifactWeaponForCurrentSpec();
	void FillMissingEquipmentSlots();
	const ItemTemplate* GetRandomAmmoByType(ItemSubclassProjectile ammoType, uint32 startLV);
	const ItemTemplate* GetRandomItemFromLoopLV(uint32 prof, InventoryType iType, uint32 startLV, const ItemTemplate* filter = 0);
	void RandomWeaponByWarrior();
	void RandomWeaponByPaladin();
	void RandomWeaponByDeathKight();
	void RandomWeaponByRogue();
	void RandomWeaponByDruid();
	void RandomWeaponByHunter();
	void RandomWeaponByShaman();
	void RandomWeaponByMage();
	void RandomWeaponByWarlock();
	void RandomWeaponByPriest();

	void SupplementItemByWarrior();
	void SupplementItemByPaladin();

private:
	bool m_Finish;
	bool m_TenacitySetting;
	uint32 m_ResetStep;
	Player* m_Player;
	BotNeedEquips m_NeedEquips;
	uint32 m_ActiveTalentType;

	static uint32 classesTrainersGUID[MAX_CLASSES][2];
	static BotTalentPage classesTalents[MAX_CLASSES][3];
	static BotEquips classesEquips[MAX_CLASSES][InventoryType::INVTYPE_RELIC+1];
	// Starter items are kept out of the original level-110 selection pool.
	static BotEquips starterEquips[MAX_CLASSES][InventoryType::INVTYPE_RELIC+1];
	static BotCommonSpells classesCommonSpells[MAX_CLASSES];
	static BeastCreatureEntrys beastCreatureEntrys;
	static BeastCreatureEntrys normalMountSpells;
	static BeastCreatureEntrys fastMountSpells;
	static SetEntrys botFlyMountEntrys;
};

#endif // __PLAYERBOTSETTING_H__


