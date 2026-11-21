
#ifndef _BOT_GROUP_AI_H
#define _BOT_GROUP_AI_H

#include "BotAITool.h"
#include "PvpLegionRotation.h"
#include "BotActivityPolicy.h"
#include "FelsoriaMechanicsGeometry.h"
#include "BotAISpells.h"
#include "SpellMgr.h"
#include "PlayerBotSetting.h"
#include "GridNotifiers.h"
#include <mutex>
#include <map>
#include <set>
#include <string>
#include <map>
#include <utility>
#include <vector>

class Player;
class PathGenerator;
class BotBGAIMovement;
class BotAIVehicleMovement3D;

typedef std::list<Player*> NearPlayerList;
typedef std::vector<Player*> NearPlayerVec;
typedef std::list<Creature*> NearCreatureList;
typedef std::vector<Creature*> NearCreatureVec;
typedef std::list<Unit*> NearUnitList;
typedef std::vector<Unit*> NearUnitVec;

class BotGroupAI : public PlayerAI
{
public:
	static BotGroupAI* CreateBotGroupAIByPlayerClass(Player* player);
	static BotGroupAI* debugGroupAI;
    // Called by the native spell validator for bot and bot-owned pet spells.
    bool BlocksBossPullSpell(SpellInfo const* spell,Unit* caster,Unit* target,Position const* destination);
    bool BlocksBossPullAttack(Unit* target) { return ShouldBlockBossPull(target); }
    bool AllowsDungeonRoute(std::vector<FelsoriaMechanics::Vec> const& route);
    void ResetDungeonMovementAfterTeleport();
    bool SuppressDungeonTankFlee();


public:
	BotGroupAI(Player* player);
	~BotGroupAI();

	Player* GetAIPayer() { return me; }
	void ProcessBotCommand(Player* srcPlayer, std::string cmd);
    char const* FullMovementText();
    void FullExecuteTextOrder(Player* requester, std::string const& order);
	void DamageDealt(Unit* victim, uint32& damage, DamageEffectType damageType) override;
	void DamageEndure(Unit* attacker, uint32& damage, DamageEffectType damageType);
	void UpdateAI(uint32 diff) override;
	virtual void ResetBotAI();
	void StartFullDispel();
	void ClearFullDispel() { m_FullDispel = 0; }
	bool HasTeleport() { return !m_Teleporting.CanMovement(); }
	void SetTeleport(Position& telePos) { m_Teleporting.SetTeleport(telePos); }
	void SetTeleportToMaster() { m_Teleporting.SetTeleport(m_MasterPlayer, 0); }
    void UpdateTeleport(uint32 diff);
	void ClearCruxMovement();
	void SetCruxMovement(Position& pos) { me->SetSelection(ObjectGuid::Empty); m_CruxMovement.SetMovement(pos); }
	void RndCruxMovement(float dist = 12.0f) { m_CruxMovement.RandomMovement(dist); }
	void AddTankTarget(Creature* pCreature);
	void ClearTankTarget() { m_TankTargets.ClearTarget(); }
	void SetTankPosition(Position& pos) { m_TankTargets.SetMovement(pos); }
	bool ExistPullTarget() { return m_TankTargets.ExistPullTarget(); }
	bool IsNotSelect(Unit* pTarget);
	bool IsIDLEBot();
	bool CanExecuteSeduce();
	virtual uint32 GetSeducePriority() { return 0; }
	virtual void OnLevelUp(uint32 talentType);
	void OnLootedItem(uint32 entry);
	void DelayGiveXP(uint32 xp) { m_DelayGiveXP.DelayAddXP(xp); }
	void SearchCreatureListFromRange(Unit* center, NearCreatureVec& nearCreatures, float range, bool selfFaction);
	void ToggleFliterCreature(Creature* pCreature, bool fliter);
	virtual bool TryBlockCastingByTarget(Unit* pTarget) { return false; }
	bool IsFleeing() { return m_Flee.Fleeing(); }
	void AddCruxFlee(uint32 dur, Unit* pRefUnit) { if(me->isAlive()) m_Flee.AddCruxFlee(dur, pRefUnit, m_Movement); }
	void AddWaitSpecialAura(uint32 aura) { m_WaitAuras.AddSpecialAura(aura); }
	virtual bool ProcessPullSpell(Unit* pTarget) { return false; }
	void SetCruxHealTarget(ObjectGuid& healTarget) { if (IsHealerBotAI()) m_CruxHealTarget = healTarget; else m_CruxHealTarget = ObjectGuid::Empty; }
	void SetForceFleeState(bool forceFlee) { m_ForceFlee = forceFlee; }
	void SetSeduceTarget(ObjectGuid target) { m_SeduceTarget = target; me->SetSelection(m_SeduceTarget); }
	bool CastingSummonRite() { return m_MovetoUseGO.CastingSummonRite(); }
	bool SetMovetoUseGOTarget(ObjectGuid target) { return m_MovetoUseGO.SetNeedMovetoUseGO(target); }
	void Dismount();

	void SetVehicle3DNextMoveGap(float gap);
	void SetVehicle3DMoveTarget(Unit* pTarget, float offset);
	void SetMovetoHaltPos(Position& pos, uint32 dur = 1000) { m_MovetoHaltPos.SetMovetoPos(pos, dur); }

protected:
    PvpLegionRotation m_PvpRotation;
    BotActivityPolicy::Attempts m_OptionalPreparation;
    bool ProcessLegionPvp(Unit* target);

    bool m_CombatCastIssued = false;
    uint32 m_SpellAuditSpec = 0;
    std::map<std::pair<uint32, ObjectGuid>, uint32> m_UtilityAttempts;
	bool TryUpMount();
	bool CanReciveCommand(std::string& cmd, std::string& param);
	void ProcessSummonCommand();
	void ProcessAttackCommand();
	void ProcessFleeCommand();
	void ProcessStopCommand();
	void ProcessSetting();
	void ProcessListEquip(Player* srcPlayer);
	void ProcessUpequip(Player* srcPlayer, std::string& equipLink);
	void ProcessUnequip(Player* srcPlayer, std::string& equipLink);
	void ProcessDestroyItem(Player* srcPlayer, std::string& equipLink);
	void ProcessTradeItem(Player* srcPlayer, std::string& equipLink);
	void ProcessUseItem(Player* srcPlayer, std::string& equipLink);
	void ProcessTalent(Player* srcPlayer, std::string& talentText);
	void ProcessSummonRiteSpell(Player* srcPlayer);

protected:
	void ProcessHorror(uint32 diff);
	bool TargetIsNotDiminishingByType(Unit* pTarget, DiminishingGroup diType);
	bool TargetIsNotDiminishingByType2(Unit* pTarget, DiminishingGroup diType);
	bool HasAuraMechanic(Unit* pTarget, Mechanics mask);
	bool IsNotMovement();
	bool IsInvincible(Unit* pTarget);

	virtual void UpEnergy() {}
	bool UpdateMasterPlayer();
	bool TrySettingToMaster();
	bool TryTeleportToMaster();
	virtual Unit* GetCombatTarget(float range = BOTAI_SEARCH_RANGE);
	void ProcessFollowToMaster();
	bool ProcessRdfFormation();
    bool ProcessPredictiveMechanics();
    bool ProcessDungeonLeash(bool bridgeRescue=false);
    Position m_DungeonLeashDestination;
    bool ProcessVaultBridgeWatchdog();
    bool VaultBridgeLandingAllowed(Position const& destination) const;
    bool VaultBridgeObservedLanding(Position const& destination,uint32 now) const;
    Position m_VaultGroundedPosition;
    uint32 m_VaultGroundedSince=0;
    bool m_VaultGroundedObserved=false;
    FelsoriaMechanics::BridgeClock m_VaultCrossingWatch[2];
    Position m_VaultWatchLanding[2];
    uint32 m_VaultWatchLog[2]={0,0};
    ObjectGuid m_VaultWatchLeader;
    uint32 m_VaultWatchInstance=0;
    int m_VaultRescueIndex=-1;
    FelsoriaMechanics::RegroupClock m_DungeonLeashClock;
    ObjectGuid m_DungeonLeashLeader;
    uint32 m_DungeonLeashMap=0,m_DungeonLeashInstance=0;
    uint32 m_DungeonLeashTick=0,m_DungeonLeashAttempt=0;
    bool m_DungeonLeashAttempted=false;
    bool ProcessMountFollow();
    bool ShouldBlockBossPull(Unit* target);
    bool ProcessBossPullSafety();
    bool PathApproachesBlockedBoss(PathGenerator const& path);
    bool m_MechanicOwned=false,m_MechanicMoving=false;
    uint32 m_MechanicTick=0,m_MechanicLog=0,m_MechanicMap=0,m_MechanicInstance=0;
    uint32 m_MechanicBlockedUntil=0;
    uint32 m_MechanicProgressTime=0,m_MountFollowAttempt=0;
    unsigned m_MechanicSearchOffset=0;
    Position m_MechanicDestination,m_MechanicProgressPosition;
    ObjectGuid m_BossPullGuid;
    uint32 m_BossPullCheck=0,m_BossPullLog=0;
    bool m_BossPullBlocked=false;

    bool ProcessTankLeadFormation();
    uint32 m_TankLeadTick=0;
    bool m_TankLeadHandled=false;
    ObjectGuid m_TankLeadAnchor;
    Position m_TankLeadPosition;

    bool ProcessRdfAvoidance();
    bool ProcessRdfCombatPosition(Unit* target,float minRange,float maxRange);
    // FELSORIA_DUNGEON_COMBAT_V1: execute the path that was actually validated.
    bool LaunchRdfValidatedPath(PathGenerator const& path);
    // FELSORIA_FORMATION_RECOVERY_V1: track displacement independently of spline retries.
    Position m_RdfFormationProgressPosition;
    uint32 m_RdfFormationProgressTime=0;
    unsigned m_RdfFormationRecoveryAttempt=0;
    Position m_RdfPathProgressPosition;
    uint32 m_RdfPathProgressTime=0;
    // FELSORIA_AOE_RESUME_V1: bounded side-waypoint recovery.
    ObjectGuid m_RdfCombatDetourTarget;
    Position m_RdfCombatDetourAnchor,m_RdfCombatDetourDestination;
    unsigned m_RdfCombatDetourSteps=0;
    bool m_RdfCombatDetourMoving=false;
    uint32 m_RdfTankPullTime=0;
    uint32 m_RdfRotCastLog=0;
    bool ProcessRdfTravelRegroup();
    bool ProcessRdfVaultBridges();
    int m_VaultBridgeLink = -1;
    int m_VaultBridgeStage = -1;
    bool m_VaultBridgeReverse = false;
    bool m_VaultBridgeIssued = false;
    uint32 m_VaultBridgeStart = 0;
    uint32 m_VaultBridgeRetry = 0;
    // FELSORIA_BRIDGE_DODGE_V6: rescue after 4s stalled or 12s total.
    int m_VaultBridgeFallbackLink = -1;
    bool m_VaultBridgeFallbackReverse = false;
    uint32 m_VaultBridgeFallbackStart = 0;
    uint32 m_VaultBridgeProgressTime = 0;
    Position m_VaultBridgeProgressPosition;
    bool m_RdfTravelArmed=false,m_RdfTravelMoved=false;
    uint32 m_RdfTravelStable=0,m_RdfTravelLastRide=0,m_RdfTravelAttempt=0;
    uint32 m_RdfTravelMap=0,m_RdfTravelInstance=0;
    ObjectGuid m_RdfTravelLeader;
    Position m_RdfTravelOrigin,m_RdfTravelLanding;

    uint32 m_RdfStillSince=0,m_RdfStillLog=0;
    Position m_RdfStillPosition;
    uint32 m_RdfCombatGateLog=0,m_RdfPositionTick=0;
    unsigned m_RdfPositionAttempt=0;
    bool m_RdfRepositionRequested=false;
    std::map<std::string,std::vector<uint32>> m_RdfRotCandidates;
    uint32 m_RdfAvoidScan=0, m_RdfAvoidLog=0;
    bool m_RdfAvoidMoving=false;
	uint32 m_RdfFormationTick = 0;
	uint32 m_RdfFormationLog = 0;
	bool m_RdfFormationMoving = false;
	bool UseRdfLegionRotation() const;
    Unit* GetHumanPartyCombatTarget();
    bool UsePartyLegionRotation(Unit* enemy, bool partyCombat) const;
	bool ProcessRdfLegionRotation(Unit* enemy, bool partyCombat);
	uint32 m_RdfRotSpec = 0;
	uint32 m_RdfRotCatalogTime = 0;
	uint32 m_RdfRotLastSpell = 0;
    FelsoriaCombatPolicy::CooldownGate m_RdfDefensiveGate, m_RdfGroupGate;
	uint32 m_RdfRotLog = 0;
	std::map<std::string, uint32> m_RdfRotCatalog;
    FelsoriaCombatPolicy::RetryBook m_RdfRotRetry;
	std::set<std::string> m_RdfRotUnavailable;
	bool NonCombatProcess();
	bool ProcessRevivePlayer();
	bool DoFaceToTarget(Unit* pTarget);
	SpellCastResult TryCastSpell(uint32 spellID, Unit* pTarget = NULL, bool force = false, bool dismount = true);
	SpellCastResult TryCastSpellByLifePCTInterrupt(uint32 spellID, Unit* pTarget = NULL, uint32 interruptByLife = 99);
	SpellCastResult TryCastPullSpell(uint32 spellID, Unit* pTarget = NULL);
	SpellCastResult PetTryCastSpell(uint32 spellID, Unit* pTarget = NULL, bool force = false);
	virtual uint32 TryCastSummonRiteSpell() { return 0; }
	void SettingPetAutoCastSpell(uint32 spellID, bool autoCast);

	virtual void ClearMechanicAura() {}
	virtual bool NeedWaitSpecialSpell(uint32 diff);
	virtual bool NeedFlee();
	void FleeMovement();
	virtual void ProcessFlee();
	virtual bool ProcessNormalSpell() { return false; }
	virtual void ProcessRangeSpell(Unit* pTarget) {}
	virtual void ProcessMeleeSpell(Unit* pTarget) {}
	virtual void ProcessPrepareHealthSpell(Unit* pTarget) {}
	virtual void ProcessHealthSpell(Unit* pTarget) {}
	virtual void ProcessSeduceSpell(Unit* pTarget) {}
	virtual bool ProcessFullDispel() { m_FullDispel = 0; return false; }
	bool ExecuteSeduceCommand();
	void ProcessPrepareHealth();
	void ProcessHealth(bool canMove);
	void ProcessCombat(Unit* pTarget);
	void ProcessHalt(Unit* pTarget);
	bool ProcessTank(Unit* pTarget);

	void ChaseTarget(Unit* pTarget, bool isMelee, float range = 3.0f);
	NearUnitVec SearchFriend(float range = BOTAI_SEARCH_RANGE);
	NearUnitVec SearchFriendTargetIsTarget(Unit* pTarget, float range = BOTAI_SEARCH_RANGE);
	NearPlayerVec SearchFarFriend(float minRange, float maxRange, bool isIDLE);
	NearPlayerVec ExistFriendAttacker(float range = BOTAI_RANGESPELL_DISTANCE);
	NearUnitVec SearchNeedHealth(float range = BOTAI_SEARCH_RANGE);
	NearUnitVec SearchLifePctByFriendRange(Unit* pTarget, float lifePct, float range = NEEDFLEE_CHECKRANGE);
	Unit* RandomRangeEnemyByCasting(float range);
	Unit* SearchTankTargetEnemy(float range = BOTAI_SEARCH_RANGE);
	NearUnitVec RangeEnemyListByHasAura(uint32 aura, float range);
	NearUnitVec RangeEnemyListByNonAura(uint32 aura, float range);
	NearUnitVec RangeEnemyListByTargetIsMe(float range);
	NearUnitVec RangeListByTargetIsTarget(Unit* pTarget, float range);
	NearUnitVec RangeEnemyListByTargetRange(Unit* pTarget, float range);
	NearUnitVec SearchFarEnemy(float minRange, float maxRange);

	Unit* GetBotAIValidSelectedUnit();
	bool TargetIsControl(Unit* pTarget);
	bool TargetIsMelee(Player* pTarget);
	bool TargetIsRange(Player* pTarget);
	bool TargetIsMagic(Player* pTarget);
	bool TargetIsCastMagic(Player* pTarget);
	bool TargetIsStealth(Player* pTarget);

public:
	virtual uint32 GetReviveSpell() { return 0; }
	virtual bool IsTankBotAI() { return false; }
	virtual bool IsMeleeBotAI();
	virtual bool IsRangeBotAI();
	virtual bool IsHealerBotAI();
	virtual bool IsAttacker() { return true; }

protected:
	int32 m_UpdateTick;
	uint32 m_DuelEmoteTick;
	uint32 m_UseMountID;

	BotBGAIMovement* m_Movement;
	BotAIVehicleMovement3D* m_VehicleMovement3D;
	BotAIGuild m_Guild;
	BotAIHorrorState* pHorrorState;
	BotAIStoped m_CheckStoped;
	BotAITeleport m_Teleporting;
	BotAIUseFood m_UseFood;
	BotAIUsePotion m_UsePotion;
	BotAIFindNearLoot m_FindLoot;
	BotAILootedItems m_LootedItems;
	BotAITrade m_AITrade;
	BotAIGiveXP m_DelayGiveXP;
	BotAIRevive m_Revive;
	BotAIRevivePlayer m_RevivePlayer;
	BotAIFlee m_Flee;
	BotAICruxMovement m_CruxMovement;
	BotAITankTarget m_TankTargets;
	BotAIFly m_Flying;
	BotAIWishStore m_WishStore;
	BotAIFliterCreatures m_FliterCreatures;
	BotAIWaitSpecialAura m_WaitAuras;
	BotAICheckDuel m_CheckDuel;
	BotAIFastAid m_FastAid;
	BotAIMovetoUseGO m_MovetoUseGO;
	BotAIGroupLeader m_GroupLeader;
	BotAIMoveToHaltPosition m_MovetoHaltPos;

	ObjectGuid m_CruxHealTarget;
	ObjectGuid m_SeduceTarget;
	Player* m_MasterPlayer;
	uint32 m_FullDispel;
	uint32 m_MeleeFleeTick;
	uint32 bothp;
	bool m_ForceFlee;
	bool m_StopFollow;
	bool m_HasReset;

	bool m_IsMeleeBot;
	bool m_IsRangeBot;
	bool m_IsHealerBot;

	std::mutex m_ItemLock;

public:
	static bool PVE_MAX_DUNGEON;
	static bool PVE_DRIVING;
	static bool PVE_PULL;
private:
    uint32 m_RdfTeleportAttempt = 0;
    bool m_RdfTeleportAttempted = false;
private:
    uint32 m_RdfTraceTick = 0;
};

#endif // !_BOT_GROUP_AI_H
