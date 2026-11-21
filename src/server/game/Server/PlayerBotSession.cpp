
#include "PlayerBotSession.h"
#include "CommandBG/FelsoriaGilneasEntryTrace.h"
#include "FelsoriaBGRoleBalance.h"
#include "MapManager.h"
#include "Player.h"
#include "PlayerBotSetting.h"
#include "Group.h"
#include "Map.h"
#include "Log.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "BotAI.h"
#include "BotActivityPolicy.h"
#include "BotGroupAI.h"
#include "BotFieldAI.h"
#include "LFGMgr.h"
#include "CharacterPackets.h"
#include "MiscPackets.h"
#include "DB2Structure.h"
#include "DB2Stores.h"
#include "LFGPacketsCommon.h"
#include "Corpse.h"
#include <cmath>

PlayerBotSession::PlayerBotSession(uint32 id, std::string &name, AccountTypes sec, uint8 expansion, time_t mute_time, LocaleConstant locale, uint32 recruiter, bool isARecruiter) :
m_LastCastTime(CAST_SCHEDULE_TICK),
m_NoWorldTick(0),
m_AccountBot(false),
WorldSession(id, std::move(name), nullptr, SEC_PLAYER, 6, 0, "Wn64", LOCALE_zhCN, 0, false, AT_AUTH_FLAG_NONE, 0)
{
    SetAddress("playbot");
}

bool PlayerBotSession::IsBotSession()
{
	return true;
}

bool PlayerBotSession::HasBGSchedule()
{
	for (BotSchedules::iterator itSc = m_Schedules.begin(); itSc != m_Schedules.end(); itSc++)
	{
		BotGlobleScheduleType bgsType = (*itSc).bbgType;
		if (bgsType == BGSType_EnterBG || bgsType == BGSType_InBGQueue ||
			bgsType == BGSType_LeaveBG || bgsType == BGSType_OutBGQueue ||
			bgsType == BGSType_EnterAA || bgsType == BGSType_InAAQueue ||
			bgsType == BGSType_LeaveAA || bgsType == BGSType_OutAAQueue)
			return true;
	}
	return false;
}

bool PlayerBotSession::Update(uint32 diff, Map *map)
{
    CancelProtectedBotLogout(map);
    // FELSORIA_PARTY_TELEPORT_V2: cancel even a legacy pending logout
    // before WorldSession::Update can consume it during the human's transfer.
    if (map == GetMap() && sPlayerBotMgr->DynamicBotsEnabled())
        if (Player* bot = GetPlayer())
        {
            bool const requested = m_dynamicPartyProtection.exchange(false);
            if ((requested || isLogingOut()) &&
                sPlayerBotMgr->IsDynamicBotPartyProtected(bot) &&
                sPlayerBotMgr->IsDynamicBotPartyHumanConnected(bot))
            {
                if (isLogingOut())
                {
                    LogoutRequest(0);
                    TC_LOG_INFO(LOG_FILTER_PLAYER,
                        "DYNAMIC_PARTY_LOGOUT_CANCELLED account=%u guid=%u managed=%u",
                        GetAccountId(), bot->GetGUIDLow(), uint32(m_dynamicManagedLogout));
                }
                m_dynamicManagedLogout = false;
                m_dynamicRetire.store(false);
                m_dynamicRDFRetire.store(false);
                m_dynamicRetireAcknowledged.store(false);
                m_dynamicDormantLogout.store(false);
                {
                    std::lock_guard<std::mutex> lock(m_dynamicOpLock);
                    if (m_dynamicAwaitingWorldPlacement.load() && !m_dynamicPlacement.partyLeader)
                    {
                        m_dynamicPlacement.pending = false;
                        m_dynamicPhase.valid = false;
                        m_dynamicPlacementComplete.store(false);
                    }
                    m_dynamicAwaitingWorldPlacement.store(false);
                }
                if (bot->IsSettingFinish()) bot->SetVisible(true);
            }
        }
    // A group/proposal can form after we requested logout but before its
    // two-second delay expires. Cancel our own pending logout before base Update.
    if (map == GetMap() && m_dynamicManagedLogout)
        if (Player* member = GetPlayer())
            if ((m_dynamicRetireAcknowledged.load() &&
                    sPlayerBotMgr->IsDynamicBotPartyProtected(member)) ||
                sPlayerBotMgr->IsDynamicBotHumanActivityProtected(GetAccountId()))
            {
                LogoutRequest(0);
                m_dynamicManagedLogout = false;
                m_dynamicRetireAcknowledged.store(false);
            }
	bool updateResult = WorldSession::Update(diff, map);
	m_dynamicOffline.store(!GetPlayer() && !PlayerLoading());
    // The base update can return after the session moved to another map.
    // Complete transfers and schedules only on the owning map update.
    if (!updateResult || map != GetMap())
        return updateResult;
    // Match HandleCompleteMovie's state cleanup for sessions without a
    // client. This also recovers movies started before the bot-only guard.
    if (Player* bot = GetPlayer())
        if (bot->isWatchingMovie())
        {
            // FELSORIA_BOT_MOVIE_PUBLIC_HANDLER_V1
            WorldPacket moviePacket(CMSG_COMPLETE_MOVIE);
            WorldPackets::Misc::CompleteMovie completeMovie(std::move(moviePacket));
            HandleCompleteMovie(completeMovie);
            TC_LOG_INFO(LOG_FILTER_PLAYER,
                "BOT_MOVIE_RECOVERED guid=%u map=%u far=%u",
                bot->GetGUIDLow(), bot->GetMapId(), uint32(bot->IsBeingTeleportedFar()));
        }
    if (!GetPlayer() && !PlayerLoading())
        m_dynamicManagedLogout = false;
    if (!m_dynamicLogin.load() && !PlayerLoading())
    {
        uint64 login = m_dynamicOperation.Token(FelsoriaDynamicLifecycle::Operation::Login);
        if (login) m_dynamicOperation.Complete(login, FelsoriaDynamicLifecycle::Operation::Login);
    }
    // FELSORIA_DYNAMIC_FAST_V1: retire before consuming create/login work.
    if (m_dynamicRetire.load())
    {
        std::unique_ptr<WorldPacket> cancelled;
        {
            std::lock_guard<std::mutex> lock(m_dynamicOpLock);
            cancelled.swap(m_dynamicCreate);
            m_dynamicLogin.store(0);
        }
        if (cancelled)
        {
            WorldPackets::Character::CreateChar command(std::move(*cancelled));
            command.Read();
            uint64 operation = DynamicCreateOperation();
            if (command.CreateInfo) sPlayerBotMgr->ReleasePendingBotName(command.CreateInfo->Name);
            CompleteDynamicCreate(operation);
            sPlayerBotMgr->OnDynamicBotCreateFailed(GetAccountId(), operation, true);
        }
        // A running callback must finish (or be quarantined by the manager).
        // Never recycle the account while a late create/login can still land.
        if (DynamicCreateOperation() || PlayerLoading()) return updateResult;
        uint64 login = m_dynamicOperation.Token(FelsoriaDynamicLifecycle::Operation::Login);
        if (login) m_dynamicOperation.Complete(login, FelsoriaDynamicLifecycle::Operation::Login);
        m_dynamicRetire.store(false);
		// Recheck on the bot's owning map thread. Party membership can
		// change after the world manager last observed its snapshot.
		if (Player* member = GetPlayer())
		{
			if (sPlayerBotMgr->IsDynamicBotPartyProtected(member) ||
				sPlayerBotMgr->IsDynamicBotHumanActivityProtected(GetAccountId()))
			{
				m_dynamicRetireAcknowledged.store(false);
				return updateResult;
			}
		}
        // P15: detach only RDF leases on their owning map/session thread.
        // The protection checks above also cover a late human joining.
        if (m_dynamicRDFRetire.exchange(false))
            if (Player* member = GetPlayer())
            {
                sLFGMgr->LeaveLfg(member->GetGUID());
                if (Group* group = member->GetGroup())
                    if (group->isLFGGroup() && !group->isRaidGroup())
                        group->RemoveMember(member->GetGUID(), false, GROUP_REMOVEMETHOD_LEAVE);
            }
		// The owning map session processes the request; the world manager
		// deletes the character only after this session is fully offline.
		ClearAllSchedule();
		{
			std::lock_guard<std::mutex> lock(m_dynamicOpLock);
			m_dynamicPlacement.pending = false;
			m_dynamicPhase.valid = false;
			m_dynamicPlacementComplete.store(false);
		}
		m_dynamicRetireAcknowledged.store(true);
		m_dynamicManagedLogout = true;
		LogoutRequest(time(nullptr) - 20);
		return updateResult;
	}
	{
		std::unique_ptr<WorldPacket> createPacket;
		{
			std::lock_guard<std::mutex> lock(m_dynamicOpLock);
			createPacket.swap(m_dynamicCreate);
		}
		if (createPacket && (GetPlayer() || PlayerLoading()))
		{
			std::lock_guard<std::mutex> lock(m_dynamicOpLock);
			if (!m_dynamicCreate)
				m_dynamicCreate.swap(createPacket);
		}
		else if (createPacket)
		{
			WorldPackets::Character::CreateChar cmd(std::move(*createPacket));
			cmd.Read();
			HandleCharCreateOpcode(cmd);
			return updateResult;
		}
	}
	uint32 dynamicGuid = m_dynamicLogin.load();
	if (dynamicGuid && !GetPlayer() && !PlayerLoading())
	{
		m_dynamicLogin.store(0);
		// A session may be reused for another character after a logout. Never
		// carry a completed placement or phase lease into the new login.
		{
			std::lock_guard<std::mutex> lock(m_dynamicOpLock);
			m_dynamicPlacement = DynamicPlacement{};
			m_dynamicPhase = DynamicPhase{};
			m_dynamicPlacementComplete.store(false);
			m_dynamicPhaseApplied = false;
		}
		WorldPacket packet(CMSG_PLAYER_LOGIN);
		WorldPackets::Character::PlayerLogin cmd(std::move(packet));
		cmd.Guid = ObjectGuid::Create<HighGuid::Player>(dynamicGuid);
		cmd.FarClip = 0.0f;
		HandlePlayerLoginOpcode(cmd);
		HandleContinuePlayerLogin();
		return updateResult;
	}
	if (m_dynamicDormantLogout.exchange(false))
	{
		Player* member = GetPlayer();
		if (member && member->GetGroup() && !member->GetGroup()->GroupExistRealPlayer() &&
			!sPlayerBotMgr->IsDynamicBotHumanActivityProtected(GetAccountId()))
		{
			ClearAllSchedule();
			{
				std::lock_guard<std::mutex> lock(m_dynamicOpLock);
				m_dynamicPlacement.pending = false;
				m_dynamicPhase.valid = false;
				m_dynamicPlacementComplete.store(false);
			}
			m_dynamicManagedLogout = true;
			LogoutRequest(time(nullptr) - 20);
			return updateResult;
		}
	}
    if (Player* bot = GetPlayer())
        if (m_dynamicAwaitingWorldPlacement.load() && bot->IsVisible())
            bot->SetVisible(false);
	ProcessDynamicWorldRevive();
	{
		DynamicPlacement placement;
		{
			std::lock_guard<std::mutex> lock(m_dynamicOpLock);
			placement = m_dynamicPlacement;
		}
		Player* bot = GetPlayer();
		uint32 phase = 0;
		std::set<uint32> phaseIds;
		if (GetDynamicPhaseOverride(bot, phase, phaseIds))
		{
			m_dynamicPhaseApplied = true;
			if (bot->GetPhaseMask() != phase)
				bot->SetPhaseMask(phase, true);
			if (bot->GetPhases() != phaseIds)
			{
				bot->SetPhaseId(phaseIds, true);
				bot->GetPhaseMgr().AddUpdateFlag(PHASE_UPDATE_FLAG_CLIENTSIDE_CHANGED);
				bot->GetPhaseMgr().Update();
			}
		}
		else if (m_dynamicPhaseApplied && bot && bot->IsInWorld() &&
			!bot->IsBeingTeleported() && bot->GetCurrentZoneID())
		{
			// An expired world lease or a party without a present human
			// returns the bot to its own current phase definitions.
			m_dynamicPhaseApplied = false;
			bot->GetPhaseMgr().Recalculate();
			bot->GetPhaseMgr().AddUpdateFlag(PHASE_UPDATE_FLAG_CLIENTSIDE_CHANGED);
			bot->GetPhaseMgr().AddUpdateFlag(PHASE_UPDATE_FLAG_SERVERSIDE_CHANGED);
			bot->GetPhaseMgr().Update();
		}
        bool const partySummon = placement.partyLeader &&
            m_dynamicPartySummonLeader.load() == placement.partyLeader && bot &&
            ((bot->GetGroup() && bot->GetGroup()->IsMember(ObjectGuid::Create<HighGuid::Player>(placement.partyLeader))) ||
                sPlayerBotMgr->IsRememberedDynamicPartyOwner(bot->GetGUIDLow(), placement.partyLeader)) &&
            (!bot->GetGroup() || !bot->GetGroup()->isLFGGroup());
        if (placement.partyLeader && !partySummon)
            CancelDynamicPartySummon(placement.partyLeader);
        bool const safePartyDestination = !partySummon ||
            (sMapStore.LookupEntry(placement.map) &&
                (!sMapStore.LookupEntry(placement.map)->IsDungeon() ||
                    placement.requiredClass == bot->getClass()));
        // World relocation must not depend on spec/artifact preparation: a
        // newly created hero starts on its own map and can fail that readiness
        // gate. Finish preparing on the destination map, hidden until ready.
        // Party summons keep the existing readiness requirement.
        bool const placementReady = bot && bot->IsSettingFinish() &&
            !HasScheduleByType(BGSType_Settting);
		if (placement.pending && bot && bot->IsInWorld() &&
            (!placement.partyLeader || placementReady) && !bot->IsBeingTeleported() &&
            safePartyDestination && (!placement.partyLeader || partySummon) && (!bot->GetGroup() || partySummon) && !bot->InBattleground() && !bot->isInCombat() && bot->isAlive() &&
			MapManager::IsValidMapCoord(placement.map, placement.x, placement.y, placement.z, placement.o))
		{
			bool const arrived = bot->GetMapId() == placement.map &&
				bot->GetInstanceId() == placement.instance &&
				std::abs(bot->GetPositionX() - placement.x) < 8.0f &&
				std::abs(bot->GetPositionY() - placement.y) < 8.0f &&
				std::abs(bot->GetPositionZ() - placement.z) < 8.0f;
			if (arrived && placementReady)
			{
				uint32 arrivedPhase = 0;
				std::set<uint32> arrivedIds;
				bool accepted = false;
				{
					std::lock_guard<std::mutex> lock(m_dynamicOpLock);
					if (m_dynamicPlacement.pending && m_dynamicPlacement.map == placement.map &&
						m_dynamicPlacement.instance == placement.instance &&
                        m_dynamicPlacement.partyLeader == placement.partyLeader &&
						m_dynamicPlacement.x == placement.x && m_dynamicPlacement.y == placement.y &&
						m_dynamicPlacement.z == placement.z)
					{
						arrivedPhase = m_dynamicPlacement.phase;
						arrivedIds = m_dynamicPlacement.phaseIds;
						m_dynamicPlacement.pending = false;
						m_dynamicPhase = {placement.map, placement.instance,
							arrivedPhase, arrivedIds, true, std::chrono::steady_clock::now()};
						m_dynamicPlacementComplete.store(true);
						m_dynamicAwaitingWorldPlacement.store(false);
                        if (placement.partyLeader)
                            {
                            uint32 leader = placement.partyLeader;
                            m_dynamicPartySummonLeader.compare_exchange_strong(leader, 0);
                        }
						accepted = true;
					}
				}
				// SafeTeleport may reinitialize phases. Apply only after the
				// destination map accepted the bot and this lease is still current.
				if (accepted && GetDynamicPhaseOverride(bot, arrivedPhase, arrivedIds))
				{
					if (bot->GetPhaseMask() != arrivedPhase)
						bot->SetPhaseMask(arrivedPhase, true);
					if (bot->GetPhases() != arrivedIds)
					{
						bot->SetPhaseId(arrivedIds, true);
						bot->GetPhaseMgr().AddUpdateFlag(PHASE_UPDATE_FLAG_CLIENTSIDE_CHANGED);
						bot->GetPhaseMgr().Update();
					}
				}
                if (accepted)
                {
                    bot->SetVisible(true);
                    TC_LOG_INFO(LOG_FILTER_PLAYER,
                        "DYNAMIC_BOT_PLACE_COMPLETE guid=%u class=%u map=%u zone=%u instance=%u",
                        bot->GetGUIDLow(), uint32(bot->getClass()), bot->GetMapId(),
                        bot->GetZoneId(), bot->GetInstanceId());
                }
			}
			else if (!arrived && (!placement.started ||
				std::chrono::steady_clock::now() - placement.startedAt > std::chrono::seconds(5)))
			{
				// A cross-instance transfer on the same map must use the far
				// teleport path; MapManager resolves the exact existing instance.
				uint32 const options = bot->GetMapId() == placement.map &&
					bot->GetInstanceId() != placement.instance ? TELE_TO_ZONE_MAP : 0;
                // SafeTeleport calls CreateMap synchronously. Publish the
                // instance request BEFORE dispatch, without holding the mutex
                // while MapManager reads it. The old ordering selected a bot copy.
                auto const attemptAt = std::chrono::steady_clock::now();
                bool dispatch = false;
                {
                    std::lock_guard<std::mutex> lock(m_dynamicOpLock);
                    if (m_dynamicPlacement.pending && m_dynamicPlacement.map == placement.map &&
                        m_dynamicPlacement.instance == placement.instance &&
                        m_dynamicPlacement.partyLeader == placement.partyLeader &&
                        m_dynamicPlacement.x == placement.x && m_dynamicPlacement.y == placement.y &&
                        m_dynamicPlacement.z == placement.z)
                    {
                        m_dynamicPlacement.started = true;
                        m_dynamicPlacement.startedAt = attemptAt;
                        dispatch = true;
                    }
                }
                if (dispatch)
                    TC_LOG_INFO(LOG_FILTER_PLAYER,
                        "DYNAMIC_BOT_PLACE_DISPATCH guid=%u class=%u fromMap=%u targetMap=%u targetInstance=%u ready=%u",
                        bot->GetGUIDLow(), uint32(bot->getClass()), bot->GetMapId(),
                        placement.map, placement.instance, uint32(placementReady));
                if (dispatch && !bot->TeleportTo(placement.map, placement.x, placement.y,
                        placement.z, placement.o, options))
                {
                    std::lock_guard<std::mutex> lock(m_dynamicOpLock);
                    if (m_dynamicPlacement.startedAt == attemptAt)
                        m_dynamicPlacement.started = false;
                }
			}
		}
	}
	{
		std::unique_ptr<BotGlobleSchedule> setting, queue;
		{
			std::lock_guard<std::mutex> lock(m_dynamicOpLock);
			setting.swap(m_dynamicQueueSetting);
			queue.swap(m_dynamicQueueAction);
		}
		if (setting && queue && GetPlayer() && !HasSchedules())
		{
			PushScheduleToQueue(*setting);
			PushScheduleToQueue(*queue);
		}
		else if (setting && queue && GetPlayer())
		{
			std::lock_guard<std::mutex> lock(m_dynamicOpLock);
			if (!m_dynamicQueueAction)
			{
				m_dynamicQueueSetting.swap(setting);
				m_dynamicQueueAction.swap(queue);
			}
		}
	}
	ProcessNoWorld(diff);
    // Dynamic field bots are attackable in contested and enemy territory.
    // Area overrides and sanctuaries take precedence over a broad zone mask.
    if (m_dynamicPvPTick <= diff)
    {
        m_dynamicPvPTick = 1000;
        Player* bot = GetPlayer();
        if (bot && bot->IsInWorld() && !bot->InBattleground() && !bot->InArena() &&
            sPlayerBotMgr->GetDynamicBotTargetLevel(GetAccountId()))
        {
            AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId());
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(bot->GetAreaId());
            bool const sanctuary = (area && area->IsSanctuary()) ||
                (zone && zone->IsSanctuary());
            uint8 const territory = area && area->FactionGroupMask ? area->FactionGroupMask :
                zone ? zone->FactionGroupMask : 0;
            bool const contested = (area && (area->Flags[0] & AREA_FLAG_CONTESTED_AREA)) ||
                (zone && (zone->Flags[0] & AREA_FLAG_CONTESTED_AREA)) ||
                ((area || zone) && territory == 0);
            uint8 const enemy = bot->GetTeamId() == TEAM_ALLIANCE ? FACTION_MASK_HORDE : FACTION_MASK_ALLIANCE;
            bool const enemyTerritory = territory == enemy;
            if (!sanctuary && (contested || enemyTerritory) && !bot->IsPvP())
                bot->UpdatePvP(true, true);
        }
    }
    else
        m_dynamicPvPTick -= diff;
    Player* rdfPlayer = GetPlayer();
    if (rdfPlayer && rdfPlayer->GetGroup() && rdfPlayer->GetGroup()->isLFGGroup())
    {
        if (m_RdfStatusTick <= diff)
        {
            m_RdfStatusTick = 10000;
            // Felsoria: retry entry independently of follow AI and StopFollow.
            auto rdfDungeon = sLFGMgr->GetLFGDungeon(
                sLFGMgr->GetDungeon(rdfPlayer->GetGroup()->GetGUID()), rdfPlayer->GetTeam());
            if (rdfDungeon && rdfPlayer->IsInWorld() && rdfPlayer->isAlive() &&
                rdfPlayer->IsSettingFinish() && !rdfPlayer->IsBeingTeleported() &&
                !rdfPlayer->IsChangeMap())
            {
                for (GroupReference* ref = rdfPlayer->GetGroup()->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* human = ref->getSource();
                    if (!human || !human->GetSession() || human->GetSession()->IsBotSession() ||
                        !human->IsInWorld() || human->IsBeingTeleported() ||
                        human->GetMapId() != uint32(rdfDungeon->map))
                        continue;
                    if (rdfPlayer->GetMapId() != human->GetMapId() ||
                        rdfPlayer->GetInstanceId() != human->GetInstanceId())
                    {
                        TC_LOG_INFO(LOG_FILTER_LFG,
                            "RDF_ENTRY_RETRY account=%u class=%u from=%u target=%u targetInstance=%u",
                            GetAccountId(), uint32(rdfPlayer->getClass()), rdfPlayer->GetMapId(),
                            human->GetMapId(), human->GetInstanceId());
                        sLFGMgr->TeleportPlayer(rdfPlayer, false);
                    }

                    break;
                }
            }

            TC_LOG_INFO(LOG_FILTER_LFG,
                "RDF_POSITION account=%u name=%s x=%.3f y=%.3f z=%.3f phase=%u alive=%u setting=%u groupAI=%u fieldAI=%u schedules=%u",
                GetAccountId(), rdfPlayer->GetName(), rdfPlayer->GetPositionX(),
                rdfPlayer->GetPositionY(), rdfPlayer->GetPositionZ(), rdfPlayer->GetPhaseMask(),
                uint32(rdfPlayer->isAlive()), uint32(rdfPlayer->IsSettingFinish()),
                uint32(dynamic_cast<BotGroupAI*>(rdfPlayer->GetAI()) != nullptr),
                uint32(dynamic_cast<BotFieldAI*>(rdfPlayer->GetAI()) != nullptr), uint32(HasSchedules()));
            for (GroupReference* ref = rdfPlayer->GetGroup()->GetFirstMember(); ref; ref = ref->next())
            {
                Player* human = ref->getSource();
                if (!human || !human->GetSession() || human->GetSession()->IsBotSession())
                    continue;
                TC_LOG_INFO(LOG_FILTER_LFG,
                    "RDF_HUMAN account=%u human=%s map=%u instance=%u world=%u x=%.3f y=%.3f z=%.3f phase=%u sameMap=%u samePhase=%u samePhaseId=%u",
                    GetAccountId(), human->GetName(), human->GetMapId(), human->GetInstanceId(),
                    uint32(human->IsInWorld()), human->GetPositionX(), human->GetPositionY(),
                    human->GetPositionZ(), human->GetPhaseMask(), uint32(human->GetMap() == rdfPlayer->GetMap()),
                    uint32(rdfPlayer->InSamePhase(human)), uint32(rdfPlayer->InSamePhaseId(human)));
                break;
            }

            TC_LOG_INFO(LOG_FILTER_LFG,
                "RDF_BOT_STATE account=%u map=%u instance=%u world=%u far=%u near=%u changing=%u sessionMap=%u target=%u",
                GetAccountId(), rdfPlayer->GetMapId(), rdfPlayer->GetInstanceId(),
                uint32(rdfPlayer->IsInWorld()), uint32(rdfPlayer->IsBeingTeleportedFar()),
                uint32(rdfPlayer->IsBeingTeleportedNear()), uint32(rdfPlayer->IsChangeMap()),
                GetMap() ? GetMap()->GetId() : 0,
                rdfPlayer->IsBeingTeleported() ? rdfPlayer->GetTeleportDest().GetMapId() : 0);
        }
        else
            m_RdfStatusTick -= diff;
    }
    else
        m_RdfStatusTick = 0;
	m_LastCastTime -= diff;
	if (m_LastCastTime <= 0)
	{
		//std::lock_guard<std::mutex> lock(m_optQueueLock);
		CastSchedule(CAST_SCHEDULE_TICK);
		m_LastCastTime = CAST_SCHEDULE_TICK;
	}
	return updateResult;
}

bool PlayerBotSession::GetDynamicPhaseOverride(Player* bot, uint32& phase,
	std::set<uint32>& phaseIds)
{
	if (!bot || bot != GetPlayer() || !bot->IsInWorld() ||
		bot->IsBeingTeleported() || !bot->GetMap())
		return false;
	// A human in the same physical map instance is authoritative for party
	// members; an old lease cannot override their dungeon or BG progress.
	if (Group* group = bot->GetGroup())
	{
		for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
		{
			Player* member = ref->getSource();
			if (member && member != bot && member->IsInWorld() &&
				member->GetMap() == bot->GetMap() && member->GetSession() &&
				!member->GetSession()->IsBotSession())
			{
				phase = member->GetPhaseMask();
				phaseIds = member->GetPhases();
				return true;
			}
		}
		return false;
	}
	std::lock_guard<std::mutex> lock(m_dynamicOpLock);
	if (!m_dynamicPhase.valid ||
		std::chrono::steady_clock::now() - m_dynamicPhase.refreshedAt > std::chrono::seconds(5) ||
		bot->GetMapId() != m_dynamicPhase.map ||
		bot->GetInstanceId() != m_dynamicPhase.instance)
		return false;
	phase = m_dynamicPhase.phase;
	phaseIds = m_dynamicPhase.phaseIds;
	return true;
}

bool PlayerBotSession::HandlesDynamicWorldRevive(Player const* player) const
{
	if (!player || !player->GetMap() || player->InBattleground() || player->InArena() ||
		!sPlayerBotMgr->GetDynamicBotTargetLevel(GetAccountId()))
		return false;

	// Hero introductory maps are flagged as instances despite being the bot's world.
	uint32 const mapId = player->GetMapId();
	uint8 const requiredClass = PlayerBotMgr::GetDynamicAreaRequiredClass(mapId,
		player->GetZoneId(), player->GetAreaId());
	return !player->GetMap()->Instanceable() ||
		(requiredClass && requiredClass == player->getClass());
}

void PlayerBotSession::ProcessDynamicWorldRevive()
{
	Player* bot = GetPlayer();
	if (!bot || !HandlesDynamicWorldRevive(bot) || bot->isAlive())
	{
		m_dynamicDeathGuid = ObjectGuid::Empty;
		return;
	}

	// Observe death from the session, independent of field/group AI movement and setup gates.
	// Bind the timer and position to this character and map, never to a recycled session.
	if (m_dynamicDeathGuid != bot->GetGUID() || m_dynamicDeathMap != bot->GetMapId() ||
		m_dynamicDeathInstance != bot->GetInstanceId())
	{
		m_dynamicDeathGuid = bot->GetGUID();
		m_dynamicDeathMap = bot->GetMapId();
		m_dynamicDeathInstance = bot->GetInstanceId();
		m_dynamicDeathSince = std::chrono::steady_clock::now();
		m_dynamicDeathX = bot->GetPositionX();
		m_dynamicDeathY = bot->GetPositionY();
		m_dynamicDeathZ = bot->GetPositionZ();
		m_dynamicDeathO = bot->GetOrientation();
		if (Corpse const* corpse = bot->GetCorpse())
			if (corpse->GetMapId() == m_dynamicDeathMap &&
				corpse->GetInstanceId() == m_dynamicDeathInstance)
			{
				m_dynamicDeathX = corpse->GetPositionX();
				m_dynamicDeathY = corpse->GetPositionY();
				m_dynamicDeathZ = corpse->GetPositionZ();
				m_dynamicDeathO = corpse->GetOrientation();
			}
	}

	if (!DynamicWorldReviveDue(m_dynamicDeathSince, std::chrono::steady_clock::now()) ||
		!bot->IsInWorld() || bot->IsBeingTeleported() || bot->IsChangeMap() ||
		bot->GetMap() != GetMap())
		return;

	if (!MapManager::IsValidMapCoord(m_dynamicDeathMap, m_dynamicDeathX,
		m_dynamicDeathY, m_dynamicDeathZ, m_dynamicDeathO))
		return;
	if (bot->isDying())
		bot->KillPlayer();

	// If core released the ghost, use its recorded corpse location. If it has not,
	// create the corpse at the saved death spot before a possible near teleport.
	if (!bot->GetCorpse())
		if (Corpse* corpse = bot->CreateCorpse())
			bot->GetMap()->AddToMap(corpse);

	if (std::fabs(bot->GetPositionX() - m_dynamicDeathX) > 0.5f ||
		std::fabs(bot->GetPositionY() - m_dynamicDeathY) > 0.5f ||
		std::fabs(bot->GetPositionZ() - m_dynamicDeathZ) > 0.5f)
	{
		// Let ProcessNoWorld finish the near teleport before resurrecting.
		bot->TeleportTo(m_dynamicDeathMap, m_dynamicDeathX, m_dynamicDeathY,
			m_dynamicDeathZ, m_dynamicDeathO);
		return;
	}

	bot->ResurrectPlayer(1.0f);
	bot->SpawnCorpseBones();
	bot->SetHealth(bot->GetMaxHealth());
	bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));
	bot->SetSelection(ObjectGuid::Empty);
	bot->DurabilityRepairAll(false, 1.0f, false);
	bot->SaveToDB();
	m_dynamicDeathGuid = ObjectGuid::Empty;
}

void PlayerBotSession::PushScheduleToQueue(BotGlobleSchedule& schedule)
{
    if (HasRdfReservation() && schedule.bbgType != BotGlobleScheduleType::BGSType_Settting &&
        schedule.bbgType != BotGlobleScheduleType::BGSType_DelayLevelup)
        return;

	//std::lock_guard<std::mutex> lock(m_optQueueLock);
	for (BotSchedules::iterator itSc = m_Schedules.begin(); itSc != m_Schedules.end(); itSc++)
	{
		if ((*itSc).bbgType == schedule.bbgType)
			return;
	}
	if (schedule.bbgType == BGSType_DelayLevelup)
	{
		if (PlayerLoading())
			return;
		if (Player* player = GetPlayer())
		{
			if (!player->IsSettingFinish())
				return;
		}
		else
			return;
	}

	if (schedule.bbgType == BotGlobleScheduleType::BGSType_OutBGQueue)
	{
		RemoveScheduleByType(BotGlobleScheduleType::BGSType_InBGQueue);
		RemoveScheduleByType(BotGlobleScheduleType::BGSType_EnterBG);
	}
	if (schedule.bbgType == BotGlobleScheduleType::BGSType_OutAAQueue)
	{
		RemoveScheduleByType(BotGlobleScheduleType::BGSType_InAAQueue);
		RemoveScheduleByType(BotGlobleScheduleType::BGSType_EnterAA);
	}
	m_Schedules.push_back(schedule);
}

void PlayerBotSession::RemoveScheduleByType(BotGlobleScheduleType eType)
{
	for (BotSchedules::iterator itSc = m_Schedules.begin(); itSc != m_Schedules.end(); itSc++)
	{
		if ((*itSc).bbgType == eType)
		{
			m_Schedules.erase(itSc);
			return;
		}
	}
}

bool PlayerBotSession::HasScheduleByType(BotGlobleScheduleType eType)
{
	for (BotSchedules::iterator itSc = m_Schedules.begin(); itSc != m_Schedules.end(); itSc++)
	{
		if ((*itSc).bbgType == eType)
		{
			return true;
		}
	}
	return false;
}

bool PlayerBotSession::HasSchedules()
{
	return !m_Schedules.empty() || HasRdfReservation();
}

bool PlayerBotSession::IsAccountBotSession()
{
	return m_AccountBot;
}

bool PlayerBotSession::PlayerIsReady()
{
	Player* player = GetPlayer();
	if (!player)
		return true;
	if (!player->IsSettingFinish())
		return false;
	BotFieldAI* pFieldAI = dynamic_cast<BotFieldAI*>(player->GetAI());
	if (!pFieldAI)
		return true;
	if (pFieldAI->HasTeleport())
		return false;

	return true;
}

void PlayerBotSession::ProcessNoWorld(uint32 diff)
{
	if (PlayerLoading())
		return;
	Player* player = GetPlayer();
	if (!player)
		return;
    // RDF_NEAR_ACK_FIX: finish local teleports without a game client.
    if (player->IsInWorld() &&
        player->IsBeingTeleportedNear() &&
        !player->IsBeingTeleportedFar() &&
        !player->IsChangeMap() &&
        player->m_mover == player)
    {
        HandleBotMoveTeleportAck();
    }

	if (player->IsInWorld())
	{
		m_NoWorldTick = 0;
		return;
	}

	if (player->IsBeingTeleported())
	{
        HandleWorldPortAck();
        m_NoWorldTick = 1000;
		return;
	}

	if (m_NoWorldTick == 0)
	{
		m_NoWorldTick = 2000;
		return;
	}

	m_NoWorldTick -= int32(diff);
	if (m_NoWorldTick > 0)
		return;
    // Recover only the observed orphaned RDF transfer on the destination session.
    // Preserve native CanEnter/AddPlayerToMap checks; never attach directly to a map.
    if (player->GetGroup() && player->GetGroup()->isLFGGroup() &&
        !player->IsChangeMap() && !player->IsBeingTeleported() &&
        GetMap() && GetMap()->IsDungeon() && player->GetMap() != GetMap() &&
        player->GetTeleportDest().GetMapId() == GetMap()->GetId())
    {
        TC_LOG_INFO(LOG_FILTER_LFG, "RDF_TRANSFER_RETRY account=%u oldMap=%u sessionMap=%u",
            GetAccountId(), player->GetMapId(), GetMap()->GetId());
        player->SetSemaphoreTeleportFar(true);
        HandleWorldPortAck();
        TC_LOG_INFO(LOG_FILTER_LFG, "RDF_TRANSFER_RESULT account=%u map=%u instance=%u world=%u far=%u changing=%u",
            GetAccountId(), player->GetMapId(), player->GetInstanceId(), uint32(player->IsInWorld()),
            uint32(player->IsBeingTeleportedFar()), uint32(player->IsChangeMap()));
        m_NoWorldTick = 5000;
        return;
    }
	if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
	{
		if (pGroupAI->HasTeleport())
			pGroupAI->UpdateTeleport(diff);
		else
			pGroupAI->SetTeleportToMaster();
		m_NoWorldTick = 500;
	}
	else if (BotBGAI* pGroupAI = dynamic_cast<BotBGAI*>(player->GetAI()))
	{
		if (player->InBattleground())
		{
			PlayerBotMgr::SwitchPlayerBotAI(player, PlayerBotAIType::PBAIT_FIELD, true);
			WorldPacket opcode(CMSG_BATTLEFIELD_LEAVE);
			WorldPackets::Battleground::NullCmsg msg(std::move(opcode));
			HandleLeaveBattlefield(msg);
		}
		HandleWorldPortAck();
		m_NoWorldTick = 500;
	}
	else// if (BotFieldAI* pGroupAI = dynamic_cast<BotFieldAI*>(player->GetAI()))
	{
		HandleWorldPortAck();
		m_NoWorldTick = 500;
	}
}

void PlayerBotSession::CastSchedule(uint32 diff)
{
	if (m_Schedules.empty())
		return;
	bool result = false;
	BotGlobleSchedule& schedule = *m_Schedules.begin();
	schedule.processTick += diff;
    // FELSORIA_RDF_PREPARE_FAILURE_V2: terminal repair failure must not wait 3 minutes.
    bool rdfPreparing = false;
    uint32 requestedRole = 0;
    for (auto const& pending : m_Schedules)
        if (pending.bbgType == BGSType_InLFGQueue)
        {
            rdfPreparing = true;
            requestedRole = pending.parameter1;
            break;
        }
    if (rdfPreparing)
    {
        Player* preparing = GetPlayer();
        PlayerBotSetting* state = preparing ? preparing->m_PlayerBotSetting : nullptr;
        if (state && state->IsPreparationFailed())
        {
            TC_LOG_ERROR(LOG_FILTER_LFG,
                "RDF_BOT_PREPARE_FAILED account=%u guid=%u role=%u spec=%u step=%u repair=%u attempts=%u",
                GetAccountId(), preparing->GetGUIDLow(), requestedRole,
                preparing->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID),
                state->GetPreparationStep(), uint32(state->IsPreparationRepair()),
                state->GetPreparationAttempts());
            m_Schedules.erase(m_Schedules.begin());
            sPlayerBotMgr->OnDynamicBotQueueFailed(GetAccountId());
            return;
        }
        if (schedule.processTick == diff ||
            schedule.processTick / 10000 != (schedule.processTick - diff) / 10000)
            TC_LOG_ERROR(LOG_FILTER_LFG,
                "RDF_BOT_PREPARE_WAIT account=%u guid=%u role=%u job=%u loading=%u world=%u alive=%u teleport=%u ready=%u finish=%u spec=%u step=%u repair=%u attempts=%u elapsed_ms=%u",
                GetAccountId(), preparing ? preparing->GetGUIDLow() : 0u,
                requestedRole, uint32(schedule.bbgType), uint32(PlayerLoading()),
                uint32(preparing && preparing->IsInWorld()),
                uint32(preparing && preparing->isAlive()),
                uint32(preparing && preparing->IsBeingTeleported()), uint32(PlayerIsReady()),
                uint32(preparing && preparing->IsSettingFinish()),
                preparing ? preparing->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID) : 0u,
                state ? state->GetPreparationStep() : 0u,
                uint32(state && state->IsPreparationRepair()),
                state ? state->GetPreparationAttempts() : 0u, schedule.processTick);
    }

    if (schedule.processTick > 60000 * 3)
    {
        // FELSORIA_BOT_RDF_TIMEOUT_V1: preparation can expire before the join job.
        bool rdfPending = false;
        for (auto const& pending : m_Schedules)
            if (pending.bbgType == BGSType_InLFGQueue)
                rdfPending = true;
        bool roleBGPending=false;
        for(auto const&pending:m_Schedules)if(pending.bbgType==BGSType_InBGQueue)
            if(auto*bg=sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundTypeId(pending.parameter1)))
                if(bg->IsBattleground()&&!bg->IsArena()&&!bg->IsRated()&&!bg->IsWargame()&&!bg->IsBrawl())roleBGPending=true;
        bool abPending=false;
        for(auto const& pending:m_Schedules)
            if(pending.bbgType==BGSType_InBGQueue && pending.parameter1==MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin) abPending=true;
        if(Player* bot=GetPlayer())
            if(bot->GetBattlegroundQueueIndex(MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin))<PLAYER_MAX_BATTLEGROUND_QUEUES) abPending=true;
        m_Schedules.erase(m_Schedules.begin());
        if(abPending) {TC_LOG_ERROR(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_SCHEDULE_TIMEOUT account=%u",GetAccountId());FailBattlegroundEntry();}
        else if(roleBGPending){TC_LOG_ERROR(LOG_FILTER_BATTLEGROUND,"FELSORIA_BG_ROLE_SCHEDULE_TIMEOUT account=%u",GetAccountId());FailBattlegroundEntry();}
        if (rdfPending)
        {
            TC_LOG_ERROR(LOG_FILTER_LFG, "RDF_BOT_SCHEDULE_TIMEOUT account=%u", GetAccountId());
            sPlayerBotMgr->OnDynamicBotQueueFailed(GetAccountId());
        }
        return;
    }
    bool gilneasWork=false;
    for(auto const&job:m_Schedules)if((job.bbgType==BGSType_InBGQueue||job.bbgType==BGSType_EnterBG)&&job.parameter1==120)gilneasWork=true;
    Player*entryPlayer=GetPlayer();
    if(entryPlayer)for(uint8 i=0;i<PLAYER_MAX_BATTLEGROUND_QUEUES;++i){uint8 queue=entryPlayer->GetBattlegroundQueueTypeId(i);
        if(queue&&MS::Battlegrounds::GetBgTemplateIdByQueueTypeID(queue)==120)gilneasWork=true;}
    if(gilneasWork){
        std::string stage=!entryPlayer?"loading":!entryPlayer->IsSettingFinish()?"preparing":entryPlayer->IsBeingTeleported()?"port_pending":("schedule_"+std::to_string(unsigned(schedule.bbgType)));
        FelsoriaGilneasEntry::Record(getMSTime(),GetAccountId(),stage.c_str(),entryPlayer?entryPlayer->GetGUIDLow():0,
            unsigned(PlayerIsReady()),entryPlayer&&entryPlayer->m_PlayerBotSetting?entryPlayer->m_PlayerBotSetting->GetPreparationStep():0,schedule.processTick);
    }
	if (!PlayerIsReady())
		return;
	switch (schedule.bbgType)
	{
	case BGSType_Online:
		result = ProcessOnline(schedule);
		break;
	case BGSType_Online_GUID:
		result = ProcessOnlineByGUID(schedule);
		break;
	case BGSType_Offline:
		result = ProcessOffline(schedule);
		break;
	case BGSType_Settting:
		result = ProcessSetting(schedule);
		break;
	case BGSType_InBGQueue:
		result = ProcessInBGQueue(schedule);
		break;
	case BGSType_OutBGQueue:
		result = ProcessOutBGQueue(schedule);
		break;
	case BGSType_EnterBG:
		result = ProcessEnterBG(schedule);
		break;
	case BGSType_LeaveBG:
		result = ProcessLeaveBG(schedule);
		break;
	case BGSType_InAAQueue:
		result = ProcessInAAQueue(schedule);
		break;
	case BGSType_OutAAQueue:
		result = ProcessOutAAQueue(schedule);
		break;
	case BGSType_EnterAA:
		result = ProcessEnterAA(schedule);
		break;
	case BGSType_LeaveAA:
		result = ProcessLeaveAA(schedule);
		break;
	case BGSType_DelayLevelup:
		result = ProcessDelayLevelup(schedule);
		break;
	case BGSType_InLFGQueue:
		result = ProcessInLFGQueue(schedule);
		break;
	case BGSType_OutLFGQueue:
		result = ProcessOutLFGQueue(schedule);
		break;
	case BGSType_AcceptLFGProposal:
		result = ProcessAcceptLFGProposal(schedule);
		break;
	case BGSType_OfferPetitionSign:
		result = ProcessOfferPetitionSign(schedule);
		break;
	default:
		result = true;
		break;
	}
	if (result)
	{
		m_Schedules.erase(m_Schedules.begin());
	}
}

bool PlayerBotSession::ProcessOnline(BotGlobleSchedule& schedule)
{
	if (!sPlayerBotMgr->DynamicBotsEnabled() && sPlayerBotMgr->m_MaxOnlineBot <= sPlayerBotMgr->m_BotOnlineCount)
	{
		LogoutPlayer(false, "playerbot max");
		return true;
	}

	if (schedule.parameter1 <= 0)
		return true;
	if (PlayerLoading())
		return false;
	if (GetPlayer())
		return true;
	PlayerBotBaseInfo* pInfo = sPlayerBotMgr->GetPlayerBotAccountInfo(GetAccountId());
	if (!pInfo)
	{
		pInfo = sPlayerBotMgr->GetAccountBotAccountInfo(GetAccountId());
		if (!pInfo)
		{
			ClearAllSchedule();
			return false;
		}
	}

	bool fuction = true;
	if (schedule.parameter1 > 1)
		fuction = false;
	PlayerBotCharBaseInfo& charInfo = (schedule.parameter2 == 0) ? pInfo->GetRandomCharacterByFuction(fuction) : pInfo->GetCharacter(fuction, schedule.parameter2);
	if (charInfo.guid == 0)
		return true;

    WorldPacket _worldPacket(CMSG_PLAYER_LOGIN);
    WorldPackets::Character::PlayerLogin cmd(std::move(_worldPacket));
    cmd.Guid = ObjectGuid::Create<HighGuid::Player>(charInfo.guid);
    cmd.FarClip = 0.0f;
    HandlePlayerLoginOpcode(cmd);
	HandleContinuePlayerLogin();
	return false;
}

bool PlayerBotSession::ProcessOnlineByGUID(BotGlobleSchedule& schedule)
{
    if (!sPlayerBotMgr->DynamicBotsEnabled() && sPlayerBotMgr->m_MaxOnlineBot <= sPlayerBotMgr->m_BotOnlineCount)
    {
        LogoutPlayer(false, "playerbot max");
        return true;
    }

	if (schedule.playerGUID.IsEmpty())
		return true;
	if (PlayerLoading())
		return false;
	if (GetPlayer())
		return true;
    WorldPacket _worldPacket(CMSG_PLAYER_LOGIN);
    WorldPackets::Character::PlayerLogin cmd(std::move(_worldPacket));
    cmd.Guid = schedule.playerGUID;
    cmd.FarClip = 0.0f;
    HandlePlayerLoginOpcode(cmd);
	HandleContinuePlayerLogin();
	return false;
}

bool PlayerBotSession::ProcessOffline(BotGlobleSchedule& schedule)
{
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
		return true;
	if (sPlayerBotMgr->DynamicBotsEnabled() && sPlayerBotMgr->IsDynamicBotPartyProtected(player)) return true;
	if (schedule.scheduleState > 0)
		return false;
	LogoutRequest(time(NULL) - 18);
	//LogoutPlayer(false);
	schedule.scheduleState = 1;
	return false;
}

namespace {
bool FelsoriaPrepareBGRole(Player*player,uint32 account,BotGlobleSchedule&setting,BotGlobleSchedule&queue){
    auto*bg=sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundTypeId(queue.parameter1));
    if(!bg||!bg->IsBattleground()||bg->IsArena()||bg->IsRated()||bg->IsWargame()||bg->IsBrawl())return true;
    auto*bracket=sDB2Manager.GetBattlegroundBracketByLevel(bg->GetMapId(),setting.parameter2);
    if(!bracket)return false;
    FelsoriaBGRole::Context context{queue.parameter1,uint32(bracket->RangeIndex),0,uint32(player->GetTeamId()),bg->GetMaxPlayersPerTeam()};
    unsigned previous=0;FelsoriaBGRole::Context planned;
    if(FelsoriaBGRole::Registry().Plan(account,queue.parameter1,planned,previous))context=planned;
    auto lookup=[](unsigned c,unsigned i){return sDB2Manager.GetChrSpecializationByIndex(c,i);};
    unsigned allowed=FelsoriaBGRole::Supported(player->getClass(),lookup);
    unsigned mask=FelsoriaBGRole::Registry().Reserve(account,context,getMSTime(),allowed);
    if(!mask)return false;
    unsigned code=FelsoriaBGRole::SelectCode(player->getClass(),mask,player->GetSpecializationId(),lookup,
        [](unsigned n){return urand(0,n-1);});
    if(!code)return false;
    setting.parameter3=code;queue.parameter2=mask;
    TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_BG_ROLE_PREP account=%u bg=%u team=%u mask=%u specCode=%u",
        account,queue.parameter1,uint32(player->GetTeamId()),mask,code);
    return true;
}
}

bool PlayerBotSession::ProcessSetting(BotGlobleSchedule& schedule)
{
	if (schedule.parameter3 == 0 || schedule.parameter3 > 6)
		schedule.parameter3 = 4;
    if (schedule.parameter2 > 110 || schedule.parameter2 == 0)
        schedule.parameter2 = 110;
	if (schedule.parameter1 > schedule.parameter2)
		schedule.parameter2 = schedule.parameter1;
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	if (!player->IsInWorld() || !player->IsSettingFinish())
		return false;
	if (schedule.scheduleState != 0)
		return true;
    bool preparedBGRole=false;
    for(auto&pending:m_Schedules)if(pending.bbgType==BGSType_InBGQueue){
        if(!FelsoriaPrepareBGRole(player,GetAccountId(),schedule,pending))return false;
        auto*bg=sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundTypeId(pending.parameter1));
        preparedBGRole=bg&&bg->IsBattleground()&&!bg->IsArena()&&!bg->IsRated()&&!bg->IsWargame()&&!bg->IsBrawl()&&
            FelsoriaBGRole::Role(pending.parameter2)<3 &&
            pending.parameter2==FelsoriaBGRole::Mask(player->GetSpecializationRole()) &&
            player->getLevel()==schedule.parameter2 && player->EquipIsTidiness();
        break;
    }
	bool needTenacity = (schedule.parameter4 != 0) ? true : false;
	if (needTenacity)
		needTenacity = player->CheckNeedTenacityFlush();
    // IsSettingFinish above confirms the previous talent/artifact preparation
    // completed. Reuse it only for this exact normal-BG role and target level.
    if(preparedBGRole && !needTenacity){schedule.scheduleState=1;return true;}
	uint32 flushTalent = (schedule.parameter3 > 0 && schedule.parameter3 < 4) ? schedule.parameter3 - 1 :
		(schedule.parameter3 == 5 || schedule.parameter3 == 6) ? schedule.parameter3 - 1 : 3;
	player->ResetPlayerToLevel(schedule.parameter2, flushTalent, needTenacity);
	schedule.scheduleState = 1;
	return false;
}

bool PlayerBotSession::ProcessInBGQueue(BotGlobleSchedule& schedule)
{
    bool const arathi=schedule.parameter1==MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin;
    if(arathi) {
        if(!schedule.parameter5) schedule.parameter5=getMSTime()?getMSTime():1;
        if(uint32(getMSTime()-schedule.parameter5)>30000) {
            TC_LOG_ERROR(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_JOIN_TIMEOUT account=%u",GetAccountId());
            FailBattlegroundEntry();return true;
        }
    }
    if (!BattlegroundRetryAllowed()) return true;
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	if (!player->IsInWorld())
		return false;
	if (!player->IsSettingFinish())
		return false;
	if (player->InBattlegroundQueue()) {
        if(arathi && player->GetBattlegroundQueueIndex(MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(schedule.parameter1))>=PLAYER_MAX_BATTLEGROUND_QUEUES) {
            TC_LOG_ERROR(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_JOIN_WRONG_QUEUE account=%u",GetAccountId());
            FailBattlegroundEntry();
        }
        return true;
    }
	if (player->InBattleground() || player->InArena() || player->GetMap()->IsDungeon())
	{
		ClearAllSchedule();
		return false;
	}

	Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundTypeId(schedule.parameter1));
	if (!bg)
	{
        if(arathi){TC_LOG_ERROR(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_JOIN_FAILED account=%u reason=missing_template",GetAccountId());FailBattlegroundEntry();return true;}
		ClearAllSchedule();
		return false;
	}
	PVPDifficultyEntry const* bracketEntry = DB2Manager::GetBattlegroundBracketByLevel(bg->GetMapId(), player->getLevel());
	if (!bracketEntry)
	{
        if(arathi){TC_LOG_ERROR(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_JOIN_FAILED account=%u reason=missing_bracket level=%u",GetAccountId(),uint32(player->getLevel()));FailBattlegroundEntry();return true;}
		ClearAllSchedule();
		return false;
	}

    if(arathi && schedule.parameter4 && uint32(getMSTime()-schedule.parameter4)<1500) return false;
    if(arathi) schedule.parameter4=getMSTime()?getMSTime():1;
	WorldPacket cmd(CMSG_BATTLEMASTER_JOIN);
	WorldPackets::Battleground::Join packet(std::move(cmd));
	packet.QueueID = MS::Battlegrounds::QueueOffsets::Battleground + schedule.parameter1;
    packet.RolesMask=14;
    if(bg->IsBattleground()&&!bg->IsArena()&&!bg->IsRated()&&!bg->IsWargame()&&!bg->IsBrawl()){
    uint8 const actualMask=uint8(FelsoriaBGRole::Mask(player->GetSpecializationRole()));
    if(!actualMask||(schedule.parameter2&&schedule.parameter2!=actualMask)){
        TC_LOG_ERROR(LOG_FILTER_BATTLEGROUND,"FELSORIA_BG_ROLE_MISMATCH account=%u bg=%u expected=%u actual=%u",
            GetAccountId(),schedule.parameter1,schedule.parameter2,uint32(actualMask));
        FailBattlegroundEntry();return true;
    }
    packet.RolesMask=actualMask;
    player->SetQueueRoleMask(bracketEntry->RangeIndex,actualMask,true);
    }
	HandleBattlemasterJoin(packet);
    if(arathi) TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_JOIN account=%u guid=%u level=%u queued=%u",
        GetAccountId(),player->GetGUIDLow(),uint32(player->getLevel()),uint32(player->GetBattlegroundQueueIndex(MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(schedule.parameter1))<PLAYER_MAX_BATTLEGROUND_QUEUES));
	return false;
}

bool PlayerBotSession::ProcessOutBGQueue(BotGlobleSchedule& schedule)
{
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	if (!player->IsInWorld() || player->InBattleground() || player->InArena() || !player->InBattlegroundQueue() || player->GetMap()->IsDungeon())
		return true;

	if (sPlayerBotMgr->DynamicBotsEnabled() && sPlayerBotMgr->IsDynamicBotPartyProtected(player)) return true;
	LogoutPlayer(false, "playerbot");
	return false;
}

void PlayerBotSession::FailBattlegroundEntry()
{
    FelsoriaBGRole::Registry().Cancel(GetAccountId(),getMSTime());
    m_bgRetryAfter.store(std::time(nullptr) + 120);
    if (Player* player = GetPlayer())
        for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            uint8 type = player->GetBattlegroundQueueTypeId(i);
            if (!type) continue;
            player->RemoveBattlegroundQueueId(type);
            sBattlegroundMgr->GetBattlegroundQueue(type).RemovePlayer(player->GetGUID(), true);
        }
    sPlayerBotMgr->OnDynamicBotQueueFailed(GetAccountId());
    TC_LOG_ERROR(LOG_FILTER_PLAYER, "BOT_BG_ENTRY_FAILED account=%u retry_after=120s", GetAccountId());
}

bool PlayerBotSession::ProcessEnterBG(BotGlobleSchedule& schedule)
{
    Player* entering = GetPlayer();
    if (entering && (entering->InBattleground() || entering->InArena())) return true;
    if (schedule.parameter5 && BotActivityPolicy::joinTimedOut(getMSTime(), schedule.parameter5))
    {
        FailBattlegroundEntry();
        return true; // CastSchedule erases this item; never clear its list here.
    }
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	if (!player->IsInWorld())
		return false;

	if (player->isInCombat())
		player->CombatStop(true);

	if (player->GetMap()->IsDungeon())
	{
		BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_OutBGQueue, 0);
		schedule1.parameter1 = schedule.parameter1;
		ClearAllSchedule();
		PushScheduleToQueue(schedule1);
		return false;
	}
	if (player->InBattleground() || player->InArena())
		return true;

	if (player->InBattlegroundQueue())
	{
		for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
		{
			uint8 bgQueueTypeId = player->GetBattlegroundQueueTypeId(i);
			if (!bgQueueTypeId)
				continue;
			if (player->IsInvitedForBattlegroundQueueType(bgQueueTypeId))
			{
                if (!schedule.parameter5) schedule.parameter5 = getMSTime() ? getMSTime() : 1;
				// May switch an excess tank/healer to DPS. The existing spec
				// repair must finish (talents AND artifact) before entering.
				if (!player->IsSettingFinish() ||
					!sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId).PrepareWSGBotEntry(player))
					return false;
				PlayerBotMgr::SwitchPlayerBotAI(player, PlayerBotAIType::PBAIT_BG, true);

                WorldPacket cmd(CMSG_BATTLEFIELD_PORT);
                WorldPackets::Battleground::Port packet(std::move(cmd));
                uint16 actualType=MS::Battlegrounds::GetBgTemplateIdByQueueTypeID(bgQueueTypeId);
                bool arathi=actualType==MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin;
                bool const gilneas=actualType==MS::Battlegrounds::BattlegroundTypeId::BattlegroundBattleForGilneas;
                bool const slotBasedEntry=arathi || gilneas;
                // The port handler expects the queue slot, not the battleground type.
                packet.Ticket.Id = slotBasedEntry ? i : schedule.parameter1;
				packet.Ticket.RequesterGuid = schedule.playerGUID;
				packet.Ticket.Type = WorldPackets::LFG::RideType::Battlegrounds;
				packet.Ticket.Time = time(0);
				packet.AcceptedInvite = true;
                if(slotBasedEntry && player->IsBeingTeleported()) return false;
                if(slotBasedEntry && schedule.parameter4 && uint32(getMSTime()-schedule.parameter4)<1500) return false;
                if(slotBasedEntry) schedule.parameter4=getMSTime()?getMSTime():1;
                HandleBattleFieldPort(packet);
                if(gilneas)FelsoriaGilneasEntry::Record(getMSTime(),GetAccountId(),"port_sent",player->GetGUIDLow(),unsigned(player->IsSettingFinish()),0,schedule.processTick);
                if(arathi) TC_LOG_INFO(LOG_FILTER_BATTLEGROUND,"FELSORIA_AB_PORT account=%u guid=%u slot=%u bg=%u teleport=%u",
                    GetAccountId(),player->GetGUIDLow(),uint32(i),player->GetBattlegroundId(),uint32(player->IsBeingTeleported()));
				//HandleWorldPortAck();
				break;
			}
		}
	}
	return false;
}

bool PlayerBotSession::ProcessLeaveBG(BotGlobleSchedule& schedule)
{
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	if (!player->InBattleground())
		return true;

	PlayerBotMgr::SwitchPlayerBotAI(player, PlayerBotAIType::PBAIT_FIELD, true);

	WorldPackets::Battleground::NullCmsg cmd(std::move(WorldPacket()));
	HandleLeaveBattlefield(cmd);
	//HandleWorldPortAck();
	return false;
}

bool PlayerBotSession::ProcessInAAQueue(BotGlobleSchedule& schedule)
{
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	if (!player->IsInWorld())
		return false;
	if (player->InBattlegroundQueue())
		return true;
	if (player->HasAura(26013) || player->InBattleground() || player->InArena() || player->GetMap()->IsDungeon())
	{
		ClearAllSchedule();
		return false;
	}

	WorldPacket packet(CMSG_BATTLEMASTER_JOIN_SKIRMISH);
	WorldPackets::Battleground::JoinSkirmish cmd(std::move(packet));
	cmd.RolesMask = schedule.parameter1;
	cmd.Bracket = schedule.parameter2;
	HandleJoinSkirmish(cmd);
	return false;
}

bool PlayerBotSession::ProcessOutAAQueue(BotGlobleSchedule& schedule)
{
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	if (!player->IsInWorld() || player->InBattleground() || player->InArena() || !player->InBattlegroundQueue() || player->GetMap()->IsDungeon())
		return true;

	if (sPlayerBotMgr->DynamicBotsEnabled() && sPlayerBotMgr->IsDynamicBotPartyProtected(player)) return true;
	LogoutPlayer(false, "playerbot");
	return true;
}

bool PlayerBotSession::ProcessEnterAA(BotGlobleSchedule& schedule)
{
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	if (!player->IsInWorld())
		return false;
	if (player->GetMap()->IsDungeon())
	{
		BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_OutAAQueue, 0);
		schedule1.parameter1 = schedule.parameter1;
		schedule1.parameter2 = schedule.parameter2;
		schedule1.parameter3 = schedule.parameter3;
		ClearAllSchedule();
		PushScheduleToQueue(schedule1);
		return false;
	}
	if (player->InBattleground() || player->InArena())
		return true;

	if (player->InBattlegroundQueue())
	{
		for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
		{
			uint8 bgQueueTypeId = player->GetBattlegroundQueueTypeId(i);
			if (!bgQueueTypeId)
				continue;
			if (player->IsInvitedForBattlegroundQueueType(bgQueueTypeId))
			{
				PlayerBotMgr::SwitchPlayerBotAI(player, PlayerBotAIType::PBAIT_ARENA, true);

                WorldPacket cmd(CMSG_BATTLEFIELD_PORT);
                WorldPackets::Battleground::Port packet(std::move(cmd));
                packet.Ticket.Id = schedule.parameter1;
                packet.Ticket.RequesterGuid = schedule.playerGUID;
                packet.Ticket.Type = WorldPackets::LFG::RideType::Battlegrounds;
                packet.Ticket.Time = time(0);
                packet.AcceptedInvite = true;
                HandleBattleFieldPort(packet);
				break;
			}
			else
			{
				Battleground* bg_template = sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundTypeId(schedule.parameter1));
				if (!bg_template)
					continue;
				BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
				PVPDifficultyEntry const* bracketEntry = DB2Manager::GetBattlegroundBracketById(bg_template->GetMapId(), uint8(schedule.parameter2));
				if (bgQueue.ExistRealPlayer(bracketEntry, (schedule.parameter4 != 0) ? true : false))
					continue;
				BotGlobleSchedule schedule1(BotGlobleScheduleType::BGSType_OutAAQueue, 0);
				schedule1.parameter1 = schedule.parameter1;
				schedule1.parameter2 = schedule.parameter2;
				schedule1.parameter3 = schedule.parameter3;
				ClearAllSchedule();
				PushScheduleToQueue(schedule1);
				return false;
			}
		}
	}
	return false;
}

bool PlayerBotSession::ProcessLeaveAA(BotGlobleSchedule& schedule)
{
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	if (!player->InBattleground())
		return true;

	PlayerBotMgr::SwitchPlayerBotAI(player, PlayerBotAIType::PBAIT_FIELD, true);
	//uint8 un8 = 0;
	//uint16 un16 = 0;
	//uint32 un32 = 0;
	//WorldPacket opcode(1);
	//opcode << un8;
	//opcode << un8;
	//opcode << un32;
	//opcode << un16;
	//HandleBattlefieldLeaveOpcode(opcode);
	//HandleMoveWorldportAckOpcode();
    WorldPackets::Battleground::NullCmsg cmd(std::move(WorldPacket()));
    HandleLeaveBattlefield(cmd);
    //HandleWorldPortAck();
	return false;
}

bool PlayerBotSession::ProcessDelayLevelup(BotGlobleSchedule& schedule)
{
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	player->OnLevelupToBotAI();
	return true;
}

bool PlayerBotSession::ProcessInLFGQueue(BotGlobleSchedule& schedule)
{
	if (PlayerLoading())
		return false;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}
	if (player->isUsingLfg())
		return true;
    // FELSORIA_BOT_RDF_INVALID_V1: never retain a failed demand reservation.
    if ((schedule.parameter1 != 2 && schedule.parameter1 != 4 && schedule.parameter1 != 8) ||
        schedule.parameter2 > 3 || schedule.parameter2 == 0)
    {
        TC_LOG_ERROR(LOG_FILTER_LFG, "RDF_BOT_INVALID_JOB account=%u role=%u count=%u",
            GetAccountId(), uint32(schedule.parameter1), uint32(schedule.parameter2));
        sPlayerBotMgr->OnDynamicBotQueueFailed(GetAccountId());
        return true;
    }
	if (!player->IsSettingFinish())
		return false;
	if ((uint8(player->GetSpecializationRoleMaskForGroup()) & schedule.parameter1) != schedule.parameter1)
	{
		TC_LOG_ERROR(LOG_FILTER_LFG, "Dynamic RDF bot %u has spec role %u, requested %u; refusing wrong-role join",
			player->GetGUIDLow(), uint32(player->GetSpecializationRoleMaskForGroup()), schedule.parameter1);
		sPlayerBotMgr->OnDynamicBotQueueFailed(GetAccountId());
		return true;
	}

	//WorldPacket cmd(1);
	//cmd << schedule.parameter1;
	//cmd << uint16(0);
	//cmd << uint8(schedule.parameter2);
	//if (schedule.parameter2 >= 1)
	//	cmd << schedule.parameter3;
	//if (schedule.parameter2 >= 2)
	//	cmd << schedule.parameter4;
	//if (schedule.parameter2 >= 3)
	//	cmd << schedule.parameter5;
	//cmd << uint32(0);
	//cmd << "";
    lfg::LfgDungeonSet dungeons;
    if (schedule.parameter2 >= 1)
        dungeons.insert(schedule.parameter3);
    if (schedule.parameter2 >= 2)
        dungeons.insert(schedule.parameter4);
    if (schedule.parameter2 >= 3)
        dungeons.insert(schedule.parameter5);

    sLFGMgr->JoinLfg(player, uint8(schedule.parameter1), dungeons);
    // FELSORIA_BOT_RDF_JOIN_V1: JoinLfg returns void even on rejection.
    // Accepted solo/group joins publish HasQueue synchronously before returning.
    if (!player->isUsingLfg())
    {
        TC_LOG_ERROR(LOG_FILTER_LFG, "RDF_BOT_JOIN_REJECTED account=%u guid=%u role=%u dungeon=%u",
            GetAccountId(), player->GetGUIDLow(), uint32(schedule.parameter1),
            uint32(schedule.parameter3));
        sPlayerBotMgr->OnDynamicBotQueueFailed(GetAccountId());
    }
    return true;
}

bool PlayerBotSession::ProcessOutLFGQueue(BotGlobleSchedule& schedule)
{
	//if (PlayerLoading())
	//	return false;
	//Player* player = GetPlayer();
	//if (!player)
	//{
	//	ClearAllSchedule();
	//	return false;
	//}
	//if (!player->isUsingLfg())
	//	return true;
	//HandleLfgLeaveOpcode(WorldPacket(1));
	return true;
}

bool PlayerBotSession::ProcessAcceptLFGProposal(BotGlobleSchedule& schedule)
{
	//if (PlayerLoading())
	//	return false;
	//if (schedule.parameter1 == 0)
	//	return true;
	//Player* player = GetPlayer();
	//if (!player)
	//{
	//	ClearAllSchedule();
	//	return false;
	//}
	//if (!player->isUsingLfg())
	//	return true;

	//sLFGMgr->UpdateProposal(schedule.parameter1, player->GetGUID(), (schedule.parameter2 != 0) ? true : false);
	return true;
}

bool PlayerBotSession::ProcessOfferPetitionSign(BotGlobleSchedule& schedule)
{
	if (PlayerLoading())
		return false;
	if (schedule.parameter1 == 0 && schedule.parameter2 == 0)
		return true;
	Player* player = GetPlayer();
	if (!player)
	{
		ClearAllSchedule();
		return false;
	}

	uint64 signGUID = uint64(schedule.parameter1);
	uint64 highID = uint64(schedule.parameter2) << 32;
	signGUID |= highID;
	//WorldPacket cmd(1);
	//cmd << ObjectGuid(signGUID);
	//cmd << uint8(1);
	//HandlePetitionSignOpcode(cmd);
	return true;
}
