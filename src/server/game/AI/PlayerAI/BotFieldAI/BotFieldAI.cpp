#include "FelsoriaDynamicBotMetrics.h"

#include "BotFieldAI.h"
#include "FelsoriaDHDummyLimit.h"
#include "MoveSplineInit.h"
#include "BotBGAIMovement.h"
#include "PlayerBotMgr.h"
#include "FieldBotMgr.h"
#include "PlayerBotSession.h"
#include "Spell.h"
#include "Pet.h"
#include "BotFieldClassAI.h"
#include "Group.h"
#include "WorldSession.h"
#include "PartyPackets.h"
#include "MiscPackets.h"
#include "CharmInfo.h"
#include <corecrt_math_defines.h>

BotFieldAI* BotFieldAI::debugFieldAI = NULL;

BotFieldAI* BotFieldAI::CreateBotFieldAIByPlayerClass(Player* player)
{
	PlayerBotSetting::ClearUnknowMount(player);
	BotFieldAI* pAI = NULL;
	switch (player->getClass())
	{
	case CLASS_WARRIOR:
		pAI = new FieldWarriorAI(player);
		break;
	case CLASS_MAGE:
		pAI = new FieldMageAI(player);
		break;
	case CLASS_PRIEST:
		pAI = new FieldPriestAI(player);
		break;
	case CLASS_HUNTER:
		pAI = new FieldHunterAI(player);
		break;
	case CLASS_WARLOCK:
		pAI = new FieldWarlockAI(player);
		break;
	case CLASS_PALADIN:
		pAI = new FieldPaladinAI(player);
		break;
	case CLASS_ROGUE:
		pAI = new FieldRogueAI(player);
		break;
	case CLASS_SHAMAN:
		pAI = new FieldShamanAI(player);
		break;
	case CLASS_DRUID:
		pAI = new FieldDruidAI(player);
		break;
	case CLASS_DEATH_KNIGHT:
		pAI = new FieldDeathknightAI(player);
		break;
	}
	if (!pAI)
		pAI = new BotFieldAI(player);
	pAI->ResetBotAI();
	return pAI;
}

BotFieldAI::BotFieldAI(Player* player) :
PlayerAI(player),
m_UpdateTick(BOTAI_UPDATE_TICK),
m_DrivingPVP(false),
m_LastWorldAttacker(ObjectGuid::Empty),
m_Movement(new BotBGAIMovement(player, this)),
m_WorldTravel(player),
m_UseMountID(PlayerBotSetting::MountForPlayer(player)),
m_WarfareTargetID(ObjectGuid::Empty),
m_Guild(player),
pHorrorState(NULL),
m_CheckStoped(player),
m_Teleporting(player),
m_UseFood(player),
m_UsePotion(player),
m_FindLoot(player),
m_AITrade(player),
m_Revive(player),
m_Flee(player),
m_IDLE(player),
m_CruxMovement(player),
m_WishStore(player),
m_CheckSetting(player),
m_CastRecords(player),
m_CheckDuel(player),
m_HasReset(false)
{
	if (!me->IsPvP())
	{
		BotUtility::PlayerBotTogglePVP(player, true);
	}
}

BotFieldAI::~BotFieldAI()
{

}

void BotFieldAI::UpdateAI(uint32 diff)
{
    // FELSORIA_DH_DUMMY_LIMIT_V1: release surplus trainers before cast/idle returns.
    if (FelsoriaDHDummyLimit::Enforce(me, m_Movement))
    {
        m_CruxMovement.ClearMovement();
        m_IDLE.Clear();
    }
	m_UpdateTick -= diff;
	if (m_UpdateTick > 0)
		return;
	m_UpdateTick = BOTAI_UPDATE_TICK;
    FelsoriaDynamicMetrics::Scope diagnosticAI(FelsoriaDynamicMetrics::FieldAIWork);

	m_WorldTravel.Observe(me->isInCombat() || !me->isAlive() ||
		me->HasUnitState(UNIT_STATE_CASTING) || me->GetSelectedUnit() != NULL);
	if (!me->isAlive())
		if (PlayerBotSession* session = dynamic_cast<PlayerBotSession*>(me->GetSession()))
			if (session->HandlesDynamicWorldRevive(me))
				return;
	if (!me->IsSettingFinish())
		return;

    if (PlayerBotSession* session = dynamic_cast<PlayerBotSession*>(me->GetSession()))
        if (session->WaitingForDynamicWorldPlacement())
            return;
	UpdateTeleport(BOTAI_UPDATE_TICK);
	if (!m_Teleporting.CanMovement())
		return;
	me->UpdateObjectVisibility(false);
	m_Guild.UpdateGuildProcess();
	if (ProcessGroupInvite())
		return;
	if (IsBGSchedule())
	{
		BotUtility::TryTeleportHome(this);
		return;
	}

	if (!m_HasReset)
		ResetBotAI();
	if (me->isAlive())
	{
        GetBotAIValidSelectedUnit(); // Validate before PvP/maintenance early returns.
		m_CheckStoped.UpdatePosition(diff);
		BotUtility::TryTeleportPlayerPet(me);
		if (!PvpLegionRotation::Applicable(me, me->GetSelectedUnit())) ClearMechanicAura();
		if (!IsNotMovement())
			ProcessHorror(diff);
		if (NeedWaitSpecialSpell(BOTAI_UPDATE_TICK))
			return;

        if(me->HasUnitState(UNIT_STATE_CASTING) && !m_CruxMovement.HasCruxMovement() &&
            PvpLegionRotation::Applicable(me,me->GetSelectedUnit()))
            BotUtility::TryCombatInterrupt(me,me->GetSelectedUnit(),m_Movement);
		if (me->HasUnitState(UNIT_STATE_CASTING))
			return;
        if (me->getClass() == CLASS_HUNTER && me->GetSummonedBattlePet())
            me->UnsummonCurrentBattlePetIfAny(false);
        if (m_PvpRotation.SelfHeal(me, m_Movement)) return;
        // Acquire PvP threats before food, trade, buffs and mount attempts.
        bool humanAssist = false;
        Unit* worldEnemy = GetCombatTarget(BOTAI_SEARCH_RANGE,&humanAssist);
        if (worldEnemy && (worldEnemy->ToPlayer() || me->isInCombat() || humanAssist))
        {
            if (me->GetSelectedUnit() != worldEnemy)
            {
                me->AttackStop();
                me->SetSelection(worldEnemy->GetGUID());
                m_Movement->ClearMovement();
                m_IDLE.Clear();
            }
            m_CruxMovement.ClearMovement();
            m_UseFood.ClearFoodState();
            Dismount();
        }
        bool const combatPriority = humanAssist || me->isInCombat() ||
            (worldEnemy && worldEnemy->ToPlayer()) ||
            (me->GetSelectedUnit() && me->GetSelectedUnit()->ToPlayer());
		if (!combatPriority && !m_CruxMovement.HasCruxMovement() && NonCombatProcess())
		{
			m_WorldTravel.Observe(true);
			return;
		}

		if (!combatPriority && ProcessNormalSpell())
			return;
		m_Movement->SyncPosition(me->GetPosition());
        if (!m_CruxMovement.HasCruxMovement() && ProcessLegionPvp(me->GetSelectedUnit())) return;
		if (TryUpMount())
			return;
		if (!me->HasAura(m_UseMountID) && !me->HasUnitState(UNIT_STATE_CASTING))
			m_UsePotion.TryUsePotion();
		if (me->isInCombat() && !PvpLegionRotation::Applicable(me, me->GetSelectedUnit()))
			UpEnergy();
		Unit* pTarget = GetBotAIValidSelectedUnit();
		if (m_CruxMovement.HasCruxMovement())
		{
			m_CruxMovement.UpdateCruxMovement(m_Movement);
		}
		else if (pTarget && pTarget->isAlive() && !IsInvincible(pTarget))
		{
			float distance = me->GetDistance(pTarget->GetPosition());
			if (distance < BOTAI_SEARCH_RANGE)
			{
				if (IsHealerBotAI() && me->getLevel() >= 10)
					ProcessHealth();
				else
					ProcessCombat(pTarget);
			}
			else if (distance > BOTAI_SEARCH_RANGE * 2.5f || me->GetMap() != pTarget->GetMap())
			{
				//me->StopMoving();
				me->SetSelection(ObjectGuid::Empty);
			}
			else
			{
				m_Movement->MovementToTarget();
			}
		}
		else if (pTarget = worldEnemy)
		{
			me->AttackStop();
			me->SetSelection(pTarget->GetGUID());
		}
		else
		{
			me->SetSelection(ObjectGuid::Empty);
			ProcessIDLE();
		}
	}
	else
	{
		m_CastRecords.ClearRecordSpell();
		m_WishStore.ClearStores();
		m_CruxMovement.ClearMovement();
		me->SetSelection(ObjectGuid::Empty);
		m_Revive.UpdateRevive(BOTAI_UPDATE_TICK, m_Teleporting);
	}
}

void BotFieldAI::ResetBotAI()
{
	m_WorldTravel.Reset();
    m_WorldProgressTarget.Clear();m_WorldBlockedTarget.Clear();
    m_WorldProgressTime=m_WorldBlockedUntil=m_WarfareProgressTime=0;
	m_LastWorldAttacker = ObjectGuid::Empty;
	PlayerBotSetting::ClearUnknowMount(me);
	m_Movement->ClearMovement();
	if (m_UseMountID != 0)
	{
		if (!me->HasSpell(m_UseMountID))
			me->learnSpell(m_UseMountID, false);
	}
	m_IsMeleeBot = IsMeleeBotAI();
	m_IsRangeBot = IsRangeBotAI();
	m_IsHealerBot = IsHealerBotAI();
	m_HasReset = true;

	m_CastRecords.ClearRecordSpell();
}

void BotFieldAI::SetDrivingPVP(bool driving)
{
	if (m_DrivingPVP != driving)
	{
		m_DrivingPVP = driving;
		me->SetSelection(ObjectGuid::Empty);
		me->CombatStop(true);
	}
}

void BotFieldAI::SetWarfareTarget(Unit* pTarget)
{
    ObjectGuid guid=pTarget ? pTarget->GetGUID() : ObjectGuid::Empty;
    if (guid!=m_WarfareTargetID) m_WarfareProgressTime=0;
    m_WarfareTargetID=guid;
}

void BotFieldAI::SetCruxMovement(Position& pos)
{
	me->SetSelection(ObjectGuid::Empty);
	m_CruxMovement.SetMovement(pos);
}

bool BotFieldAI::ProcessGroupInvite()
{
	if (Group* pGroup = me->GetGroup())
	{
		//if (pGroup->isLFGGroup())
		{
			m_Movement->ClearMovement();
			PlayerBotMgr::SwitchPlayerBotAI(me, PlayerBotAIType::PBAIT_GROUP, true);
			return true;
		}
		//return false;
	}
	WorldSession* pWorldSession = me->GetSession();
	if (!pWorldSession)
		return false;
	PlayerBotSession* pSession = dynamic_cast<PlayerBotSession*>(pWorldSession);
	if (!pSession || pSession->HasSchedules())
		return false;
	bool const dynamicHeroStart =
		sPlayerBotMgr->GetDynamicBotTargetLevel(pSession->GetAccountId()) &&
		(PlayerBotMgr::GetDynamicAreaRequiredClass(me->GetMapId(),
			me->GetZoneId(), me->GetAreaId()) == me->getClass());
	if (me->IsInWorld() && me->GetMap()->IsDungeon() && !dynamicHeroStart)
	{
		// World-leased class-hall bots are owned by the dynamic placement manager.
		if (!sPlayerBotMgr->GetDynamicBotTargetLevel(pSession->GetAccountId()))
			BotUtility::TryTeleportHome(this);
		return true;
	}
	Group* pGroup = me->GetGroupInvite();
	if (!pGroup)
		return false;
	WorldPacket opcode(CMSG_PARTY_INVITE_RESPONSE);
	if (pSession->HasSchedules() || me->InBattleground())
	{
		WorldPackets::Party::PartyInviteResponse packet(std::move(opcode));
		packet.Accept = 0;
		pWorldSession->HandlePartyInviteResponse(packet);
	}
	else
	{
		WorldPackets::Party::PartyInviteResponse packet(std::move(opcode));
		packet.Accept = 1;
        uint32 const leader = pGroup->GetLeaderGUID().GetGUIDLow();
		pWorldSession->HandlePartyInviteResponse(packet);
        if (me->GetGroup() && me->GetGroup()->GetLeaderGUID().GetGUIDLow() == leader &&
            sPlayerBotMgr->GetDynamicBotTargetLevel(pSession->GetAccountId()))
            pSession->RequestDynamicPartySummon(leader);
		m_Movement->ClearMovement();
		PlayerBotMgr::SwitchPlayerBotAI(me, PlayerBotAIType::PBAIT_GROUP, true);
	}
	return true;
}

bool BotFieldAI::IsNotSelect(Unit* pTarget)
{
    if (!FelsoriaDHDummyLimit::CanUse(me, pTarget))
        return true;
	if (!pTarget || !pTarget->isAlive())
		return true;
	if (pTarget->HasAura(27827)) // (27827 ¾ÈÊêÖ®»ê ÉñÄÁËÀÍöºó)
		return true;
	return false;
}

bool BotFieldAI::IsIDLEBot()
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

bool BotFieldAI::TryUpMount()
{
	PlayerBotSetting::RemoveForbiddenMount(me);
	if (!m_UseMountID)
		return false;
	if (me->isInCombat() || me->GetSelectedUnit() || me->HasAura(m_UseMountID) || me->getLevel() < 20)
		return false;
	if (!me->GetMap()->IsOutdoors(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ()))
		return false;
	if (me->IsMounted())
		return false;
	if (me->HasUnitState(UNIT_STATE_CASTING))
		return false;
    if (!m_OptionalPreparation.Begin((uint64(1)<<62)|m_UseMountID,getMSTime(),15000))
        return false;
	m_Movement->ClearMovement();
	return (TryCastSpell(m_UseMountID, me) == SPELL_CAST_OK);
}

void BotFieldAI::Dismount()
{
	if (!me->HasAura(m_UseMountID))
		return;
	me->RemoveOwnedAura(m_UseMountID, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
}

void BotFieldAI::ProcessHorror(uint32 diff)
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

bool BotFieldAI::HasAuraMechanic(Unit* pTarget, Mechanics mask)
{
	if (!pTarget)
		return false;
	return (pTarget->HasAuraWithMechanic(1 << mask));
}

bool BotFieldAI::IsNotMovement()
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

bool BotFieldAI::IsInvincible(Unit* pTarget)
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

bool BotFieldAI::IsBGSchedule()
{
	if (me->GetSession()->HasBGSchedule() || me->InBattleground() || me->isUsingLfg())
		return true;
	return false;
}

// FELSORIA_WORLD_PVP_V1: acquire legal enemies independently of their selection.
bool BotFieldAI::IsWorldTargetBlocked(Unit* target) const
{
    return target && target->GetGUID()==m_WorldBlockedTarget && m_WorldBlockedUntil &&
        int32(getMSTime()-m_WorldBlockedUntil)<0;
}

bool BotFieldAI::CanSelectPlayerEnemy(Player* player)
{
    return player && !IsWorldTargetBlocked(player) && !IsInvincible(player) && player != me && player->GetTeamId() != me->GetTeamId() &&
        player->IsInWorld() && player->GetMap() == me->GetMap() &&
        me->InSamePhase(player) && me->InSamePhaseId(player) && !IsNotSelect(player) &&
        me->IsValidAttackTarget(player) && me->canSeeOrDetect(player, false, true) &&
        me->IsWithinLOSInMap(player);
}

void BotFieldAI::DamageTaken(Unit* attacker, uint32& /*damage*/, DamageEffectType /*damageType*/)
{
    // Damage hooks only remember a GUID: no casts, movement or cross-AI mutation here.
    if (attacker && attacker != me)
        m_LastWorldAttacker = attacker->GetGUID();
}

Unit* BotFieldAI::GetCombatTarget(float range, bool* humanAssist)
{
    if (humanAssist) *humanAssist=false;
    Unit* encounter = BotUtility::FindWorldPvPEnemy(me, range);
    auto validThreat = [this, range](Unit* unit)
    {
        return unit && !IsWorldTargetBlocked(unit) && !IsInvincible(unit) && unit != me && unit->IsInWorld() && unit->GetMap() == me->GetMap() &&
            me->InSamePhase(unit) && me->InSamePhaseId(unit) && !IsNotSelect(unit) &&
            me->IsValidAttackTarget(unit) && me->GetDistance(unit) <= range &&
            me->canSeeOrDetect(unit, false, true) && me->IsWithinLOSInMap(unit);
    };
    if (m_LastWorldAttacker != ObjectGuid::Empty)
    {
        Unit* attacker = ObjectAccessor::GetUnit(*me, m_LastWorldAttacker);
        if (validThreat(attacker))
            return attacker;
        m_LastWorldAttacker = ObjectGuid::Empty;
    }
    Unit* nearest = NULL;
    float nearestDistance = range;
    for (Unit* attacker : *me->getAttackers())
        if (validThreat(attacker) && me->GetDistance(attacker) <= nearestDistance)
        {
            nearest = attacker;
            nearestDistance = me->GetDistance(attacker);
        }
    if (nearest)
        return nearest;
    // Keep a legal PvP opponent rather than switching randomly every update.
    Unit* selected = me->GetSelectedUnit();
    if (selected && selected->ToPlayer() && validThreat(selected) &&
        selected->ToPlayer()->GetTeamId() != me->GetTeamId())
        return selected;
    NearPlayerList playersNearby;
    me->GetPlayerListInGrid(playersNearby, range);
    for (Player* player : playersNearby)
        if (CanSelectPlayerEnemy(player) && me->GetDistance(player) <= nearestDistance)
        {
            nearest = player;
            nearestDistance = me->GetDistance(player);
        }
    if (nearest)
        return nearest;
    if (validThreat(encounter)) return encounter;
    if (Unit* assist=BotUtility::FindWorldAssistEnemy(me,range))
    {
        if (validThreat(assist))
        {
            if (humanAssist) *humanAssist=true;
            return assist;
        }
    }
    // Preserve the existing outdoor PvE fallback, with the same legality guards.
    NearCreatureVec creatures;
    SearchCreatureListFromRange(me, creatures, range, false);
    NearUnitVec validTarget;
    for (Creature* creature : creatures)
        if (validThreat(creature))
            validTarget.push_back(creature);
    return validTarget.empty() ? NULL : validTarget[urand(0, validTarget.size() - 1)];
}

bool BotFieldAI::NonCombatProcess()
{
	{
		std::lock_guard<std::mutex> lock(m_ItemLock);
		if (m_CheckDuel.CheckDuel())
			sPlayerBotMgr->SwitchPlayerBotAI(me, PlayerBotAIType::PBAIT_DUEL, true);
		m_CheckSetting.UpdateCheckSetting();
		//m_Guild.UpdateGuildProcess();
		if (m_AITrade.ProcessTrade())
			return true;
		if (m_UseFood.UpdateBotFood(BOTAI_UPDATE_TICK, m_UseMountID))
			return true;
		if (m_FindLoot.DoFindLoot(BOTAI_UPDATE_TICK, m_Movement, m_UseMountID))
			return true;
		//if (ProcessGroupInvite())
		//	return true;
		m_WishStore.UpdateWishStore();
	}
	return false;
}

bool BotFieldAI::DoFaceToTarget(Unit* pTarget)
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

SpellCastResult BotFieldAI::TryCastSpell(uint32 spellID, Unit* pTarget, bool force, bool dismount)
{
	if (!spellID || !me->HasSpell(spellID))
	{
		if (spellID)
			SetResetAI();
		return SpellCastResult::SPELL_FAILED_SPELL_LEARNED;
	}
	SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
	if (!spellInfo || spellInfo->IsPassive())
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
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
	//spell->InitExplicitTargets(targets);
	//SpellCastResult castResult = spell->CheckCast(true);
	//if (castResult != SpellCastResult::SPELL_CAST_OK)
	//{
	//	spell->finish(false);
	//	delete spell;
	//	return castResult;
	//}
	if (dismount)
		Dismount();
	SpellCastResult castResult = spell->prepare(&targets);
	if (castResult != SpellCastResult::SPELL_CAST_OK)
	{
		if (castResult == SpellCastResult::SPELL_FAILED_NOT_MOUNTED)
			PlayerBotSetting::ClearUnknowMount(me);
		else if (castResult == SpellCastResult::SPELL_FAILED_BAD_TARGETS)
		{
			if (pTarget && me->GetTargetGUID() == pTarget->GetGUID())
				me->SetTarget(ObjectGuid::Empty);

		}
		//spell->finish(false);
		//delete spell;
		return castResult;
	}
	m_WishStore.TryWishStore(spellID, pTarget);
    if (gemPreparation) m_OptionalPreparation.Complete(spellID);
	return SpellCastResult::SPELL_CAST_OK;
}

SpellCastResult BotFieldAI::TryCastPullSpell(uint32 spellID, Unit* pTarget)
{
	if (!spellID || !me->HasSpell(spellID))
		return SpellCastResult::SPELL_FAILED_SPELL_LEARNED;
	SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
	if (!spellInfo || spellInfo->IsPassive())
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

SpellCastResult BotFieldAI::PetTryCastSpell(uint32 spellID, Unit* pTarget, bool force)
{
	Pet* pPet = me->GetPet();
	if (!pPet || !pPet->isAlive())
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
	if (!spellID || !pPet->HasSpell(spellID))
		return SpellCastResult::SPELL_FAILED_SPELL_LEARNED;
	SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
	if (!spellInfo || spellInfo->IsPassive())
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
	TriggerCastData data;
	Spell* spell = new Spell(pPet, spellInfo, data);
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

void BotFieldAI::SettingPetAutoCastSpell(uint32 spellID, bool autoCast)
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

bool BotFieldAI::NeedWaitSpecialSpell(uint32 diff)
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
	return false;
}

bool BotFieldAI::NeedFlee()
{
	if (m_Flee.Fleeing())
		return true;
	if (IsMeleeBotAI())
	{
		if (me->GetHealthPct() < 25)
			return true;
	}
	return false;
}

void BotFieldAI::FleeMovement()
{
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
			selectEnemy = enemys[urand(0, enemys.size() - 1)];
		m_Flee.UpdateFleeMovementByPVP(selectEnemy, m_Movement);
	}
	else
		m_Flee.Clear();
}

void BotFieldAI::ProcessFlee()
{
	FleeMovement();
}

void BotFieldAI::ProcessIDLE()
{
	if (!ProcessWarfare())
	{
		if (!m_WorldTravel.Update(m_Movement))
			m_IDLE.UpdateIDLEMovement(m_Movement);
		else
			m_IDLE.Clear();
	}
	else
		m_IDLE.Clear();
}

void BotFieldAI::ProcessHealth()
{
    if (ProcessLegionPvp(me->GetSelectedUnit())) return;
	if (me->HasUnitState(UNIT_STATE_CASTING))
		return;
	NearUnitVec& needHealth = SearchNeedHealth(BOTAI_SEARCH_RANGE * 1.5);
	if (needHealth.empty())
	{
		ProcessCombat(me->GetSelectedUnit());
		return;
	}
	Unit* healthPlayer = needHealth[urand(0, needHealth.size() - 1)];
	bool inView = me->IsWithinLOSInMap(healthPlayer);
	if (inView)
	{
		if (me->GetDistance(healthPlayer) > BOTAI_RANGESPELL_DISTANCE - 3)
		{
			if (!IsNotMovement())
				m_Movement->MovementTo(healthPlayer->GetPositionX(), healthPlayer->GetPositionY(), healthPlayer->GetPositionZ());
			return;
		}
		else
		{
			m_Movement->ClearMovement();
			me->SetInFront(healthPlayer);
			//me->SetFacingToObject(healthPlayer);
			ProcessHealthSpell(healthPlayer);
			return;
		}
	}
	else if (!IsNotMovement())
	{
		m_Movement->MovementTo(healthPlayer->GetGUID());
		return;
	}
	if (NeedFlee())
	{
		ProcessFlee();
		return;
	}
}

void BotFieldAI::ProcessCombat(Unit* pTarget)
{
    if (!FelsoriaDHDummyLimit::CanUse(me, pTarget))
    {
        FelsoriaDHDummyLimit::Enforce(me, m_Movement);
        return;
    }
    if (ProcessLegionPvp(pTarget)) return;
	if (!pTarget)
		return;
	if (TargetIsStealth(pTarget->ToPlayer()))
	{
		me->SetSelection(ObjectGuid::Empty);
		return;
	}
	bool inView = me->IsWithinLOSInMap(pTarget);
	if (inView)
	{
		if (me->HasUnitState(UNIT_STATE_CASTING))
			return;
		if (m_IsRangeBot)
		{
			if (NeedFlee())
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
			if (me->IsWithinMeleeRange(pTarget))
			{
				me->SetInFront(pTarget);
				me->SetFacingToObject(pTarget);
				//if (!DoFaceToTarget(pTarget))
				{
					Unit* pVictim = me->getVictim();
					if (!pVictim || pVictim != pTarget)
						me->Attack(pTarget, true);
					if (!IsNotMovement())
						ChaseTarget(pTarget, true);
					if (!IsNotSelect(pTarget))
						ProcessMeleeSpell(pTarget);
				}
			}
			else
			{
				if (!IsNotMovement())
					m_Movement->MovementToTarget();
				if (me->GetDistance(pTarget) < BOTAI_RANGESPELL_DISTANCE && pTarget->isAlive() && !me->IsVehicle())
				{
					ProcessRangeSpell(pTarget);
				}
			}
		}
	}
	else if (!IsNotMovement())
		m_Movement->MovementToTarget();
}

void BotFieldAI::ProcessMeleeSpell(Unit* target)
{
    if (!target || me->HasUnitState(UNIT_STATE_CASTING))
        return;
    auto cast = [this, target](uint32 spell)
    {
        return me->HasSpell(spell) && TryCastSpell(spell, target) == SPELL_CAST_OK;
    };
    switch (me->getClass())
    {
    case CLASS_DEMON_HUNTER:
        if (me->GetSpecializationId() == 581)
        {
            if (me->GetHealthPct() < 70 && cast(228477)) return; // Soul Cleave
            if (cast(203782)) return; // Shear
        }
        else
        {
            if (cast(201427) || cast(162794)) return; // Annihilation / Chaos Strike
            if (cast(162243)) return; // Demon's Bite
        }
        break;
    case CLASS_MONK:
        if (cast(107428) || cast(205523) || cast(100784)) return;
        cast(100780); // Rising Sun Kick / Blackout Strike / Blackout Kick / Tiger Palm
        break;
    default:
        break;
    }
}

void BotFieldAI::ProcessRangeSpell(Unit* target)
{
    if (me->getClass() == CLASS_DEMON_HUNTER && target && !me->HasUnitState(UNIT_STATE_CASTING))
    {
        uint32 const spell = me->GetSpecializationId() == 581 ? 204157 : 185123;
        if (me->HasSpell(spell))
            TryCastSpell(spell, target); // Throw Glaive, correct specialization variant
    }
}

bool BotFieldAI::ProcessWarfare()
{
    if (m_WarfareTargetID.IsEmpty()) {m_WarfareProgressTime=0;return false;}
    Player* target=ObjectAccessor::FindPlayer(m_WarfareTargetID);
    if (!target || !target->isAlive() || !target->IsInWorld() ||
        target->GetMap()!=me->GetMap() || target->InBattleground() ||
        !me->InSamePhase(target) || !me->InSamePhaseId(target))
    {SetWarfareTarget(NULL);m_WarfareProgressTime=0;return false;}
    if (me->GetDistance(target->GetPosition())<=BOTAI_RANGESPELL_DISTANCE)
    {m_WarfareProgressTime=0;return false;}
    uint32 now=getMSTime();
    if (!m_WarfareProgressTime || me->GetExactDist(&m_WarfareProgressPosition)>1.0f ||
        me->HasUnitState(UNIT_STATE_CASTING | UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED |
            UNIT_STATE_FLEEING) || me->HasAuraWithMechanic(1 << Mechanics::MECHANIC_ROOT))
    {m_WarfareProgressTime=now;m_WarfareProgressPosition=me->GetPosition();}
    else if (uint32(now-m_WarfareProgressTime)>=10000)
    {
        SetWarfareTarget(NULL);m_WarfareProgressTime=0;
        m_Movement->ClearMovement();return false;
    }
    m_Movement->MovementTo(target->GetPositionX(),target->GetPositionY(),target->GetPositionZ(),20);
    return true;
}

void BotFieldAI::ChaseTarget(Unit* pTarget, bool isMelee, float range)
{
	if (IsNotSelect(pTarget))
		return;
	if (isMelee && pTarget->ToPlayer())
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
			bool const dynamicOutdoor = me->GetSession() &&
				sPlayerBotMgr->GetDynamicBotTargetLevel(me->GetSession()->GetAccountId()) &&
				!me->GetGroup() && !me->InBattleground() && !me->InArena();
			if (dynamicOutdoor)
				m_Movement->MovementTo(pTarget->GetGUID(), range);
			else
			{
				me->GetMotionMaster()->Clear();
				me->GetMotionMaster()->MoveChase(pTarget, range);
			}
		}
		else
			m_Movement->ClearMovement();
	}
}

void BotFieldAI::SearchCreatureListFromRange(Unit* center, NearCreatureVec& nearCreatures, float range, bool selfFaction)
{
	NearCreatureList nearCreature;
	Trinity::AllWorldObjectsInRange checker(center, range);
	Trinity::CreatureListSearcher<Trinity::AllWorldObjectsInRange> searcher(center, nearCreature, checker);
	center->VisitNearbyGridObject(range, searcher);
	for (Creature* pCreature : nearCreature)
	{
        if (!selfFaction && !FelsoriaDHDummyLimit::CanUse(me, pCreature))
            continue;
		if (!pCreature->isAlive() || pCreature->isPet())// || pCreature->IsTotem())
			continue;
		if (pCreature->IsInEvadeMode())
			continue;
		if (selfFaction && me->IsValidAttackTarget(pCreature))
			continue;
		if (!selfFaction && !me->IsValidAttackTarget(pCreature))
			continue;
		if (FieldBotMgr::FIELDBOT_CREATURE || selfFaction)
		{
			nearCreatures.push_back(pCreature);
		}
		else if (!selfFaction)
		{
			ObjectGuid& targetGUID = pCreature->GetTargetGUID();
			if (targetGUID != ObjectGuid::Empty)
				nearCreatures.push_back(pCreature);
		}
	}
}

NearUnitVec BotFieldAI::SearchFriend(float range)
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

NearPlayerVec BotFieldAI::SearchFarFriend(float minRange, float maxRange, bool isIDLE)
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

NearPlayerVec BotFieldAI::ExistFriendAttacker(float range /* = BOTAI_RANGESPELL_DISTANCE */)
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

NearUnitVec BotFieldAI::SearchNeedHealth(float range /* = BOTAI_SEARCH_RANGE */)
{
	NearPlayerList playersNearby;
	NearUnitVec lifeTo25, life25To55, life55To80;
	me->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() == me->GetTeamId())
		{
			float healthPct = pVisionPlayer->GetHealthPct();
			if (healthPct < 25)
				lifeTo25.push_back(pVisionPlayer);
			else if (healthPct >= 25 && healthPct < 55)
				life25To55.push_back(pVisionPlayer);
			else if (healthPct >= 55 && healthPct < 80)
				life55To80.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(me, creatures, range, true);
	for (Creature* pCreature : creatures)
	{
		float healthPct = pCreature->GetHealthPct();
		if (healthPct < 25)
			lifeTo25.push_back(pCreature);
		else if (healthPct >= 25 && healthPct < 55)
			life25To55.push_back(pCreature);
		else if (healthPct >= 55 && healthPct < 80)
			life55To80.push_back(pCreature);
	}

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

NearUnitVec BotFieldAI::SearchLifePctByFriendRange(Unit* pTarget, float lifePct, float range /* = NEEDFLEE_CHECKRANGE */)
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

Unit* BotFieldAI::RandomRangeEnemyByCasting(float range)
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

NearUnitVec BotFieldAI::RangeEnemyListByHasAura(uint32 aura, float range)
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

NearUnitVec BotFieldAI::RangeEnemyListByNonAura(uint32 aura, float range)
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

NearUnitVec BotFieldAI::RangeEnemyListByTargetIsMe(float range)
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

NearUnitVec BotFieldAI::RangeListByTargetIsTarget(Unit* pTarget, float range)
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

NearUnitVec BotFieldAI::RangeEnemyListByTargetRange(Unit* pTarget, float range)
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

NearUnitVec BotFieldAI::SearchFarEnemy(float minRange, float maxRange)
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

bool BotFieldAI::IsMeleeBotAI()
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

bool BotFieldAI::IsRangeBotAI()
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

bool BotFieldAI::IsHealerBotAI()
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

Unit* BotFieldAI::GetBotAIValidSelectedUnit()
{
	Unit* pTarget = me->GetSelectedUnit();
	bool isValid = true;
    if (pTarget && (!pTarget->IsInWorld() || pTarget->GetMap()!=me->GetMap() ||
        me->GetDistance(pTarget)>BOTAI_SEARCH_RANGE*2.5f ||
        !me->canSeeOrDetect(pTarget,false,true) || IsInvincible(pTarget) ||
        IsWorldTargetBlocked(pTarget))) isValid=false;
	if (!pTarget)
		isValid = false;
	if (isValid && !pTarget->IsVisible())
		isValid = false;
	if (isValid && (!me->InSamePhase(pTarget) || !me->InSamePhaseId(pTarget)))
		isValid = false;
	if (isValid && !me->IsValidAttackTarget(pTarget))
		isValid = false;
	if (isValid && IsNotSelect(pTarget))
		isValid = false;
	if (isValid && pTarget->ToCreature() && pTarget->ToCreature()->IsInEvadeMode())
		isValid = false;
    // A reachable fight owns the update; a rejected chase cannot own it
    // forever. Count actual movement and exempt native CC/cast/travel states.
    if (isValid)
    {
        uint32 now=getMSTime();
        bool protectedState=me->HasUnitState(UNIT_STATE_CASTING | UNIT_STATE_STUNNED |
            UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING | UNIT_STATE_JUMPING) ||
            me->HasAuraWithMechanic(1 << Mechanics::MECHANIC_ROOT) ||
            me->IsBeingTeleported() || me->IsFlying() || me->isInFlight() ||
            me->GetTransport() || me->GetVehicleBase() ||
            me->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
        bool inRange=me->GetDistance(pTarget)<(m_IsMeleeBot ? 4.0f : BOTAI_RANGESPELL_DISTANCE) &&
            me->IsWithinLOSInMap(pTarget);
        if (protectedState || inRange || m_WorldProgressTarget!=pTarget->GetGUID() ||
            !m_WorldProgressTime || me->GetExactDist(&m_WorldProgressPosition)>1.0f)
        {
            m_WorldProgressTarget=pTarget->GetGUID();m_WorldProgressTime=now;
            m_WorldProgressPosition=me->GetPosition();
        }
        else if (uint32(now-m_WorldProgressTime)>=10000)
        {
            m_WorldBlockedTarget=pTarget->GetGUID();m_WorldBlockedUntil=now+15000;
            isValid=false;
        }
    }
	if (!isValid)
	{
        if (!me->GetSelection().IsEmpty())
        {
            m_Movement->ReleaseCombatMovement();m_IDLE.Clear();
            m_WorldProgressTarget.Clear();m_WorldProgressTime=0;
        }
		me->AttackStop();
		me->SetSelection(ObjectGuid::Empty);
		return NULL;
	}
	return pTarget;
}

bool BotFieldAI::TargetIsRange(Player* pTarget)
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

bool BotFieldAI::TargetIsMagic(Player* pTarget)
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

bool BotFieldAI::TargetIsCastMagic(Player* pTarget)
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

bool BotFieldAI::TargetIsStealth(Player* pTarget)
{
	if (!pTarget)
		return false;
	// (1784 µÁÔôÇ±ÐÐ || 5215 µÂÂ³ÒÁÇ±ÐÐ || 66 ·¨Ê¦ÒþÐÎ || 58984 °µÒ¹Òþ¶Ý)
	if (pTarget->HasAura(1784) || pTarget->HasAura(5215) ||
		pTarget->HasAura(66) || pTarget->HasAura(58984))
	{
		if (!me->canSeeOrDetect(pTarget, false, true)) // Õì²âÇ±ÐÐ
			return true;
	}
	return false;
}

// FELSORIA_PVP_LEGION_ROTATIONS_V1
bool BotFieldAI::ProcessLegionPvp(Unit* target)
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
