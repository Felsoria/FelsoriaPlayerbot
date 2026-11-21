
#include "CommandWS.h"
#include "BotAI.h"
#include "BattlegroundWarsongGulch.h"

CommandWS::CommandWS(Battleground* pBG, TeamId team) :
CommandBG(pBG, team),
lastUpdateTick(COMMANDBG_UPDATE_TICK / 5)
{
	if (team == TeamId::TEAM_ALLIANCE)
	{
		m_AIWPEntrys.push_back(7);
		m_AIWPEntrys.push_back(8);
		m_AIWPEntrys.push_back(9);
		m_AIWPEntrys.push_back(10);

		m_AIWPEntrys.push_back(11);
		m_AIWPEntrys.push_back(12);
		m_AIWPEntrys.push_back(13);
		m_AIWPEntrys.push_back(14);
	}
	else
	{
		m_AIWPEntrys.push_back(11);
		m_AIWPEntrys.push_back(12);
		m_AIWPEntrys.push_back(13);
		m_AIWPEntrys.push_back(14);

		m_AIWPEntrys.push_back(7);
		m_AIWPEntrys.push_back(8);
		m_AIWPEntrys.push_back(9);
		m_AIWPEntrys.push_back(10);
	}
}

CommandWS::~CommandWS()
{
}

void CommandWS::Initialize()
{
	CommandBG::Initialize();
}

const Creature* CommandWS::GetMatchGraveyardNPC(const Player* player)
{
	if (!player || !player->InBattleground())
		return NULL;
	Creature* pCreature = NULL;
	if (player->GetBGTeamId() == TeamId::TEAM_ALLIANCE)
		pCreature = player->GetBattleground()->GetBGCreature(BG_WS_CreatureTypes::WS_SPIRIT_MAIN_ALLIANCE);
	else
		pCreature = player->GetBattleground()->GetBGCreature(BG_WS_CreatureTypes::WS_SPIRIT_MAIN_HORDE);
	return pCreature;
}

void CommandWS::Update(uint32 diff)
{
	CommandBG::Update(diff);

	if (m_pBattleground && m_pBattleground->GetStatus() == BattlegroundStatus::STATUS_IN_PROGRESS)
	{
		lastUpdateTick -= int32(diff);
		if (lastUpdateTick <= 0)
		{
			switch (CommandBG::g_CommandModelType)
			{
			case CommandModel::CM_Dispersibility:
				RndStartCommand();
				lastUpdateTick = COMMANDBG_UPDATE_TICK * 10;
				break;
			case CommandModel::CM_GroupFocus:
				ProcessGroupFocus(m_BGKeyWaypoints[AIWP_SELF_PORT2], m_BGKeyWaypoints[AIWP_ENEMY_PORT2]);
				lastUpdateTick = COMMANDBG_UPDATE_TICK;
				break;
			case CommandModel::CM_Regulation:
				ProcessRegulation();
				lastUpdateTick = COMMANDBG_UPDATE_TICK / 5;
				break;
			default:
				lastUpdateTick = COMMANDBG_UPDATE_TICK;
				break;
			}
		}
	}
}

bool CommandWS::CanUpMount(Player* player)
{
	if (EnemyBGFlagPicker() == player->GetGUID())
		return false;
	return true;
}

void CommandWS::InsureAttackAndDefance(uint32 count)
{
	uint32 index = 0;
	for (PlayerStatus::iterator itGuid = m_PlayerGUIDs.begin(); itGuid != m_PlayerGUIDs.end(); itGuid++)
	{
		if (index < count)
		{
			itGuid->second = true;
		}
		else
		{
			itGuid->second = false;
		}
		++index;
	}
}

void CommandWS::RndStartCommand()
{
	for (PlayerStatus::iterator itGuid = m_PlayerGUIDs.begin(); itGuid != m_PlayerGUIDs.end(); itGuid++)
	{
		BotBGAI* pBotAI = GetBotBGAI(itGuid->first);
		if (!pBotAI)
			continue;
		BGCommandInfo comInfo(BGCommandType::BGCT_GuardPoint);
		comInfo.rndOffset = 5.0f;
		uint32 rnd = irand(0, 3);
		switch (rnd)
		{
		case 0:
			comInfo.targetAIWP = m_BGKeyWaypoints[AIWP_SELF_PORT2];
			break;
		case 1:
			comInfo.targetAIWP = m_BGKeyWaypoints[AIWP_SELF_PORT3];
			break;
		case 2:
			comInfo.targetAIWP = m_BGKeyWaypoints[AIWP_ENEMY_PORT2];
			break;
		case 3:
			comInfo.targetAIWP = m_BGKeyWaypoints[AIWP_ENEMY_PORT3];
			break;
		}
		pBotAI->GetAIMovement()->AcceptPatrolCommand(comInfo.targetAIWP,
            m_BGKeyWaypoints[rnd<2 ? AIWP_ENEMY_PORT2 : AIWP_SELF_PORT2]);
	}
}

void CommandWS::ProcessRegulation()
{
    InsureAttackAndDefance(7);

    ObjectGuid needGuaredTarget = EnemyBGFlagPicker();
    ObjectGuid needAttackTarget = SelfBGFlagPicker();
    bool pickedFlagTarget = needAttackTarget.IsEmpty();

    if (pickedFlagTarget)
    {
        if (needGuaredTarget.IsEmpty())
            ProcessAllPicked();
        else
            ProcessAllGuared(needGuaredTarget);
    }
    else
    {
        if (needGuaredTarget.IsEmpty())
            ProcessAllAttack(needAttackTarget);
        else
            ProcessAttackAndGuard(needAttackTarget, needGuaredTarget);
    }

    // Highest WSG priority: our enemy-flag carrier must return to our
    // flag stand and remain there until the capture becomes possible.
    // Applying this after all other commands prevents combat and escort
    // logic from sending the carrier back toward the middle of the map.
    needGuaredTarget = EnemyBGFlagPicker(); // A pickup above may have just changed the keeper.
    if (!needGuaredTarget.IsEmpty())
    {
        uint64 carrierGuid = needGuaredTarget.GetGUIDLow();

        if (BotBGAI* carrierAI = GetBotBGAI(carrierGuid))
        {
            AIWaypoint* selfFlagPoint =
                m_BGKeyWaypoints[AIWP_SELF_FLAG];

            if (selfFlagPoint)
            {
                carrierAI->SetNeedFindpathSearch();
                carrierAI->GetAIMovement()->AcceptCommand(
                    selfFlagPoint,
                    true);

                // This succeeds only while our own flag is on its base.
                // Otherwise the carrier keeps waiting at the flag stand.
                TryCaptureFlag(carrierGuid);
            }
        }
    }
}
void CommandWS::TryPickEnemyFlag(uint64 guid)
{
	if (!m_pBattleground || m_pBattleground->GetStatus() != STATUS_IN_PROGRESS)
		return;
	BattlegroundWarsongGulch* pBGWS = dynamic_cast<BattlegroundWarsongGulch*>(m_pBattleground);
	if (!pBGWS)
		return;
	uint8 flagState = pBGWS->GetFlagState((m_TeamID == TEAM_ALLIANCE) ? HORDE : ALLIANCE);
	if (flagState == BG_WS_FLAG_STATE_ON_PLAYER || flagState == BG_WS_FLAG_STATE_WAIT_RESPAWN)
		return;
	Player* player = GetBGPlayer(guid);
	if (!player || !player->isAlive() || !player->IsInWorld() ||
        player->GetBattleground() != m_pBattleground)
		return;
	GameObject* pObject = NULL;
	if (flagState == BG_WS_FLAG_STATE_ON_BASE)
	{
		pObject = pBGWS->GetBGObject((m_TeamID == TEAM_ALLIANCE) ? BG_WS_ObjectTypes::BG_WS_OBJECT_H_FLAG : BG_WS_ObjectTypes::BG_WS_OBJECT_A_FLAG);
		if (pObject && !pObject->isSpawned())
			return;
	}
	else
	{
		if (m_TeamID == TEAM_ALLIANCE)
			pObject = SearchDropedFlag(guid, TeamId::TEAM_HORDE);
		else
			pObject = SearchDropedFlag(guid, TeamId::TEAM_ALLIANCE);
	}
	if (!pObject)
		return;
	if (player->GetDistance(pObject->GetPosition()) > 5)
		return;
	if (BotBGAI* pAI = GetBotBGAI(guid))
	{
		pAI->Dismount();
		if (!pAI->CanUseBGObject())
			return;
	}
    (void)0; // FELSORIA_P14_QUIET_WSG_FLAG_USE
	pObject->Use(player);
}

void CommandWS::TryPickSelfFlag(uint64 guid)
{
	if (!m_pBattleground || m_pBattleground->GetStatus() != STATUS_IN_PROGRESS)
		return;
	BattlegroundWarsongGulch* pBGWS = dynamic_cast<BattlegroundWarsongGulch*>(m_pBattleground);
	if (!pBGWS)
		return;
	uint8 flagState = pBGWS->GetFlagState((m_TeamID == TEAM_ALLIANCE) ? ALLIANCE : HORDE);
	if (flagState != BG_WS_FLAG_STATE_ON_GROUND)
		return;
	Player* player = GetBGPlayer(guid);
	if (!player || !player->isAlive() || !player->IsInWorld() ||
        player->GetBattleground() != m_pBattleground)
		return;
	GameObject* pObject = NULL;// pBGWS->GetBGObject((m_TeamID == TEAM_ALLIANCE) ? BG_WS_ObjectTypes::BG_WS_OBJECT_A_FLAG : BG_WS_ObjectTypes::BG_WS_OBJECT_H_FLAG);
	if (m_TeamID == TEAM_ALLIANCE)
		pObject = SearchDropedFlag(guid, TeamId::TEAM_ALLIANCE);
	else
		pObject = SearchDropedFlag(guid, TeamId::TEAM_HORDE);
	if (!pObject)
		return;
	if (player->GetDistance(pObject->GetPosition()) > 5)
		return;
	if (BotBGAI* pAI = GetBotBGAI(guid))
	{
		pAI->Dismount();
        if (!pAI->CanUseBGObject()) return;
	}
	pObject->Use(player);
}

void CommandWS::TryCaptureFlag(uint64 guid)
{
	if (!m_pBattleground || m_pBattleground->GetStatus() != STATUS_IN_PROGRESS)
		return;
	BattlegroundWarsongGulch* pBGWS = dynamic_cast<BattlegroundWarsongGulch*>(m_pBattleground);
	if (!pBGWS)
		return;
	if (pBGWS->GetFlagState((m_TeamID == TEAM_ALLIANCE) ? HORDE : ALLIANCE) != BG_WS_FLAG_STATE_ON_PLAYER)
		return;
	if (pBGWS->GetFlagState((m_TeamID == TEAM_ALLIANCE) ? ALLIANCE : HORDE) != BG_WS_FLAG_STATE_ON_BASE)
		return;
	Player* player = GetBGPlayer(guid);
	if (!player || !player->isAlive() || !player->IsInWorld() ||
        player->GetBattleground() != m_pBattleground)
		return;
	GameObject* pObject = pBGWS->GetBGObject((m_TeamID == TEAM_ALLIANCE) ? BG_WS_ObjectTypes::BG_WS_OBJECT_A_FLAG : BG_WS_ObjectTypes::BG_WS_OBJECT_H_FLAG);
	if (!pObject)
		return;
	if (player->GetDistance(pObject->GetPosition()) > 4)
		return;
	if (BotBGAI* pAI = GetBotBGAI(guid))
	{
		pAI->Dismount();
	}
	pBGWS->HandleAreaTrigger(player, (m_TeamID == TEAM_ALLIANCE) ? 3646 : 3647);
}

void CommandWS::ProcessAllPicked()
{
    uint32 runners = 0;
    for (PlayerStatus::iterator it = m_PlayerGUIDs.begin(); it != m_PlayerGUIDs.end(); ++it)
    {
        BotBGAI* ai = GetBotBGAI(it->first);
        Player* player = GetBGPlayer(it->first);
        if (!ai || !player || !player->isAlive() || !player->IsInWorld() ||
            player->GetBattleground() != m_pBattleground)
            continue;
        // Two living attackers keep taking the flag despite midfield fights.
        // The other attackers still fight; the existing defenders keep patrolling.
        bool runner = it->second && runners++ < 2;
        ai->SetNeedFindpathSearch();
        GameObject* ownDrop = SearchDropedFlag(it->first, m_TeamID);
        GameObject* enemyDrop = SearchDropedFlag(it->first,
            m_TeamID == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE);
        if (ownDrop && (!it->second || player->GetDistance(ownDrop->GetPosition()) < COMMAND_POINT_IFDISTANCE / 2))
        {
            ai->GetAIMovement()->AcceptCommand(ownDrop->GetGUID(), true);
            TryPickSelfFlag(it->first);
        }
        else if (enemyDrop && (runner || player->GetDistance(enemyDrop->GetPosition()) < COMMAND_POINT_IFDISTANCE / 2))
        {
            ai->GetAIMovement()->AcceptCommand(enemyDrop->GetGUID(), true);
            TryPickEnemyFlag(it->first);
        }
        else if (it->second)
        {
            ai->GetAIMovement()->AcceptCommand(m_BGKeyWaypoints[AIWP_ENEMY_FLAG], runner);
            TryPickEnemyFlag(it->first);
        }
        else
            ai->GetAIMovement()->AcceptPatrolCommand(m_BGKeyWaypoints[AIWP_SELF_PORT2], m_BGKeyWaypoints[AIWP_SELF_PORT3]);
    }
}

void CommandWS::ProcessAllGuared(ObjectGuid guaredGuid)
{
    for (PlayerStatus::iterator it = m_PlayerGUIDs.begin(); it != m_PlayerGUIDs.end(); ++it)
    {
        BotBGAI* ai = GetBotBGAI(it->first);
        Player* player = GetBGPlayer(it->first);
        if (!ai || !player || !player->isAlive() || !player->IsInWorld() ||
            player->GetBattleground() != m_pBattleground)
            continue;
        ai->SetNeedFindpathSearch();
        if (player->GetGUID() == guaredGuid)
        {
            ai->GetAIMovement()->AcceptCommand(m_BGKeyWaypoints[AIWP_SELF_FLAG], true);
            TryCaptureFlag(it->first);
            continue;
        }
        GameObject* ownDrop = SearchDropedFlag(it->first, m_TeamID);
        if (ownDrop && (!it->second || player->GetDistance(ownDrop->GetPosition()) < COMMAND_POINT_IFDISTANCE / 2))
        {
            ai->GetAIMovement()->AcceptCommand(ownDrop->GetGUID(), true);
            TryPickSelfFlag(it->first);
        }
        else if (!it->second && GetBGFlagFromSelf().IsEmpty())
            ai->GetAIMovement()->AcceptPatrolCommand(m_BGKeyWaypoints[AIWP_SELF_PORT2], m_BGKeyWaypoints[AIWP_SELF_PORT3]);
        else
            ai->GetAIMovement()->AcceptCommand(guaredGuid);
    }
}

void CommandWS::ProcessAllAttack(ObjectGuid attackGuid)
{
    Player* carrier = ObjectAccessor::FindPlayer(attackGuid);
    if (!carrier || !carrier->isAlive() || carrier->GetBattleground() != m_pBattleground)
        return;
    uint32 living = 0;
    for (PlayerStatus::iterator it = m_PlayerGUIDs.begin(); it != m_PlayerGUIDs.end(); ++it)
    {
        Player* player = GetBGPlayer(it->first);
        if (GetBotBGAI(it->first) && player && player->isAlive() && player->IsInWorld() &&
            player->GetBattleground() == m_pBattleground)
            ++living;
    }
    // Always retain a retrieval group, even in an underfilled match.
    uint32 runnerLimit = living > 3 ? 2 : living > 1 ? 1 : 0;
    uint32 runners = 0;
    for (PlayerStatus::iterator it = m_PlayerGUIDs.begin(); it != m_PlayerGUIDs.end(); ++it)
    {
        BotBGAI* ai = GetBotBGAI(it->first);
        Player* player = GetBGPlayer(it->first);
        if (!ai || !player || !player->isAlive() || !player->IsInWorld() ||
            player->GetBattleground() != m_pBattleground)
            continue;
        bool runner = it->second && runners++ < runnerLimit;
        ai->SetNeedFindpathSearch();
        if (!runner || player->GetDistance(carrier->GetPosition()) < COMMAND_POINT_IFDISTANCE)
        {
            // Distance no longer sends every remote bot to the enemy flag room.
            ai->GetAIMovement()->AcceptCommand(attackGuid);
            continue;
        }
        GameObject* enemyDrop = SearchDropedFlag(it->first,
            m_TeamID == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE);
        if (enemyDrop)
            ai->GetAIMovement()->AcceptCommand(enemyDrop->GetGUID(), true);
        else
            ai->GetAIMovement()->AcceptCommand(m_BGKeyWaypoints[AIWP_ENEMY_FLAG], true);
        TryPickEnemyFlag(it->first);
    }
}

void CommandWS::ProcessAttackAndGuard(ObjectGuid attackGuid, ObjectGuid guaredGuid) // Ă‹Â«Â·ËťÄ†Ä›Ă—Ă“Â¶Ä˝Â±Â»Ă‡Ĺ”
{
    Position attackPosition = GetPositionByGuid(attackGuid.GetGUIDLow());
	for (PlayerStatus::iterator itGuid = m_PlayerGUIDs.begin(); itGuid != m_PlayerGUIDs.end(); itGuid++)
	{
		BotBGAI* pBotAI = GetBotBGAI(itGuid->first);
		if (!pBotAI)
			continue;
		pBotAI->SetNeedFindpathSearch();
		if (guaredGuid.GetGUIDLow() == itGuid->first)
		{
			AIWaypoint* pWaypoint = m_BGKeyWaypoints[AIWP_SELF_FLAG];
			pBotAI->GetAIMovement()->AcceptCommand(pWaypoint, true);
			continue;
		}
		Position playerPos = GetPositionByGuid(itGuid->first);
		if (playerPos.IsInDist(&attackPosition, COMMAND_POINT_IFDISTANCE))
		{
			pBotAI->GetAIMovement()->AcceptCommand(attackGuid);
		}
		else
		{
			if (itGuid->second)
				pBotAI->GetAIMovement()->AcceptCommand(attackGuid);
			else
				pBotAI->GetAIMovement()->AcceptCommand(guaredGuid);
		}
	}
}

ObjectGuid CommandWS::SelfBGFlagPicker()
{
	if (!m_pBattleground)
		return ObjectGuid::Empty;
	BattlegroundWarsongGulch* pBGWS = dynamic_cast<BattlegroundWarsongGulch*>(m_pBattleground);
	if (!pBGWS)
		return ObjectGuid::Empty;
	if (m_TeamID == TEAM_ALLIANCE)
	{
		if (pBGWS->GetFlagState(ALLIANCE) == BG_WS_FLAG_STATE_ON_PLAYER)
		{
			return pBGWS->GetFlagPickerGUID(TEAM_ALLIANCE, 0);
		}
	}
	else if (m_TeamID == TEAM_HORDE)
	{
		if (pBGWS->GetFlagState(HORDE) == BG_WS_FLAG_STATE_ON_PLAYER)
		{
			return pBGWS->GetFlagPickerGUID(TEAM_HORDE, 0);
		}
	}
	return ObjectGuid::Empty;
}

ObjectGuid CommandWS::EnemyBGFlagPicker()
{
	if (!m_pBattleground)
		return ObjectGuid::Empty;
	BattlegroundWarsongGulch* pBGWS = dynamic_cast<BattlegroundWarsongGulch*>(m_pBattleground);
	if (!pBGWS)
		return ObjectGuid::Empty;
	if (m_TeamID == TEAM_ALLIANCE)
	{
		if (pBGWS->GetFlagState(HORDE) == BG_WS_FLAG_STATE_ON_PLAYER)
		{
			return pBGWS->GetFlagPickerGUID(TEAM_HORDE, 0);
		}
	}
	else if (m_TeamID == TEAM_HORDE)
	{
		if (pBGWS->GetFlagState(ALLIANCE) == BG_WS_FLAG_STATE_ON_PLAYER)
		{
			return pBGWS->GetFlagPickerGUID(TEAM_ALLIANCE, 0);
		}
	}
	return ObjectGuid::Empty;
}

ObjectGuid CommandWS::GetBGFlagFromSelf()
{
    BattlegroundWarsongGulch* bg = dynamic_cast<BattlegroundWarsongGulch*>(m_pBattleground);
    if (!bg || bg->GetFlagState((m_TeamID == TEAM_ALLIANCE) ? ALLIANCE : HORDE) != BG_WS_FLAG_STATE_ON_GROUND)
        return ObjectGuid::Empty;
    // The base flag is hidden while dropped. Follow the actual dropped object,
    // retaining its full GUID rather than turning its counter into a player GUID.
    return bg->GetDroppedFlagGUID(m_TeamID);
}

ObjectGuid CommandWS::GetBGFlagFromEnemy()
{
    BattlegroundWarsongGulch* bg = dynamic_cast<BattlegroundWarsongGulch*>(m_pBattleground);
    if (!bg || bg->GetFlagState((m_TeamID == TEAM_ALLIANCE) ? HORDE : ALLIANCE) != BG_WS_FLAG_STATE_ON_GROUND)
        return ObjectGuid::Empty;
    // The base flag is hidden while dropped. Follow the actual dropped object,
    // retaining its full GUID rather than turning its counter into a player GUID.
    return bg->GetDroppedFlagGUID((m_TeamID == TEAM_ALLIANCE) ? TEAM_HORDE : TEAM_ALLIANCE);
}

GameObject* CommandWS::SearchDropedFlag(uint64 guid, TeamId team)
{
    Player* player = GetBGPlayer(guid);
    BattlegroundWarsongGulch* bg = dynamic_cast<BattlegroundWarsongGulch*>(m_pBattleground);
    if (!player || !player->isAlive() || !player->IsInWorld() || !bg ||
        player->GetBattleground() != bg || bg->GetStatus() != STATUS_IN_PROGRESS ||
        bg->GetFlagState(team == TEAM_ALLIANCE ? ALLIANCE : HORDE) != BG_WS_FLAG_STATE_ON_GROUND)
        return nullptr;
    GameObject* object = player->GetMap()->GetGameObject(bg->GetDroppedFlagGUID(team));
    uint32 entry = team == TEAM_ALLIANCE ? BG_OBJECT_A_FLAG_GROUND_WS_ENTRY : BG_OBJECT_H_FLAG_GROUND_WS_ENTRY;
    return object && object->isSpawned() && object->GetGOInfo()->entry == entry ? object : nullptr;
}

