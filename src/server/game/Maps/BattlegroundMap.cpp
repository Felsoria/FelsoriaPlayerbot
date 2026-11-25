#include "BattlegroundMap.h"
#include "ObjectMgr.h"
#include "Map.h"
#include "MapManager.h"
#include "PlayerBotMgr.h"
#include "BotAITool.h"
#include "CommandBG/CommandAB.h"
#include "CommandBG/CommandBFG.h"
#include "CommandBG/CommandAV.h"
#include "CommandBG/CommandEY.h"
#include "CommandBG/CommandIC.h"
#include "CommandBG/CommandWS.h"
#include "CommandBG/CommandObjectiveBG.h"

namespace
{
    // CommandBG's native destructor is not virtual. Delete through the actual
    // derived type so commanders release their route/task containers safely.
    void DestroyCommander(CommandBG*& commander)
    {
        if(auto*p=dynamic_cast<CommandObjectiveBG*>(commander))delete p;
        else if(auto*p=dynamic_cast<CommandBFG*>(commander))delete p;
        else if(auto*p=dynamic_cast<CommandAB*>(commander))delete p;
        else if(auto*p=dynamic_cast<CommandAV*>(commander))delete p;
        else if(auto*p=dynamic_cast<CommandEY*>(commander))delete p;
        else if(auto*p=dynamic_cast<CommandIC*>(commander))delete p;
        else if(auto*p=dynamic_cast<CommandWS*>(commander))delete p;
        commander=nullptr;
    }
    bool IsFelsoriaNormalWSG(Battleground const* bg)
    {
        return bg && bg->GetMapId() == 489 && !bg->IsArena() &&
            !bg->IsRated() && !bg->IsWargame() && !bg->IsBrawl();
    }
    bool IsFelsoriaNormalAV(Battleground const* bg)
    {return bg && bg->GetMapId()==30 && !bg->IsArena() && !bg->IsRated() && !bg->IsWargame() && !bg->IsBrawl();}

    bool IsFelsoriaNormalBFG(Battleground const* bg)
    {return bg && bg->GetMapId()==761 && !bg->IsArena() && !bg->IsRated() && !bg->IsWargame() && !bg->IsBrawl();}

    bool IsFelsoriaNormalAB(Battleground const* bg)
    {return bg && bg->GetMapId()==529 && !bg->IsArena() && !bg->IsRated() && !bg->IsWargame() && !bg->IsBrawl();}

}

BattlegroundMap::BattlegroundMap(uint32 id, time_t expiry, uint32 instanceId, Map* parent, Difficulty difficulty) : Map(id, expiry, instanceId, difficulty, parent)
{
    m_bg = nullptr;

    BattlegroundMap::InitVisibilityDistance();

    if (auto _distMap = GetVisibleDistance(TYPE_VISIBLE_MAP, id))
        m_VisibleDistance = _distMap;
}

BattlegroundMap::~BattlegroundMap()
{
    auto lifetime=LockBattlegroundLifetime();
    if(auto*commander=dynamic_cast<CommandBFG*>(m_pAllianceCommander))commander->DetachBattleground();
    if(auto*commander=dynamic_cast<CommandBFG*>(m_pHordeCommander))commander->DetachBattleground();
    DestroyCommander(m_pAllianceCommander);
    DestroyCommander(m_pHordeCommander);
    if (!m_bg)
        return;

    m_bg->SetBgMap(nullptr);
    m_bg = nullptr;
}

void BattlegroundMap::InitVisibilityDistance()
{
    m_VisibleDistance = GetEntry()->IsBattleArena() ? sWorld->GetMaxVisibleDistanceInArenas() : sWorld->GetMaxVisibleDistanceInBG();
    m_VisibilityNotifyPeriod = World::GetVisibilityNotifyPeriodInBGArenas();
}

void BattlegroundMap::UpdateSessions(const uint32 diff)
{
    auto lifetime=LockBattlegroundLifetime();
    Map::UpdateSessions(diff);
}

void BattlegroundMap::Update(const uint32 diff)
{
    auto lifetime=LockBattlegroundLifetime();
    // FELSORIA_BG_LEAVE_V1: player teleports run from Map::Update. The last
    // participant can already be removed from the BG roster while still on
    // this map, waiting for TeleportTo's 100 ms event. Keep servicing the map
    // after the BG is marked for deletion or detached; only commanders stop.
    if(m_bg && !m_bg->ToBeDeleted() && IsFelsoriaNormalBFG(m_bg))
        InsureCommander(m_bg->GetTypeID(true));
    Map::Update(diff);
    if(!m_bg||m_bg->ToBeDeleted())return;
    if (m_pAllianceCommander)
        m_pAllianceCommander->Update(diff);
    if (m_pHordeCommander)
        m_pHordeCommander->Update(diff);
}

void BattlegroundMap::InsureCommander(uint16 bgType)
{
    auto lifetime=LockBattlegroundLifetime();
    if(!m_bg)return;
    // The random queue stores its wrapper and the selected battleground as separate IDs.
    bool const normalRandom = bgType == MS::Battlegrounds::BattlegroundTypeId::BattlegroundRandom &&
        !m_bg->IsArena() && !m_bg->IsRated() && !m_bg->IsWargame() && !m_bg->IsBrawl();
    if (normalRandom || IsFelsoriaNormalWSG(m_bg) || IsFelsoriaNormalAB(m_bg) ||
        IsFelsoriaNormalAV(m_bg) || IsFelsoriaNormalBFG(m_bg) || CommandObjectiveBG::Supports(m_bg))
        bgType = m_bg->GetTypeID(true);

    if (!m_pAllianceCommander)
    {
        switch (bgType)
        {
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundTwinPeaks:
                if (CommandObjectiveBG::Supports(m_bg)) m_pAllianceCommander = new CommandTP(m_bg, TEAM_ALLIANCE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundKotmoguTemplate:
                if (CommandObjectiveBG::Supports(m_bg)) m_pAllianceCommander = new CommandKT(m_bg, TEAM_ALLIANCE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundBattleForGilneas:
                if(IsFelsoriaNormalBFG(m_bg)) m_pAllianceCommander = new CommandBFG(m_bg, TeamId::TEAM_ALLIANCE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin:
                m_pAllianceCommander = new CommandAB(m_bg, TeamId::TEAM_ALLIANCE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundWarsongGulch:
                m_pAllianceCommander = new CommandWS(m_bg, TeamId::TEAM_ALLIANCE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundEyeOfTheStorm:
                m_pAllianceCommander = new CommandEY(m_bg, TeamId::TEAM_ALLIANCE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundAlteracValley:
                m_pAllianceCommander = new CommandAV(m_bg, TeamId::TEAM_ALLIANCE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundIsleOfConquest:
                m_pAllianceCommander = new CommandIC(m_bg, TeamId::TEAM_ALLIANCE);
                break;
        }
    }
    if (!m_pHordeCommander)
    {
        switch (bgType)
        {
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundTwinPeaks:
                if (CommandObjectiveBG::Supports(m_bg)) m_pHordeCommander = new CommandTP(m_bg, TEAM_HORDE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundKotmoguTemplate:
                if (CommandObjectiveBG::Supports(m_bg)) m_pHordeCommander = new CommandKT(m_bg, TEAM_HORDE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundBattleForGilneas:
                if(IsFelsoriaNormalBFG(m_bg)) m_pHordeCommander = new CommandBFG(m_bg, TeamId::TEAM_HORDE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundArathiBasin:
                m_pHordeCommander = new CommandAB(m_bg, TeamId::TEAM_HORDE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundWarsongGulch:
                m_pHordeCommander = new CommandWS(m_bg, TeamId::TEAM_HORDE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundEyeOfTheStorm:
                m_pHordeCommander = new CommandEY(m_bg, TeamId::TEAM_HORDE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundAlteracValley:
                m_pHordeCommander = new CommandAV(m_bg, TeamId::TEAM_HORDE);
                break;
            case MS::Battlegrounds::BattlegroundTypeId::BattlegroundIsleOfConquest:
                m_pHordeCommander = new CommandIC(m_bg, TeamId::TEAM_HORDE);
                break;
        }
    }
}

void BattlegroundMap::InitCommander()
{
    auto lifetime=LockBattlegroundLifetime();
    if(!m_bg)return;
    if (m_pAllianceCommander)
        m_pAllianceCommander->Initialize();
    if (m_pHordeCommander)
        m_pHordeCommander->Initialize();
}

void BattlegroundMap::ResetCommander()
{
    auto lifetime=LockBattlegroundLifetime();
    if(!m_bg)return;
    if (m_pAllianceCommander)
        m_pAllianceCommander->UpdateBelongBattleground(m_bg);
    if (m_pHordeCommander)
        m_pHordeCommander->UpdateBelongBattleground(m_bg);
}

void BattlegroundMap::ReadyCommander()
{
    auto lifetime=LockBattlegroundLifetime();
    if(!m_bg)return;
    if (m_pAllianceCommander)
        m_pAllianceCommander->ReadyGame();
    if (m_pHordeCommander)
        m_pHordeCommander->ReadyGame();
}

void BattlegroundMap::StartCommander()
{
    auto lifetime=LockBattlegroundLifetime();
    if(!m_bg)return;
    if (m_pAllianceCommander)
        m_pAllianceCommander->StartGame();
    if (m_pHordeCommander)
        m_pHordeCommander->StartGame();
}

CommandBG* BattlegroundMap::GetCommander(TeamId team)
{
    auto lifetime=LockBattlegroundLifetime();
    if(!m_bg)return nullptr;
    if (team == TEAM_ALLIANCE)
        return m_pAllianceCommander;
    else if (team == TEAM_HORDE)
        return m_pHordeCommander;
    return NULL;
}


Battleground* BattlegroundMap::GetBG()
{
    auto lifetime=LockBattlegroundLifetime();
    return m_bg;
}

void BattlegroundMap::SetBG(Battleground* bg)
{
    auto lifetime=LockBattlegroundLifetime();
    if(m_bg==bg)return;
    if(auto*commander=dynamic_cast<CommandBFG*>(m_pAllianceCommander))commander->DetachBattleground();
    if(auto*commander=dynamic_cast<CommandBFG*>(m_pHordeCommander))commander->DetachBattleground();
    if(bg){DestroyCommander(m_pAllianceCommander);DestroyCommander(m_pHordeCommander);}
    m_bg=bg;
}

bool BattlegroundMap::CanEnter(Player* player)
{
    if (player->GetMapRef().getTarget() == this)
    {
        TC_LOG_ERROR(LOG_FILTER_MAPS, "BGMap::CanEnter - player %u is already in map!", player->GetGUIDLow());
        ASSERT(false);
        return false;
    }

    if (player->GetBattlegroundId() != GetInstanceId())
        return false;

    return Map::CanEnter(player);
}

bool BattlegroundMap::AddPlayerToMap(Player* player, bool initPlayer /*= true*/)
{
    auto lifetime=LockBattlegroundLifetime();
    if (auto bg = player->GetBattleground())
        bg->OnPlayerEnter(player);

    player->AddDelayedEvent(10, [player]() -> void
    {
        player->OnEnterMap(); // UpdatePhase
    });

    player->m_InstanceValid = true;
    bool ret = Map::AddPlayerToMap(player, initPlayer);
    if (ret && player->IsPlayerBot())
    {
        if ((IsFelsoriaNormalWSG(m_bg) || IsFelsoriaNormalAB(m_bg) || IsFelsoriaNormalAV(m_bg) || IsFelsoriaNormalBFG(m_bg)))
            InsureCommander(m_bg->GetTypeID(true));
        if (((IsFelsoriaNormalWSG(m_bg) || IsFelsoriaNormalAB(m_bg) || IsFelsoriaNormalAV(m_bg) || IsFelsoriaNormalBFG(m_bg)) ? player->GetBGTeamId() : player->GetTeamId()) == TEAM_ALLIANCE && m_pAllianceCommander)
        {
            m_pAllianceCommander->AddPlayerBot(player, m_bg);
        }
        else if (((IsFelsoriaNormalWSG(m_bg) || IsFelsoriaNormalAB(m_bg) || IsFelsoriaNormalAV(m_bg) || IsFelsoriaNormalBFG(m_bg)) ? player->GetBGTeamId() : player->GetTeamId()) == TEAM_HORDE && m_pHordeCommander)
        {
            m_pHordeCommander->AddPlayerBot(player, m_bg);
        }
    }

    return ret;
}

void BattlegroundMap::RemovePlayerFromMap(Player* player, bool remove)
{
    auto lifetime=LockBattlegroundLifetime();
    if (player->IsPlayerBot())
    {
        PlayerBotMgr::SwitchPlayerBotAI(player, PlayerBotAIType::PBAIT_FIELD, true);
        if (((IsFelsoriaNormalWSG(m_bg) || IsFelsoriaNormalAB(m_bg) || IsFelsoriaNormalAV(m_bg) || IsFelsoriaNormalBFG(m_bg)) ? player->GetBGTeamId() : player->GetTeamId()) == TEAM_ALLIANCE && m_pAllianceCommander)
        {
            m_pAllianceCommander->RemovePlayerBot(player);
        }
        else if (((IsFelsoriaNormalWSG(m_bg) || IsFelsoriaNormalAB(m_bg) || IsFelsoriaNormalAV(m_bg) || IsFelsoriaNormalBFG(m_bg)) ? player->GetBGTeamId() : player->GetTeamId()) == TEAM_HORDE && m_pHordeCommander)
        {
            m_pHordeCommander->RemovePlayerBot(player);
        }
    }

    if (player && player->IsSpectator() && !player->IsSpectateCanceled())
        if (GetBG())
            GetBG()->RemoveSpectator(player);

#ifdef TRINITY_DEBUG
    TC_LOG_INFO(LOG_FILTER_MAPS, "MAP: Removing player '%s' from bg '%u' of map '%s' before relocating to another map", player->GetName(), GetInstanceId(), GetMapName());
#endif
    Map::RemovePlayerFromMap(player, remove);
}

void BattlegroundMap::SetUnload()
{
    m_unloadTimer = MIN_UNLOAD_DELAY;
}

void BattlegroundMap::RemoveAllPlayers()
{
    auto lifetime=LockBattlegroundLifetime();
    if (!HavePlayers())
        return;

    for (auto itr = m_mapRefManager.begin(); itr != m_mapRefManager.end();)
    {
        // SafeTeleport can remove this map reference immediately.
        auto player = itr->getSource();
        ++itr;
        if (!player || player->IsBeingTeleportedFar())
            continue;

        auto const& entry = player->GetBattlegroundEntryPoint();
        if (!MapManager::IsValidMapCoord(entry.GetMapId(), entry.GetPositionX(),
            entry.GetPositionY(), entry.GetPositionZ(), entry.GetOrientation()))
        {
            TC_LOG_ERROR(LOG_FILTER_MAPS, "FELSORIA_BG_LEAVE_V1: invalid return location for player %u on BG map %u instance %u.",
                player->GetGUIDLow(), GetId(), GetInstanceId());
            continue;
        }

        if (player->IsSpectator())
            player->SetSpectateRemoving(true);

        // An unloading map may no longer receive Map::Update. Do not queue a
        // player-owned event here: start the core transfer while it is alive.
        if (!player->SafeTeleport(entry))
            TC_LOG_ERROR(LOG_FILTER_MAPS, "FELSORIA_BG_LEAVE_V1: return transfer failed for player %u on BG map %u instance %u.",
                player->GetGUIDLow(), GetId(), GetInstanceId());
    }
}


