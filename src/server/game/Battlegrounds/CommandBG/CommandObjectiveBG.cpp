#include "CommandObjectiveBG.h"
#include "FelsoriaObjectivePolicy.h"
#include "BattlegroundTwinPeaks.h"
#include "BattlegroundKotmoguTemplate.h"
#include "BattlegroundMap.h"
#include "BotAI.h"
#include "GameObject.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include <cmath>
#include <limits>

bool CommandObjectiveBG::Supports(Battleground const* bg)
{
    if (!bg || bg->IsArena() || bg->IsRated() || bg->IsRBG() || bg->IsWargame() || bg->IsBrawl()) return false;
    auto type = bg->GetTypeID(true);
    return type == MS::Battlegrounds::BattlegroundTypeId::BattlegroundTwinPeaks ||
        type == MS::Battlegrounds::BattlegroundTypeId::BattlegroundKotmoguTemplate;
}

bool CommandObjectiveBG::Eligible(Player* player) const
{
    return player && Supports(m_pBattleground) && m_pBattleground->GetStatus() == STATUS_IN_PROGRESS &&
        player->IsPlayerBot() && player->IsInWorld() && player->isAlive() &&
        player->GetBattleground() == m_pBattleground && player->GetMap() == m_pBattleground->GetBgMap() &&
        player->GetBGTeamId() == m_TeamID && m_PlayerGUIDs.count(player->GetGUIDLow());
}

std::vector<Player*> CommandObjectiveBG::LivingBots()
{
    std::vector<Player*> result;
    for (auto const& entry : m_PlayerGUIDs)
        if (Player* player = GetBGPlayer(entry.first))
            if (Eligible(player)) result.push_back(player);
    return result;
}

Player* CommandObjectiveBG::Carrier(ObjectGuid guid) const
{
    Player* player = guid.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(guid);
    return player && player->IsInWorld() && player->isAlive() &&
        player->GetBattleground() == m_pBattleground && player->GetMap() == m_pBattleground->GetBgMap() ? player : nullptr;
}

bool CommandObjectiveBG::Walk(Player* player, Position const& goal, float stop)
{
    if (BotBGAI* ai = GetBotBGAI(player->GetGUIDLow()))
    {
        if (player->GetExactDist(goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ()) <= stop)
            return false;
        if (!player->isInCombat() && CanUpMount(player) && ai->TryUpMount()) return true;
        if (ai->GetAIMovement()->MoveObjectiveTo(goal, stop))
        {
            player->AttackStop();
            player->SetSelection(ObjectGuid::Empty);
            return true;
        }
    }
    return false;
}

bool CommandObjectiveBG::Follow(Player* player, Player* target, bool attack)
{
    if (!target || target == player) return false;
    if (attack)
    {
        if (target->GetBGTeamId() == m_TeamID || !player->IsValidAttackTarget(target)) return false;
        if (player->GetDistance(target) < BOTAI_SEARCH_RANGE && player->canSeeOrDetect(target, false, true))
        {
            player->SetSelection(target->GetGUID());
            return false; // Let the class rotation fight the actual carrier.
        }
        return Walk(player, target->GetPosition(), 5.0f);
    }
    if (target->GetBGTeamId() != m_TeamID) return false;
    return Walk(player, target->GetPosition(), 15.0f); // Combat and healing remain available nearby.
}

bool CommandObjectiveBG::Use(Player* player, GameObject* object)
{
    if (!object || !object->isSpawned() || object->GetMap() != player->GetMap() ||
        player->GetDistance(object) > 4.0f || !player->IsWithinLOSInMap(object)) return false;
    BotBGAI* ai = GetBotBGAI(player->GetGUIDLow());
    if (!ai || !ai->CanUseBGObject()) return false;
    ai->Dismount();
    object->Use(player); // The native BG validates ownership, state, credit and scoring.
    return true;
}

bool CommandTP::UpdateBot(Player* player)
{
    if (!Eligible(player)) return false;
    auto* bg = dynamic_cast<BattlegroundTwinPeaks*>(m_pBattleground);
    if (!bg) return false;
    TeamId enemy = m_TeamID == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE;
    bool carrying = bg->GetFlagPickerGUID(enemy) == player->GetGUID();
    if (carrying) if (BotBGAI* ai = GetBotBGAI(player->GetGUIDLow())) ai->Dismount();
    std::vector<Player*> bots = LivingBots();
    auto bot = std::find(bots.begin(), bots.end(), player);
    if (bot == bots.end()) return false;
    bool ownDropped = bg->GetFlagState(m_TeamID) == BG_TP_FLAG_STATE_ON_GROUND;
    Player* attacker = Carrier(bg->GetFlagPickerGUID(m_TeamID));
    Player* escort = Carrier(bg->GetFlagPickerGUID(enemy));
    bool available = bg->GetFlagState(enemy) == BG_TP_FLAG_STATE_ON_BASE ||
        bg->GetFlagState(enemy) == BG_TP_FLAG_STATE_ON_GROUND;
    auto task = FelsoriaObjectives::PlanTwin(carrying, ownDropped, attacker != nullptr,
        escort != nullptr, available, unsigned(bot - bots.begin()), unsigned(bots.size()));
    if (task == FelsoriaObjectives::TwinTask::Intercept) return Follow(player, attacker, true);
    if (task == FelsoriaObjectives::TwinTask::Escort) return Follow(player, escort, false);

    TeamId flagTeam = task == FelsoriaObjectives::TwinTask::TakeFlag ? enemy : m_TeamID;
    GameObject* object = bg->GetBGObject(BG_TP_OBJECT_A_FLAG + flagTeam);
    if (task == FelsoriaObjectives::TwinTask::ReturnFlag ||
        (task == FelsoriaObjectives::TwinTask::TakeFlag && bg->GetFlagState(enemy) == BG_TP_FLAG_STATE_ON_GROUND))
        object = player->GetMap()->GetGameObject(bg->GetDroppedFlagGUID(flagTeam));
    if (!object) return false;
    if (task == FelsoriaObjectives::TwinTask::ReturnFlag || task == FelsoriaObjectives::TwinTask::TakeFlag)
        if (Use(player, object)) return true;
    return Walk(player, object->GetPosition(), task == FelsoriaObjectives::TwinTask::Defend ? 15.0f : 1.0f);
}

bool CommandTP::CanUpMount(Player* player)
{
    auto* bg = dynamic_cast<BattlegroundTwinPeaks*>(m_pBattleground);
    return bg && player && bg->GetFlagPickerGUID(TEAM_ALLIANCE) != player->GetGUID() &&
        bg->GetFlagPickerGUID(TEAM_HORDE) != player->GetGUID();
}

Creature const* CommandTP::GetMatchGraveyardNPC(Player const* player)
{
    if (!player || player->GetBattleground() != m_pBattleground || player->GetBGTeamId() != m_TeamID) return nullptr;
    Creature const* start = m_pBattleground->GetBGCreature(TP_SPIRIT_ALLIANCE + m_TeamID);
    Creature const* middle = m_pBattleground->GetBGCreature(TP_SPIRIT_MIDDLE_ALLIANCE + m_TeamID);
    return !start || (middle && player->GetDistance(middle) < player->GetDistance(start)) ? middle : start;
}

void CommandKT::Update(uint32 diff)
{
    CommandBG::Update(diff);
    if (!Supports(m_pBattleground) || m_pBattleground->GetStatus() != STATUS_IN_PROGRESS)
    { _orbAssignments.clear(); _assignmentTimer = 0; return; }
    if (_assignmentTimer > diff) { _assignmentTimer -= diff; return; }
    AssignOrbs();
    _assignmentTimer = 1000;
}

void CommandKT::AssignOrbs()
{
    _orbAssignments.clear();
    auto* bg = dynamic_cast<BattlegroundKotmoguTemplate*>(m_pBattleground);
    if (!bg) return;
    auto bots = LivingBots();
    std::array<bool, 4> free = {{false, false, false, false}};
    for (unsigned orb = 0; orb < MAX_ORBS; ++orb)
        if (GameObject* object = bg->GetBGObject(BG_KT_OBJECT_ORB_GREEN + orb))
            free[orb] = bg->GetFlagPickerGUID(orb).IsEmpty() && object->isSpawned();
    std::vector<std::array<float, 4>> costs(bots.size());
    for (std::size_t bot = 0; bot < bots.size(); ++bot)
        for (unsigned orb = 0; orb < MAX_ORBS; ++orb)
        {
            float distance = bots[bot]->GetExactDist(BG_KT_OrbPositions[orb][0],
                BG_KT_OrbPositions[orb][1], BG_KT_OrbPositions[orb][2]);
            costs[bot][orb] = CanUpMount(bots[bot]) ? distance * distance : std::numeric_limits<float>::infinity();
        }
    auto assignments = FelsoriaObjectives::AssignOrbs(costs, free);
    for (std::size_t bot = 0; bot < bots.size(); ++bot)
        if (assignments[bot] >= 0) _orbAssignments.emplace(bots[bot]->GetGUIDLow(), unsigned(assignments[bot]));
}

bool CommandKT::ScoringPosition(Player* player, Position& result, unsigned slot)
{
    // Native scoring bounds are in BattlegroundKotmoguTemplate::CheckOrbKeepersPosition.
    float x = (1750.27f + 1816.04f) * 0.5f + (slot % 2 ? 7.0f : -7.0f);
    float y = (1287.34f + 1379.34f) * 0.5f + (slot / 2 % 2 ? 7.0f : -7.0f);
    float z = player->GetMap()->GetHeight(player->GetPhases(), x, y, BG_KT_OrbPositions[0][2] + 2.0f, true, 30.0f);
    if (!std::isfinite(z) || z <= INVALID_HEIGHT || z >= 10.0f) return false;
    result.Relocate(x, y, z);
    return true;
}

bool CommandKT::UpdateBot(Player* player)
{
    if (!Eligible(player)) return false;
    auto* bg = dynamic_cast<BattlegroundKotmoguTemplate*>(m_pBattleground);
    if (!bg) return false;
    Player* enemy = nullptr;
    Player* friendly = nullptr;
    for (unsigned orb = 0; orb < MAX_ORBS; ++orb)
    {
        Player* holder = Carrier(bg->GetFlagPickerGUID(orb));
        if (holder == player)
        {
            if (BotBGAI* ai = GetBotBGAI(player->GetGUIDLow())) ai->Dismount();
            Position center;
            return ScoringPosition(player, center, orb) && Walk(player, center, 2.0f);
        }
        if (!holder) continue;
        Player*& target = holder->GetBGTeamId() == m_TeamID ? friendly : enemy;
        if (!target || player->GetDistance(holder) < player->GetDistance(target)) target = holder;
    }
    auto assigned = _orbAssignments.find(player->GetGUIDLow());
    if (assigned != _orbAssignments.end() && bg->GetFlagPickerGUID(assigned->second).IsEmpty())
        if (GameObject* object = bg->GetBGObject(BG_KT_OBJECT_ORB_GREEN + assigned->second))
            if (object->isSpawned()) return Use(player, object) || Walk(player, object->GetPosition());
    if (enemy) return Follow(player, enemy, true);
    if (friendly) return Follow(player, friendly, false);
    Position center;
    return ScoringPosition(player, center, unsigned(player->GetGUIDLow() % 4)) && Walk(player, center, 12.0f);
}

bool CommandKT::CanUpMount(Player* player)
{
    auto* bg = dynamic_cast<BattlegroundKotmoguTemplate*>(m_pBattleground);
    if (!bg || !player) return false;
    for (unsigned orb = 0; orb < MAX_ORBS; ++orb)
        if (bg->GetFlagPickerGUID(orb) == player->GetGUID()) return false;
    return true;
}

Creature const* CommandKT::GetMatchGraveyardNPC(Player const* player)
{
    return player && player->GetBattleground() == m_pBattleground && player->GetBGTeamId() == m_TeamID ?
        m_pBattleground->GetBGCreature(BG_KT_CREATURE_SPIRIT_1 + m_TeamID) : nullptr;
}
