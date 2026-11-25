
#include "BotAI.h"
#include "Config.h"
#include "PathfindingMgr.h"
#include "WorldSession.h"
#include "Player.h"
#include "CommandBG.h"
#include "CommandBG/CommandAB.h"
#include "CommandBG/CommandAV.h"
#include "CommandBG/CommandBFG.h"
#include "CommandBG/CommandObjectiveBG.h"
#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "BotBGAIMovement.h"
#include "MoveSplineInit.h"
#include "MoveSpline.h"
#include "Spell.h"
#include "Pet.h"
#include "CreatureAI.h"
#include "BattlegroundWarsongGulch.h"
#include "BattlegroundTwinPeaks.h"
#include "BattlegroundKotmoguTemplate.h"
#include "BattlegroundEyeOfTheStorm.h"

#include "BotWarriorAI.h"
#include "BotMageAI.h"
#include "BotPriestAI.h"
#include "BotHunterAI.h"
#include "BotWarlockAI.h"
#include "BotPaladinAI.h"
#include "BotRogueAI.h"
#include "BotShamanAI.h"
#include "BotDruidAI.h"
#include "MiscPackets.h"
#include "SpellPackets.h"
#include "PlayerDefines.h"
#include "CharmInfo.h"
#include <corecrt_math_defines.h>

namespace {
bool FelsoriaAVNpcRotation(Player* player,Unit* target)
{
    if(!player||!target||!target->ToCreature()||player->GetMapId()!=30||
       !player->GetBattleground()||player->GetBattleground()->GetStatus()!=STATUS_IN_PROGRESS)return false;
    auto*commander=dynamic_cast<CommandAV*>(player->GetMap()->GetCommander(player->GetBGTeamId()));
    return commander&&commander->IsCombatTarget(player,target);
}
bool FelsoriaLegionRotation(Player* player,Unit* target)
{
    return PvpLegionRotation::Applicable(player,target,FelsoriaAVNpcRotation(player,target));
}

bool FelsoriaAVCaptureTick(Player* player)
{
    if(!player||player->GetMapId()!=30||!player->GetBattleground()||
       player->GetBattleground()->GetStatus()!=STATUS_IN_PROGRESS)return false;
    auto*commander=dynamic_cast<CommandAV*>(player->GetMap()->GetCommander(player->GetBGTeamId()));
    return commander&&commander->UpdateCapture(player);
}

bool FelsoriaAVAdvanceTick(Player* player)
{
    if(!player||player->GetMapId()!=30||!player->GetBattleground()||
       player->GetBattleground()->GetStatus()!=STATUS_IN_PROGRESS)return false;
    auto*commander=dynamic_cast<CommandAV*>(player->GetMap()->GetCommander(player->GetBGTeamId()));
    return commander&&commander->AdvanceToObjective(player);
}

bool FelsoriaAVCombatTarget(Player* player,Unit* target)
{
    if(player->GetMapId()==761){
        auto*commander=dynamic_cast<CommandBFG*>(player->GetMap()->GetCommander(player->GetBGTeamId()));
        return !commander||commander->IsCombatTarget(player,target);
    }
    if(player->GetMapId()!=30)return true;
    auto*commander=dynamic_cast<CommandAV*>(player->GetMap()->GetCommander(player->GetBGTeamId()));
    return commander&&commander->IsCombatTarget(player,target);
}
bool FelsoriaAVAssistTarget(Player* player,Unit* target)
{
    if(player->GetMapId()!=30)return true;
    if(player==target)return true;
    auto*commander=dynamic_cast<CommandAV*>(player->GetMap()->GetCommander(player->GetBGTeamId()));
    return commander&&commander->CanAssist(player,target);
}
}

//#define THREAD_PATHFINDING
#ifdef _DEBUG
//#define EXCLUDE_PLAYER
#endif

BotBGAI* BotBGAI::debugBGAI = NULL;

BotBGAI* BotBGAI::CreateBotBGAIByPlayerClass(Player* player)
{
	BotBGAI* ret = nullptr;
	PlayerBotSetting::ClearUnknowMount(player);
	switch (player->getClass())
	{
		case CLASS_WARRIOR:
		{
			ret = new BotWarriorAI(player);
		} break;

        case CLASS_MAGE:
        {
            ret = new BotMageAI(player);
        } break;

        case CLASS_PRIEST:
        {
            ret = new BotPriestAI(player);
        } break;

        case CLASS_HUNTER:
        {
            ret = new BotHunterAI(player);
        } break;

        case CLASS_WARLOCK:
        {
            ret = new BotWarlockAI(player);
        } break;

        case CLASS_PALADIN:
        {
            ret = new BotPaladinAI(player);
        } break;
        case CLASS_ROGUE:
		{
			ret = new BotRogueAI(player);
		} break;

        case CLASS_SHAMAN:
        {
            ret = new BotShamanAI(player);
        } break;

        case CLASS_DRUID:
		{
			ret = new BotDruidAI(player);
		} break;

        case CLASS_DEATH_KNIGHT:
            break;
	}

	if (ret == nullptr)
		ret = new BotBGAI(player);
	ret->ResetBotAI();
	return ret;
}

BotBGAI::BotBGAI(Player* player) :
PlayerAI(player),
m_NeedFindpathSearch(false),
m_UpmountTick(0),
m_Movement(new BotBGAIMovement(player, this)),
m_CurrentTargetTick(0),
m_UseMountID(PlayerBotSetting::MountForPlayer(player)),
pHorrorState(NULL),
m_CheckStoped(player),
m_Teleporting(player),
m_UsePotion(player),
m_Flee(player),
m_NeedFlee(player),
m_CastRecords(player),
m_FastAid(player),
m_GroupLeader(player),
m_MovetoUseGO(player),
m_AIBGStateType(BotAIBGState::AIBGState_Leave),
m_UpdateTick(BOTAI_UPDATE_TICK),
m_CanDireFlee(0),
m_CruxControlTarget(ObjectGuid::Empty),
m_LastControlTarget(ObjectGuid::Empty),
m_NeedReserveCtrlSpell(false),
m_InitRndPos(),
m_lastClearCtrlTick(0)
{
	m_FilterCreatureEntrys.clear();
	m_FilterCreatureEntrys.insert(14848);
	m_FilterCreatureEntrys.insert(11603);
	m_FilterCreatureEntrys.insert(11604);
	m_FilterCreatureEntrys.insert(11605);
	m_FilterCreatureEntrys.insert(11677);
	m_FilterCreatureEntrys.insert(10982);
	m_FilterCreatureEntrys.insert(10987);
	m_FilterCreatureEntrys.insert(11600);
	m_FilterCreatureEntrys.insert(11602);
	m_FilterCreatureEntrys.insert(11657);
	m_FilterCreatureEntrys.insert(10990);
	m_FilterCreatureEntrys.insert(10981);
}

BotBGAI::~BotBGAI()
{
	delete m_Movement;
}

void BotBGAI::ProcessBotCommand(Player* srcPlayer, std::string cmd)
{
	if (srcPlayer->GetMap() != me->GetMap())
		return;
	std::string param;
	if (!CanReciveCommand(cmd, param))
		return;
	if (cmd == "rite")
		ProcessSummonRiteSpell(srcPlayer);
	else if (cmd == "attack")
		ProcessAttackCommand(srcPlayer);
}

bool BotBGAI::CanReciveCommand(std::string& cmd, std::string& param)
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
			if (IsHealerBotAI())
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

void BotBGAI::ProcessSummonRiteSpell(Player* srcPlayer)
{
	Battleground* pBattleground = me->GetBattleground();
	if (!pBattleground || pBattleground->GetStatus() != BattlegroundStatus::STATUS_WAIT_JOIN)
		return;
	if (!srcPlayer || !srcPlayer->IsInWorld() || me->GetMap() != srcPlayer->GetMap())
		return;
	if (srcPlayer->GetDistance(me->GetPosition()) > BOTAI_RANGESPELL_DISTANCE)
		return;
	if (!m_MovetoUseGO.CanCastSummonRite())
	{
		std::string outString;
		consoleToUtf8(std::string("Ă„â€šĂ˘â‚¬ĹľĂ„Ä…Ă„ËťĂ„â€šĂ˘â‚¬Ë‡Ä‚â€šĂ‚Â°Ă„â€šÄąËťĂ„Ä…Ă‹ÂÄ‚â€šĂ‚Â·Ä‚â€šĂ‚Â¨Ă„Ä…Ă„ËťĂ„Ä…ÄąÄľÄ‚â€žĂ‚ÂÄ‚â€žĂ‹ĹĄĂ„Ä…Ă‚ÂĂ„Ä…Ă‚Â®Ä‚â€šĂ‚Â»Ä‚â€ąÄąÄ„Ă„Ä…Ă˘â‚¬Ë‡Ă„â€šĂ˘â‚¬Ë‡Ä‚â€žĂ‚ÂÄ‚â€ąÄąÄ„Ă„Ä…Ă‚ÂÄ‚â€ąĂ˘â‚¬Ë‡"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
		return;
	}
	uint32 castSpellID = TryCastSummonRiteSpell();
	if (castSpellID)
	{
		m_MovetoUseGO.StartSummonRite(castSpellID);
		std::string outString;
		consoleToUtf8(std::string("Ă„Ä…Ă‚ÂĂ„Ä…Ă‚Â®Ä‚â€šĂ‚Â»Ä‚â€ąÄąÄ„Ă„Ä…Ă˘â‚¬Ë‡Ă„â€šĂ˘â‚¬Ë‡Ä‚â€žĂ‚ÂÄ‚â€ąÄąÄ„Ä‚â€žĂ˘â‚¬Â Ă„â€šĂ‚Â´Ä‚â€šĂ‚Â¶Ă„Ä…Ă‚Â»Ă„Ä…Ă‚ÂÄ‚â€ąĂ˘â‚¬Ë‡"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
	}
	else
	{
		std::string outString;
		consoleToUtf8(std::string("Ă„â€šĂ˘â‚¬ĹľĂ„Ä…Ă„ËťĂ„â€šĂ˘â‚¬Ë‡Ä‚â€šĂ‚Â°Ă„â€šÄąËťĂ„Ä…Ă‹ÂÄ‚â€šĂ‚Â·Ä‚â€šĂ‚Â¨Ă„Ä…Ă„ËťĂ„Ä…ÄąÄľÄ‚â€žĂ‚ÂÄ‚â€žĂ‹ĹĄĂ„Ä…Ă‚ÂĂ„Ä…Ă‚Â®Ä‚â€šĂ‚Â»Ä‚â€ąÄąÄ„Ă„Ä…Ă˘â‚¬Ë‡Ă„â€šĂ˘â‚¬Ë‡Ä‚â€žĂ‚ÂÄ‚â€ąÄąÄ„Ă„Ä…Ă‚ÂÄ‚â€ąĂ˘â‚¬Ë‡"), outString);
		me->Whisper(outString, Language::LANG_COMMON, srcPlayer->GetGUID());
	}
}

void BotBGAI::ProcessAttackCommand(Player* srcPlayer)
{
	if (!srcPlayer || srcPlayer->IsPlayerBot())
		return;
	Unit* pMasterTarget = srcPlayer->GetSelectedUnit();
	if (!pMasterTarget || !pMasterTarget->isAlive())
		return;
	if (!me->IsValidAttackTarget(pMasterTarget))
		return;
	if (me->GetDistance(pMasterTarget->GetPosition()) > BOTAI_SEARCH_RANGE)
		return;
	me->SetSelection(pMasterTarget->GetGUID());
}

void BotBGAI::EachTick()
{
	m_RangeCreatureLists.clear();
	m_RangePlayerLists.clear();
	me->UpdateObjectVisibility(true);
}

bool BotBGAI::InBattleground()
{
	Player* pBot = dynamic_cast<Player*>(me);
	if (!pBot || !pBot->InBattleground() || !me->IsInWorld())
		return false;
	Map* pMap = me->GetMap();
	Battleground* pBG = pBot->GetBattleground();
	if (!pBG || !pMap || pBG->FindBgMap() != pMap)
		return false;
	if (pBG->GetStatus() == BattlegroundStatus::STATUS_NONE || pBG->GetStatus() == BattlegroundStatus::STATUS_WAIT_LEAVE)
		return false;
	return true;
}

void BotBGAI::UpdateAVReviveState()
{
    if(me->GetMapId()!=30){m_AVDeadSince=0;m_AVGhostLogAt=0;return;}
    if(!m_AVDeadSince||!me->isAlive())return;
    if(auto*commander=dynamic_cast<CommandAV*>(me->GetMap()->GetCommander(me->GetBGTeamId())))commander->OnPlayerRevive(me);
    m_AVDeadSince=0;m_AVGhostLogAt=0;
}

void BotBGAI::BattlegroundRevive()
{
	if (me->InArena())
		return;
	if (!me->IsPlayerBot() || !me->InBattleground())
		return;
	Player* player = dynamic_cast<Player*>(me);
	if (!player || player->isAlive())
		return;
	CommandBG* bgCommander = player->GetMap()->GetCommander((player->GetMapId()==529 || player->GetMapId()==30 || player->GetMapId()==761 || CommandObjectiveBG::Supports(player->GetBattleground())) ? player->GetBGTeamId() : player->GetTeamId());
    if(player->GetMapId()==30){
        auto*av=dynamic_cast<BattlegroundAV*>(player->GetBattleground());if(!av)return;
        uint32 now=getMSTime();if(!m_AVDeadSince)m_AVDeadSince=now?now:1;
        if(player->getDeathState()==DeathState::CORPSE&&!player->HasFlag(PLAYER_FIELD_PLAYER_FLAGS,PLAYER_FLAGS_GHOST)){
            m_Movement->ClearMovement();if(bgCommander)bgCommander->OnPlayerDead(player->GetGUIDLow());
            WorldPackets::Misc::RepopRequest release(WorldPacket{CMSG_REPOP_REQUEST});release.CheckInstance=false;
            player->GetSession()->HandleRepopRequest(release);
        }
        if(player->IsBeingTeleportedNear())player->GetSession()->HandleBotMoveTeleportAck();
        if(player->HasFlag(PLAYER_FIELD_PLAYER_FLAGS,PLAYER_FLAGS_GHOST))av->EnsureFriendlyGhostGraveyard(player);
        if(now-m_AVDeadSince>=45000&&(!m_AVGhostLogAt||now-m_AVGhostLogAt>=15000)&&
           sConfigMgr->GetBoolDefault("Felsoria.AlteracValley.Debug",false)){
            m_AVGhostLogAt=now?now:1;
            TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_GHOST_WAIT bot=%s ghost=%u queued=%u teleport=%u",
                player->GetName(),unsigned(player->HasFlag(PLAYER_FIELD_PLAYER_FLAGS,PLAYER_FLAGS_GHOST)),
                unsigned(av->HasJoinNearGrave(player)),unsigned(player->IsBeingTeleported()));
        }
        return;
    }
	if (!bgCommander)
		return;
    if(player->GetMapId()==761&&player->HasFlag(PLAYER_FIELD_PLAYER_FLAGS,PLAYER_FLAGS_GHOST))
        if(auto*bfg=dynamic_cast<BattlegroundBattleForGilneas*>(player->GetBattleground()))
            if(!bfg->EnsureFriendlyGhostGraveyard(player))return;
    // AB ghosts must be at an owned graveyard before joining a resurrection
    // queue. Retry failed/in-flight relocations; never respawn at a lost node.
    if(player->GetMapId()==529&&player->HasFlag(PLAYER_FIELD_PLAYER_FLAGS,PLAYER_FLAGS_GHOST))
        if(auto*ab=dynamic_cast<BattlegroundArathiBasin*>(player->GetBattleground()))
            if(!ab->EnsureFriendlyGhostGraveyard(player))return;
	if (player->getDeathState() == DeathState::CORPSE && !player->HasFlag(PLAYER_FIELD_PLAYER_FLAGS, PLAYER_FLAGS_GHOST)) // need release soul
	{
		//m_IsFlagTarget = false;
		m_Movement->ClearMovement();
		bgCommander->OnPlayerDead(player->GetGUIDLow());
		WorldPackets::Misc::RepopRequest packet(WorldPacket{ CMSG_REPOP_REQUEST });
		packet.CheckInstance = false;
		player->GetSession()->HandleRepopRequest(packet);

        WorldPacket opcode2(CMSG_MOVE_TELEPORT_ACK);
		WorldPackets::Movement::MoveTeleportAck pakcet(std::move(opcode2));
		pakcet.MoverGUID = player->GetGUID();
		player->GetSession()->HandleMoveTeleportAck(pakcet);

		//WorldPacket opcode3(3);
		//player->GetSession()->HandleAreaSpiritHealerQueueOpcode(opcode3);
		const Creature* pCreature = bgCommander->GetMatchGraveyardNPC(player);
		if (!pCreature || !pCreature->isSpiritService())	// it's not spirit service
			return;
		player->GetBattleground()->AddPlayerToResurrectQueue(pCreature->GetGUID(), player->GetGUID());
		if (Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(player->GetZoneId()))
			bf->AddPlayerToResurrectQueue(pCreature->GetGUID(), player->GetGUID());
	}
	else if (!player->GetBattleground()->HasJoinNearGrave(player))
	{
		const Creature* pCreature = bgCommander->GetMatchGraveyardNPC(player);
		if (!pCreature || !pCreature->isSpiritService())	// it's not spirit service
			return;
		player->GetBattleground()->AddPlayerToResurrectQueue(pCreature->GetGUID(), player->GetGUID());
	}
}

void BotBGAI::DamageDealt(Unit* victim, uint32& damage, DamageEffectType damageType)
{
	//if (!victim || !me->IsInWorld() || damage == 0)
	//	return;
	//if (!me->InBattleground())
	//	return;
	//Player* vicPlayer = victim->ToPlayer();
	//if (!vicPlayer)
	//	return;

	//int32 meCP = me->GetEquipCombatPower();
	//int32 vicCP = vicPlayer->GetEquipCombatPower();
	//int32 cpGap = meCP - vicCP;
	//if (cpGap == 0)
	//	return;
	//float addion = 0;
	//if (cpGap > 0 && meCP > 0)
	//{
	//	addion = float(cpGap) / float(meCP);
	//	addion = 1.0f - addion;
	//}
	//else if (cpGap < 0 && vicCP > 0)
	//{
	//	addion = float(cpGap * (-1.0f)) / float(vicCP);
	//	addion += 1.0f;
	//}
	//if (addion <= 0)
	//	return;
	//float result = float(damage);
	//switch (damageType)
	//{
	//case DamageEffectType::DIRECT_DAMAGE:
	//case DamageEffectType::SPELL_DIRECT_DAMAGE:
	//case DamageEffectType::DOT:
	//	result *= addion;
	//	break;
	//default:
	//	return;
	//}
	//damage = uint32(result);
}

void BotBGAI::DamageEndure(Unit* attacker, uint32& damage, DamageEffectType damageType)
{
    // Damage hooks remember identity only; combat/movement changes happen on AI update.
    if (attacker && attacker != me && damage)
    {
        m_LastBgAttacker = attacker->GetGUID();
        m_LastBgAttackTime = getMSTime();
    }
	//if (!attacker || !me->IsInWorld() || damage == 0)
	//	return;
	//if (!me->InBattleground())
	//	return;
	//Player* attPlayer = attacker->ToPlayer();
	//if (!attPlayer)
	//	return;
	//int32 meCP = me->GetEquipCombatPower();
	//int32 attCP = attPlayer->GetEquipCombatPower();
	//int32 cpGap = attCP - meCP;
	//if (cpGap == 0)
	//	return;
	//float addion = 0;
	//if (cpGap > 0)
	//{
	//	addion = float(cpGap) / float(attPlayer->GetEquipCombatPower());
	//	addion = 1.0f - addion;
	//}
	//else
	//{
	//	addion = float(cpGap * (-1.0f)) / float(me->GetEquipCombatPower());
	//	addion += 1.0f;
	//}
	//if (addion <= 0)
	//	return;
	//float result = float(damage);
	//switch (damageType)
	//{
	//case DamageEffectType::DIRECT_DAMAGE:
	//case DamageEffectType::SPELL_DIRECT_DAMAGE:
	//case DamageEffectType::DOT:
	//	result *= addion;
	//	break;
	//default:
	//	return;
	//}
	//damage = uint32(result);
}

void BotBGAI::UpdateAI(uint32 diff)
{
	m_UpdateTick -= diff;

	if (m_UpdateTick <= 0)
	{
		m_UpdateTick = BOTAI_UPDATE_TICK;
        if(me->IsInWorld()&&me->GetMapId()==761)
            if(auto*commander=dynamic_cast<CommandBFG*>(me->GetMap()->GetCommander(me->GetBGTeamId())))
                commander->ObserveAI(me,m_Teleporting.CanMovement());
		if (!InBattleground())
			return;
		if (IsAlive())
		{
            UpdateAVReviveState();
			if (me->HasAura(BattlegroundSpells::SPELL_WAITING_FOR_RESURRECT))
			{
				WorldPacket opcode(CMSG_CANCEL_AURA);
                WorldPackets::Spells::CancelAura packet(std::move(opcode));
				packet.CasterGUID = me->GetGUID();
				packet.SpellID = BattlegroundSpells::SPELL_WAITING_FOR_RESURRECT;
				me->GetSession()->HandleCancelAura(packet);
			}
			if (m_Teleporting.CanMovement())
				UpdateBotAI(BOTAI_UPDATE_TICK);
			else
				m_Teleporting.Update(diff, m_Movement);
		}
		else if (!me->InArena())
		{
			m_MovetoUseGO.ClearUseGO();
			m_CastRecords.ClearRecordSpell();
			m_Teleporting.ClearTeleport();
			me->SetSelection(ObjectGuid::Empty);
			BattlegroundRevive();
		}

		//{
		//	std::lock_guard<std::mutex> lock(m_movQueueLock);
		//	if (!m_pfFinishQueue.empty())
		//	{
		//		PathParameter* parameter = m_pfFinishQueue.front();
		//		m_pfFinishQueue.pop();
		//		m_Movement->ApplyFinishPath(parameter);
		//		delete parameter;
		//	}
		//}
	}
}

void BotBGAI::ProcessHealth()
{
    if (ProcessLegionPvp(me->GetSelectedUnit())) return;
	if (me->HasUnitState(UNIT_STATE_CASTING))
		return;
	if (NeedFlee())
	{
		//Dismount();
		ProcessFlee();
		return;
	}
	//Dismount();
	float searchDist = (me->InArena()) ? (BOTAI_SEARCH_RANGE * 3) : (BOTAI_SEARCH_RANGE * 1.5);
	NearUnitVec& needHealth = SearchNeedHealth(searchDist);
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
			ChaseTarget(healthPlayer, false, BOTAI_RANGESPELL_DISTANCE - 3);
		}
		else
		{
			me->StopMoving();
			ProcessHealthSpell(healthPlayer);
		}
	}
	else if (!IsNotMovement())
	{
		m_Movement->MovementTo(healthPlayer->GetGUID());
	}
}

void BotBGAI::ProcessCombat(Unit* pTarget)
{
    if (ProcessLegionPvp(pTarget)) return;
	//TC_LOG_ERROR(LOG_FILTER_SQL, "%u", m_IsRangeBot);
	if (!pTarget)
		return;
	bool inView = me->IsWithinLOSInMap(pTarget);
	if (inView)
	{
		if (me->HasUnitState(UNIT_STATE_CASTING))
			return;
		if (m_IsRangeBot)
		{
			if (NeedFlee())
			{
				if (pTarget->GetGUID() != me->GetTargetGUID())
				{
					pTarget = me->GetSelectedUnit();
					if (!pTarget)
						return;
				}
				//Dismount();
				if (me->getVictim() != pTarget || !me->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
					me->Attack(pTarget, true);
				ProcessFlee();
				if (!IsNotSelect(pTarget))
					ProcessMeleeSpell(pTarget);
				//ChaseTarget(pTarget, true);
			}
			else if (me->GetDistance(pTarget) <= BOTAI_RANGESPELL_DISTANCE)
			{
                if (me->IsMounted())
                    Dismount();
				if (me->getVictim() != pTarget)// || me->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
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
				if (me->getVictim() != pTarget)// || me->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
					me->Attack(pTarget, false);
				m_Movement->MovementToTarget();
			}
		}
		else
		{
			if (NeedFlee())
			{
				if (me->getVictim() != pTarget || !me->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
					me->Attack(pTarget, true);
				ProcessFlee();
				if (!IsNotSelect(pTarget))
					ProcessMeleeSpell(pTarget);
			}
			else if (me->IsWithinMeleeRange(pTarget))
			{
				if (me->IsMounted())
					Dismount();
				if (!me->getVictim() && !me->IsVehicle())
					me->Attack(pTarget, true);
				if (!IsNotMovement())
					ChaseTarget(pTarget, true);
				if (!IsNotSelect(pTarget) && !me->IsVehicle())
					ProcessMeleeSpell(pTarget);
			}
			else
			{
				if (!IsNotMovement())
					m_Movement->MovementToTarget();
				if (me->GetDistance(pTarget) < BOTAI_RANGESPELL_DISTANCE && pTarget->isAlive())
				{
                    if (me->IsMounted())
                        Dismount();
					ProcessRangeSpell(pTarget);
				}
			}
		}
	}
	else if (!IsNotMovement())
		m_Movement->MovementToTarget();
}

void BotBGAI::Dismount()
{
	if (!me->HasAura(m_UseMountID))
		return;
	m_UpmountTick = 0;
	me->RemoveOwnedAura(m_UseMountID, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
}

DiminishingGroup BotBGAI::GetControlSpellDiminishingGroup()
{
	switch (me->getClass())
	{
	case Classes::CLASS_PALADIN:
		return DiminishingGroup::DIMINISHING_DISORIENT;
	case Classes::CLASS_ROGUE:
	//	return DiminishingGroup::DIMINISHING_FEAR;
	//case Classes::CLASS_DRUID:
	//	return DiminishingGroup::DIMINISHING_CYCLONE;
	case Classes::CLASS_HUNTER:
		return DiminishingGroup::DIMINISHING_DISORIENT;
	case Classes::CLASS_SHAMAN:
		return DiminishingGroup::DIMINISHING_DISORIENT;
	case Classes::CLASS_MAGE:
		return DiminishingGroup::DIMINISHING_DISORIENT;
	//case Classes::CLASS_WARLOCK:
	//	return DiminishingGroup::DIMINISHING_FEAR;
	}
	return DIMINISHING_NONE;
}

void BotBGAI::ProcessReady()
{

}

void BotBGAI::ChaseTarget(Unit* pTarget, bool isMelee, float range)
{
	if (IsNotSelect(pTarget))
		return;
    if(me->GetMapId()==761){
        if(!pTarget||pTarget->GetMap()!=me->GetMap()||m_Movement->IsBFGJumpInProgress())return;
        if(!FelsoriaAVCombatTarget(me,pTarget)){
            me->AttackStop();me->SetSelection(ObjectGuid::Empty);m_Movement->ReleaseCombatMovement();return;
        }
        if(auto*commander=dynamic_cast<CommandBFG*>(me->GetMap()->GetCommander(me->GetBGTeamId())))
            if(commander->CombatReposition(me,pTarget))return;
        bool close=isMelee?me->IsWithinMeleeRange(pTarget):me->GetDistance(pTarget)<=range*.9f;
        if(close&&me->IsWithinLOSInMap(pTarget)){
            if(me->movespline&&!me->movespline->Finalized())m_Movement->ClearMovement();
            return;
        }
        m_Movement->MovementTo(pTarget->GetGUID(),isMelee?1.5f:range);return;
    }
    if(me->GetMapId()==30)
    {
        // Native MoveChase uses another path generator and bypasses AV floor
        // validation. Both caster and melee pursuit use the AV ground mover.
        if(!pTarget||pTarget->GetMap()!=me->GetMap())return;
        if(!FelsoriaAVCombatTarget(me,pTarget)){
            me->AttackStop();me->SetSelection(ObjectGuid::Empty);m_Movement->ReleaseCombatMovement();return;
        }
        bool close=isMelee?me->IsWithinMeleeRange(pTarget):me->GetDistance(pTarget)<=range*.9f;
        if(close&&me->IsWithinLOSInMap(pTarget)){
            if(me->movespline&&!me->movespline->Finalized()){
                m_Movement->ClearMovement();me->StopMoving();
            }
            return;
        }
        m_Movement->MovementTo(pTarget->GetGUID());return;
    }
	if (isMelee)
	{
		if (me->IsStopped())
		{
			Position& targetPos = pTarget->GetPosition();
			float rndOffset = frand(-float(M_PI_4) * 0.75f, float(M_PI_4) * 0.75f);
			Position pos;
			me->GetFirstCollisionPosition(pos, me->GetDistance(targetPos) + range, me->GetRelativeAngle(&targetPos) + rndOffset);
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
		{
			me->GetMotionMaster()->Clear();
			me->GetMotionMaster()->MoveChase(pTarget, range);
		}
	}
}

void BotBGAI::SearchCreatureListFromRange(Unit* center, NearCreatureVec& nearCreatures, float range, bool selfFaction)
{
	NearCreatureList nearCreature;
	//if (center == me)
	//	QueryNearCreatureList(range, nearCreature);
	//else
	{
		Trinity::AllWorldObjectsInRange checker(center, range);
		Trinity::CreatureListSearcher<Trinity::AllWorldObjectsInRange> searcher(center, nearCreature, checker);
		center->VisitNearbyGridObject(range, searcher);
	}
	for (Creature* pCreature : nearCreature)
	{
		if (!selfFaction && !FelsoriaAVCombatTarget(me,pCreature))
			continue;
		if (IsFilterCreatureByEntry(pCreature->GetEntry()))
			continue;
		if (pCreature->isPet() || pCreature->isTotem() || pCreature->isSummon())
			continue;
		if (pCreature->IsInEvadeMode())
			continue;
		if (selfFaction && center->IsValidAttackTarget(pCreature))
			continue;
		if (!selfFaction && !center->IsValidAttackTarget(pCreature))
			continue;
		nearCreatures.push_back(pCreature);
	}
}

Unit* BotBGAI::SearchEnemy(float range)
{
	NearPlayerList playersNearby;
	QueryNearPlayerList(range, playersNearby);
	NearPlayerVec enemyPlayers, invinciblePlayers;
	Player* minHealthPlayer = NULL;
	for (Player* pVisionPlayer : playersNearby)
	{
        if (m_Movement->IsWSGTargetBlocked(pVisionPlayer)) continue;
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
		if(TargetIsStealth(pVisionPlayer))
			continue;
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() != me->GetTeamId())
		{
			if (IsInvincible(pVisionPlayer) || HasAuraMechanic(pVisionPlayer, Mechanics::MECHANIC_POLYMORPH))
			{
				invinciblePlayers.push_back(pVisionPlayer);
			}
			else
			{
				float hp = pVisionPlayer->GetHealthPct();
				if (hp < 80.0f)
				{
					if (!minHealthPlayer)
						minHealthPlayer = pVisionPlayer;
					else if (hp < minHealthPlayer->GetHealthPct())
						minHealthPlayer = pVisionPlayer;
				}
				if (m_NeedFindpathSearch)
				{
					if (m_Movement->CanMovementTo(pVisionPlayer->GetPositionX(), pVisionPlayer->GetPositionY(), pVisionPlayer->GetPositionZ()))
						enemyPlayers.push_back(pVisionPlayer);
				}
				else
					enemyPlayers.push_back(pVisionPlayer);
			}
		}
	}
	if (enemyPlayers.empty())
	{
		NearCreatureVec creatures;
		SearchCreatureListFromRange(me, creatures, range, false);
		if (!creatures.empty())
		{
			uint32 index = urand(0, creatures.size() - 1);
			return creatures[index];
		}
		if (invinciblePlayers.empty())
			return NULL;
		uint32 index = urand(0, invinciblePlayers.size() - 1);
		Player* pVisionPlayer = invinciblePlayers[index];
		return pVisionPlayer;
	}
	if (!minHealthPlayer)
	{
		uint32 index = urand(0, enemyPlayers.size() - 1);
		Player* pVisionPlayer = enemyPlayers[index];
		return pVisionPlayer;
	}
	return minHealthPlayer;
}

NearObjectList BotBGAI::SearchGameObject(float range)
{
	std::list<GameObject*> results;
	Position& pos = me->GetPosition();
	Trinity::GameObjectInRangeCheck checker(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), range);
	Trinity::GameObjectListSearcher<Trinity::GameObjectInRangeCheck> searcher(me, results, checker);
	me->VisitNearbyGridObject(range, searcher);
	return results;
}

NearUnitVec BotBGAI::SearchFriend(float range)
{
	NearPlayerList playersNearby;
	NearUnitVec friendNearby;
	QueryNearPlayerList(range, playersNearby);
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() == me->GetTeamId())
		{
			friendNearby.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(me, creatures, range, true);
	for (Creature* pCreature : creatures)
		friendNearby.push_back(pCreature);
	return friendNearby;
}

NearUnitVec BotBGAI::SearchFriendTargetIsTarget(Unit* pTarget, float range)
{
	NearPlayerList playersNearby;
	NearUnitVec friendNearby;
	if (pTarget == me)
		QueryNearPlayerList(range, playersNearby);
	else
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

NearPlayerVec BotBGAI::SearchFarFriend(float minRange, float maxRange, bool isIDLE)
{
	NearPlayerList playersNearby;
	NearPlayerVec friendNearby;
	QueryNearPlayerList(maxRange, playersNearby);
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
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

NearPlayerVec BotBGAI::ExistFriendAttacker(float range /* = BOTAI_RANGESPELL_DISTANCE */)
{
	NearPlayerList playersNearby;
	NearPlayerVec friendNearby;
	QueryNearPlayerList(range, playersNearby);
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() == me->GetTeamId() && IsAttacker())
		{
			friendNearby.push_back(pVisionPlayer);
		}
	}

	return friendNearby;
}

NearUnitVec BotBGAI::SearchNeedHealth(float range /* = BOTAI_SEARCH_RANGE */)
{
	NearPlayerList playersNearby;
	NearUnitVec lifeTo25, life25To55, life55To80;
	QueryNearPlayerList(range, playersNearby);
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
		if (!IsNotSelect(pVisionPlayer) && FelsoriaAVAssistTarget(me,pVisionPlayer) && (me->GetMapId()==30 ? pVisionPlayer->GetBGTeamId()==me->GetBGTeamId() : pVisionPlayer->GetTeamId()==me->GetTeamId()))
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

NearUnitVec BotBGAI::BeforeSearchNeedHealth(float range, float minHeal)
{
	NearPlayerList playersNearby;
	NearUnitVec minLife;
	QueryNearPlayerList(range, playersNearby);
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
		if (!IsNotSelect(pVisionPlayer) && FelsoriaAVAssistTarget(me,pVisionPlayer) && (me->GetMapId()==30 ? pVisionPlayer->GetBGTeamId()==me->GetBGTeamId() : pVisionPlayer->GetTeamId()==me->GetTeamId()))
		{
			float healthPct = pVisionPlayer->GetHealthPct();
			if (healthPct < minHeal)
				minLife.push_back(pVisionPlayer);
		}
	}
	return minLife;
}

NearUnitVec BotBGAI::SearchLifePctByFriendRange(Unit* pTarget, float lifePct, float range /* = NEEDFLEE_CHECKRANGE */)
{
	NearPlayerList playersNearby;
	NearUnitVec lifePctPlayers;
	if (pTarget == me)
		QueryNearPlayerList(range, playersNearby);
	else
		pTarget->GetPlayerListInGrid(playersNearby, range);
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
		if (!IsNotSelect(pVisionPlayer) && FelsoriaAVAssistTarget(me,pVisionPlayer) && (me->GetMapId()==30 ? pVisionPlayer->GetBGTeamId()==me->GetBGTeamId() : pVisionPlayer->GetTeamId()==me->GetTeamId()))
		{
			float healthPct = pVisionPlayer->GetHealthPct();
			if (healthPct <= lifePct)
				lifePctPlayers.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(me, creatures, range, true);
	for (Creature* pCreature : creatures)
	{
		float healthPct = pCreature->GetHealthPct();
		if (healthPct <= lifePct)
			lifePctPlayers.push_back(pCreature);
	}

	return lifePctPlayers;
}

bool BotBGAI::NeedDireFlee()
{
	if (m_CanDireFlee == 0)
	{
		if (Map* pMap = me->GetMap())
		{
			if (CommandBG* bgCommander = pMap->GetCommander(CommandObjectiveBG::Supports(me->GetBattleground()) ? me->GetBGTeamId() : me->GetTeamId()))
			{
				if (bgCommander->CanDireFlee())
					m_CanDireFlee = 1;
				else
					m_CanDireFlee = -1;
			}
		}
	}
	if (m_CanDireFlee <= 0)
		return false;
	if (me->GetHealthPct() < 20 && RangeEnemyListByTargetIsMe(BOTAI_SEARCH_RANGE * 0.6f).size() > 0)
		return true;
	NearPlayerVec& rangeFriends = ExistFriendAttacker(BOTAI_SEARCH_RANGE * 0.6f);
	NearUnitVec& rangeEnemys = RangeEnemyListByHasAura(0, BOTAI_SEARCH_RANGE * 0.6f);
	int32 enemyCount = int32(rangeEnemys.size());
	int32 friendCount = int32(rangeFriends.size());
	float gap = (float)(enemyCount - friendCount);
	if (gap < 2)
		return false;
	if (friendCount <= 0)
		return true;
	float rate = gap / ((float)friendCount);
	return rate > 0.2f;
}

bool BotBGAI::DoDireFlee()
{
	if (IsNotMovement())
		return false;
	NearUnitVec& enemys = RangeEnemyListByHasAura(0, BOTAI_SEARCH_RANGE * 0.6f);
	if (enemys.empty())
		return false;
	NearPlayerVec& friends = SearchFarFriend(BOTAI_SEARCH_RANGE * 0.8f, BOTAI_SEARCH_RANGE * 1.4f, true);
	if (friends.empty())
		return false;
	Unit* nearFriend = NULL;
	float nearDistance = 0;
	for (Player* player : friends)
	{
		float distance = me->GetDistance(player->GetPosition());
		if (nearFriend == NULL)
		{
			nearFriend = player;
			nearDistance = distance;
		}
		else if (distance < nearDistance)
		{
			nearFriend = player;
			nearDistance = distance;
		}
		if (player->GetSelectedUnit() == NULL)
		{
			player->StopMoving();
			uint32 index = urand(0, enemys.size() - 1);
			player->SetSelection(enemys[index]->GetGUID());
		}
	}
	if (nearFriend)
	{
		me->AttackStop();
		//me->SetSelection(ObjectGuid::Empty);
		m_Movement->MovementTo(nearFriend->GetPositionX(), nearFriend->GetPositionY(), nearFriend->GetPositionZ(), 3.0f);
		return true;
	}
	return false;
}

bool BotBGAI::DoFaceToTarget(Unit* pTarget)
{
	float relative = me->GetRelativeAngle(pTarget->GetPositionX(), pTarget->GetPositionY());
	if (relative > M_PI_4 && !IsNotMovement())// (fabsf(selfAngle) > M_PI_4)
	{
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

SpellCastResult BotBGAI::TryCastSpell(uint32 spellID, Unit* pTarget, bool force, bool dismount)
{
	if(me->GetMapId()==761&&m_Movement->IsBFGJumpInProgress())return SPELL_FAILED_MOVING;
	if (!spellID || !me->HasSpell(spellID))
	{
        if(!(me->GetMapId()==761&&m_PreparingBg))
		TC_LOG_ERROR(LOG_FILTER_SQL, "1 bot spell %u not found", spellID);
		return SpellCastResult::SPELL_FAILED_SPELL_LEARNED;
	}
	if (spellID == BotCommon_ClearAllCtrl)
	{
		uint32 curTick = getMSTime();
		if (m_lastClearCtrlTick + 120000 > curTick)
		{
			return SpellCastResult::SPELL_FAILED_NOT_READY;
		}
		m_lastClearCtrlTick = curTick;
	}

	SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
	if (!spellInfo || spellInfo->IsPassive())
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
    // Native combat pets only: never use the old companion summon (23498).
    if (spellID == 23498) return SPELL_FAILED_UNKNOWN;
    if(m_GilneasPreparingCall&&me->GetBattleground()&&
        !FelsoriaGilneasPreparation::CastFits(me->GetBattleground()->GetStartDelayTime().count(),spellInfo->CalcCastTime(me),spellInfo->IsChanneled()))
        return SPELL_FAILED_NOT_READY;
    std::string optionalName = spellInfo->SpellName ? spellInfo->SpellName : "";
    bool petPreparation = spellID == 883 || spellID == 982 || optionalName.find("Call Pet") == 0 || optionalName == "Revive Pet";
    bool gemPreparation = spellID == 36883 || optionalName.find("Conjure Mana") == 0;
    if ((petPreparation || gemPreparation) &&
        !m_OptionalPreparation.Begin(spellID, getMSTime(), 15000, gemPreparation))
        return SPELL_FAILED_NOT_READY;
    uint64 buffKey = (uint64(spellID) << 32) | (pTarget ? pTarget->GetGUID().GetCounter() : 0);
    bool const trackBuff=m_PreparingBg && (me->GetMapId()!=761 || spellID!=m_UseMountID);
    if (trackBuff && !m_BgBuffs.Begin(buffKey, getMSTime(), 5000, true))
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
		//spell->finish(false);
		//delete spell;
		return castResult;
	}
    if (gemPreparation) m_OptionalPreparation.Complete(spellID);
    if (trackBuff) m_BgBuffs.Complete(buffKey);
    if(m_GilneasPreparingCall)++m_GilneasPrepCastSequence;
	return SpellCastResult::SPELL_CAST_OK;
}

SpellCastResult BotBGAI::PetTryCastSpell(uint32 spellID, Unit* pTarget, bool force)
{
	Pet* pPet = me->GetPet();
	if (!pPet || !pPet->isAlive())
		return SpellCastResult::SPELL_FAILED_UNKNOWN;
	if (!spellID || !pPet->HasSpell(spellID))
	{
		TC_LOG_ERROR(LOG_FILTER_SQL, "2 bot spell %u not found", spellID);
		return SpellCastResult::SPELL_FAILED_SPELL_LEARNED;
	}
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

void BotBGAI::SettingPetAutoCastSpell(uint32 spellID, bool autoCast)
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

Unit* BotBGAI::RandomRangeEnemyByCasting(float range)
{
	NearPlayerList playersNearby;
	QueryNearPlayerList(range, playersNearby);
	NearPlayerVec enemyPlayers;
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() != me->GetTeamId())
		{
			if (pVisionPlayer->HasUnitState(UNIT_STATE_CASTING))
				enemyPlayers.push_back(pVisionPlayer);
		}
	}
	if (enemyPlayers.empty())
	{
		NearCreatureVec creatures;
		SearchCreatureListFromRange(me, creatures, range, false);
		if (!creatures.empty())
		{
			uint32 index = urand(0, creatures.size() - 1);
			return creatures[index];
		}
		return NULL;
	}
	uint32 count = enemyPlayers.size();
	uint32 index = urand(0, count - 1);
	return enemyPlayers[index];
}

NearUnitVec BotBGAI::RangeEnemyListByHasAura(uint32 aura, float range)
{
	NearPlayerList playersNearby;
	QueryNearPlayerList(range, playersNearby);
	NearUnitVec enemyPlayers;
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
		if(pVisionPlayer && pVisionPlayer->IsInWorld())
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

NearUnitVec BotBGAI::RangeEnemyListByNonAura(uint32 aura, float range)
{
	NearUnitVec enemyPlayers;
	if (aura == 0)
		return enemyPlayers;
	NearPlayerList playersNearby;
	QueryNearPlayerList(range, playersNearby);
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
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

NearUnitVec BotBGAI::RangeEnemyListByTargetIsMe(float range)
{
	NearPlayerList playersNearby;
	QueryNearPlayerList(range, playersNearby);
	NearUnitVec enemyPlayers;
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
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

NearUnitVec BotBGAI::RangeListByTargetIsTarget(Unit* pTarget, float range)
{
	NearPlayerList playersNearby;
	if (pTarget == me)
		QueryNearPlayerList(range, playersNearby);
	else
		pTarget->GetPlayerListInGrid(playersNearby, range);
	NearUnitVec enemyPlayers;
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
		if (pVisionPlayer->GetTargetGUID() != pTarget->GetGUID())
			continue;
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() != me->GetTeamId())
		{
			if (TargetIsStealth(pVisionPlayer))
				continue;
			enemyPlayers.push_back(pVisionPlayer);
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(pTarget, creatures, range, true);
	for (Creature* pCreature : creatures)
	{
		if (pCreature->GetTargetGUID() == pTarget->GetGUID())
			enemyPlayers.push_back(pCreature);
	}
	return enemyPlayers;
}

NearUnitVec BotBGAI::RangeEnemyListByTargetRange(Unit* pTarget, float range)
{
	NearPlayerList playersNearby;
	if (pTarget == me)
		QueryNearPlayerList(range, playersNearby);
	else
		pTarget->GetPlayerListInGrid(playersNearby, range);
	NearUnitVec enemyPlayers;
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
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

NearUnitVec BotBGAI::SearchFarEnemy(float minRange, float maxRange)
{
	NearPlayerList playersNearby;
	NearUnitVec enemyNearby;
	QueryNearPlayerList(maxRange, playersNearby);
	for (Player* pVisionPlayer : playersNearby)
	{
#ifdef EXCLUDE_PLAYER
		if (!pVisionPlayer->IsPlayerBot())
			continue;
#endif
		if (TargetIsStealth(pVisionPlayer))
			continue;
		if (!IsNotSelect(pVisionPlayer) && pVisionPlayer->GetTeamId() != me->GetTeamId())
		{
			if (me->GetDistance(pVisionPlayer->GetPosition()) > minRange)
			{
				enemyNearby.push_back(pVisionPlayer);
			}
		}
	}
	NearCreatureVec creatures;
	SearchCreatureListFromRange(me, creatures, maxRange, false);
	for (Creature* pCreature : creatures)
	{
		if (me->GetDistance(pCreature->GetPosition()) > minRange)
			enemyNearby.push_back(pCreature);
	}
	return enemyNearby;
}

/*
void BotBGAI::UpdateBotAI(uint32 diff)
{

	if (FelsoriaLegionRotation(me, me->GetSelectedUnit())) BotBGAI::EachTick();
	else EachTick();
	if (me->HasUnitState(UNIT_STATE_CASTING))
	{
		m_MovetoUseGO.ProcessMovetoUseGO(m_Movement);
		return;
	}
	m_CheckStoped.UpdatePosition(diff);
	TC_LOG_INFO("server.loading", "PBOT 1 %s %d %d", me->GetName().c_str(), m_AIBGStateType, me->GetBattleground()->GetStatus());
	if (!FelsoriaLegionRotation(me, me->GetSelectedUnit())) ClearMechanicAura();
	if (!IsNotMovement())
		ProcessHorror(diff);

	if (NeedWaitSpecialSpell(BOTAI_UPDATE_TICK))
		return;
	TC_LOG_INFO("server.loading", "PBOT 2");
	if (m_FastAid.TryDoingFastAidForMe())
		return;
	TC_LOG_INFO("server.loading", "PBOT 3");
	if (!me->IsInCombat() && ProcessNormalSpell())
		return;
	TC_LOG_INFO("server.loading", "PBOT 4");
	Battleground* pBattleground = me->GetBattleground();
	BattlegroundStatus bgStatus = pBattleground->GetStatus();
	if (m_AIBGStateType == BotAIBGState::AIBGState_Ready && bgStatus == BattlegroundStatus::STATUS_WAIT_JOIN)
	{
		if (m_InitRndPos.GetPositionZ() != 0)
		{
			CommandBG* pBGCommand = pBattleground->GetBgMap()->GetCommander(me->GetBGTeamId());
			AIWaypoint* pAIWP = pBGCommand ? pBGCommand->GetReadyPosition() : NULL;
			if (pAIWP)
				m_InitRndPos = pAIWP->GetPosition();
			else
				m_InitRndPos = me->GetPosition();
			if (me->InArena())
			{
				m_InitRndPos = BotAIHorrorState::GetNewHorrorPosByRange(me, 3.0f);
			}
			else
			{
				float randomOffset = 5.0f;
				m_InitRndPos.m_positionX += frand(-randomOffset, randomOffset);
				m_InitRndPos.m_positionY += frand(-randomOffset, randomOffset);
			}
			m_Movement->MovementTo(m_InitRndPos.GetPositionX(), m_InitRndPos.GetPositionY(), m_InitRndPos.GetPositionZ());
			m_InitRndPos.m_positionZ = 0;
		}
		else if (me->IsStopped())
		{
			if (m_MovetoUseGO.ProcessMovetoUseGO(m_Movement))
				return;
			ProcessReady();
			m_GroupLeader.ProcessGroupLeader();
		}
		else
		{
			if (m_MovetoUseGO.ProcessMovetoUseGO(m_Movement))
				return;
		}
	}
	else if (m_AIBGStateType == BotAIBGState::AIBGState_Start && bgStatus == BattlegroundStatus::STATUS_IN_PROGRESS)
	{
		BotUtility::TryTeleportPlayerPet(me);
		m_Movement->SyncPosition(me->GetPosition());
		if (m_Movement->ExecuteCruxMovementCommand())
			return;
		bool inArena = me->InArena();
		if (!inArena)
		{
			if (!me->HasAura(m_UseMountID) && !me->HasUnitState(UNIT_STATE_CASTING))
				m_UsePotion.TryUsePotion();
		}
		else if (!FelsoriaLegionRotation(me, me->GetSelectedUnit()) && TryStartControlCommand())
			return;
		Unit* pSelect = GetBotAIValidSelectedUnit();
        if (ProcessLegionPvp(pSelect)) return;
		if (pSelect && TargetIsStealth(pSelect->ToPlayer()))
		{
			me->SetSelection(ObjectGuid::Empty);
			if (!me->InArena() && !IsNotMovement())
				m_Movement->ExecuteMovementCommand();
		}
		else if (pSelect && !IsInvincible(pSelect) && !HasAuraMechanic(pSelect, Mechanics::MECHANIC_POLYMORPH))
		{
			float distance = me->GetDistance(pSelect->GetPosition());
			if (distance < BOTAI_SEARCH_RANGE || inArena)
			{
				if (m_CurrentTargetTick < BOTAI_MAXTARGET_TICKTIME || inArena)
				{
					m_CurrentTargetTick += diff;
					if (inArena || !NeedDireFlee() || !DoDireFlee())
					{
						if (IsHealerBotAI() && HaveManaStore())
							ProcessHealth();
						else
							ProcessCombat(pSelect);
					}
					else
						ProcessFlee();
				}
				else
				{
					m_Movement->ClearMovement();
					me->SetSelection(ObjectGuid::Empty);
				}
			}
			else if (distance > BOTAI_SEARCH_RANGE * 1.7f || me->GetMap() != pSelect->GetMap())
			{
				//me->StopMoving();
				me->SetSelection(ObjectGuid::Empty);
			}
			else
			{
				m_Movement->MovementToTarget();
			}
		}
		else
		{
			m_CurrentTargetTick = 0;
			if (pSelect)
			{
				me->AttackStop();
				//me->SetSelection(ObjectGuid::Empty);
			}
			//else
			//{
			//	m_MovementTick = 0;
			//}
			if (Unit* enemy = SearchEnemy(inArena ? 200 : BOTAI_SEARCH_RANGE))
			{
				me->SetSelection(enemy->GetGUID());
				//me->Attack(enemy, true);
			}
			else
			{
				me->SetSelection(ObjectGuid::Empty);
				if (!me->InArena() && !IsNotMovement())
					m_Movement->ExecuteMovementCommand();
			}
		}
	}
	else if (m_AIBGStateType == BotAIBGState::AIBGState_Leave)
	{
		m_CruxControlTarget = ObjectGuid::Empty;
		m_LastControlTarget = ObjectGuid::Empty;
		me->AttackStop();
		me->SetSelection(ObjectGuid::Empty);
	}
}
*/

void BotBGAI::UpdateBotAI(uint32 diff)
{
	Battleground* pBattleground = me->GetBattleground();
	if (!pBattleground)
		return;

	BattlegroundStatus bgStatus = pBattleground->GetStatus();
    if(me->GetMapId()==30&&m_Movement->RecoverFallenAVBot())return;
    if (m_PreparationInstance != pBattleground->GetInstanceID())
    {
        m_PreparationInstance = pBattleground->GetInstanceID();
        m_BgBuffs.Reset(); m_OptionalPreparation.Reset();
        m_LastBgAttacker = ObjectGuid::Empty;
    }
    m_PreparingBg = BotActivityPolicy::preparationWindow(bgStatus == STATUS_WAIT_JOIN,
        pBattleground->GetStartDelayTime().count());
    if (me->getClass() == CLASS_HUNTER && me->GetSummonedBattlePet())
        me->UnsummonCurrentBattlePetIfAny(false);
    if(me->GetMapId()==761&&bgStatus==STATUS_WAIT_JOIN)
        if(auto*commander=dynamic_cast<CommandBFG*>(me->GetMap()->GetCommander(me->GetBGTeamId())))
            if(commander->WaitingTick(me))return;
    // Resolve the nearby flag and its single capper before even defensive
    // target selection can stop movement or replace a capture with PvP.
    if (bgStatus==STATUS_IN_PROGRESS && FelsoriaAVCaptureTick(me)) return;
    // The commander owns a bounded advance after a short defensive fight.
    // Instant self-defence remains available while ordinary casts/chases cannot
    // hold the objective squad in the same midfield fight indefinitely.
    if (bgStatus==STATUS_IN_PROGRESS && FelsoriaAVAdvanceTick(me))
    {
        m_PvpRotation.SelfHeal(me, m_Movement, true);
        return;
    }
    // Release dead, distant or phased targets before any PvP/maintenance
    // return can consume this update. The commander intent is kept separately.
    if (me->GetMapId()==489 || me->GetMapId()==529 || me->GetMapId()==30 || me->GetMapId()==761 || CommandObjectiveBG::Supports(pBattleground)) GetBotAIValidSelectedUnit();
    if(me->GetMapId()==761 && bgStatus==STATUS_IN_PROGRESS)
        if(auto*commander=dynamic_cast<CommandBFG*>(me->GetMap()->GetCommander(me->GetBGTeamId()))) {
            m_AIBGStateType=BotAIBGState::AIBGState_Start;
            BotBGAI::EachTick();
            if(commander->UpdateBot(me)) {if(!commander->IsCapturing(me)&&!m_Movement->IsBFGJumpInProgress())m_PvpRotation.SelfHeal(me,m_Movement,true);return;}
        }
    Player* objectiveCarrier = m_Movement->GetWSGAttackCarrier();
    bool flagTask = m_Movement->HasWSGFlagTask();
    Unit* defending = nullptr;
    if (bgStatus == STATUS_IN_PROGRESS && !IsFlagCarrier() && !CommandObjectiveBG::Supports(pBattleground))
    {
        if (objectiveCarrier)
        {
            if (me->GetDistance(objectiveCarrier) < BOTAI_SEARCH_RANGE &&
                me->IsValidAttackTarget(objectiveCarrier) &&
                me->canSeeOrDetect(objectiveCarrier, false, true) && !IsInvincible(objectiveCarrier))
                defending = objectiveCarrier;
        }
        else if (!flagTask)
            defending = me->GetMapId()==761 && bgStatus==STATUS_IN_PROGRESS ? me->GetSelectedUnit() : DefensiveTarget();
    }
    // Keep the commander objective ahead of incidental midfield aggression.
    // Clearing only an existing combat selection avoids resetting a task spline
    // every update; persistent task state survives ReleaseCombatMovement.
    if ((flagTask || objectiveCarrier) && me->GetSelectedUnit() && me->GetSelectedUnit() != defending)
    {
        me->AttackStop(); me->SetSelection(ObjectGuid::Empty);
        m_Movement->ReleaseCombatMovement(); m_CurrentTargetTick = 0;
    }
    if (defending && me->GetSelectedUnit() != defending)
    {
        me->AttackStop(); me->SetSelection(defending->GetGUID());
        m_Movement->ClearMovement(); m_CurrentTargetTick = 0;
        if (Spell* preparation = me->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        {
            SpellInfo const* info = preparation->GetSpellInfo();
            std::string name = info && info->SpellName ? info->SpellName : "";
            if (name.find("Conjure Mana") == 0 || name.find("Call Pet") == 0 || name == "First Aid")
                me->InterruptSpell(CURRENT_GENERIC_SPELL, false);
        }
    }

	switch (bgStatus)
	{
	case STATUS_NONE:
	case STATUS_WAIT_QUEUE:
		return;
	case STATUS_WAIT_JOIN:
		if (m_AIBGStateType != BotAIBGState::AIBGState_Ready)
			m_AIBGStateType = BotAIBGState::AIBGState_Ready;
		break;
	case STATUS_IN_PROGRESS:
		if (m_AIBGStateType != BotAIBGState::AIBGState_Start)
			m_AIBGStateType = BotAIBGState::AIBGState_Start;
		break;
	case STATUS_WAIT_LEAVE:
		if (m_AIBGStateType != BotAIBGState::AIBGState_Leave)
			m_AIBGStateType = BotAIBGState::AIBGState_Leave;
		break;
	default:
		return;
	}

    // Let the native graveyard jump finish. Combat/idle controllers must not
    // StopMoving halfway through a validated airborne transition.
    if (m_Movement->IsWSGDropInProgress()) return;
	if (FelsoriaLegionRotation(me, me->GetSelectedUnit())) BotBGAI::EachTick();
	else EachTick();
    if(me->HasUnitState(UNIT_STATE_CASTING) && FelsoriaLegionRotation(me,me->GetSelectedUnit()))
        BotUtility::TryCombatInterrupt(me,me->GetSelectedUnit(),m_Movement);
	if (me->HasUnitState(UNIT_STATE_CASTING))
	{
		m_MovetoUseGO.ProcessMovetoUseGO(m_Movement);
		return;
	}
	m_CheckStoped.UpdatePosition(diff);
	if (!FelsoriaLegionRotation(me, me->GetSelectedUnit())) ClearMechanicAura();
	if (!IsNotMovement())
		ProcessHorror(diff);

	if (NeedWaitSpecialSpell(BOTAI_UPDATE_TICK))
		return;

    if (bgStatus == STATUS_IN_PROGRESS && CommandObjectiveBG::Supports(pBattleground))
        if (auto* commander = dynamic_cast<CommandObjectiveBG*>(me->GetMap()->GetCommander(me->GetBGTeamId())))
            if (commander->UpdateBot(me) || IsFlagCarrier())
            {
                // A carrier at its destination holds position. Legacy combat
                // commands must not pull it out of the capture/scoring area.
                me->AttackStop(); me->SetSelection(ObjectGuid::Empty);
                m_PvpRotation.SelfHeal(me, m_Movement, true);
                return;
            }
    if (bgStatus == STATUS_IN_PROGRESS && IsFlagCarrier())
    {
        me->AttackStop(); me->SetSelection(ObjectGuid::Empty);
        m_PvpRotation.SelfHeal(me, m_Movement, true); // Instant, flag-safe heals only.
        m_Movement->SyncPosition(me->GetPosition());
        if (!m_Movement->ExecuteCruxMovementCommand()) m_Movement->ExecuteMovementCommand();
        return;
    }
    if (bgStatus==STATUS_IN_PROGRESS && !defending && me->GetMapId()==489)
        m_Movement->UpdateWSGActivity();
    if (m_PvpRotation.SelfHeal(me, m_Movement)) return;
    if (!defending && m_FastAid.TryDoingFastAidForMe()) return;
    if (m_PreparingBg && !me->isInCombat() && ProcessNormalSpell()) return;
    // AB commander intent survives native cast/combat movement resets.
    if ((me->GetMapId()==529 || me->GetMapId()==30) && bgStatus==STATUS_IN_PROGRESS)
    {
        if (me->GetSpecializationRole()==ROLES_HEALER)
        {
            Unit* injured=nullptr;
            for(Unit* ally : PvpLegionRotation::Nearby(me))
                if(ally && FelsoriaAVAssistTarget(me,ally) && me->IsValidAssistTarget(ally) && ally->GetHealthPct()<75.0f && me->IsWithinLOSInMap(ally) &&
                    (!injured || ally->GetHealthPct()<injured->GetHealthPct())) injured=ally;
            if(injured) for(auto const& learned:me->GetSpellMap())
            {
                if(learned.second.state==PLAYERSPELL_REMOVED || learned.second.disabled || !learned.second.active)continue;
                SpellInfo const* info=sSpellMgr->GetSpellInfo(learned.first);
                if(!info || !info->SpellName || info->IsPassive())continue;
                std::string name=info->SpellName;
                bool healing=name=="Flash of Light" || name=="Holy Shock" || name=="Flash Heal" || name=="Shadow Mend" ||
                    name=="Healing Wave" || name=="Riptide" || name=="Regrowth" || name=="Vivify" || name=="Effuse";
                if(healing && TryCastSpell(learned.first,injured)==SPELL_CAST_OK)return;
            }
        }
        if(auto* commander=dynamic_cast<CommandAB*>(me->GetMap()->GetCommander(me->GetBGTeamId())))
            if(commander->UpdateBot(me)) return;
    }
    if(me->GetMapId()==30 && bgStatus==STATUS_IN_PROGRESS)
        if(auto*commander=dynamic_cast<CommandAV*>(me->GetMap()->GetCommander(me->GetBGTeamId())))
            if(commander->UpdateBot(me))return;
    if (bgStatus == STATUS_IN_PROGRESS && !defending && !me->GetSelectedUnit() && TryUpMount()) return;

	if (m_AIBGStateType == BotAIBGState::AIBGState_Ready)
        {
                if(me->GetMapId()==761) {
                    m_InitRndPos.m_positionZ=0;
                    if(auto*commander=dynamic_cast<CommandBFG*>(me->GetMap()->GetCommander(me->GetBGTeamId())))commander->ReadyBot(me);
                    if(m_PreparingBg && me->IsStopped())ProcessReady();
                    return;
                }
                // Retry a rejected waiting position without losing the AV
                // preparation task. The mover keeps a validated random spot
                // for this bot and instance, including after a buff cast.
                if (me->GetMapId() == 30)
                {
                        m_InitRndPos.m_positionZ = 0;
                        m_Movement->MovementTo(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ());
                }
                // WSG: remain at the real spawn during preparation.
                else if ((me->GetMapId() == 489 || me->GetMapId() == 529) &&
                        m_InitRndPos.GetPositionZ() != 0)
                {
                        m_Movement->ClearMovement();
                        me->GetMotionMaster()->Clear();
                        me->StopMoving();
                        m_InitRndPos.m_positionZ = 0;
                }

                if (m_InitRndPos.GetPositionZ() != 0)
		{
			CommandBG* pBGCommand = pBattleground->GetBgMap()->GetCommander(me->GetBGTeamId());
			AIWaypoint* pAIWP = pBGCommand ? pBGCommand->GetReadyPosition() : NULL;
			if (pAIWP)
				m_InitRndPos = pAIWP->GetPosition();
			else
				m_InitRndPos = me->GetPosition();
			if (me->InArena())
			{
				m_InitRndPos = BotAIHorrorState::GetNewHorrorPosByRange(me, 3.0f);
			}
			else
			{
				float randomOffset = 5.0f;
				m_InitRndPos.m_positionX += frand(-randomOffset, randomOffset);
				m_InitRndPos.m_positionY += frand(-randomOffset, randomOffset);
			}
			m_Movement->MovementTo(m_InitRndPos.GetPositionX(), m_InitRndPos.GetPositionY(), m_InitRndPos.GetPositionZ());
			m_InitRndPos.m_positionZ = 0;
		}
		else if (me->IsStopped())
		{
			if (m_MovetoUseGO.ProcessMovetoUseGO(m_Movement))
				return;
			if (m_PreparingBg) ProcessReady();
			m_GroupLeader.ProcessGroupLeader();
		}
		else
		{
			if (m_MovetoUseGO.ProcessMovetoUseGO(m_Movement))
				return;
		}
	}
	else if (m_AIBGStateType == BotAIBGState::AIBGState_Start)
	{
		BotUtility::TryTeleportPlayerPet(me);
		m_Movement->SyncPosition(me->GetPosition());
		if (!defending && m_Movement->ExecuteCruxMovementCommand())
			return;
		bool inArena = me->InArena();
		if (!inArena)
		{
			if (!me->HasAura(m_UseMountID) && !me->HasUnitState(UNIT_STATE_CASTING))
				m_UsePotion.TryUsePotion();
		}
		else if (!FelsoriaLegionRotation(me, me->GetSelectedUnit()) && TryStartControlCommand())
			return;
		Unit* pSelect = GetBotAIValidSelectedUnit();
        if (ProcessLegionPvp(pSelect)) return;
		if (pSelect && TargetIsStealth(pSelect->ToPlayer()))
		{
			me->SetSelection(ObjectGuid::Empty);
			if (!me->InArena() && !IsNotMovement())
				m_Movement->ExecuteMovementCommand();
		}
		else if (pSelect && !IsInvincible(pSelect) && !HasAuraMechanic(pSelect, Mechanics::MECHANIC_POLYMORPH))
		{
			float distance = me->GetDistance(pSelect->GetPosition());
			if (distance < BOTAI_SEARCH_RANGE || inArena)
			{
				if (m_CurrentTargetTick < BOTAI_MAXTARGET_TICKTIME || inArena)
				{
					m_CurrentTargetTick += diff;
					if (inArena || !NeedDireFlee() || !DoDireFlee())
					{
						if (IsHealerBotAI() && HaveManaStore())
							ProcessHealth();
						else
							ProcessCombat(pSelect);
					}
					else
						ProcessFlee();
				}
				else
				{
					m_Movement->ClearMovement();
					me->SetSelection(ObjectGuid::Empty);
				}
			}
			else if (distance > BOTAI_SEARCH_RANGE * 1.7f || me->GetMap() != pSelect->GetMap())
			{
				me->SetSelection(ObjectGuid::Empty);
			}
			else
			{
				m_Movement->MovementToTarget();
			}
		}
		else
		{
			m_CurrentTargetTick = 0;
			if (pSelect)
			{
				me->AttackStop();
			}
			if (Unit* enemy = SearchEnemy(inArena ? 200 : BOTAI_SEARCH_RANGE))
			{
				me->SetSelection(enemy->GetGUID());
			}
			else
			{
				me->SetSelection(ObjectGuid::Empty);
				if (!me->InArena() && !IsNotMovement())
					m_Movement->ExecuteMovementCommand();
			}
		}
	}
	else if (m_AIBGStateType == BotAIBGState::AIBGState_Leave)
	{
        m_Movement->ResetWSGTask();
		m_CruxControlTarget = ObjectGuid::Empty;
		m_LastControlTarget = ObjectGuid::Empty;
		me->AttackStop();
		me->SetSelection(ObjectGuid::Empty);
	}
}

void BotBGAI::ResetBotAI()
{
    m_Movement->ResetWSGTask();
	PlayerBotSetting::ClearUnknowMount(me);
	if (m_UseMountID != 0)
	{
		if (!me->HasSpell(m_UseMountID))
			me->learnSpell(m_UseMountID, false);
	}
	m_IsMeleeBot = IsMeleeBotAI();
	m_IsRangeBot = IsRangeBotAI();
	m_IsHealerBot = IsHealerBotAI();

	if (!me->IsStandState())
		me->SetStandState(UNIT_STAND_STATE_STAND);

	BotCommon_ClearAllCtrl = FindMaxRankSpellByExist(59752);
	if (BotCommon_ClearAllCtrl == 0)
	{
		me->learnSpell(59752, false);
		BotCommon_ClearAllCtrl = FindMaxRankSpellByExist(59752);
	}
	m_CastRecords.ClearRecordSpell();
	m_FastAid.CheckPlayerFastAid();
}

bool BotBGAI::IsAlive()
{
	return me->isAlive();
}

bool BotBGAI::IsNotSelect(Unit* pTarget)
{
	if (!pTarget || !pTarget->isAlive() || !pTarget->IsInWorld())
		return true;




	Player* pPlayer = pTarget->ToPlayer();

	if (pPlayer && me  && me->IsInWorld())
	{

		
		if (!pPlayer || !pPlayer->IsInWorld())
			return true;



		if (pPlayer->GetVehicleKit()  )
			return true;
		//if (pPlayer->GetGUID().isNaN()  && m_CruxControlTarget.isNaN() && m_LastControlTarget.isNaN() )

		if (pPlayer->GetTeamId() != me->GetTeamId())
		{
			if (pPlayer->GetGUID() == m_CruxControlTarget || pPlayer->GetGUID() == m_LastControlTarget)
				return true;
		}

	}
      //  if (!pTarget)
       // return true;
     //   if (!pTarget->IsInWorld())
    //    return true;
if (pTarget && pTarget->IsInWorld()  && me  && me->IsInWorld())
	if (pTarget->HasAura(27827)) // (27827 Ä‚â€žĂ„ÄľÄ‚â€žÄąĹˇÄ‚â€žĂ‚ÂÄ‚â€žĂ˘â€žËĂ„â€šĂ˘â‚¬â€śÄ‚â€šĂ‚Â®Ä‚â€šĂ‚Â»Ä‚â€žĂ˘â€žË Ă„â€šĂ˘â‚¬Â°Ă„Ä…Ă˘â‚¬ĹľĂ„â€šĂ˘â‚¬ĹľĂ„â€šĂ‚ÂĂ„â€šĂ˘â‚¬Ä…Ă„Ä…Ă˘â‚¬ĹĄĂ„â€šÄąÂ¤Ă„â€šĂ‚Â¶Ă„Ä…ÄąĹźĂ„â€šÄąâ€š)
		return true;

	return false;
}

bool BotBGAI::TryUpMount()
{
    bool const isAV=me->GetMapId()==30;
    bool const gilneasWaiting=me->GetMapId()==761&&me->GetBattleground()&&me->GetBattleground()->GetStatus()==STATUS_WAIT_JOIN;
    if(gilneasWaiting)return TryGilneasWaitingMount();
	PlayerBotSetting::RemoveForbiddenMount(me);
	if (!m_UseMountID)
		return false;
	if (m_AIBGStateType != BotAIBGState::AIBGState_Start && !gilneasWaiting)
		return false;

    bool const isWSG =
        me->GetMapId() == 489;

    // WSG outdoor mount points:
    // first two are Horde-side exits, second two are Alliance-side exits.
    if (isWSG)
    {
        CommandBG* wsgCommander =
            me->GetMap()->GetCommander(isAV?me->GetBGTeamId():me->GetTeamId());

        // The WSG commander rejects the actual enemy-flag carrier.
        if (wsgCommander &&
            !wsgCommander->CanUpMount(me))
        {
            if (me->IsMounted())
                Dismount();

            return false;
        }

        // Once mounted outdoors, remain mounted until combat or another
        // existing BG rule explicitly dismounts the bot.
        if (me->IsMounted())
            return false;

        static G3D::Vector3 const mountPoints[] =
        {
            // Horde-side exits.
            { 992.712219f, 1422.849609f, 345.208344f },
            { 1123.086670f, 1462.265991f, 315.914581f },

            // Alliance-side exits.
            { 1351.001343f, 1461.952026f, 323.537750f },
            { 1459.041748f, 1493.667725f, 351.523804f }
        };

        bool nearMountPoint = false;

        for (G3D::Vector3 const& mountPoint : mountPoints)
        {
            float const deltaX =
                me->GetPositionX() - mountPoint.x;

            float const deltaY =
                me->GetPositionY() - mountPoint.y;

            if (deltaX * deltaX + deltaY * deltaY <= 100.0f)
            {
                nearMountPoint = true;
                break;
            }
        }

        if (!nearMountPoint)
            return false;
    }
	if (me->isInCombat() || me->GetSelectedUnit() || me->HasAura(m_UseMountID) || me->getLevel() < 20)
		return false;
	if (!me->GetMap()->IsOutdoors(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ()))
		return false;
	if (!isAV && RangeEnemyListByHasAura(0, BOTAI_SEARCH_RANGE).size() > 0)
		return false;
	if (me->IsMounted())
		return false;
	if (me->HasUnitState(UNIT_STATE_CASTING))
		return false;
	ObjectGuid& tarGUID = m_Movement->GetTargetObjectID();
	if (!tarGUID.IsEmpty() && tarGUID.IsGameObject())
	{
		if (GameObject* pObject = me->GetMap()->GetGameObject(tarGUID))
		{
			if (me->GetPosition().GetExactDist(&pObject->GetPosition()) < NEEDFLEE_CHECKRANGE)
				return false;
		}
	}
	CommandBG* bgCommander = me->GetMap()->GetCommander((isAV||me->GetMapId()==761||CommandObjectiveBG::Supports(me->GetBattleground()))?me->GetBGTeamId():me->GetTeamId());
	if (bgCommander && !bgCommander->CanUpMount(me))
		return false;
	if (!me->InArena() && !isAV && !gilneasWaiting)
	{
		++m_UpmountTick;
		if (m_UpmountTick < 3)
			return false;
	}
    if(isAV){
        uint32 now=getMSTime();
        if(m_AVMountRetryAt&&int32(m_AVMountRetryAt-now)>0)return false;
        m_AVMountRetryAt=now+5000;
    }
    if(gilneasWaiting){
        uint32 now=getMSTime();if(m_GilneasMountRetryAt&&int32(m_GilneasMountRetryAt-now)>0)return false;
        m_GilneasMountRetryAt=now+3000;
    }
    if (isWSG && !m_OptionalPreparation.Begin((uint64(1)<<62)|m_UseMountID,getMSTime(),15000))
        return false;
	if (isWSG || isAV)
	{
	    // Stop only the current spline. Keep the recorded WSG route,
	    // target command, route variant and direction intact.
	    me->StopMoving();
	    me->GetMotionMaster()->Clear();
	}
	else
	{
	    m_Movement->ClearMovement();
	}

	bool okUp = (TryCastSpell(m_UseMountID, me, false, !isAV) == SPELL_CAST_OK);
	if (okUp)
		m_UpmountTick = 0;
	// An AV cast attempt owns this AI tick even when native casting rejects it.
    // The commander resumes the same objective next tick; retries are bounded.
    return isAV || okUp;
}

void BotBGAI::ProcessHorror(uint32 diff)
{
    if (me->GetMapId()==489 || me->GetMapId()==30)
    {
        // AV and WSG auras already own the native fear/confusion generator.
        // Clearing it and issuing random height-corrected MovePoint commands
        // bypasses that controller and can select the terrain under the base.
        if (pHorrorState) {delete pHorrorState;pHorrorState=NULL;}
        return;
    }
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
			return;
		}
		pHorrorState->UpdateHorror(diff, m_Movement);
	}
	else if (pHorrorState)
	{
		delete pHorrorState;
		pHorrorState = NULL;
		me->StopMoving();
	}
}

bool BotBGAI::TargetIsNotDiminishingByType(Unit* pTarget, DiminishingGroup diType)
{
	if (!pTarget)
		return false;
	if (diType < 1 || diType > 20)
		return false;
	DiminishingLevels diLevel = pTarget->GetDiminishing(diType);
	return diLevel < 1;
}

bool BotBGAI::TargetIsNotDiminishingByType2(Unit* pTarget, DiminishingGroup diType)
{
	if (!pTarget)
		return false;
	if (diType < 1 || diType > 20)
		return false;
	DiminishingLevels diLevel = pTarget->GetDiminishing(diType);
	return diLevel < 2;
}

bool BotBGAI::HasAuraMechanic(Unit* pTarget, Mechanics mask)
{
	if (!pTarget)
		return false;
	return (pTarget->HasAuraWithMechanic(1 << mask));
}

bool BotBGAI::NeedWaitSpecialSpell(uint32 diff)
{
	if (HasAuraMechanic(me, Mechanics::MECHANIC_CHARM))
	{
		me->StopMoving();
		if (BotCommon_ClearAllCtrl)
			TryCastSpell(BotCommon_ClearAllCtrl, me);
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_DISORIENTED))
	{
		me->StopMoving();
		if (BotCommon_ClearAllCtrl)
			TryCastSpell(BotCommon_ClearAllCtrl, me);
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_FEAR))
	{
		//me->StopMoving();
		if (BotCommon_ClearAllCtrl)
			TryCastSpell(BotCommon_ClearAllCtrl, me);
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_SLEEP))
	{
		me->StopMoving();
		if (BotCommon_ClearAllCtrl)
			TryCastSpell(BotCommon_ClearAllCtrl, me);
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_STUN))
	{
		me->StopMoving();
		if (BotCommon_ClearAllCtrl)
			TryCastSpell(BotCommon_ClearAllCtrl, me);
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_FREEZE))
	{
		me->StopMoving();
		//if (BotCommon_ClearAllCtrl)
		//	TryCastSpell(BotCommon_ClearAllCtrl, me);
		return true;
	}
	//if (HasAuraMechanic(me, Mechanics::MECHANIC_KNOCKOUT))
	//{
	//	me->StopMoving();
	//	//if (BotCommon_ClearAllCtrl)
	//	//	TryCastSpell(BotCommon_ClearAllCtrl, me);
	//	return true;
	//}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_POLYMORPH))
	{
		me->StopMoving();
		if (BotCommon_ClearAllCtrl)
			TryCastSpell(BotCommon_ClearAllCtrl, me);
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_BANISH))
	{
		me->StopMoving();
		//if (BotCommon_ClearAllCtrl)
		//	TryCastSpell(BotCommon_ClearAllCtrl, me);
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_HORROR))
	{
		//me->StopMoving();
		if (BotCommon_ClearAllCtrl)
			TryCastSpell(BotCommon_ClearAllCtrl, me);
		return true;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_SAPPED))
	{
		me->StopMoving();
		//if (BotCommon_ClearAllCtrl)
		//	TryCastSpell(BotCommon_ClearAllCtrl, me);
		return true;
	}
	return false;
}

bool BotBGAI::NeedFlee()
{
	return m_NeedFlee.TargetHasFleeAura();
}

void BotBGAI::FleeMovement()
{
	//if (me->IsStopped() && !IsNotMovement())
	//{
	//	NearUnitVec& enemys = RangeEnemyListByTargetIsMe(NEEDFLEE_CHECKRANGE);
	//	if (enemys.empty())
	//		return;
	//	Unit* selectEnemy = enemys[urand(0, enemys.size() - 1)];
	//	G3D::Vector3 dir = (me->GetVector3() - selectEnemy->GetVector3()).direction();
	//	Position targetPos = me->GetFirstCollisionPosition(BOTAI_SEARCH_RANGE * 0.5, selectEnemy->GetRelativeAngle(&me->GetPosition()));
	//	if ((targetPos.GetVector3() - me->GetVector3()).length() <= 5.0f)
	//	{
	//		targetPos = me->GetFirstCollisionPosition(BOTAI_SEARCH_RANGE * 0.5, frand(0.1f, float(M_PI) * 2.0f - 0.1f));
	//	}
	//	m_Movement->MovementTo(targetPos.GetPositionX(), targetPos.GetPositionY(), targetPos.GetPositionZ());
	//}
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

bool BotBGAI::IsNotMovement()
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

bool BotBGAI::IsInvincible(Unit* pTarget)
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

uint32 BotBGAI::FindMaxRankSpellByExist(uint32 spellID)
{
    return BotUtility::FindMaxRankSpellByExist(me,spellID);
}

uint32 BotBGAI::FindPetMaxRankSpellByExist(uint32 spellID)
{
	if (spellID == 0)
		return 0;
	Pet* pPet = me->GetPet();
	if (!pPet)
		return 0;
	uint32 selectSpell = sSpellMgr->GetLastSpellInChain(spellID);
	if (selectSpell == 0)
	{
		if (pPet->HasSpell(spellID))
			return spellID;
		return 0;
	}
	while (!pPet->HasSpell(selectSpell))
	{
		selectSpell = sSpellMgr->GetPrevSpellInChain(selectSpell);
		if (selectSpell == 0)
			return 0;
	}
	return selectSpell;
}

void BotBGAI::ReadyBattleground()
{
    m_Movement->ResetWSGTask();
	m_AIBGStateType = BotAIBGState::AIBGState_Ready;
	m_InitRndPos = me->GetPosition();
}

void BotBGAI::PushFinishQueue(PathParameter* pathParam)
{
	if (!pathParam)
		return;
	std::lock_guard<std::mutex> lock(m_movQueueLock);
	m_pfFinishQueue.push(pathParam);
}

bool BotBGAI::CanUseBGObject()
{
	if (me->HasAura(27827)) // (27827 Ä‚â€žĂ„ÄľÄ‚â€žÄąĹˇÄ‚â€žĂ‚ÂÄ‚â€žĂ˘â€žËĂ„â€šĂ˘â‚¬â€śÄ‚â€šĂ‚Â®Ä‚â€šĂ‚Â»Ä‚â€žĂ˘â€žË Ă„â€šĂ˘â‚¬Â°Ă„Ä…Ă˘â‚¬ĹľĂ„â€šĂ˘â‚¬ĹľĂ„â€šĂ‚ÂĂ„â€šĂ˘â‚¬Ä…Ă„Ä…Ă˘â‚¬ĹĄĂ„â€šÄąÂ¤Ă„â€šĂ‚Â¶Ă„Ä…ÄąĹźĂ„â€šÄąâ€š)
		return true;
	if (HasAuraMechanic(me, Mechanics::MECHANIC_ROOT))
	{
		return false;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_BANISH))
	{
		return false;
	}
	//if (HasAuraMechanic(me, Mechanics::MECHANIC_IMMUNE_SHIELD))
	//{
	//	return false;
	//}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_CHARM))
	{
		return false;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_DISORIENTED))
	{
		return false;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_FEAR))
	{
		return false;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_SLEEP))
	{
		return false;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_STUN))
	{
		return false;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_FREEZE))
	{
		return false;
	}
	//if (HasAuraMechanic(me, Mechanics::MECHANIC_KNOCKOUT))
	//{
	//	return false;
	//}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_POLYMORPH))
	{
		return false;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_HORROR))
	{
		return false;
	}
	if (HasAuraMechanic(me, Mechanics::MECHANIC_SAPPED))
	{
		return false;
	}
	return true;
}

bool BotBGAI::IsMeleeBotAI()
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

bool BotBGAI::IsRangeBotAI()
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

bool BotBGAI::IsHealerBotAI()
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

Unit* BotBGAI::GetBotAIValidSelectedUnit()
{
	Unit* pTarget = me->GetSelectedUnit();
	bool isValid = true;
    if(pTarget&&!FelsoriaAVCombatTarget(me,pTarget))isValid=false;
    if (pTarget && me->GetMapId()==489 &&
        (!pTarget->IsInWorld() || !me->IsValidAttackTarget(pTarget) ||
         !me->canSeeOrDetect(pTarget,false,true) || IsInvincible(pTarget) ||
         me->GetDistance(pTarget)>BOTAI_SEARCH_RANGE*1.7f)) isValid=false;
    if (pTarget && me->GetMapId()==489 &&
        (m_Movement->IsWSGTargetBlocked(pTarget) || pTarget->GetMap()!=me->GetMap() ||
         !me->InSamePhase(pTarget) || !me->InSamePhaseId(pTarget))) isValid=false;
	if (!pTarget)
		isValid = false;
	if (isValid && !pTarget->IsVisible())
		isValid = false;
	if (isValid && !me->InSamePhase(pTarget->GetPhaseMask()))
		isValid = false;
	if (isValid && IsNotSelect(pTarget))
		isValid = false;
	if (isValid && pTarget->ToCreature() && pTarget->ToCreature()->IsInEvadeMode())
		isValid = false;
	if (!isValid)
	{
        if (me->GetMapId()==489 && !me->GetSelection().IsEmpty())
            m_Movement->ReleaseCombatMovement();
		me->AttackStop();
		me->SetSelection(ObjectGuid::Empty);
		return NULL;
	}
	return pTarget;
}

bool BotBGAI::TargetIsControl(Unit* pTarget)
{
	if (!pTarget)
		return false;
	if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_CHARM) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_DISORIENTED) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_DISTRACT) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_SLEEP) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_POLYMORPH) ||
		//HasAuraMechanic(pTarget, Mechanics::MECHANIC_ROOT) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_STUN) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_BANISH) ||
		HasAuraMechanic(pTarget, Mechanics::MECHANIC_FEAR)
		//|| HasAuraMechanic(pTarget, Mechanics::MECHANIC_IMMUNE_SHIELD)
		)
		return true;
	return false;
}

bool BotBGAI::TargetIsMelee(Player* pTarget)
{
	if (!pTarget)
		return false;
	switch (pTarget->getClass())
	{
	case CLASS_PALADIN:
		return (pTarget->FindTalentType() != 0);
	case CLASS_WARRIOR:
	case CLASS_ROGUE:
	case CLASS_DEATH_KNIGHT:
		return true;
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_HUNTER:
	case CLASS_PRIEST:
	case CLASS_DRUID:
		return false;
	case CLASS_SHAMAN:
		return (pTarget->FindTalentType() == 1);
	}
	return false;
}

bool BotBGAI::TargetIsRange(Player* pTarget)
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

bool BotBGAI::TargetIsMagic(Player* pTarget)
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

bool BotBGAI::TargetIsHealth(Player* pTarget)
{
	if (!pTarget)
		return false;
	switch (pTarget->getClass())
	{
	case CLASS_WARRIOR:
	case CLASS_ROGUE:
	case CLASS_DEATH_KNIGHT:
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_HUNTER:
		return false;
	case CLASS_PALADIN:
		return (pTarget->FindTalentType() == 0);
	case CLASS_PRIEST:
		return (pTarget->FindTalentType() != 2);
	case CLASS_SHAMAN:
		return (pTarget->FindTalentType() == 2);
	case CLASS_DRUID:
		return (pTarget->FindTalentType() == 2);
	}
	return false;
}

bool BotBGAI::TargetCanManaBurn(Player* pTarget)
{
	if (!pTarget)
		return false;
	uint32 minManaPct = 8;
	switch (pTarget->getClass())
	{
	case CLASS_WARRIOR:
	case CLASS_ROGUE:
	case CLASS_DEATH_KNIGHT:
	case CLASS_HUNTER:
		return false;
	case CLASS_MAGE:
	case CLASS_WARLOCK:
	case CLASS_PALADIN:
	case CLASS_PRIEST:
	case CLASS_SHAMAN:
	case CLASS_DRUID:
		return (TargetManaPCT(pTarget) > minManaPct);
	}
	return false;
}

bool BotBGAI::TargetIsCastMagic(Player* pTarget)
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

bool BotBGAI::TargetIsStealth(Player* pTarget)
{
	if (!pTarget)
		return false;
	// (1784 Ä‚â€šĂ‚ÂµĂ„â€šĂ‚ÂĂ„â€šĂ˘â‚¬ĹĄĂ„â€šĂ‚Â´Ă„â€šĂ˘â‚¬Ë‡Ä‚â€šĂ‚Â±Ä‚â€žĂ‚ÂÄ‚â€žĂ‚Â || 5215 Ä‚â€šĂ‚ÂµĂ„â€šĂ˘â‚¬ĹˇĂ„â€šĂ˘â‚¬ĹˇĂ„Ä…Ă˘â‚¬ĹˇĂ„Ä…Ă˘â‚¬Ë‡Ă„â€šĂ‚ÂĂ„â€šĂ˘â‚¬Ë‡Ä‚â€šĂ‚Â±Ä‚â€žĂ‚ÂÄ‚â€žĂ‚Â || 66 Ä‚â€šĂ‚Â·Ä‚â€šĂ‚Â¨Ä‚â€žĂ‚ÂÄ‚â€šĂ‚Â¦Ă„Ä…Ă˘â‚¬Ë‡Ă„Ä…ÄąÂÄ‚â€žĂ‚ÂĂ„â€šÄąËť || 58984 Ä‚â€šĂ‚Â°Ä‚â€šĂ‚ÂµĂ„Ä…Ă˘â‚¬Ë‡Ä‚â€žĂ˘â‚¬Â¦Ă„Ä…Ă˘â‚¬Ë‡Ă„Ä…ÄąÂÄ‚â€šĂ‚Â¶Ă„â€šÄąÄ„)
	if (pTarget->HasAura(1784) || pTarget->HasAura(5215) ||
		pTarget->HasAura(66) || pTarget->HasAura(58984))
	{
		if (!me->canSeeOrDetect(pTarget, false, true)) // Ă„Ä…Ă‚ÂÄ‚â€žĂ˘â‚¬ĹźÄ‚â€ąĂ˘â‚¬ĹźĂ„â€šĂ‹ÂĂ„â€šĂ˘â‚¬Ë‡Ä‚â€šĂ‚Â±Ä‚â€žĂ‚ÂÄ‚â€žĂ‚Â
			return true;
	}
	return false;
}

uint32 BotBGAI::TargetManaPCT(Unit* pTarget)
{
	if (!pTarget)
		return 0;
	float per = (float)me->GetPower(POWER_MANA) / (float)me->GetMaxPower(POWER_MANA);
	return (uint32)(per * 100);
}

bool BotBGAI::IsSelfFaction(Unit* pTarget, uint32 faction)
{
	if (!pTarget)
		return false;
	if (pTarget->ToPlayer())
	{
		if (pTarget->ToPlayer()->GetTeamId() == TEAM_ALLIANCE)
		{
			if (faction == c_Alliance_Faction)
				return true;
			if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(1))
			{
				if (rEntry->FactionID == faction)
					return true;
			}
			if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(3))
			{
				if (rEntry->FactionID == faction)
					return true;
			}
			if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(4))
			{
				if (rEntry->FactionID == faction)
					return true;
			}
			if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(7))
			{
				if (rEntry->FactionID == faction)
					return true;
			}
			if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(11))
			{
				if (rEntry->FactionID == faction)
					return true;
			}
		}
		else
		{
			if (faction == c_Horde_Faction)
				return true;
			if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(2))
			{
				if (rEntry->FactionID == faction)
					return true;
			}
			if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(5))
			{
				if (rEntry->FactionID == faction)
					return true;
			}
			if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(6))
			{
				if (rEntry->FactionID == faction)
					return true;
			}
			if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(8))
			{
				if (rEntry->FactionID == faction)
					return true;
			}
			if (ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(10))
			{
				if (rEntry->FactionID == faction)
					return true;
			}
		}
	}
	else if (pTarget->ToCreature())
	{
		if (faction == c_Alliance_Faction && pTarget->getFaction() == c_Alliance_Faction)
			return true;
		if (faction == c_Horde_Faction && pTarget->getFaction() == c_Horde_Faction)
			return true;
	}
	return false;
}

bool BotBGAI::IsFilterCreatureByEntry(uint32 entry)
{
	if (m_FilterCreatureEntrys.find(entry) != m_FilterCreatureEntrys.end())
		return true;
	//for (uint32 ids : m_FilterCreatureEntrys)
	//{
	//	if (ids == entry)
	//		return true;
	//}
	return false;
}

bool BotBGAI::IsFleeTargetByRangeBot(Unit* pTarget)
{
	if (!pTarget || !pTarget->ToPlayer())
		return false;
	if (me->getClass() == Classes::CLASS_HUNTER)
		return true;
	if (pTarget->GetTargetGUID() != me->GetGUID())
		return false;
	if (IsHealerBotAI())
	{
		if (BeforeSearchNeedHealth(BOTAI_SEARCH_RANGE * 3, 70).size() > 0)
			return false;
	}
	if (BotBGAI* pBotAI = dynamic_cast<BotBGAI*>(pTarget->GetAI()))
	{
		if (!pBotAI->IsMeleeBotAI())
			return false;
		return true;
	}
	else if (!pTarget->IsPlayerBot())
	{
		Player* player = pTarget->ToPlayer();
		Classes cls = Classes(player->getClass());
		switch (cls)
		{
		case CLASS_PALADIN:
			return (player->FindTalentType() == 2);
		case CLASS_WARRIOR:
		case CLASS_ROGUE:
		case CLASS_DEATH_KNIGHT:
			return true;
		case CLASS_MAGE:
		case CLASS_WARLOCK:
		case CLASS_PRIEST:
		case CLASS_HUNTER:
			return false;
		case CLASS_SHAMAN:
			return (player->FindTalentType() == 1);
		case CLASS_DRUID:
			return (player->FindTalentType() == 1);
		}
	}
	return false;
}

void BotBGAI::QueryNearCreatureList(float range, NearCreatureList& creatureList)
{
	std::map<float, NearCreatureList>::iterator itMap = m_RangeCreatureLists.find(range);
	if (itMap != m_RangeCreatureLists.end())
	{
		creatureList = itMap->second;
		return;
	}

	NearCreatureList& nearCreature = m_RangeCreatureLists[range];
	Trinity::AllWorldObjectsInRange checker(me, range);
	Trinity::CreatureListSearcher<Trinity::AllWorldObjectsInRange> searcher(me, nearCreature, checker);
	me->VisitNearbyGridObject(range, searcher);
	creatureList = m_RangeCreatureLists[range];
}

void BotBGAI::QueryNearPlayerList(float range, NearPlayerList& playerList)
{
	std::map<float, NearPlayerList>::iterator itMap = m_RangePlayerLists.find(range);
	if (itMap != m_RangePlayerLists.end())
	{
		playerList = itMap->second;
		return;
	}

	NearPlayerList& nearPlayer = m_RangePlayerLists[range];
	me->GetPlayerListInGrid(nearPlayer, range);
	playerList = m_RangePlayerLists[range];
}

// FELSORIA_PVP_LEGION_ROTATIONS_V1
bool BotBGAI::ProcessLegionPvp(Unit* target)
{
    if(me->GetMapId()==761){
        if(m_Movement->IsBFGJumpInProgress())return true;
        if(!FelsoriaAVCombatTarget(me,target)){
            me->AttackStop();me->SetSelection(ObjectGuid::Empty);m_Movement->ReleaseCombatMovement();return true;
        }
        if(auto*commander=dynamic_cast<CommandBFG*>(me->GetMap()->GetCommander(me->GetBGTeamId())))
            if(commander->CombatReposition(me,target))return true;
    }
    bool avNpc=FelsoriaAVNpcRotation(me,target);
    if (!PvpLegionRotation::Applicable(me, target,avNpc)) return false;
    if (m_UsePotion.TryUsePotion()) return true;
    auto nearby = PvpLegionRotation::Nearby(me);
    if(me->GetMapId()==761)nearby.erase(std::remove_if(nearby.begin(),nearby.end(),[&](Unit*unit){
        return me->IsValidAttackTarget(unit)&&!FelsoriaAVCombatTarget(me,unit);
    }),nearby.end());
    if(avNpc)nearby.erase(std::remove_if(nearby.begin(),nearby.end(),[&](Unit* unit){
        return me->IsValidAssistTarget(unit)&&!FelsoriaAVAssistTarget(me,unit);
    }),nearby.end());
    bool handled = m_PvpRotation.Run(me, target, m_Movement, nearby, nearby,avNpc);
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

// FELSORIA_BG_PARTY_V2: carry first, defend against actual incoming attacks.
bool BotBGAI::IsFlagCarrier() const
{
    Battleground* bg = me->GetBattleground();
    if (BattlegroundWarsongGulch* wsg = dynamic_cast<BattlegroundWarsongGulch*>(bg))
        return wsg->GetFlagPickerGUID(TEAM_ALLIANCE, 0) == me->GetGUID() ||
            wsg->GetFlagPickerGUID(TEAM_HORDE, 0) == me->GetGUID();
    if (BattlegroundTwinPeaks* twin = dynamic_cast<BattlegroundTwinPeaks*>(bg))
        return twin->GetFlagPickerGUID(TEAM_ALLIANCE) == me->GetGUID() ||
            twin->GetFlagPickerGUID(TEAM_HORDE) == me->GetGUID();
    if (CommandObjectiveBG::Supports(bg))
        if (BattlegroundKotmoguTemplate* temple = dynamic_cast<BattlegroundKotmoguTemplate*>(bg))
            for (unsigned orb = 0; orb < MAX_ORBS; ++orb)
                if (temple->GetFlagPickerGUID(orb) == me->GetGUID()) return true;
    if (BattlegroundEyeOfTheStorm* eye = dynamic_cast<BattlegroundEyeOfTheStorm*>(bg))
        return eye->GetFlagPickerGUID() == me->GetGUID();
    return false;
}
Unit* BotBGAI::DefensiveTarget()
{
    auto legal = [this](Unit* unit)
    {
        return unit && unit != me && FelsoriaAVCombatTarget(me,unit) && !m_Movement->IsWSGTargetBlocked(unit) && unit->IsInWorld() && unit->isAlive() &&
            unit->GetMap() == me->GetMap() && me->InSamePhase(unit) &&
            me->IsValidAttackTarget(unit) && me->canSeeOrDetect(unit) &&
            me->InSamePhaseId(unit) && me->GetDistance(unit) < BOTAI_SEARCH_RANGE * (me->GetMapId()==489 ? 1.7f : 2.0f) && !IsInvincible(unit);
    };
    if (getMSTime() - m_LastBgAttackTime < 10000)
        if (Unit* recent = ObjectAccessor::GetUnit(*me, m_LastBgAttacker))
            if (legal(recent)) return recent;
    Unit* nearest = nullptr;
    for (Unit* attacker : *me->getAttackers())
        if (legal(attacker) && (!nearest || me->GetDistance(attacker) < me->GetDistance(nearest)))
            nearest = attacker;
    return nearest;
}

bool BotBGAI::TryGilneasLearnedBuff()
{
    // Cast only spells this character actually knows. These names are read
    // from the server SpellInfo, not from the client's language or a fixed ID.
    static char const*const names[]={"Arcane Intellect","Arcane Brilliance","Dalaran Brilliance",
        "Power Word: Fortitude","Mark of the Wild","Battle Shout","Blessing of Kings","Blessing of Might",
        "Blessing of Wisdom","Greater Blessing of Kings","Greater Blessing of Wisdom","Inner Fire",
        "Lightning Shield","Demon Armor","Fel Armor","Ice Armor","Frost Armor","Mage Armor"};
    for(auto const&learned:me->GetSpellMap()){
        if(learned.second.state==PLAYERSPELL_REMOVED||learned.second.disabled||!learned.second.active||me->HasAura(learned.first))continue;
        SpellInfo const*info=sSpellMgr->GetSpellInfo(learned.first);
        if(!info||!info->SpellName||info->IsPassive())continue;
        std::string name=info->SpellName;bool eligible=false;
        for(char const*candidate:names)if(name==candidate){eligible=true;break;}
        if(eligible&&TryCastSpell(learned.first,me)==SPELL_CAST_OK)return true;
    }
    return false;
}

bool BotBGAI::PrepareGilneasStart()
{
    auto*bg=me->GetBattleground();
    if(!bg||me->GetMapId()!=761||!me->IsPlayerBot()||!me->isAlive()||me->IsBeingTeleported())return false;
    auto remaining=bg->GetStartDelayTime().count();
    if(!FelsoriaGilneasPreparation::BuffWindow(bg->GetStatus()==STATUS_WAIT_JOIN,remaining))return false;
    uint32 now=getMSTime();
    if(m_GilneasPrepInstance!=bg->GetInstanceID()){
        m_GilneasPrepInstance=bg->GetInstanceID();m_GilneasPreparation.Reset();m_GilneasMountRetryAt=0;m_GilneasPrepTickAt=0;
        m_GilneasPrepCastSequence=0;m_PreparationInstance=bg->GetInstanceID();m_BgBuffs.Reset();m_OptionalPreparation.Reset();
    }
    m_PreparingBg=true;m_AIBGStateType=BotAIBGState::AIBGState_Ready;
    if(!m_GilneasPreparation.Begun()){
        m_GilneasPreparation.Begin(now);Dismount();m_Movement->ClearMovement();me->AttackStop();me->SetSelection(ObjectGuid::Empty);
    }
    if(me->HasUnitState(UNIT_STATE_CASTING))return true;
    if(m_GilneasPrepTickAt&&now-m_GilneasPrepTickAt<500)return true;
    m_GilneasPrepTickAt=now?now:1;
    if(!m_GilneasPreparation.Done()&&remaining>FelsoriaGilneasPreparation::MountReserveMS){
        EachTick();uint32 before=m_GilneasPrepCastSequence;m_GilneasPreparingCall=true;
        TryGilneasLearnedBuff();
        if(before==m_GilneasPrepCastSequence&&!me->HasUnitState(UNIT_STATE_CASTING))ProcessReady();
        if(before==m_GilneasPrepCastSequence&&!me->HasUnitState(UNIT_STATE_CASTING))ProcessNormalSpell();
        m_GilneasPreparingCall=false;
        if(before!=m_GilneasPrepCastSequence){m_GilneasPreparation.Activity(now);return true;}
    }
    if(!m_GilneasPreparation.Finish(now,remaining,me->HasUnitState(UNIT_STATE_CASTING)))return true;
    if(!me->IsMounted())TryUpMount();
    return true;
}

bool BotBGAI::TryGilneasWaitingMount()
{
    auto*bg=me->GetBattleground();
    auto*commander=me->GetMap()?dynamic_cast<CommandBFG*>(me->GetMap()->GetCommander(me->GetBGTeamId())):nullptr;
    auto note=[&](char const*reason,unsigned result=0){if(commander)commander->ObserveMount(me,reason,m_UseMountID,result);};
    if(!bg||me->GetMapId()!=761||bg->GetStatus()!=STATUS_WAIT_JOIN||!me->isAlive()||me->IsBeingTeleported())return false;
    if(!m_GilneasPreparation.Done()){note("buffing");return false;}
    if(me->IsMounted()){note("mounted");return false;}
    if(me->HasUnitState(UNIT_STATE_CASTING)){note("casting");return true;}
    if(me->isInCombat()||me->GetVehicleBase()||me->GetTransport()||
       me->HasUnitState(UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||
       me->HasAuraType(SPELL_AURA_MOD_ROOT)||me->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR)){
        note("movement_or_cc");return false;
    }
    if(commander&&!commander->CanUpMount(me)){note("commander_rule");return false;}
    uint32 now=getMSTime();
    if(m_GilneasMountRetryAt&&int32(m_GilneasMountRetryAt-now)>0)return false;
    m_GilneasMountRetryAt=now+250;
    PlayerBotSetting::RemoveForbiddenMount(me);
    if(!m_UseMountID||!PlayerBotSetting::IsMountAllowed(me,m_UseMountID))m_UseMountID=PlayerBotSetting::MountForPlayer(me);
    if(!m_UseMountID){note("no_allowed_mount");return false;}
    if(!me->HasSpell(m_UseMountID))me->learnSpell(m_UseMountID,false);
    me->AttackStop();me->SetSelection(ObjectGuid::Empty);
    // Native spell checks decide whether the waiting area permits mounting.
    // A separate raw VMAP outdoors query rejected otherwise legal BG starts.
    m_Movement->ClearMovement();
    SpellCastResult result=TryCastSpell(m_UseMountID,me,false,false);
    note(result==SPELL_CAST_OK?"mount_cast_started":"native_cast_rejected",unsigned(result));
    return result==SPELL_CAST_OK;
}

