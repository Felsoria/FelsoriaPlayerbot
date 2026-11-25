
#include "BotBGAIMovement.h"
#include "CommandBG/FelsoriaGilneasWaterTraversal.h"
#include "CommandBG/FelsoriaGilneasGroundPolicy.h"
#include "CommandBG/FelsoriaGilneasExitRoutes.h"
#include "PathfindingMgr.h"
#include "CommandBG.h"
#include "CommandBG/CommandAV.h"
#include "CommandBG/CommandBFG.h"
#include "CommandBG/CommandObjectiveBG.h"
#include "BattlegroundWarsongGulch.h"
#include "BattlegroundAlteracValley.h"
#include "FelsoriaAVSafety.h"
#include "DB2Structure.h"
#include "WorldSession.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "VMapFactory.h"
#include "PlayerBotMgr.h"
#include "BotAIHumanTrail.h"
#include "BotActivityPolicy.h"
#include <cmath>
#include <algorithm>
#include "MovementTypedefs.h"
#include <mutex>
#include <map>
#include <deque>
#include <limits>
#include <set>

namespace {
static bool IsWSGFlagCarrier(Player* player);
bool FelsoriaAVMovementAllowed(Player* player,float x,float y,float z)
{
 if(!player||player->GetMapId()!=30||!player->GetBattleground()||
    player->GetBattleground()->GetStatus()!=STATUS_IN_PROGRESS)return true;
 auto*commander=dynamic_cast<CommandAV*>(player->GetMap()->GetCommander(player->GetBGTeamId()));
 return !commander||commander->CanCombatMove(player,x,y,z);
}
}

namespace BotAIHumanTrail
{
    struct Trail
    {
        uint32 map = 0, phase = 0, instance = 0, updated = 0, lastPoint = 0;
        uint32 revision = 0;
        std::set<uint32> phaseIds;
        std::deque<G3D::Vector3> points;
    };
    struct Cursor
    {
        uint32 owner = 0, revision = 0, updated = 0;
        size_t index = 0;
        int direction = 1;
    };
    static std::mutex trailLock;
    static std::map<uint32, Trail> trails;
    static std::map<uint32, Cursor> cursors;

    // Call with trailLock held so a new trail for the same player cannot
    // inherit a bot cursor from an invalidated sample.
    static void ForgetTrail(uint32 ownerId)
    {
        trails.erase(ownerId);
        for (auto it = cursors.begin(); it != cursors.end(); )
            if (it->second.owner == ownerId)
                it = cursors.erase(it);
            else
                ++it;
    }

    void Record(Player const* owner)
    {
        if (!owner || !owner->GetSession() || owner->GetSession()->IsBotSession())
            return;
        uint32 const ownerId = owner->GetGUIDLow();
        if (!owner->IsInWorld() || !owner->GetMap() || !owner->isAlive() ||
            owner->IsBeingTeleported() || owner->InBattleground() ||
            owner->InArena() || owner->IsMounted() || owner->IsFlying() ||
            owner->isInFlight() || owner->IsInWater() || owner->GetVehicleBase() ||
            owner->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR))
        {
            std::lock_guard<std::mutex> guard(trailLock);
            ForgetTrail(ownerId);
            return;
        }
        G3D::Vector3 p = owner->GetVector3();
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        {
            std::lock_guard<std::mutex> guard(trailLock);
            ForgetTrail(ownerId);
            return;
        }
        float ground = owner->GetMap()->GetHeight(owner->GetPhases(), p.x, p.y, p.z + 2.0f);
        if (!std::isfinite(ground) || ground <= INVALID_HEIGHT || std::abs(ground - p.z) > 1.5f)
        {
            std::lock_guard<std::mutex> guard(trailLock);
            ForgetTrail(ownerId);
            return;
        }

        uint32 now = getMSTime();
        std::lock_guard<std::mutex> guard(trailLock);
        if (trails.size() >= 32 && !trails.count(ownerId))
        {
            auto oldest = trails.begin();
            for (auto it = trails.begin(); it != trails.end(); ++it)
                if (int32(it->second.updated - oldest->second.updated) < 0)
                    oldest = it;
            ForgetTrail(oldest->first);
        }
        Trail& t = trails[ownerId];
        if (t.map != owner->GetMapId() || t.phase != owner->GetPhaseMask() ||
            t.phaseIds != owner->GetPhases() || t.instance != owner->GetInstanceId() ||
            now - t.updated > 3000)
        {
            t.points.clear();
            ++t.revision;
        }
        t.map = owner->GetMapId();
        t.phase = owner->GetPhaseMask();
        t.phaseIds = owner->GetPhases();
        t.instance = owner->GetInstanceId();
        t.updated = now;
        if (!t.points.empty())
        {
            G3D::Vector3 const& last = t.points.back();
            float dx = p.x - last.x, dy = p.y - last.y;
            float distanceSq = dx * dx + dy * dy;
            // Teleports, jumps and missing samples cannot join walking edges.
            if (distanceSq > 144.0f || std::abs(p.z - last.z) > 2.5f ||
                now - t.lastPoint > 3000)
            {
                t.points.clear();
                ++t.revision;
            }
            else if (distanceSq < 4.0f)
            {
                // A standing player is still sampled: a later walking step
                // connects to the same observed ground position.
                t.lastPoint = now;
                return;
            }
        }
        t.points.push_back(p);
        t.lastPoint = now;
        while (t.points.size() > 80)
        {
            t.points.pop_front();
            ++t.revision;
        }
    }

    bool NextWalkingPoint(Player const* bot, float x, float y, float z, G3D::Vector3& next)
    {
        if (!bot || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return false;
        float const bx = bot->GetPositionX(), by = bot->GetPositionY(), bz = bot->GetPositionZ();
        uint32 now = getMSTime();
        std::lock_guard<std::mutex> guard(trailLock);
        auto const validTrail = [bot, now](Trail const& t)
        {
            return t.map == bot->GetMapId() && t.instance == bot->GetInstanceId() &&
                t.phase == bot->GetPhaseMask() && t.phaseIds == bot->GetPhases() &&
                now - t.updated <= 600000 && t.points.size() >= 3;
        };
        auto const safeLeg = [bx, by, bz](G3D::Vector3 const& p)
        {
            float const dx = p.x - bx, dy = p.y - by;
            return dx * dx + dy * dy <= 144.0f && std::abs(p.z - bz) <= 2.5f;
        };
        uint32 const botId = bot->GetGUIDLow();
        auto cursorIt = cursors.find(botId);
        if (cursorIt != cursors.end())
        {
            Cursor& cursor = cursorIt->second;
            auto trailIt = trails.find(cursor.owner);
            if (now - cursor.updated <= 600000 && trailIt != trails.end() &&
                validTrail(trailIt->second) && cursor.revision == trailIt->second.revision &&
                cursor.index < trailIt->second.points.size())
            {
                Trail const& t = trailIt->second;
                G3D::Vector3 const& point = t.points[cursor.index];
                float const dx = point.x - bx, dy = point.y - by;
                if (dx * dx + dy * dy <= 6.25f && std::abs(point.z - bz) <= 2.0f)
                {
                    // Patrol the same observed chain in reverse at its end.
                    if ((cursor.direction > 0 && cursor.index + 1 == t.points.size()) ||
                        (cursor.direction < 0 && cursor.index == 0))
                        cursor.direction = -cursor.direction;
                    cursor.index = size_t(int(cursor.index) + cursor.direction);
                }
                cursor.updated = now;
                if (safeLeg(t.points[cursor.index]))
                {
                    next = t.points[cursor.index];
                    return true;
                }
            }
            cursors.erase(cursorIt);
        }

        float best = std::numeric_limits<float>::max();
        Cursor selected;
        for (auto const& entry : trails)
        {
            Trail const& t = entry.second;
            if (!validTrail(t))
                continue;
            for (size_t i = 0; i < t.points.size(); ++i)
            {
                G3D::Vector3 const& anchor = t.points[i];
                float const dx = anchor.x - bx, dy = anchor.y - by;
                float const anchorSq = dx * dx + dy * dy;
                if (anchorSq > 64.0f || std::abs(anchor.z - bz) > 1.5f)
                    continue;
                for (int direction : { -1, 1 })
                {
                    int const nextIndex = int(i) + direction;
                    if (nextIndex < 0 || nextIndex >= int(t.points.size()))
                        continue;
                    G3D::Vector3 const& p = t.points[size_t(nextIndex)];
                    if (!safeLeg(p))
                        continue;
                    float const tx = p.x - x, ty = p.y - y;
                    float const score = anchorSq + (tx * tx + ty * ty) * 0.01f;
                    if (score < best)
                    {
                        best = score;
                        next = p;
                        selected.owner = entry.first;
                        selected.revision = t.revision;
                        selected.index = size_t(nextIndex);
                        selected.direction = direction;
                        selected.updated = now;
                    }
                }
            }
        }
        if (best == std::numeric_limits<float>::max())
            return false;
        if (cursors.size() >= 256 && !cursors.count(botId))
        {
            auto oldest = cursors.begin();
            for (auto it = cursors.begin(); it != cursors.end(); ++it)
                if (int32(it->second.updated - oldest->second.updated) < 0)
                    oldest = it;
            cursors.erase(oldest);
        }
        cursors[botId] = selected;
        return true;
    }
}

bool BotBGAIMovement::IsDynamicOutdoorBot() const
{
    return m_FieldAI && m_Player && m_Player->GetSession() &&
        sPlayerBotMgr->GetDynamicBotTargetLevel(m_Player->GetSession()->GetAccountId()) &&
        m_Player->IsInWorld() && m_Player->GetMap() &&
        !m_Player->GetGroup() &&
        (!m_Player->GetMap()->IsDungeon() ||
         PlayerBotMgr::GetDynamicAreaRequiredClass(m_Player->GetMapId(),
             m_Player->GetZoneId(), m_Player->GetAreaId()) == m_Player->getClass()) &&
        !m_Player->InBattleground() && !m_Player->InArena();
}

bool BotBGAIMovement::IsSafeDynamicOutdoorPath(Pathfinding const& path, float x, float y, float z) const
{
    PathType const type = path.GetPathType();
    if (!(type & PATHFIND_NORMAL) ||
        (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_NOT_USING_PATH |
                 PATHFIND_SHORTCUT | PATHFIND_SHORT)))
        return false;

    Movement::PointsArray const& points = path.GetPath();
    if (points.size() < 2 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;
    G3D::Vector3 const& end = path.GetActualEndPosition();
    if (!std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z) ||
        std::abs(end.x - x) > 2.0f || std::abs(end.y - y) > 2.0f ||
        std::abs(end.z - z) > 2.0f)
        return false;

    // A usable polygon alone does not guarantee a visible walking surface.
    // Check the route against the phase-aware surface at regular intervals;
    // missing ground and paths far below the terrain must never be splined.
    Map* map = m_Player->GetMap();
    for (size_t i = 0; i < points.size(); ++i)
    {
        G3D::Vector3 const& point = points[i];
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            return false;
        uint32 const zone = map->GetZoneId(point.x, point.y, point.z);
        uint32 const area = map->GetAreaId(point.x, point.y, point.z);
        uint8 const required = PlayerBotMgr::GetDynamicAreaRequiredClass(m_Player->GetMapId(), zone, area);
        if (zone != m_Player->GetZoneId() || (required && required != m_Player->getClass()) ||
            !PlayerBotMgr::IsDynamicRaceAllowedAt(m_Player->GetMapId(), zone, area,
                m_Player->getRace(), uint8(m_Player->GetTeamId())))
            return false;
        float const ground = map->GetHeight(m_Player->GetPhases(), point.x, point.y,
            point.z + 3.0f);
        if (!std::isfinite(ground) || ground <= INVALID_HEIGHT ||
            point.z < ground - 0.75f || point.z > ground + 2.0f)
            return false;
    }
    float const ground = map->GetHeight(m_Player->GetPhases(), end.x, end.y, end.z + 3.0f);
    return std::isfinite(ground) && ground > INVALID_HEIGHT &&
        end.z >= ground - 0.75f && end.z <= ground + 2.0f;
}

bool BotBGAIMovement::CanDynamicOutdoorMovementTo(float x, float y, float z)
{
    if (!IsDynamicOutdoorBot())
        return false;
    PathParameter parameter(m_Player->GetSession()->GetAccountId(), m_Player);
    Pathfinding path(&parameter, NULL, NULL);
    float const requestedZ = z;
    path.UpdateAllowedPositionZ(x, y, z);
    if (!std::isfinite(z) || std::abs(z - requestedZ) > 2.0f)
        return false;
    if (path.CalculatePath(x, y, z) && IsSafeDynamicOutdoorPath(path, x, y, z))
        return true;
    WarnRejectedDynamicPath(path, x, y, z);
    return false;
}

void BotBGAIMovement::WarnRejectedDynamicPath(Pathfinding const& path, float x, float y, float z)
{
    static std::mutex warningLock;
    static std::map<uint32, uint32> lastWarningByMap;
    uint32 const now = getMSTime();
    {
        std::lock_guard<std::mutex> guard(warningLock);
        uint32& lastWarning = lastWarningByMap[m_Player->GetMapId()];
        if (lastWarning && now - lastWarning < 60000)
            return;
        lastWarning = now;
    }
    TC_LOG_INFO(LOG_FILTER_PATH_GENERATOR,
        "DYNAMIC_BOT_PATH_REJECT bot=%s map=%u type=%u from=(%.2f,%.2f,%.2f) to=(%.2f,%.2f,%.2f) points=%u; check mmap and surface/phase",
        m_Player->GetName(), m_Player->GetMapId(), uint32(path.GetPathType()),
        m_Player->GetPositionX(), m_Player->GetPositionY(), m_Player->GetPositionZ(),
        x, y, z, uint32(path.GetPath().size()));
}
BotAIVehicleMovement3D::BotAIVehicleMovement3D(Player* player) :
m_Player(player),
m_NextMoveGap(3.0f)
{
}

void BotAIVehicleMovement3D::ClearMovement()
{
	m_MovementPos = Position();
	m_NextMovementPos = Position();
}

bool BotAIVehicleMovement3D::HaveNextmovement()
{
	if (m_NextMovementPos.GetPositionX() != 0 && m_NextMovementPos.GetPositionY() != 0 && m_NextMovementPos.GetPositionZ() != 0)
		return true;
	return false;
}

bool BotAIVehicleMovement3D::HaveCurrentmovement()
{
	if (m_MovementPos.GetPositionX() != 0 && m_MovementPos.GetPositionY() != 0 && m_MovementPos.GetPositionZ() != 0)
		return true;
	return false;
}

void BotAIVehicleMovement3D::AddMovement(Unit* pTarget, float maxOffset)
{
	if (!pTarget || HaveNextmovement())
		return;
	Position pos = pTarget->GetPosition();
	float x = pos.GetPositionX() + frand(-maxOffset, maxOffset);
	float y = pos.GetPositionY() + frand(-maxOffset, maxOffset);
	float z = pos.GetPositionZ() + frand(-maxOffset, maxOffset);
	m_NextMovementPos = Position(x, y, z);
}

bool BotAIVehicleMovement3D::UpdateVehicleMovement3D()
{
	Unit* vehicle = m_Player->GetVehicleBase();
	if (!vehicle)
	{
		ClearMovement();
		return false;
	}
	float dist = HaveCurrentmovement() ? vehicle->GetDistance(m_MovementPos) : 0;
	if (dist > m_NextMoveGap)
	{
		vehicle->GetMotionMaster()->Clear();
		vehicle->GetMotionMaster()->MovePoint(1, m_MovementPos);
	}
	else if (HaveNextmovement())
	{
		m_MovementPos = m_NextMovementPos;
		m_NextMovementPos = Position();
		vehicle->GetMotionMaster()->Clear();
		vehicle->GetMotionMaster()->MovePoint(1, m_MovementPos);
	}
	else
	{
		ClearMovement();
		vehicle->GetMotionMaster()->Clear();
	}
	return true;
}

BotBGAIMovement::BotBGAIMovement(Player* player, BotBGAI* ai) :
m_Player(player),
m_BGAI(ai),
m_FieldAI(NULL),
m_GroupAI(NULL),
m_ArenaAI(NULL),
m_DuelAI(NULL),
m_IsFlagTarget(false),
lastPathfindSure(0),
pTargetAIWP(NULL),
m_MovementTick(0),
m_LastSyncTick(0)
{
}

BotBGAIMovement::BotBGAIMovement(Player* player, BotFieldAI* ai) :
m_Player(player),
m_BGAI(NULL),
m_FieldAI(ai),
m_GroupAI(NULL),
m_ArenaAI(NULL),
m_DuelAI(NULL),
m_IsFlagTarget(false),
lastPathfindSure(0),
pTargetAIWP(NULL),
m_MovementTick(0),
m_LastSyncTick(0)
{
}

BotBGAIMovement::BotBGAIMovement(Player* player, BotGroupAI* ai) :
m_Player(player),
m_BGAI(NULL),
m_FieldAI(NULL),
m_GroupAI(ai),
m_ArenaAI(NULL),
m_DuelAI(NULL),
m_IsFlagTarget(false),
lastPathfindSure(0),
pTargetAIWP(NULL),
m_MovementTick(0),
m_LastSyncTick(0)
{
}

BotBGAIMovement::BotBGAIMovement(Player* player, BotArenaAI* ai) :
m_Player(player),
m_BGAI(NULL),
m_FieldAI(NULL),
m_GroupAI(NULL),
m_ArenaAI(ai),
m_DuelAI(NULL),
m_IsFlagTarget(false),
lastPathfindSure(0),
pTargetAIWP(NULL),
m_MovementTick(0),
m_LastSyncTick(0)
{
}

BotBGAIMovement::BotBGAIMovement(Player* player, BotDuelAI* ai) :
m_Player(player),
m_BGAI(NULL),
m_FieldAI(NULL),
m_GroupAI(NULL),
m_ArenaAI(NULL),
m_DuelAI(ai),
m_IsFlagTarget(false),
lastPathfindSure(0),
pTargetAIWP(NULL),
m_MovementTick(0),
m_LastSyncTick(0)
{
}

BotBGAIMovement::~BotBGAIMovement()
{
}

void BotBGAIMovement::ClearMovement()
{
    if(IsBFGJumpInProgress())return;
    m_BFGWaterActive=false;m_BFGHopActive=false;m_BFGRecoveryHop=false;m_BFGStartSettling=false;
    m_ObjectiveMoveAt = 0; m_ObjectiveMoving = false;
    m_AVSpline=false; m_AVRecorded=false; m_AVAttemptAt=0;
    m_ABSpline=false;
    if (IsWSGDropInProgress()) return;
    if (m_BGAI && m_Player->GetMapId()==489 &&
        (!m_Player->isAlive() || !m_Player->IsInWorld() ||
         !m_Player->GetBattleground() ||
         m_Player->GetBattleground()->GetStatus()!=STATUS_IN_PROGRESS))
        ResetWSGTask();
    if (!m_Player->isAlive())
    {m_WSGExitingGraveyard=false;m_WSGExecutingExit=false;m_WSGExitVariant=0;}
    if (m_WSGDropActive) m_Player->ClearUnitState(UNIT_STATE_LONG_JUMP);
    m_WSGDropActive=false;
    m_WSGTaskRouting=false;
    m_IdleSplineProgressTime=0;
    m_WSGEscortGuid.Clear();
    m_WSGEscortSpline = false;
    m_WSGEscortRetryTick = 0;
	m_IsFlagTarget = false;
	m_MovementTick = 0;
	lastPathfindSure = 0;
	targetGuid.Clear();
	pTargetAIWP = NULL;
        // Forget the route from the bot's previous life.
        // A newly resurrected WSG bot must start from its Spirit Healer.
        m_WSGRouteIndex = -1;
        m_WSGRouteDirection = 0;
        m_WSGRecoveryWait = 0;
        m_WSGRouteVariant = 0;
    m_WSGProgressTime=0;m_WSGRetryUntil=0;m_WSGChunkStart=-1;m_WSGGroundRoutePath=false;m_WSGWalkFallback=false;
	m_Player->StopMoving();
	if (Unit* pVehicle = m_Player->GetVehicleBase())
	{
		pVehicle->StopMoving();
	}
}

void BotBGAIMovement::ReleaseCombatMovement()
{
    if (!WSGMovementProtected()) ClearMovement();
}

void BotBGAIMovement::ResetWSGTask()
{
    m_WSGTaskInstance=0;
    m_WSGTaskGuid.Clear();
    m_WSGTaskWaypoint=nullptr;
    m_WSGPatrolFirst=m_WSGPatrolSecond=nullptr;
    m_WSGTaskFlag=false;
    m_WSGHasObjective=false;
    if (m_BGAI && m_Player->GetMapId()==489)
    {targetGuid.Clear();pTargetAIWP=nullptr;m_IsFlagTarget=false;}
}

void BotBGAIMovement::AcceptPatrolCommand(AIWaypoint* first, AIWaypoint* second)
{
    if (!m_BGAI || m_Player->GetMapId()!=489 || !first || !second || first==second)
    {AcceptCommand(first);return;}
    ObserveWSGGround();
    AIWaypoint* point=(m_WSGPatrolFirst==first && m_WSGPatrolSecond==second &&
        (m_WSGTaskWaypoint==first || m_WSGTaskWaypoint==second)) ?
        m_WSGTaskWaypoint : first;
    AcceptCommand(point);
    m_WSGPatrolFirst=first;m_WSGPatrolSecond=second;
}

bool BotBGAIMovement::HasWSGFlagTask() const
{
    Battleground* bg = m_Player->GetBattleground();
    return m_BGAI && m_Player->GetMapId() == 489 && bg && bg->GetStatus() == STATUS_IN_PROGRESS &&
        m_WSGTaskInstance == m_Player->GetInstanceId() && m_WSGTaskFlag &&
        (m_WSGTaskWaypoint || (m_WSGTaskGuid.IsGameObject() &&
            m_Player->GetMap()->GetGameObject(m_WSGTaskGuid)));
}

Player* BotBGAIMovement::GetWSGAttackCarrier() const
{
    Battleground* bg = m_Player->GetBattleground();
    if (!m_BGAI || m_Player->GetMapId() != 489 || !bg || bg->GetStatus() != STATUS_IN_PROGRESS ||
        m_WSGTaskInstance != m_Player->GetInstanceId() || !m_WSGTaskGuid.IsPlayer())
        return nullptr;
    Player* carrier = ObjectAccessor::FindPlayer(m_WSGTaskGuid);
    if (!carrier || !carrier->IsInWorld() || !carrier->isAlive() || carrier->GetBattleground() != bg ||
        carrier->GetBGTeamId() == m_Player->GetBGTeamId() || !IsWSGFlagCarrier(carrier) ||
        !m_Player->InSamePhase(carrier) || !m_Player->InSamePhaseId(carrier) || IsWSGTargetBlocked(carrier))
        return nullptr;
    return carrier;
}

bool BotBGAIMovement::RestoreWSGTask()
{
    if (!m_BGAI || m_Player->GetMapId()!=489) return false;
    if (m_WSGTaskInstance!=m_Player->GetInstanceId())
    {ResetWSGTask();return false;}
    if (!m_WSGTaskGuid.IsEmpty())
    {
        if (m_WSGTaskGuid.IsGameObject())
        {
            if (!m_Player->GetMap()->GetGameObject(m_WSGTaskGuid))
            {ResetWSGTask();targetGuid.Clear();return false;}
        }
        else
        {
            Unit* unit=ObjectAccessor::GetUnit(*m_Player,m_WSGTaskGuid);
            if (!unit || !unit->IsInWorld() || !unit->isAlive() ||
                unit->GetMap()!=m_Player->GetMap() || !m_Player->InSamePhase(unit) ||
                !m_Player->InSamePhaseId(unit) || IsWSGTargetBlocked(unit) ||
                (unit->ToPlayer() && unit->ToPlayer()->GetBGTeamId()==m_Player->GetBGTeamId() &&
                 !IsWSGFlagCarrier(unit->ToPlayer())))
            {ResetWSGTask();targetGuid.Clear();return false;}
        }
    }
    if (m_WSGPatrolFirst && m_WSGPatrolSecond && m_WSGTaskWaypoint &&
        !IsWSGFlagCarrier(m_Player) &&
        m_Player->GetDistance(m_WSGTaskWaypoint->GetPosition())<6.0f)
    {
        m_WSGTaskWaypoint=m_WSGTaskWaypoint==m_WSGPatrolFirst ?
            m_WSGPatrolSecond : m_WSGPatrolFirst;
        ClearMovement(); // A new leg, using the same validated ground routing.
    }
    targetGuid=m_WSGTaskGuid;pTargetAIWP=m_WSGTaskWaypoint;
    m_IsFlagTarget=m_WSGTaskFlag;
    return !targetGuid.IsEmpty() || pTargetAIWP;
}

void BotBGAIMovement::AcceptCommand(AIWaypoint* targetAIWP, bool isFlag)
{
	if (!targetAIWP)
		return;
    if (m_BGAI && m_Player->GetMapId()==489)
    {
        ObserveWSGGround();
        bool changed=m_WSGTaskWaypoint!=targetAIWP || !m_WSGTaskGuid.IsEmpty() || m_WSGTaskFlag!=isFlag;
        m_WSGTaskInstance=m_Player->GetInstanceId();
        m_WSGTaskWaypoint=targetAIWP;m_WSGTaskGuid.Clear();m_WSGTaskFlag=isFlag;
        m_WSGPatrolFirst=m_WSGPatrolSecond=nullptr;
        if (changed && !WSGMovementProtected()) ClearMovement();
    }
    ResetWSGEscort();
	if (pTargetAIWP)
	{
		if (targetAIWP->entry == pTargetAIWP->entry && m_IsFlagTarget == isFlag)
			return;
	}
	m_IsFlagTarget = isFlag;
	pTargetAIWP = targetAIWP;
	m_MovementTick = 0;
	if (pTargetAIWP)
		targetGuid.Clear();
}

void BotBGAIMovement::AcceptCommand(ObjectGuid guid, bool isFlag)
{
    if (m_BGAI && m_Player->GetMapId()==489)
    {
        ObserveWSGGround();
        bool changed=m_WSGTaskGuid!=guid || m_WSGTaskWaypoint || m_WSGTaskFlag!=isFlag;
        m_WSGTaskInstance=m_Player->GetInstanceId();
        m_WSGTaskGuid=guid;m_WSGTaskWaypoint=nullptr;m_WSGTaskFlag=isFlag;
        m_WSGPatrolFirst=m_WSGPatrolSecond=nullptr;
        if (changed && !WSGMovementProtected()) ClearMovement();
    }
    if (guid != targetGuid)
        ResetWSGEscort();
	m_IsFlagTarget = isFlag;
	targetGuid = guid;
	if (!targetGuid.IsEmpty())
		pTargetAIWP = NULL;
}

void BotBGAIMovement::ExecuteMovementCommand()
{
    if (m_BGAI && m_Player->GetMapId()==489)
    {ObserveWSGGround();RestoreWSGTask();}
    if (RecoverFallenWSGBot())
        return;

	if (m_Player->HasUnitState(UNIT_STATE_CASTING))
		return;
	if (!targetGuid.IsEmpty() && (targetGuid.IsPlayer() || targetGuid.IsCreature()))
		MovementTo(targetGuid, 2.0f);
	else// if (m_Player->IsStopped() || lastPathfindSure == 0)
	{
		if (!targetGuid.IsEmpty())
			MovementTo(targetGuid, 2.0f);
		else if (pTargetAIWP)
			MovementToAIWP(m_Player->GetMapId() == 489 ? 0.0f : 5.0f);
	}
}

bool BotBGAIMovement::ExecuteCruxMovementCommand()
{
    if (m_BGAI && m_Player->GetMapId()==489)
    {ObserveWSGGround();RestoreWSGTask();}
    Player* escortCarrier = GetWSGEscortCarrier();
    if (!escortCarrier)
        ResetWSGEscort();
    if (RecoverFallenWSGBot())
        return true;

	if (!m_BGAI)
		return false;
	if (m_Player->HasUnitState(UNIT_STATE_CASTING))
		return true;
    // Intercept a remote carrier even while unrelated enemies are nearby.
    // Within combat range, return control to the class rotation on that carrier.
    if (Player* carrier = GetWSGAttackCarrier())
    {
        if (m_Player->GetDistance(carrier->GetPosition()) >= BOTAI_SEARCH_RANGE ||
            !m_Player->canSeeOrDetect(carrier, false, true))
        {
            m_Player->AttackStop();
            m_Player->SetSelection(ObjectGuid::Empty);
            MovementTo(carrier->GetGUID(), 5.0f);
            return true;
        }
        m_Player->SetSelection(carrier->GetGUID());
        return false;
    }
    // Regroup even if an unrelated enemy is nearby. Close to the carrier,
    // normal combat/healing remains available instead of a permanent follow.
    if (escortCarrier)
    {
        if (m_Player->GetDistance(escortCarrier->GetPosition()) > 18.0f ||
            !m_Player->IsWithinLOSInMap(escortCarrier))
        {
            FollowWSGCarrier(escortCarrier);
            return true;
        }
        if (m_WSGEscortSpline)
        {
            m_Player->StopMoving();
            m_Player->GetMotionMaster()->Clear();
            m_WSGEscortSpline = false;
        }
        return false;
    }
	if (m_IsFlagTarget)
	{
		if (pTargetAIWP)
		{
			m_Player->SetSelection(ObjectGuid::Empty);
			MovementToAIWP(1.0f);
			return true;
		}
	}
	if (targetGuid.IsEmpty())
		return false;
	if (targetGuid.IsGameObject())
	{
		MovementTo(targetGuid, 0.5f);
		return true;
	}
	else if (targetGuid.IsPlayer())
	{
		Player* player = ObjectAccessor::FindPlayer(targetGuid);
		if (!player || !player->isAlive())
			return false;
		float distance = m_Player->GetDistance(player->GetPosition());
		if (player->GetTeamId() != m_Player->GetTeamId())
		{
			if (distance < BOTAI_SEARCH_RANGE)
			{
				m_Player->SetSelection(targetGuid);
				MovementTo(targetGuid, 5);// m_IsMeleeBot ? 5.0f : BOTAI_RANGESPELL_DISTANCE);
			}
			return false;
		}
		if (distance < BOTAI_RANGESPELL_DISTANCE)
			return false;
		if (m_BGAI->NearHasEnemy())
			return false;
		MovementTo(targetGuid, 5.0f);
		return true;
	}
	return false;
}

namespace
{
    // Legion WSG tunnel route, ordered from the Alliance flag
    // to the Horde flag. Horde bots use the same route in reverse.
    static int16 const WSGAllianceTunnelRecordedEnd = 51;
    static G3D::Vector3 const WSGLegionTunnelRoute[] =
    {
        { 1538.975342f, 1481.792236f, 352.538574f },
        { 1524.723022f, 1479.286987f, 351.995605f },
        { 1521.659424f, 1477.155151f, 352.018555f },
        { 1517.579102f, 1474.397949f, 352.029236f },
        { 1513.853882f, 1471.972778f, 352.014740f },
        { 1510.762695f, 1469.678101f, 351.998932f },
        { 1509.221436f, 1467.675049f, 351.989777f },
        { 1510.814819f, 1466.407227f, 351.991119f },
        { 1513.596191f, 1465.532593f, 351.999268f },
        { 1515.166748f, 1463.916016f, 352.001038f },
        { 1515.827515f, 1461.749390f, 352.036530f },
        { 1515.667236f, 1459.479126f, 352.055084f },
        { 1514.468262f, 1457.409790f, 352.061584f },
        { 1513.756104f, 1457.303101f, 352.054413f },
        { 1512.885010f, 1457.864868f, 352.038483f },
        { 1508.975830f, 1457.729492f, 351.018585f },
        { 1507.002319f, 1457.726562f, 350.010162f },
        { 1504.255615f, 1457.755005f, 348.606354f },
        { 1499.811401f, 1457.841187f, 346.260895f },
        { 1495.367188f, 1457.927368f, 343.756287f },
        { 1493.289062f, 1458.020508f, 342.795685f },
        { 1488.853149f, 1458.305908f, 342.795685f },
        { 1486.730957f, 1458.410034f, 342.795685f },
        { 1482.290039f, 1458.600952f, 342.795685f },
        { 1480.006592f, 1458.679565f, 342.795685f },
        { 1475.563232f, 1458.800659f, 342.795685f },
        { 1471.119995f, 1458.921631f, 342.795685f },
        { 1466.676636f, 1459.042725f, 342.795685f },
        { 1462.233276f, 1459.163696f, 342.795685f },
        { 1457.789917f, 1459.284790f, 342.761108f },
        { 1453.346558f, 1459.405884f, 342.626862f },
        { 1448.903198f, 1459.526855f, 342.412933f },
        { 1444.459839f, 1459.647949f, 342.179626f },
        { 1440.016479f, 1459.769043f, 341.559479f },
        { 1435.573120f, 1459.890015f, 340.156311f },
        { 1431.129761f, 1460.011108f, 338.621765f },
        { 1426.686401f, 1460.132080f, 336.990021f },
        { 1422.243042f, 1460.253174f, 335.525085f },
        { 1417.799683f, 1460.374268f, 333.986298f },
        { 1413.356323f, 1460.495239f, 332.490112f },
        { 1408.912964f, 1460.616333f, 331.089142f },
        { 1404.469727f, 1460.737305f, 329.947388f },
        { 1400.026367f, 1460.858398f, 328.539612f },
        { 1395.583008f, 1460.979492f, 327.216492f },
        { 1391.139648f, 1461.100464f, 326.295593f },
        { 1386.696289f, 1461.221558f, 325.472137f },
        { 1382.252930f, 1461.342651f, 324.922394f },
        { 1377.809570f, 1461.463623f, 324.670959f },
        { 1373.366211f, 1461.584717f, 324.331787f },
        { 1368.922852f, 1461.705688f, 324.420258f },
        { 1364.479492f, 1461.826782f, 324.552521f },
        { 1360.036133f, 1461.947876f, 324.530792f },
        { 1353.772339f, 1461.863525f, 324.494629f },
        { 1329.923950f, 1462.485840f, 320.279572f },


        { 1252.831421f, 1461.989380f, 311.463135f },
        { 1243.428955f, 1471.687134f, 309.113556f },
        { 1229.123047f, 1475.681152f, 307.595306f },
        { 1216.999756f, 1474.245117f, 307.042236f },
        { 1204.742310f, 1472.414185f, 307.369293f },
        { 1187.750854f, 1462.644775f, 305.778320f },
        { 1166.706177f, 1462.141602f, 310.433624f },
        { 1146.458496f, 1463.207764f, 310.601776f },
        { 1135.640991f, 1463.663452f, 315.639221f },
        { 1116.829834f, 1462.568604f, 316.496216f },

        { 1095.573608f, 1461.057373f, 316.860168f },
        { 1076.688354f, 1460.004150f, 316.696472f },
        { 1051.059082f, 1459.806030f, 323.127411f },
        { 1013.940918f, 1459.231445f, 334.108398f },
        { 981.933228f, 1459.217285f, 336.140350f },
        { 956.101624f, 1459.124146f, 341.136414f },
        { 940.495667f, 1459.046875f, 344.643707f },
        { 933.295959f, 1434.721802f, 345.535858f },
        { 916.864563f, 1433.573975f, 346.297363f }
    };

    static int16 const WSGLegionTunnelRouteCount =
        int16(sizeof(WSGLegionTunnelRoute) /
            sizeof(WSGLegionTunnelRoute[0]));

    // Alternative Silverwing gate route, sharing the Horde-side destination.
    static G3D::Vector3 const WSGLegionAlternativeRoute[] =
    {
        // Recorded from real player movement on map 489.
        { 1525.079224f, 1481.969482f, 351.984589f },
        { 1521.584106f, 1484.103394f, 351.995331f },
        { 1518.089111f, 1486.237305f, 352.006165f },
        { 1514.900757f, 1488.197754f, 352.013519f },
        { 1511.688721f, 1490.134033f, 352.013519f },
        { 1509.097778f, 1491.539795f, 352.013519f },
        { 1506.386719f, 1492.617065f, 352.013733f },
        { 1502.916260f, 1493.197998f, 352.201050f },
        { 1499.125610f, 1493.447632f, 352.194153f },
        { 1496.343018f, 1493.552490f, 352.189392f },
        { 1492.249268f, 1493.651978f, 352.167023f },
        { 1490.210205f, 1493.673706f, 352.138000f },
        { 1486.115234f, 1493.668457f, 352.131836f },
        { 1482.020264f, 1493.661621f, 351.912994f },
        { 1478.089233f, 1493.669556f, 351.643280f },
        { 1473.994385f, 1493.711060f, 351.555176f },
        { 1469.899658f, 1493.752563f, 351.770325f },
        { 1465.805298f, 1493.812256f, 351.913025f },
        { 1463.808105f, 1493.878906f, 352.018311f },
        { 1461.591553f, 1493.983643f, 351.716919f },
        { 1458.504517f, 1494.414551f, 351.417816f },
        { 1456.288940f, 1495.164917f, 350.993378f },
        { 1454.488647f, 1496.321533f, 350.669006f },
        { 1451.956421f, 1498.730591f, 350.113831f },
        { 1449.893311f, 1500.848267f, 349.233032f },
        { 1447.092773f, 1503.835205f, 348.096252f },
        { 1445.789185f, 1505.792358f, 347.476715f },
        { 1445.250488f, 1507.867432f, 347.096130f },
        { 1445.122070f, 1511.304077f, 346.565765f },
        { 1445.216431f, 1517.109985f, 345.564423f },
        { 1445.290039f, 1521.204346f, 344.739777f },
        { 1445.363770f, 1525.298706f, 344.273895f },
        { 1445.378662f, 1528.189453f, 343.885132f },
        { 1445.289673f, 1530.677002f, 343.581512f },
        { 1445.093750f, 1533.610718f, 343.268158f },
        { 1444.528931f, 1537.666260f, 342.876526f },
        { 1443.720459f, 1540.996338f, 342.780243f },
        { 1442.523438f, 1544.350464f, 342.760529f },
        { 1441.038818f, 1547.125122f, 342.760010f },
        { 1439.231323f, 1549.376831f, 342.781647f },
        { 1437.168457f, 1551.235718f, 342.854736f },
        { 1434.496338f, 1552.569580f, 342.749542f },
        { 1431.957764f, 1553.673462f, 342.180237f },
        { 1430.157959f, 1554.377686f, 341.387665f },
        { 1426.911987f, 1555.540527f, 340.043945f },
        { 1425.084717f, 1556.194946f, 338.167145f },
        { 1423.450073f, 1556.780518f, 335.570160f },
        { 1420.387085f, 1557.877563f, 328.369232f },
        { 1417.274292f, 1558.588013f, 328.343140f },
        { 1413.967651f, 1558.710571f, 328.365479f },
        { 1410.816650f, 1558.154419f, 328.481873f },
        { 1407.571533f, 1557.045166f, 328.698700f },
        { 1404.196777f, 1555.505737f, 328.903046f },
        { 1401.156494f, 1553.879395f, 329.227417f },
        { 1397.901367f, 1551.871704f, 329.633331f },
        { 1396.091431f, 1550.703857f, 329.892548f },
        { 1393.106934f, 1548.715820f, 330.020966f },
        { 1390.237549f, 1546.660645f, 330.129089f },
        { 1387.777344f, 1544.795166f, 329.429169f },
        { 1384.506592f, 1542.331055f, 326.419769f },
        { 1382.923706f, 1541.138428f, 323.231201f },
        { 1382.026978f, 1540.462891f, 320.923859f },
        { 1378.886963f, 1539.169556f, 320.914673f },
        { 1375.604492f, 1538.072998f, 321.254089f },
        { 1372.071289f, 1537.026367f, 321.093842f },
        { 1368.866211f, 1536.140625f, 320.833344f },
        { 1365.473999f, 1535.080444f, 320.399933f },
        { 1361.927979f, 1533.833740f, 319.727356f },
        { 1358.619873f, 1532.447876f, 318.845367f },
        { 1354.870728f, 1530.800659f, 317.470490f },
        { 1351.121704f, 1529.153442f, 315.710510f },
        { 1347.372559f, 1527.506226f, 315.250427f },
        { 1343.623535f, 1525.858887f, 315.381226f },
        { 1339.873047f, 1524.214966f, 315.301270f },
        { 1337.976074f, 1523.423218f, 315.142365f },
        { 1334.722534f, 1522.211182f, 314.716827f },
        { 1331.829224f, 1521.249634f, 314.860962f },
        { 1328.510864f, 1520.377197f, 315.109039f },
        { 1326.484863f, 1519.904785f, 315.305359f },
        { 1320.800903f, 1518.674194f, 316.066223f },
        { 1316.792358f, 1517.837158f, 317.117340f },
        { 1313.453491f, 1517.120361f, 317.644135f },
        { 1309.466187f, 1516.187256f, 318.013794f },
        { 1306.398193f, 1515.392578f, 318.137878f },
        { 1302.452759f, 1514.295898f, 318.162445f },
        { 1298.493042f, 1513.252075f, 318.078888f },
        { 1294.532471f, 1512.211670f, 317.812592f },
        { 1290.571777f, 1511.171143f, 317.351440f },
        { 1286.611206f, 1510.130737f, 316.953857f },
        { 1284.413452f, 1509.567871f, 316.768555f },
        { 1280.440918f, 1508.574097f, 316.258148f },
        { 1276.468994f, 1507.577759f, 315.518860f },
        { 1274.465698f, 1507.048340f, 314.992371f },
        { 1270.517578f, 1505.961548f, 313.853149f },
        { 1266.569458f, 1504.874512f, 312.538330f },
        { 1262.621460f, 1503.787476f, 311.142334f },
        { 1258.673340f, 1502.700439f, 310.400696f },
        { 1254.725342f, 1501.613403f, 309.734955f },
        { 1250.777222f, 1500.526245f, 309.086853f },
        { 1246.829102f, 1499.439209f, 308.437347f },
        { 1242.881104f, 1498.352173f, 307.811096f },
        { 1238.932983f, 1497.265137f, 307.294342f },
        { 1235.034912f, 1496.182007f, 306.856445f },
        { 1231.198853f, 1495.060181f, 306.512268f },
        { 1227.524780f, 1493.939697f, 306.195648f },
        { 1224.059570f, 1492.784912f, 305.845947f },
        { 1218.518921f, 1490.791016f, 305.358429f },
        { 1214.679321f, 1489.367310f, 305.057739f },
        { 1210.839844f, 1487.943604f, 305.310120f },
        { 1207.000244f, 1486.520020f, 305.540894f },
        { 1203.160645f, 1485.096313f, 305.668945f },
        { 1199.321167f, 1483.672607f, 305.447235f },
        { 1195.481567f, 1482.248901f, 304.903900f },
        { 1191.641968f, 1480.825317f, 304.171295f },
        { 1187.802490f, 1479.401611f, 303.404724f },
        { 1183.962891f, 1477.977905f, 303.193359f },
        { 1180.402222f, 1476.718262f, 303.666168f },
        { 1177.021240f, 1475.621704f, 304.266907f },
        { 1173.386963f, 1474.599487f, 305.081818f },
        { 1169.870117f, 1473.674805f, 306.193756f },
        { 1167.322510f, 1473.025391f, 307.175812f },
        { 1163.349976f, 1472.031738f, 308.508423f },
        { 1159.377319f, 1471.037964f, 309.555145f },
        { 1155.410156f, 1470.045532f, 310.534515f },
        { 1151.438354f, 1469.052002f, 311.044220f },
        { 1147.465820f, 1468.058350f, 310.988464f },
        { 1143.499878f, 1467.066162f, 311.571594f },
        { 1139.525757f, 1466.082886f, 313.729095f },
        { 1135.822510f, 1465.195068f, 315.560120f },
        { 1133.027710f, 1464.557739f, 315.727081f },
        { 1129.351074f, 1463.859863f, 315.546997f },
        { 1125.826294f, 1463.342285f, 315.812744f },
        { 1121.980713f, 1462.875488f, 316.162048f },
        { 1118.116333f, 1462.506714f, 316.411011f },
        { 1115.984741f, 1462.347168f, 316.552094f },
        { 1113.043335f, 1462.143555f, 316.559143f },
        { 1107.180786f, 1461.807373f, 316.557617f },
        { 1103.093262f, 1461.560181f, 316.550110f },
        { 1099.005737f, 1461.312378f, 316.543304f },
        { 1094.922485f, 1461.064941f, 316.863373f },
        { 1090.834961f, 1460.817139f, 316.496246f },
        { 1086.748291f, 1460.569336f, 316.366760f },
        { 1082.660767f, 1460.321533f, 316.393982f },
        { 1078.573242f, 1460.073730f, 316.458191f },
        { 1074.485840f, 1459.825928f, 316.960815f },
        { 1070.398315f, 1459.578125f, 317.852692f },
        { 1066.310791f, 1459.330322f, 318.839844f },
        { 1062.224365f, 1459.065674f, 319.911194f },
        { 1058.141357f, 1458.753784f, 321.051910f },
        { 1054.625977f, 1458.524658f, 322.103851f },
        { 1051.195801f, 1458.530396f, 323.259796f },
        { 1045.398315f, 1458.856812f, 325.240509f },
        { 1043.459351f, 1458.947754f, 325.948517f },
        { 1041.085938f, 1459.038330f, 326.707062f },
        { 1035.109497f, 1459.197876f, 328.616638f },
        { 1031.015625f, 1459.300293f, 329.921722f },
        { 1026.921021f, 1459.357788f, 331.111420f },
        { 1022.826477f, 1459.415405f, 332.207764f },
        { 1018.731750f, 1459.457764f, 333.152954f },
        { 1014.636780f, 1459.451050f, 333.948547f },
        { 1010.541748f, 1459.444336f, 334.635986f },
        { 1006.446777f, 1459.437500f, 335.106964f },
        { 1002.351746f, 1459.430786f, 335.407654f },
        { 998.256775f, 1459.424072f, 335.553772f },
        { 994.161865f, 1459.414551f, 335.234161f },
        { 990.067200f, 1459.359619f, 335.437805f },
        { 985.972595f, 1459.304565f, 335.983917f },
        { 981.885193f, 1459.249756f, 336.138123f },
        { 977.790527f, 1459.194824f, 336.240662f },
        { 973.695923f, 1459.139771f, 336.819489f },
        { 969.601257f, 1459.084839f, 337.111664f },
        { 965.506653f, 1459.029785f, 337.917511f },
        { 961.412048f, 1458.974854f, 339.244141f },
        { 957.322327f, 1458.919922f, 340.675995f },
        { 953.227722f, 1458.864990f, 341.928925f },
        { 949.133057f, 1458.810059f, 342.953369f },
        { 945.038452f, 1458.755005f, 344.117981f },
        { 942.586670f, 1458.690796f, 344.604858f },
        { 940.557556f, 1458.130371f, 344.697388f },
        { 938.813293f, 1456.968506f, 344.813965f },
        { 937.498962f, 1455.328613f, 345.059509f },
        { 936.163025f, 1452.367920f, 345.587646f },
        { 934.546936f, 1446.791260f, 345.535675f },
        { 933.426392f, 1442.852539f, 345.535675f },
        { 932.305847f, 1438.913818f, 345.535675f },
        { 931.185242f, 1434.975098f, 345.535675f },
    };

    // Recorded on the Horde side from Horde toward Alliance.
    // Stored in reverse so every WSG route remains Alliance -> Horde.
    static G3D::Vector3 const WSGLegionHordeAlternativeRoute[] =
    {
        { 1533.514893f, 1480.851440f, 351.956818f },
        { 1530.398071f, 1478.971436f, 351.968567f },
        { 1527.645630f, 1477.046509f, 351.989502f },
        { 1525.671753f, 1475.216064f, 352.005371f },
        { 1524.110229f, 1473.377441f, 352.018646f },
        { 1522.784546f, 1471.358765f, 352.033173f },
        { 1521.673096f, 1469.601685f, 352.034698f },
        { 1519.192749f, 1465.517944f, 352.019806f },
        { 1517.854858f, 1462.224243f, 352.057861f },
        { 1516.166382f, 1459.405029f, 352.061493f },
        { 1513.783203f, 1458.184082f, 352.044434f },
        { 1511.314087f, 1457.783203f, 352.010834f },
        { 1508.948364f, 1457.754028f, 351.004211f },
        { 1507.156372f, 1457.771606f, 350.088379f },
        { 1503.853516f, 1457.858276f, 348.399261f },
        { 1500.354980f, 1457.959595f, 346.565491f },
        { 1496.856445f, 1458.061035f, 344.593140f },
        { 1493.357910f, 1458.162354f, 342.794220f },
        { 1489.859375f, 1458.263672f, 342.794220f },
        { 1486.360840f, 1458.364990f, 342.794220f },
        { 1482.862305f, 1458.466431f, 342.794220f },
        { 1479.363770f, 1458.567749f, 342.794220f },
        { 1475.865234f, 1458.669067f, 342.794220f },
        { 1472.366699f, 1458.770386f, 342.794220f },
        { 1468.868164f, 1458.871826f, 342.794220f },
        { 1465.369629f, 1458.973145f, 342.794220f },
        { 1461.871094f, 1459.074463f, 342.794220f },
        { 1458.372559f, 1459.175781f, 342.766449f },
        { 1454.874023f, 1459.277222f, 342.708618f },
        { 1451.375488f, 1459.378540f, 342.525146f },
        { 1447.877075f, 1459.479858f, 342.361847f },
        { 1444.378540f, 1459.581177f, 342.169830f },
        { 1440.880005f, 1459.682617f, 341.810181f },
        { 1437.381470f, 1459.783936f, 340.734863f },
        { 1433.882935f, 1459.885254f, 339.609894f },
        { 1430.384399f, 1459.986572f, 338.350861f },
        { 1426.885864f, 1460.088013f, 337.065613f },
        { 1423.387329f, 1460.189331f, 335.898041f },
        { 1419.888794f, 1460.290649f, 334.718903f },
        { 1416.390259f, 1460.391968f, 333.486084f },
        { 1412.891724f, 1460.493408f, 332.320557f },
        { 1409.393188f, 1460.594727f, 331.103088f },
        { 1405.894653f, 1460.696045f, 330.397858f },
        { 1402.396118f, 1460.797363f, 329.290161f },
        { 1398.897583f, 1460.898804f, 328.181732f },
        { 1395.399048f, 1461.000122f, 327.165558f },
        { 1391.900513f, 1461.101440f, 326.435150f },
        { 1388.401978f, 1461.202759f, 325.788025f },
        { 1384.903442f, 1461.304199f, 325.227692f },
        { 1381.404907f, 1461.405518f, 324.821045f },
        { 1377.906372f, 1461.506836f, 324.670807f },
        { 1374.407837f, 1461.608154f, 324.583252f },
        { 1370.909302f, 1461.709595f, 324.388580f },
        { 1367.410767f, 1461.810913f, 324.399902f },
        { 1365.326416f, 1461.871216f, 324.550049f },
        { 1361.827881f, 1461.972656f, 324.537994f },
        { 1358.329224f, 1462.067383f, 324.518158f },
        { 1354.830322f, 1462.155029f, 324.498352f },
        { 1351.333008f, 1462.215698f, 323.598907f },
        { 1348.072998f, 1462.169922f, 323.161407f },
        { 1344.888794f, 1461.701294f, 322.730621f },
        { 1342.033081f, 1461.033447f, 322.193970f },
        { 1338.977661f, 1460.187500f, 321.639771f },
        { 1334.740112f, 1458.823120f, 321.142944f },
        { 1331.910889f, 1457.837402f, 320.741791f },
        { 1328.620728f, 1456.643799f, 320.200470f },
        { 1325.330566f, 1455.450073f, 319.711853f },
        { 1322.042480f, 1454.257202f, 319.321869f },
        { 1318.751587f, 1453.065918f, 319.007294f },
        { 1315.456787f, 1451.885254f, 318.378906f },
        { 1312.161987f, 1450.704468f, 317.290619f },
        { 1308.867065f, 1449.523804f, 316.570374f },
        { 1305.572266f, 1448.343018f, 316.369965f },
        { 1302.277466f, 1447.162354f, 316.511627f },
        { 1299.330078f, 1446.132935f, 316.547485f },
        { 1295.367920f, 1444.848755f, 316.353729f },
        { 1293.092041f, 1444.152832f, 316.203522f },
        { 1289.739746f, 1443.146729f, 315.946045f },
        { 1286.385010f, 1442.148804f, 315.609528f },
        { 1283.030518f, 1441.150879f, 315.234314f },
        { 1279.675781f, 1440.153076f, 314.727386f },
        { 1276.321045f, 1439.155151f, 314.428192f },
        { 1272.971802f, 1438.158936f, 314.173615f },
        { 1269.610107f, 1437.184448f, 314.132660f },
        { 1266.247681f, 1436.213013f, 314.552490f },
        { 1261.523193f, 1434.836426f, 314.871155f },
        { 1258.186401f, 1433.780640f, 314.635040f },
        { 1254.874756f, 1432.648438f, 313.981903f },
        { 1251.803223f, 1431.571045f, 313.136383f },
        { 1248.697632f, 1430.423218f, 312.131744f },
        { 1245.408325f, 1429.091064f, 311.061523f },
        { 1242.865723f, 1428.004883f, 310.431305f },
        { 1239.665283f, 1426.598877f, 309.941162f },
        { 1236.463989f, 1425.188599f, 309.757172f },
        { 1233.880005f, 1424.022339f, 309.653961f },
        { 1230.693726f, 1422.574219f, 309.529541f },
        { 1227.498413f, 1421.146118f, 309.397156f },
        { 1224.746460f, 1420.053223f, 309.322754f },
        { 1220.505127f, 1418.551636f, 309.221802f },
        { 1217.301514f, 1417.539551f, 309.068329f },
        { 1213.933960f, 1416.585693f, 308.842072f },
        { 1210.563965f, 1415.640625f, 308.584503f },
        { 1207.196045f, 1414.696167f, 308.254822f },
        { 1203.826050f, 1413.751099f, 307.880402f },
        { 1201.092163f, 1413.000244f, 307.622284f },
        { 1197.713989f, 1412.085083f, 307.413147f },
        { 1194.333008f, 1411.179688f, 307.202393f },
        { 1191.079834f, 1410.339600f, 307.046417f },
        { 1187.688477f, 1409.474243f, 306.834686f },
        { 1184.297241f, 1408.608765f, 306.638153f },
        { 1180.905884f, 1407.743408f, 306.612396f },
        { 1177.514526f, 1406.877930f, 306.808533f },
        { 1174.123291f, 1406.012573f, 307.595947f },
        { 1170.731934f, 1405.147095f, 308.416962f },
        { 1167.340576f, 1404.281738f, 309.219360f },
        { 1163.949341f, 1403.416260f, 309.972382f },
        { 1160.557983f, 1402.550903f, 310.680756f },
        { 1155.381348f, 1401.236206f, 311.002899f },
        { 1153.399902f, 1400.763550f, 310.930054f },
        { 1151.421753f, 1400.307983f, 310.949432f },
        { 1148.321045f, 1399.643433f, 311.106812f },
        { 1144.895508f, 1398.925293f, 311.412201f },
        { 1141.469971f, 1398.207153f, 311.986694f },
        { 1138.041870f, 1397.501221f, 312.859009f },
        { 1134.611206f, 1396.807861f, 313.770874f },
        { 1131.180176f, 1396.116577f, 314.526855f },
        { 1128.844971f, 1395.656372f, 314.939423f },
        { 1125.408691f, 1394.992065f, 315.116486f },
        { 1121.972290f, 1394.327881f, 314.987152f },
        { 1118.535889f, 1393.663574f, 314.869415f },
        { 1115.099487f, 1392.999268f, 315.147949f },
        { 1111.663086f, 1392.335083f, 315.526520f },
        { 1108.226685f, 1391.670776f, 316.017181f },
        { 1104.790283f, 1391.006470f, 316.565552f },
        { 1101.353882f, 1390.342896f, 317.149567f },
        { 1097.912476f, 1389.704834f, 317.689697f },
        { 1094.470459f, 1389.070557f, 318.134216f },
        { 1091.026489f, 1388.446777f, 318.553101f },
        { 1087.582520f, 1387.823120f, 319.116577f },
        { 1084.138550f, 1387.199341f, 319.730591f },
        { 1081.259766f, 1386.638184f, 320.592743f },
        { 1079.666382f, 1386.164185f, 324.050537f },
        { 1078.210449f, 1385.731079f, 326.258728f },
        { 1074.855713f, 1384.733276f, 328.655609f },
        { 1072.864746f, 1384.134766f, 329.049866f },
        { 1069.889038f, 1383.191040f, 328.812134f },
        { 1067.001587f, 1382.081421f, 328.348694f },
        { 1064.118286f, 1380.779663f, 328.364136f },
        { 1061.963257f, 1379.690186f, 328.435242f },
        { 1058.848633f, 1378.093506f, 328.606812f },
        { 1056.224609f, 1376.783569f, 328.649719f },
        { 1054.081055f, 1375.798828f, 328.538422f },
        { 1052.065063f, 1374.959351f, 328.396362f },
        { 1049.285522f, 1374.008179f, 328.354675f },
        { 1046.613159f, 1374.094971f, 328.443146f },
        { 1044.637939f, 1375.823486f, 328.509949f },
        { 1044.751587f, 1377.844238f, 328.518555f },
        { 1044.220581f, 1380.406860f, 328.545319f },
        { 1042.378906f, 1381.438965f, 328.563141f },
        { 1040.859619f, 1383.105713f, 328.559082f },
        { 1038.897583f, 1384.406738f, 328.502197f },
        { 1037.368042f, 1386.087280f, 328.518005f },
        { 1035.223145f, 1387.536133f, 328.691528f },
        { 1033.544189f, 1388.852905f, 333.880585f },
        { 1032.387573f, 1389.760132f, 336.412537f },
        { 1031.236450f, 1390.663086f, 338.087769f },
        { 1027.115723f, 1393.915894f, 340.319855f },
        { 1024.899902f, 1395.837158f, 341.035645f },
        { 1023.045654f, 1397.911987f, 341.177765f },
        { 1021.443176f, 1400.424683f, 341.423431f },
        { 1019.839478f, 1402.938354f, 341.570557f },
        { 1017.864990f, 1405.828125f, 341.309113f },
        { 1015.899536f, 1408.724243f, 341.391785f },
        { 1013.936401f, 1411.621826f, 341.705627f },
        { 1011.928284f, 1414.487915f, 342.320618f },
        { 1009.959717f, 1416.779785f, 342.912872f },
        { 1007.900696f, 1418.436279f, 343.365448f },
        { 1005.196716f, 1420.048218f, 343.769653f },
        { 1002.520447f, 1421.207275f, 344.087677f },
        { 999.700195f, 1422.008179f, 344.404053f },
        { 996.828857f, 1422.381470f, 344.745605f },
        { 993.822266f, 1422.510742f, 345.076111f },
        { 991.575623f, 1422.470825f, 345.321442f },
        { 988.076538f, 1422.393555f, 345.402802f },
        { 982.820740f, 1422.281250f, 345.463104f },
        { 980.508423f, 1422.265625f, 345.451385f },
        { 977.092712f, 1422.298950f, 345.386993f },
        { 973.593018f, 1422.345337f, 345.275513f },
        { 970.093384f, 1422.396118f, 345.567474f },
        { 966.593933f, 1422.456299f, 345.223022f },
        { 963.094421f, 1422.516479f, 345.275391f },
        { 959.595093f, 1422.585815f, 345.225555f },
        { 954.206482f, 1422.706299f, 345.774048f },
        { 950.376404f, 1422.886475f, 345.372162f },
        { 947.736816f, 1423.748657f, 345.396637f },
        { 944.501221f, 1426.128540f, 345.443481f },
        { 942.069885f, 1427.902588f, 345.478180f },
        { 939.161255f, 1429.849243f, 345.520325f },
        { 936.246399f, 1431.786621f, 345.535675f },
        { 933.331482f, 1433.723999f, 345.535675f },
    };

    // Alliance flag carrier: enemy Horde flag to Alliance capture point.
    // Stored Alliance -> Horde, so Alliance carriers traverse it in reverse.
    static G3D::Vector3 const WSGAllianceFlagCarrierRoute[] =
    {
        { 1536.712769f, 1481.639893f, 352.238220f },
        { 1534.202881f, 1482.113403f, 351.955841f },
        { 1530.781616f, 1482.852051f, 351.962219f },
        { 1527.328247f, 1483.497559f, 351.968323f },
        { 1524.753174f, 1482.949951f, 351.983154f },
        { 1523.297485f, 1480.701782f, 351.998962f },
        { 1522.183472f, 1477.383911f, 352.015533f },
        { 1521.086914f, 1474.060181f, 352.031616f },
        { 1519.990234f, 1470.736450f, 352.033020f },
        { 1518.447021f, 1466.048950f, 352.017975f },
        { 1516.931396f, 1461.326538f, 352.055786f },
        { 1515.454468f, 1459.104980f, 352.055878f },
        { 1513.095337f, 1457.825928f, 352.039551f },
        { 1511.042725f, 1457.432739f, 351.995850f },
        { 1508.673462f, 1457.407104f, 350.867157f },
        { 1503.436035f, 1457.554199f, 348.189697f },
        { 1499.937378f, 1457.652466f, 346.333038f },
        { 1498.188110f, 1457.701660f, 345.347198f },
        { 1496.438721f, 1457.750854f, 344.361328f },
        { 1492.940186f, 1457.849121f, 342.794586f },
        { 1489.441528f, 1457.947388f, 342.794586f },
        { 1485.942871f, 1458.045654f, 342.794586f },
        { 1483.437866f, 1458.116089f, 342.794586f },
        { 1479.939331f, 1458.214355f, 342.794586f },
        { 1476.440674f, 1458.312622f, 342.794586f },
        { 1472.942017f, 1458.410889f, 342.794586f },
        { 1469.443481f, 1458.509277f, 342.794586f },
        { 1465.944824f, 1458.607544f, 342.794586f },
        { 1462.446167f, 1458.705811f, 342.794586f },
        { 1458.947510f, 1458.804077f, 342.769318f },
        { 1455.448975f, 1458.902466f, 342.712677f },
        { 1451.950317f, 1459.000732f, 342.544769f },
        { 1448.451660f, 1459.098999f, 342.374695f },
        { 1444.953125f, 1459.197388f, 342.207306f },
        { 1441.454468f, 1459.295654f, 341.917297f },
        { 1437.955811f, 1459.393921f, 340.902740f },
        { 1434.457275f, 1459.492188f, 339.802673f },
        { 1432.260254f, 1459.562256f, 339.066650f },
        { 1428.762939f, 1459.700073f, 337.779053f },
        { 1425.265747f, 1459.839600f, 336.474426f },
        { 1421.768555f, 1459.979126f, 335.326447f },
        { 1418.271362f, 1460.118652f, 334.119293f },
        { 1414.774048f, 1460.256104f, 332.920868f },
        { 1411.276367f, 1460.381836f, 331.732574f },
        { 1407.780884f, 1460.507568f, 330.997406f },
        { 1404.283203f, 1460.633301f, 329.889557f },
        { 1400.785400f, 1460.759155f, 328.781250f },
        { 1397.292969f, 1460.884766f, 327.669098f },
        { 1393.794434f, 1460.988281f, 326.786163f },
        { 1390.295776f, 1461.086670f, 326.139191f },
        { 1386.797241f, 1461.184937f, 325.492188f },
        { 1383.298584f, 1461.283203f, 325.042755f },
        { 1379.799927f, 1461.381592f, 324.709137f },
        { 1376.301392f, 1461.479858f, 324.641296f },
        { 1372.497681f, 1461.658569f, 324.352661f },
        { 1368.832764f, 1461.794556f, 324.424835f },
        { 1365.363892f, 1462.111572f, 324.550446f },
        { 1362.263916f, 1462.581543f, 324.540802f },
        { 1360.026123f, 1462.991577f, 324.527954f },
        { 1357.143188f, 1463.685181f, 324.512390f },
        { 1355.280884f, 1464.566162f, 324.504120f },
        { 1352.371094f, 1466.509033f, 323.777435f },
        { 1350.411255f, 1467.941162f, 323.563019f },
        { 1348.453369f, 1469.752075f, 323.486572f },
        { 1345.903320f, 1472.149414f, 323.599152f },
        { 1342.267822f, 1475.537842f, 324.050049f },
        { 1339.601807f, 1477.717041f, 323.933868f },
        { 1337.467529f, 1479.362671f, 323.728638f },
        { 1334.694824f, 1481.498657f, 323.404602f },
        { 1331.922119f, 1483.634521f, 322.749237f },
        { 1329.149414f, 1485.770386f, 321.214813f },
        { 1327.693481f, 1486.891968f, 319.790985f },
        { 1325.630615f, 1488.481079f, 317.461914f },
        { 1321.419800f, 1491.709106f, 316.890533f },
        { 1318.605469f, 1493.789917f, 316.681305f },
        { 1315.981567f, 1495.668213f, 316.683960f },
        { 1313.328857f, 1497.417969f, 316.449219f },
        { 1310.708496f, 1498.954224f, 316.410828f },
        { 1308.000488f, 1500.281616f, 316.456696f },
        { 1305.073975f, 1501.402588f, 316.462494f },
        { 1301.961914f, 1502.380127f, 316.630585f },
        { 1299.650513f, 1503.029541f, 316.699951f },
        { 1296.658325f, 1503.816650f, 316.870392f },
        { 1293.816284f, 1504.482788f, 316.927216f },
        { 1290.129150f, 1505.248413f, 316.851959f },
        { 1286.622070f, 1505.876221f, 316.783875f },
        { 1283.159668f, 1506.388062f, 316.519775f },
        { 1279.993164f, 1506.968994f, 316.153656f },
        { 1276.569214f, 1507.694214f, 315.539093f },
        { 1273.145142f, 1508.419556f, 314.788818f },
        { 1269.721069f, 1509.144775f, 314.033905f },
        { 1266.297119f, 1509.869995f, 313.207520f },
        { 1262.873047f, 1510.595337f, 312.125366f },
        { 1259.448975f, 1511.320557f, 310.935852f },
        { 1256.025513f, 1512.045654f, 310.170349f },
        { 1252.601562f, 1512.770996f, 309.511353f },
        { 1249.177490f, 1513.496216f, 308.814026f },
        { 1245.753418f, 1514.221436f, 307.993408f },
        { 1242.333984f, 1514.945801f, 306.966217f },
        { 1238.909912f, 1515.671143f, 306.393311f },
        { 1235.485840f, 1516.396362f, 305.991486f },
        { 1232.061890f, 1517.121582f, 305.601501f },
        { 1228.643677f, 1517.874146f, 305.302155f },
        { 1225.228394f, 1518.639648f, 305.356689f },
        { 1221.813232f, 1519.405151f, 305.779358f },
        { 1218.397949f, 1520.170776f, 306.302643f },
        { 1214.982666f, 1520.936279f, 306.823395f },
        { 1211.567505f, 1521.701904f, 307.329865f },
        { 1208.152222f, 1522.467407f, 307.744232f },
        { 1204.751465f, 1523.293945f, 307.959015f },
        { 1201.355225f, 1524.139771f, 307.794312f },
        { 1197.960693f, 1524.992920f, 307.318909f },
        { 1194.568359f, 1525.851929f, 306.629608f },
        { 1191.175781f, 1526.710938f, 305.882446f },
        { 1187.788330f, 1527.569824f, 305.176971f },
        { 1184.405640f, 1528.468140f, 304.446259f },
        { 1181.023071f, 1529.367188f, 303.686646f },
        { 1177.640503f, 1530.266235f, 302.955780f },
        { 1174.508911f, 1531.045044f, 302.373840f },
        { 1170.935181f, 1531.838867f, 302.064514f },
        { 1167.503784f, 1532.528564f, 302.516022f },
        { 1165.579590f, 1532.862671f, 303.009521f },
        { 1162.618408f, 1533.260620f, 303.888550f },
        { 1159.577881f, 1533.404053f, 304.674225f },
        { 1157.584351f, 1533.335693f, 305.134979f },
        { 1154.447144f, 1533.075684f, 305.702179f },
        { 1151.611816f, 1532.554932f, 306.108551f },
        { 1148.212891f, 1531.719849f, 306.519806f },
        { 1144.815674f, 1530.878174f, 306.787903f },
        { 1141.418823f, 1530.035034f, 306.907867f },
        { 1138.024780f, 1529.179932f, 307.296295f },
        { 1135.565186f, 1528.570557f, 307.910736f },
        { 1132.164551f, 1527.742310f, 308.992218f },
        { 1128.764038f, 1526.913940f, 310.116333f },
        { 1123.950806f, 1525.764648f, 310.357361f },
        { 1121.053833f, 1525.496338f, 310.343201f },
        { 1117.156006f, 1525.902344f, 310.437958f },
        { 1113.677368f, 1526.288574f, 311.573364f },
        { 1110.198730f, 1526.674683f, 313.554108f },
        { 1106.720093f, 1527.060913f, 314.639282f },
        { 1103.738159f, 1527.671021f, 315.011993f },
        { 1101.503296f, 1528.325073f, 315.243256f },
        { 1099.339478f, 1529.552979f, 315.445435f },
        { 1094.908081f, 1532.584229f, 315.751862f },
        { 1090.806763f, 1535.366089f, 316.017975f },
        { 1088.588623f, 1537.391235f, 316.293365f },
        { 1086.224487f, 1539.972046f, 316.755402f },
        { 1083.863770f, 1542.409790f, 317.333557f },
        { 1081.441406f, 1544.431763f, 317.928192f },
        { 1078.955688f, 1546.163696f, 318.347687f },
        { 1076.551758f, 1547.326416f, 318.701996f },
        { 1073.675415f, 1548.153809f, 319.267578f },
        { 1070.697144f, 1548.668701f, 320.206329f },
        { 1068.264648f, 1548.698853f, 321.305389f },
        { 1065.346069f, 1548.362061f, 322.707672f },
        { 1062.492554f, 1547.583984f, 324.268127f },
        { 1059.495483f, 1546.408691f, 326.003845f },
        { 1056.713379f, 1545.205811f, 327.534729f },
        { 1054.066406f, 1543.944092f, 328.997986f },
        { 1052.159546f, 1542.797852f, 330.027313f },
        { 1050.430908f, 1541.547974f, 331.048187f },
        { 1048.547974f, 1539.841187f, 332.333496f },
        { 1047.232056f, 1538.162598f, 333.378845f },
        { 1046.146606f, 1536.438843f, 334.337097f },
        { 1045.173828f, 1534.393921f, 335.361694f },
        { 1044.294678f, 1531.546387f, 336.205963f },
        { 1043.838501f, 1529.561035f, 336.730865f },
        { 1043.285889f, 1526.390259f, 337.236298f },
        { 1042.995850f, 1523.324219f, 337.740112f },
        { 1042.953613f, 1520.562866f, 338.171661f },
        { 1043.454712f, 1517.677246f, 338.415802f },
        { 1044.390503f, 1514.848022f, 338.777344f },
        { 1045.759888f, 1511.780640f, 339.439484f },
        { 1047.211182f, 1508.820312f, 340.237701f },
        { 1048.761353f, 1505.682373f, 341.069183f },
        { 1050.028931f, 1502.983276f, 341.601593f },
        { 1051.473389f, 1499.795166f, 341.950531f },
        { 1052.880249f, 1496.537476f, 342.117035f },
        { 1053.735962f, 1494.384155f, 342.241943f },
        { 1054.469727f, 1492.386841f, 342.346558f },
        { 1055.274658f, 1489.903198f, 342.474213f },
        { 1056.327881f, 1486.565430f, 342.578400f },
        { 1057.020874f, 1483.631104f, 342.600494f },
        { 1056.909912f, 1480.696289f, 342.477661f },
        { 1056.332031f, 1477.784790f, 342.462860f },
        { 1055.296631f, 1474.946411f, 342.247009f },
        { 1053.511597f, 1471.489258f, 342.291290f },
        { 1051.795532f, 1469.036987f, 342.235748f },
        { 1049.375977f, 1466.396240f, 342.094208f },
        { 1046.861816f, 1463.962891f, 342.084381f },
        { 1044.666382f, 1462.043335f, 342.157684f },
        { 1042.253784f, 1460.352051f, 342.275024f },
        { 1040.285889f, 1458.988892f, 342.294312f },
        { 1036.305664f, 1456.474365f, 342.778229f },
        { 1034.238159f, 1455.819092f, 343.027618f },
        { 1031.208496f, 1455.126465f, 343.482788f },
        { 1028.612061f, 1454.274902f, 343.873108f },
        { 1025.752197f, 1452.970825f, 344.327820f },
        { 1022.764404f, 1451.404785f, 344.944550f },
        { 1019.397705f, 1449.278442f, 346.006378f },
        { 1016.881897f, 1447.349121f, 347.232056f },
        { 1014.547791f, 1445.003418f, 346.964752f },
        { 1012.718628f, 1442.135742f, 346.441528f },
        { 1010.852966f, 1439.174438f, 345.984711f },
        { 1008.987366f, 1436.213135f, 345.360992f },
        { 1007.121704f, 1433.251831f, 344.999908f },
        { 1005.256042f, 1430.290527f, 344.711212f },
        { 1003.515625f, 1427.449097f, 344.467377f },
        { 1001.860168f, 1424.326538f, 344.268768f },
        { 999.744019f, 1422.786377f, 344.428436f },
        { 996.419678f, 1422.746094f, 344.808258f },
        { 992.919800f, 1422.720825f, 345.181458f },
        { 989.419861f, 1422.695557f, 345.533508f },
        { 985.919983f, 1422.670288f, 345.427307f },
        { 982.419983f, 1422.657227f, 345.466278f },
        { 980.033020f, 1422.658691f, 345.458527f },
        { 976.533020f, 1422.660889f, 345.368073f },
        { 973.033020f, 1422.663086f, 345.260803f },
        { 969.533020f, 1422.665283f, 345.566864f },
        { 966.036804f, 1422.667603f, 345.222778f },
        { 962.536804f, 1422.669800f, 345.567169f },
        { 959.036804f, 1422.671997f, 345.226349f },
        { 956.663818f, 1422.682983f, 345.424438f },
        { 951.666626f, 1422.767944f, 345.834442f },
        { 948.804382f, 1423.039551f, 345.380615f },
        { 946.266479f, 1423.785645f, 345.417542f },
        { 943.318726f, 1424.768677f, 345.459839f },
        { 940.635315f, 1425.806030f, 345.499939f },
        { 936.049866f, 1427.626099f, 345.536102f },
        { 932.840759f, 1428.755249f, 345.536102f },
        { 929.545349f, 1429.870361f, 345.536102f },
        { 926.644592f, 1430.748535f, 345.566772f },
        { 922.114746f, 1432.067627f, 345.462280f },
        { 918.754333f, 1433.046143f, 345.962036f },
        { 916.624268f, 1433.692505f, 346.189087f },
    };

    // Horde flag carrier: enemy Alliance flag to Horde capture point.
    // Already recorded and stored Alliance -> Horde.
    static G3D::Vector3 const WSGHordeFlagCarrierRoute[] =
    {
        { 1538.404541f, 1481.562256f, 352.426056f },
        { 1534.840698f, 1482.769409f, 351.915283f },
        { 1531.529541f, 1483.890991f, 351.959351f },
        { 1528.476074f, 1484.997437f, 351.965271f },
        { 1525.212891f, 1486.263306f, 351.971832f },
        { 1520.593018f, 1488.114136f, 351.985474f },
        { 1517.349854f, 1489.430176f, 351.997498f },
        { 1514.106689f, 1490.746094f, 352.009491f },
        { 1510.854980f, 1491.894409f, 352.009552f },
        { 1506.128784f, 1492.985840f, 352.012360f },
        { 1503.009155f, 1493.332764f, 352.201111f },
        { 1499.376465f, 1493.370728f, 352.194183f },
        { 1495.878540f, 1493.386597f, 352.192780f },
        { 1492.378540f, 1493.402466f, 352.167023f },
        { 1488.878540f, 1493.418457f, 352.136719f },
        { 1485.378662f, 1493.434326f, 352.097748f },
        { 1481.878784f, 1493.457153f, 351.910522f },
        { 1478.379272f, 1493.514404f, 351.658783f },
        { 1473.814087f, 1493.869873f, 351.566711f },
        { 1469.969116f, 1494.168091f, 351.768616f },
        { 1464.556030f, 1494.396118f, 352.017914f },
        { 1461.056763f, 1494.437622f, 351.723480f },
        { 1457.243896f, 1494.317139f, 351.207245f },
        { 1454.304199f, 1494.280762f, 350.694977f },
        { 1452.295532f, 1494.246948f, 350.480469f },
        { 1448.798950f, 1494.093018f, 350.117462f },
        { 1445.302856f, 1493.928589f, 349.668427f },
        { 1440.667969f, 1493.593140f, 349.290344f },
        { 1437.687744f, 1492.532593f, 349.002869f },
        { 1434.574463f, 1490.933350f, 348.827362f },
        { 1432.002563f, 1489.582764f, 348.740936f },
        { 1429.497559f, 1487.944580f, 348.592773f },
        { 1425.517090f, 1484.887329f, 348.684174f },
        { 1422.741333f, 1482.638184f, 348.618378f },
        { 1420.812744f, 1480.899658f, 348.594818f },
        { 1417.355957f, 1477.261841f, 348.412445f },
        { 1415.499023f, 1474.993652f, 348.367126f },
        { 1413.730469f, 1472.618774f, 348.187073f },
        { 1412.154907f, 1470.096802f, 348.031952f },
        { 1410.807861f, 1467.484131f, 347.881226f },
        { 1409.366699f, 1464.425537f, 347.689941f },
        { 1408.340332f, 1461.672119f, 347.519379f },
        { 1407.625977f, 1459.490112f, 347.339813f },
        { 1406.558472f, 1456.157227f, 347.214996f },
        { 1405.870972f, 1453.315186f, 347.179840f },
        { 1405.605957f, 1450.405029f, 347.299988f },
        { 1405.592285f, 1447.164185f, 347.322327f },
        { 1405.643311f, 1443.664551f, 347.043823f },
        { 1405.658203f, 1438.764771f, 346.545166f },
        { 1405.811157f, 1435.688721f, 346.179688f },
        { 1406.153931f, 1431.868286f, 345.723663f },
        { 1406.521973f, 1428.387695f, 345.446259f },
        { 1406.887573f, 1424.906982f, 345.372131f },
        { 1407.181152f, 1421.420898f, 345.176697f },
        { 1407.191284f, 1416.927124f, 344.853790f },
        { 1407.044434f, 1414.518677f, 344.617828f },
        { 1406.367188f, 1412.247803f, 344.334778f },
        { 1405.487549f, 1409.894653f, 343.829376f },
        { 1404.035522f, 1406.749390f, 342.927612f },
        { 1402.441284f, 1404.309692f, 341.923462f },
        { 1400.513916f, 1402.016235f, 340.637115f },
        { 1398.325562f, 1399.625854f, 339.169647f },
        { 1396.724854f, 1397.921875f, 338.128723f },
        { 1394.317383f, 1395.381470f, 336.637360f },
        { 1391.909790f, 1392.840942f, 335.426819f },
        { 1389.501221f, 1390.301636f, 334.421875f },
        { 1387.625244f, 1388.376587f, 333.823181f },
        { 1385.542725f, 1386.361572f, 333.261139f },
        { 1383.169067f, 1384.457642f, 332.666565f },
        { 1381.154907f, 1383.263062f, 332.177094f },
        { 1378.195068f, 1382.697021f, 331.483734f },
        { 1375.925903f, 1382.954224f, 330.883789f },
        { 1373.096802f, 1383.793701f, 330.090607f },
        { 1370.309692f, 1384.981567f, 329.187469f },
        { 1367.731201f, 1386.418945f, 328.361786f },
        { 1365.287231f, 1388.174316f, 327.611206f },
        { 1362.998291f, 1390.106201f, 327.035461f },
        { 1360.804565f, 1392.177002f, 326.607574f },
        { 1357.100830f, 1396.015015f, 325.918518f },
        { 1354.806885f, 1398.283325f, 325.674652f },
        { 1350.952148f, 1401.430542f, 325.196411f },
        { 1348.216675f, 1403.613281f, 324.850525f },
        { 1345.702271f, 1405.227295f, 324.256134f },
        { 1341.573975f, 1407.436279f, 322.300140f },
        { 1338.870361f, 1408.362427f, 320.272736f },
        { 1336.728760f, 1408.884277f, 319.234619f },
        { 1332.881348f, 1409.210693f, 318.418640f },
        { 1329.648804f, 1409.186890f, 318.191406f },
        { 1327.505981f, 1409.038086f, 318.048126f },
        { 1324.957764f, 1408.448853f, 317.721893f },
        { 1321.580322f, 1407.531250f, 317.518036f },
        { 1318.186401f, 1406.676270f, 317.372253f },
        { 1315.038940f, 1405.933350f, 316.912567f },
        { 1311.623657f, 1405.168457f, 315.948792f },
        { 1308.208374f, 1404.402710f, 314.816528f },
        { 1305.395996f, 1403.589722f, 314.300964f },
        { 1303.199951f, 1402.405396f, 313.878448f },
        { 1301.304321f, 1400.826782f, 313.556732f },
        { 1298.814941f, 1397.973267f, 313.656250f },
        { 1296.540161f, 1395.313354f, 314.006744f },
        { 1294.265381f, 1392.653320f, 313.908875f },
        { 1292.695435f, 1390.818359f, 313.715637f },
        { 1290.399902f, 1388.176392f, 313.292114f },
        { 1288.104614f, 1385.534058f, 312.624939f },
        { 1285.960449f, 1382.956665f, 311.740204f },
        { 1284.109497f, 1380.618896f, 310.766571f },
        { 1282.011230f, 1377.817505f, 309.883331f },
        { 1280.640991f, 1375.949097f, 309.543030f },
        { 1278.581909f, 1373.118896f, 309.341797f },
        { 1276.706177f, 1370.562134f, 309.371765f },
        { 1274.614014f, 1367.756348f, 309.455414f },
        { 1272.531250f, 1364.943481f, 309.443451f },
        { 1269.697510f, 1360.924194f, 309.667969f },
        { 1267.683105f, 1358.062012f, 310.012390f },
        { 1265.668823f, 1355.199707f, 311.005859f },
        { 1263.654419f, 1352.337524f, 311.576355f },
        { 1261.640137f, 1349.475220f, 311.797089f },
        { 1259.625732f, 1346.613037f, 311.867950f },
        { 1257.705444f, 1344.161499f, 312.461731f },
        { 1256.327026f, 1342.595825f, 313.060547f },
        { 1254.148315f, 1340.491699f, 313.961029f },
        { 1251.781128f, 1338.765625f, 314.598206f },
        { 1249.530762f, 1337.754639f, 314.840424f },
        { 1246.997192f, 1337.140137f, 314.619965f },
        { 1243.926025f, 1336.848145f, 314.029205f },
        { 1241.086426f, 1336.768799f, 313.313690f },
        { 1236.144165f, 1337.354980f, 312.277588f },
        { 1233.594482f, 1337.668945f, 312.036285f },
        { 1230.710815f, 1338.294922f, 312.115448f },
        { 1228.335205f, 1339.315186f, 312.231964f },
        { 1226.373779f, 1340.452148f, 312.370575f },
        { 1223.196045f, 1343.214478f, 312.569092f },
        { 1221.019287f, 1344.800537f, 312.681122f },
        { 1216.282837f, 1346.826294f, 313.008789f },
        { 1213.427612f, 1347.709229f, 313.281952f },
        { 1208.947632f, 1348.919800f, 313.572937f },
        { 1206.739990f, 1349.440063f, 313.639526f },
        { 1203.321777f, 1350.192383f, 313.663177f },
        { 1199.903564f, 1350.944580f, 313.598969f },
        { 1196.485107f, 1351.695312f, 313.486755f },
        { 1191.493896f, 1352.652466f, 313.265747f },
        { 1188.621460f, 1353.030640f, 313.108673f },
        { 1185.117065f, 1353.537476f, 312.932556f },
        { 1182.049683f, 1354.116577f, 312.767273f },
        { 1179.142456f, 1354.779785f, 312.683838f },
        { 1176.855957f, 1355.385376f, 313.002350f },
        { 1173.978760f, 1356.478149f, 313.381744f },
        { 1172.096924f, 1357.345215f, 313.576935f },
        { 1169.513306f, 1358.733276f, 313.687164f },
        { 1167.130249f, 1360.107056f, 313.613892f },
        { 1164.140015f, 1361.925903f, 313.552307f },
        { 1161.150146f, 1363.744873f, 313.543121f },
        { 1158.166992f, 1365.575562f, 313.443939f },
        { 1156.420776f, 1366.664307f, 313.316803f },
        { 1154.151001f, 1368.203491f, 313.202026f },
        { 1151.703491f, 1370.140625f, 312.835541f },
        { 1149.408691f, 1372.333740f, 312.407135f },
        { 1148.103882f, 1374.148682f, 312.131317f },
        { 1147.128418f, 1376.398560f, 311.925537f },
        { 1146.639648f, 1378.658203f, 311.794617f },
        { 1146.489136f, 1381.781738f, 311.639618f },
        { 1146.710327f, 1384.795288f, 311.534943f },
        { 1146.985962f, 1387.194580f, 311.475220f },
        { 1147.226929f, 1389.210327f, 311.429138f },
        { 1147.819702f, 1393.382446f, 311.314728f },
        { 1148.355957f, 1396.841064f, 311.214417f },
        { 1148.892090f, 1400.299805f, 311.028503f },
        { 1149.428223f, 1403.758545f, 310.707977f },
        { 1149.964355f, 1407.217163f, 310.496674f },
        { 1150.418701f, 1410.213989f, 310.372620f },
        { 1150.861938f, 1413.593994f, 310.259827f },
        { 1151.070679f, 1416.798584f, 310.213379f },
        { 1150.893921f, 1419.816284f, 310.084076f },
        { 1150.495239f, 1422.716431f, 310.052399f },
        { 1150.106323f, 1424.808228f, 310.060852f },
        { 1148.980591f, 1429.699097f, 310.189514f },
        { 1148.096680f, 1432.744019f, 310.244171f },
        { 1146.869629f, 1436.065796f, 310.382965f },
        { 1144.906372f, 1440.783569f, 310.872314f },
        { 1143.445679f, 1443.909546f, 311.308472f },
        { 1141.984741f, 1446.719604f, 311.805420f },
        { 1140.633789f, 1449.107666f, 312.305664f },
        { 1139.244263f, 1450.953857f, 312.726593f },
        { 1137.037842f, 1452.883423f, 313.271545f },
        { 1132.784546f, 1455.885742f, 314.154633f },
        { 1131.100464f, 1457.018677f, 314.480835f },
        { 1128.285889f, 1458.651978f, 315.000092f },
        { 1125.411865f, 1459.882568f, 315.595520f },
        { 1122.222656f, 1460.937012f, 315.971497f },
        { 1119.301270f, 1461.532104f, 316.333710f },
        { 1114.386353f, 1462.153564f, 316.557892f },
        { 1111.070190f, 1462.198730f, 316.557892f },
        { 1107.573975f, 1462.036011f, 316.557892f },
        { 1102.577271f, 1461.647217f, 316.549103f },
        { 1099.088989f, 1461.361206f, 316.543121f },
        { 1095.600708f, 1461.075073f, 316.859314f },
        { 1092.112427f, 1460.788940f, 316.705933f },
        { 1088.623779f, 1460.508911f, 316.360565f },
        { 1085.132446f, 1460.264038f, 316.358154f },
        { 1081.640991f, 1460.019043f, 316.304443f },
        { 1078.149536f, 1459.774048f, 316.475830f },
        { 1074.657837f, 1459.534058f, 316.903625f },
        { 1071.842285f, 1459.358521f, 317.516541f },
        { 1068.347412f, 1459.168335f, 318.358307f },
        { 1065.032593f, 1459.022339f, 319.207855f },
        { 1059.887451f, 1458.916870f, 320.556946f },
        { 1056.388062f, 1458.850342f, 321.509918f },
        { 1052.888672f, 1458.783813f, 322.651886f },
        { 1049.563965f, 1458.789551f, 323.732635f },
        { 1046.064697f, 1458.860474f, 324.989838f },
        { 1042.565430f, 1458.931396f, 326.264832f },
        { 1040.577759f, 1458.965332f, 326.881805f },
        { 1037.077881f, 1458.995972f, 328.009735f },
        { 1034.711914f, 1459.007690f, 328.772614f },
        { 1031.211914f, 1459.009888f, 329.900726f },
        { 1028.328003f, 1458.997192f, 330.776398f },
        { 1024.828125f, 1458.958252f, 331.779388f },
        { 1021.328369f, 1458.919189f, 332.646027f },
        { 1017.828613f, 1458.880249f, 333.467804f },
        { 1014.328857f, 1458.841187f, 334.094696f },
        { 1010.829041f, 1458.802246f, 334.696594f },
        { 1007.329285f, 1458.763184f, 335.103912f },
        { 1003.835327f, 1458.724243f, 335.401154f },
        { 1000.335510f, 1458.685181f, 335.592590f },
        { 995.568848f, 1458.632080f, 335.503693f },
        { 992.069031f, 1458.593140f, 335.323090f },
        { 988.569275f, 1458.554077f, 335.769073f },
        { 985.069397f, 1458.523438f, 336.134460f },
        { 981.569458f, 1458.511841f, 336.210693f },
        { 978.069458f, 1458.500366f, 336.295532f },
        { 974.571289f, 1458.488892f, 336.720428f },
        { 971.071289f, 1458.477295f, 337.039093f },
        { 967.571289f, 1458.465820f, 337.311462f },
        { 965.611328f, 1458.459351f, 337.840973f },
        { 962.111328f, 1458.457886f, 338.980682f },
        { 958.611328f, 1458.472290f, 340.159180f },
        { 955.111389f, 1458.488281f, 341.433411f },
        { 951.611450f, 1458.504272f, 342.325470f },
        { 948.111450f, 1458.520142f, 343.342896f },
        { 944.604492f, 1458.536255f, 344.277039f },
        { 941.972595f, 1458.533203f, 344.632812f },
        { 939.265991f, 1457.999878f, 344.739746f },
        { 937.371216f, 1456.154785f, 344.903137f },
        { 937.948486f, 1452.523560f, 345.540192f },
        { 938.001282f, 1450.149414f, 345.538788f },
        { 937.698792f, 1448.034180f, 345.546326f },
        { 937.131531f, 1446.012939f, 345.535797f },
        { 935.089355f, 1441.481812f, 345.535797f },
        { 934.340149f, 1439.206543f, 345.535797f },
        { 933.420654f, 1435.873901f, 345.535797f },
        { 932.073547f, 1433.531128f, 345.535797f },
        { 929.611694f, 1432.639648f, 345.535797f },
        { 925.199280f, 1433.165405f, 345.626221f },
        { 923.145935f, 1433.442505f, 345.716675f },
        { 919.763733f, 1433.867310f, 345.807281f },
        { 917.366516f, 1434.093872f, 346.351349f },
    };

    static int16 const WSGLegionAlternativeRouteCount =
        int16(sizeof(WSGLegionAlternativeRoute) /
            sizeof(WSGLegionAlternativeRoute[0]));

    static int16 const WSGLegionHordeAlternativeRouteCount =
        int16(sizeof(WSGLegionHordeAlternativeRoute) /
            sizeof(WSGLegionHordeAlternativeRoute[0]));

    static int16 const WSGAllianceFlagCarrierRouteCount =
        int16(sizeof(WSGAllianceFlagCarrierRoute) /
            sizeof(WSGAllianceFlagCarrierRoute[0]));

    static int16 const WSGHordeFlagCarrierRouteCount =
        int16(sizeof(WSGHordeFlagCarrierRoute) /
            sizeof(WSGHordeFlagCarrierRoute[0]));

    static bool IsWSGFlagCarrier(Player* player)
    {
        if (!player || player->GetMapId() != 489)
            return false;

        BattlegroundWarsongGulch* battleground =
            dynamic_cast<BattlegroundWarsongGulch*>(
                player->GetBattleground());

        if (!battleground)
            return false;

        if (player->GetBGTeamId() == TEAM_ALLIANCE)
        {
            return
                battleground->GetFlagState(HORDE) ==
                    BG_WS_FLAG_STATE_ON_PLAYER &&
                battleground->GetFlagPickerGUID(
                    TEAM_HORDE, 0) == player->GetGUID();
        }

        if (player->GetBGTeamId() == TEAM_HORDE)
        {
            return
                battleground->GetFlagState(ALLIANCE) ==
                    BG_WS_FLAG_STATE_ON_PLAYER &&
                battleground->GetFlagPickerGUID(
                    TEAM_ALLIANCE, 0) == player->GetGUID();
        }

        return false;
    }
    static G3D::Vector3 const* GetWSGRoute(uint8 variant)
    {
        if (variant == 2)
            return WSGLegionAlternativeRoute;

        if (variant == 3)
            return WSGLegionHordeAlternativeRoute;

        if (variant == 4)
            return WSGAllianceFlagCarrierRoute;

        if (variant == 5)
            return WSGHordeFlagCarrierRoute;

        return WSGLegionTunnelRoute;
    }

    static int16 GetWSGRouteCount(uint8 variant)
    {
        if (variant == 2)
            return WSGLegionAlternativeRouteCount;

        if (variant == 3)
            return WSGLegionHordeAlternativeRouteCount;

        if (variant == 4)
            return WSGAllianceFlagCarrierRouteCount;

        if (variant == 5)
            return WSGHordeFlagCarrierRouteCount;

        return WSGLegionTunnelRouteCount;
    }
}
// FELSORIA_WSG_GROUND_V1: recorded waypoints are goals, never permission to
// move through walls, onto another floor, or along an airborne recording.
void BotBGAIMovement::ObserveWSGGround()
{
    if (m_WSGGroundInstance != m_Player->GetInstanceId())
    {
        m_WSGGroundInstance = m_Player->GetInstanceId();
        m_WSGTaskRouting=false;m_WSGExecutingTaskRoute=false;
        m_WSGExitingGraveyard=false;m_WSGExecutingExit=false;m_WSGExitVariant=0;
        ResetWSGTask();targetGuid.Clear();pTargetAIWP=nullptr;
        m_WSGHasSafeGround = false;
        m_WSGGroundRoutePath = false;
        m_WSGWalkFallback = false;
        m_WSGDropActive = false;
        m_WSGDropTime=0;
        m_WSGActivityTime=0;m_WSGFailedTargetTime=0;m_WSGBlockedUntil=0;
        m_WSGFailedTarget.Clear();m_WSGBlockedTarget.Clear();m_WSGHasObjective=false;
        m_WSGProgressTime = 0;
        m_WSGRetryUntil = 0;
        m_WSGChunkStart = -1;
        m_WSGRouteIndex = -1;
        m_WSGRouteDirection = 0;
        m_WSGRouteVariant = 0;
        m_WSGEscortGuid.Clear();
        m_WSGEscortSpline = false;
    }
    if (m_WSGDropActive && (!m_Player->movespline || m_Player->movespline->Finalized() ||
        uint32(getMSTime()-m_WSGDropTime)>=5000))
    {
        if (m_Player->movespline && !m_Player->movespline->Finalized() && IsWSGOnNavGround())
        {m_Player->StopMoving();m_Player->GetMotionMaster()->Clear();}
        m_WSGDropActive=false;
        m_Player->ClearUnitState(UNIT_STATE_LONG_JUMP);
    }
    if (!m_Player->isAlive() || WSGMovementProtected() || !IsWSGOnNavGround())
        return;
    m_WSGSafeGround = m_Player->GetPosition();
    m_WSGSafeGroundTime = getMSTime();
    m_WSGHasSafeGround = true;
}

bool BotBGAIMovement::IsWSGDropInProgress() const
{
    return m_BGAI && m_Player->GetMapId()==489 && m_Player->isAlive() &&
        m_WSGDropActive && m_Player->movespline && !m_Player->movespline->Finalized() &&
        uint32(getMSTime()-m_WSGDropTime)<5000;
}

bool BotBGAIMovement::WSGMovementProtected() const
{
    return !m_Player->IsInWorld() || !m_Player->isAlive() ||
        m_Player->IsBeingTeleported() || m_Player->IsChangeMap() ||
        m_Player->GetVehicleBase() || m_Player->GetTransport() || m_Player->isInFlight() ||
        m_Player->IsFlying() ||
        IsWSGDropInProgress() ||
        m_Player->HasUnitState(UNIT_STATE_CASTING | UNIT_STATE_JUMPING |
            UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING) ||
        m_Player->HasAuraWithMechanic(1 << Mechanics::MECHANIC_ROOT) ||
        m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
}

bool BotBGAIMovement::IsWSGOnNavGround() const
{
    dtNavMeshQuery const* query = MMAP::MMapFactory::createOrGetMMapManager()->
        GetNavMeshQuery(489, m_Player->GetInstanceId());
    if (!query) return false;
    float point[3] = {m_Player->GetPositionY(), m_Player->GetPositionZ(), m_Player->GetPositionX()};
    float extent[3] = {0.75f, 1.25f, 0.75f}, closest[3] = {};
    dtQueryFilter filter;
    filter.setIncludeFlags(NAV_GROUND);
    filter.setExcludeFlags(0);
    dtPolyRef polygon = 0;
    if (dtStatusFailed(query->findNearestPoly(point, extent, &filter, &polygon, closest)) || !polygon)
        return false;
    float dx = point[0]-closest[0], dz = point[2]-closest[2];
    return dx*dx+dz*dz <= 0.5625f && std::abs(point[1]-closest[1]) <= 1.25f;
}

void BotBGAIMovement::PauseForCast()
{
    if (!m_BGAI || m_Player->GetMapId()!=489) {ClearMovement();return;}
    if (IsWSGDropInProgress()) return;
    // Casting pauses an existing objective. A new commander order still calls
    // ClearMovement normally; it must not be confused with this short pause.
    m_Player->StopMoving();
    m_WSGEscortSpline = false;
    m_WSGProgressTime = 0;
    m_MovementTick = 0;
}

void BotBGAIMovement::LogWSGGroundReject(char const* reason, G3D::Vector3 const& goal)
{
    uint32 now = getMSTime();
    if (uint32(now-m_WSGExitRejectLogTime)<10000) return;
    m_WSGExitRejectLogTime = now;
    TC_LOG_INFO(LOG_FILTER_PATH_GENERATOR,
        "WSG_GROUND_PATH_REJECT bot=%u reason=%s from=(%.2f,%.2f,%.2f) goal=(%.2f,%.2f,%.2f)",
        m_Player->GetGUIDLow(), reason, m_Player->GetPositionX(), m_Player->GetPositionY(),
        m_Player->GetPositionZ(), goal.x, goal.y, goal.z);
}

bool BotBGAIMovement::WaitForWSGGroundSpline()
{
    ObserveWSGGround();
    if (!m_Player->movespline || m_Player->movespline->Finalized())
    {m_WSGProgressTime=0;return false;}
    uint32 now = getMSTime();
    if (WSGMovementProtected()) {m_WSGProgressTime=now;return true;}
    if (!m_WSGProgressTime || m_Player->GetExactDist(&m_WSGProgressPosition)>0.35f)
    {
        m_WSGProgressPosition=m_Player->GetPosition();m_WSGProgressTime=now;
        return true;
    }
    if (uint32(now-m_WSGProgressTime)<5000) return true;
    m_Player->StopMoving();
    m_Player->GetMotionMaster()->Clear();
    m_WSGProgressTime=0;
    m_WSGEscortSpline=false;
    m_WSGRetryUntil=0;
    m_MovementTick=0;
    LogWSGGroundReject("no_spline_progress_replan",m_Player->GetVector3());
    return false;
}

bool BotBGAIMovement::ValidateWSGGroundPath(Pathfinding const& path,
    G3D::Vector3 const& start, G3D::Vector3 const& goal,
    std::vector<G3D::Vector3>& output, G3D::Vector3* blocked) const
{
    if (path.GetPathType()!=PATHFIND_NORMAL || path.GetPath().size()<2 || path.GetPath().size()>256)
        return false;
    G3D::Vector3 const& end=path.GetActualEndPosition();
    float ex=end.x-goal.x, ey=end.y-goal.y;
    if (!std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z) ||
        ex*ex+ey*ey>4.0f || std::abs(end.z-goal.z)>3.0f)
        return false;
    auto points=path.GetPath();
    if (!std::isfinite(points.front().x) || !std::isfinite(points.front().y) ||
        !std::isfinite(points.front().z)) return false;
    float bx=points.back().x-end.x,by=points.back().y-end.y,bz=points.back().z-end.z;
    if (bx*bx+by*by+bz*bz>0.01f) return false;
    float sx=points.front().x-start.x, sy=points.front().y-start.y;
    if (sx*sx+sy*sy>1.0f || std::abs(points.front().z-start.z)>1.25f) return false;
    points.front()=start;
    for (size_t i=0;i<points.size();++i)
    {
        G3D::Vector3 const& b=points[i];
        if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z)) return false;
        if (!i) continue;
        G3D::Vector3 const& a=points[i-1];
        float dx=b.x-a.x, dy=b.y-a.y, horizontal=std::sqrt(dx*dx+dy*dy);
        // A walking spline cannot be a jump, vertical ladder or gap shortcut.
        if (horizontal>8.0f || std::abs(b.z-a.z)>0.5f+horizontal) return false;
        float ox=horizontal>0.01f ? -dy/horizontal*0.3f : 0.0f;
        float oy=horizontal>0.01f ? dx/horizontal*0.3f : 0.0f;
        Map* map=m_Player->GetMap();
        auto clear=[&](float x,float y,float h)
        {
            return map->isInLineOfSight(a.x+x,a.y+y,a.z+h,
                b.x+x,b.y+y,b.z+h,m_Player->GetPhases());
        };
        if (!clear(0,0,0.75f) || !clear(0,0,1.7f) ||
            !clear(ox,oy,1.0f) || !clear(-ox,-oy,1.0f))
        {
            if (blocked) *blocked=G3D::Vector3((a.x+b.x)*0.5f,(a.y+b.y)*0.5f,(a.z+b.z)*0.5f);
            return false;
        }
    }
    output.swap(points);
    return true;
}

bool BotBGAIMovement::BuildWSGGroundLeg(G3D::Vector3 const& start,
    G3D::Vector3 const& goal, std::vector<G3D::Vector3>& output, G3D::Vector3* blocked) const
{
    PathParameter param(m_Player->GetSession()->GetAccountId(),m_Player);
    param.posX=start.x;param.posY=start.y;param.posZ=start.z;
    param.targetPosX=goal.x;param.targetPosY=goal.y;param.targetPosZ=goal.z;
    Pathfinding path(&param,NULL,NULL);
    path.SetPathLengthLimit(960.0f); // a midfield bot may need the full cross-map route
    if (!path.CalculatePath(goal.x,goal.y,goal.z,false,false)) return false;
    return ValidateWSGGroundPath(path,start,goal,output,blocked);
}

bool BotBGAIMovement::BuildWSGGroundPath(G3D::Vector3 const& start,
    G3D::Vector3 const& goal, std::vector<G3D::Vector3>& output) const
{
    if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(start.z) ||
        !std::isfinite(goal.x) || !std::isfinite(goal.y) || !std::isfinite(goal.z)) return false;
    G3D::Vector3 blocked(std::numeric_limits<float>::quiet_NaN(),0,0);
    if (BuildWSGGroundLeg(start,goal,output,&blocked)) return true;
    if (!std::isfinite(blocked.x)) return false;
    float dx=goal.x-start.x,dy=goal.y-start.y,length=std::sqrt(dx*dx+dy*dy);
    if (length<0.1f) return false;
    // Bounded local avoidance for objects absent from the static mmap. Every
    // candidate and both connections are full, collision-checked nav paths.
    for (float offset : {2.0f,-2.0f,4.0f,-4.0f})
    {
        G3D::Vector3 via(blocked.x-dy/length*offset,blocked.y+dx/length*offset,blocked.z);
        std::vector<G3D::Vector3> first,second;
        if (!BuildWSGGroundLeg(start,via,first,nullptr) ||
            !BuildWSGGroundLeg(first.back(),goal,second,nullptr) || first.size()+second.size()>256)
            continue;
        first.insert(first.end(),second.begin()+1,second.end());
        output.swap(first);
        return true;
    }
    return false;
}

bool BotBGAIMovement::LaunchWSGGroundPath(std::vector<G3D::Vector3> const& points)
{
    if (points.size()<2 || WSGMovementProtected()) return false;
    G3D::Vector3 const& first=points.front();
    float dx=first.x-m_Player->GetPositionX(),dy=first.y-m_Player->GetPositionY(),dz=first.z-m_Player->GetPositionZ();
    if (dx*dx+dy*dy+dz*dz>1.0f) return false; // stale async result, not a new connector
    PathParameter param(m_Player->GetSession()->GetAccountId(),m_Player);
    param.finishPaths=points;param.finishPaths.front()=m_Player->GetVector3();
    param.destPosition=param.finishPaths.back();param.findOK=true;
    param.targetPosX=param.destPosition.x;param.targetPosY=param.destPosition.y;param.targetPosZ=param.destPosition.z;
    ObserveWSGGround();
    m_Player->StopMoving();
    m_Player->GetMotionMaster()->Clear();
    m_Player->GetMotionMaster()->MovePathfinding(&param);
    m_WSGGoal=Position(param.targetPosX,param.targetPosY,param.targetPosZ,0);
    m_WSGProgressPosition=m_Player->GetPosition();
    m_WSGProgressTime=getMSTime();
    m_WSGRetryUntil=0;
    m_WSGGroundRoutePath=false;
    return true;
}

void BotBGAIMovement::MoveWSGGroundTo(G3D::Vector3 const& goal)
{
    ObserveWSGGround();
    if (WSGMovementProtected()) return;
    if (TryWSGGraveyardLanding()) return;
    if (TryWSGGraveyardExit(goal)) return;
    if (m_WSGTaskRouting && !m_WSGExecutingTaskRoute)
    {
        m_WSGExecutingTaskRoute=true;MoveWSGTo(goal.x,goal.y,goal.z);
        m_WSGExecutingTaskRoute=false;return;
    }
    if (m_WSGRetryUntil && int32(getMSTime()-m_WSGRetryUntil)<0) return;
    if (m_Player->movespline && !m_Player->movespline->Finalized() &&
        m_WSGRequestedGoal.GetExactDist(goal.x,goal.y,goal.z)<=2.0f && WaitForWSGGroundSpline()) return;
    if (m_Player->GetExactDist(goal.x,goal.y,goal.z)<0.65f) return;
    std::vector<G3D::Vector3> points;
    if (!BuildWSGGroundPath(m_Player->GetVector3(),goal,points))
    {
        if (TryWSGTaskRoute(goal)) return;
        m_WSGRetryUntil=getMSTime()+1500;
        LogWSGGroundReject("no_complete_walkable_path",goal);
        TrackWSGUnreachableTarget(goal);
        return;
    }
    // Validate the complete route before publishing a short prefix. A long
    // objective must not be rejected forever by the old 160-yard point limit.
    float distance=0;
    for (size_t i=1;i<points.size();++i)
    {
        float dx=points[i].x-points[i-1].x,dy=points[i].y-points[i-1].y,dz=points[i].z-points[i-1].z;
        distance+=std::sqrt(dx*dx+dy*dy+dz*dz);
        if (distance>=60.0f || i>=64) {points.resize(i+1);break;}
    }
    if (LaunchWSGGroundPath(points))
    {m_WSGRequestedGoal=Position(goal.x,goal.y,goal.z,0);m_WSGFailedTargetTime=0;m_WSGFailedTarget.Clear();}
}

bool BotBGAIMovement::IsWSGTargetBlocked(Unit const* target) const
{
    return m_BGAI && m_Player->GetMapId()==489 && target &&
        target->GetGUID()==m_WSGBlockedTarget && m_WSGBlockedUntil &&
        int32(getMSTime()-m_WSGBlockedUntil)<0;
}

void BotBGAIMovement::TrackWSGUnreachableTarget(G3D::Vector3 const& goal)
{
    Unit* target=m_Player->GetSelectedUnit();
    if (!target || target->GetMap()!=m_Player->GetMap() ||
        !m_Player->IsValidAttackTarget(target) || target->GetExactDist(goal.x,goal.y,goal.z)>3.0f) return;
    uint32 now=getMSTime();
    if (!m_WSGFailedTargetTime || m_WSGFailedTarget!=target->GetGUID() ||
        m_Player->GetExactDist(&m_WSGFailedTargetPosition)>0.75f)
    {
        m_WSGFailedTarget=target->GetGUID();m_WSGFailedTargetTime=now;
        m_WSGFailedTargetPosition=m_Player->GetPosition();return;
    }
    if (uint32(now-m_WSGFailedTargetTime)<6000) return;
    m_WSGBlockedTarget=target->GetGUID();m_WSGBlockedUntil=now+10000;
    m_WSGFailedTargetTime=0;m_WSGFailedTarget.Clear();m_WSGRetryUntil=0;
    m_WSGActivityTime=now-10001;m_WSGActivityPosition=m_Player->GetPosition();
    m_Player->AttackStop();m_Player->SetSelection(ObjectGuid::Empty);
    if (targetGuid==m_WSGBlockedTarget) targetGuid.Clear();
    LogWSGGroundReject("unreachable_combat_target_released",goal);
}

void BotBGAIMovement::UpdateWSGActivity()
{
    if (!m_BGAI || m_Player->GetMapId()!=489) return;
    ObserveWSGGround();
    uint32 now=getMSTime();
    if (WSGMovementProtected()) {m_WSGActivityTime=now;return;}
    bool active=m_Player->movespline && !m_Player->movespline->Finalized();
    if (active)
    {
        if (WaitForWSGGroundSpline())
        {m_WSGActivityTime=now;m_WSGActivityPosition=m_Player->GetPosition();return;}
        m_WSGActivityTime=now-10001;m_WSGActivityPosition=m_Player->GetPosition();
    }
    if (m_Player->GetSelectedUnit()) {m_WSGActivityTime=now;return;}
    if (!m_WSGActivityTime || m_Player->GetExactDist(&m_WSGActivityPosition)>0.75f)
    {m_WSGActivityTime=now;m_WSGActivityPosition=m_Player->GetPosition();return;}
    if (!active && uint32(now-m_WSGActivityTime)<2000) return;
    m_WSGActivityTime=now;m_WSGActivityPosition=m_Player->GetPosition();
    ExecuteMovementCommand();
    if (m_Player->movespline && !m_Player->movespline->Finalized()) return;
    // A valid commander order may intentionally wait for a flag pickup,
    // carrier or capture. Never fall back to the last chased enemy position.
    if (RestoreWSGTask()) return;
    {
        bool ownBase=IsWSGFlagCarrier(m_Player);
        int16 end=(m_Player->GetBGTeamId()==TEAM_ALLIANCE)!=ownBase ? GetWSGRouteCount(1)-1 : 0;
        G3D::Vector3 const& objective=WSGLegionTunnelRoute[end];
        MoveWSGTo(objective.x,objective.y,objective.z);
    }
}

bool BotBGAIMovement::GetWSGRecordedDrop(int16 index,int16& takeoff,int16& landing) const
{
    // Directed transitions from the original recordings: never a generic
    // permission to jump onto a roof or reverse-climb a graveyard cliff.
    struct Drop {uint8 variant;int8 direction;int16 from,to;};
    static Drop const drops[]={{2,1,43,47},{2,1,59,62},{3,-1,166,162},{3,-1,141,139}};
    for (Drop const& drop : drops)
        if (m_WSGRouteVariant==drop.variant && m_WSGRouteDirection==drop.direction &&
            (index-drop.from)*drop.direction>=0 && (drop.to-index)*drop.direction>0)
        {takeoff=drop.from;landing=drop.to;return true;}
    return false;
}

bool BotBGAIMovement::HandleWSGRecordedDrop(int16 index)
{
    int16 takeoff=0,landing=0;
    if (!GetWSGRecordedDrop(index,takeoff,landing)) return false;
    if (WSGMovementProtected()) return true;
    G3D::Vector3 const* route=GetWSGRoute(m_WSGRouteVariant);
    dtNavMeshQuery const* query=MMAP::MMapFactory::createOrGetMMapManager()->
        GetNavMeshQuery(489,m_Player->GetInstanceId());
    auto ground=[query](G3D::Vector3 const& p,G3D::Vector3& result)
    {
        if (!query) return false;
        float point[3]={p.y,p.z,p.x},extent[3]={0.75f,2.25f,0.75f},closestPoint[3]={};
        dtQueryFilter filter;filter.setIncludeFlags(NAV_GROUND);filter.setExcludeFlags(0);
        dtPolyRef ref=0;
        if (dtStatusFailed(query->findNearestPoly(point,extent,&filter,&ref,closestPoint)) || !ref) return false;
        float dx=closestPoint[2]-p.x,dy=closestPoint[0]-p.y;
        if (dx*dx+dy*dy>0.5625f || std::abs(closestPoint[1]-p.z)>2.25f) return false;
        result=G3D::Vector3(closestPoint[2],closestPoint[0],closestPoint[1]);return true;
    };
    G3D::Vector3 edge,destination;
    if (!ground(route[takeoff],edge) || !ground(route[landing],destination))
    {m_WSGRetryUntil=getMSTime()+1500;LogWSGGroundReject("recorded_drop_missing_ground",route[landing]);return true;}
    if (m_Player->GetExactDist(edge.x,edge.y,edge.z)>0.65f)
    {
        std::vector<G3D::Vector3> approach;
        if (BuildWSGGroundPath(m_Player->GetVector3(),edge,approach) && LaunchWSGGroundPath(approach))
        {m_WSGChunkStart=takeoff;m_WSGRouteIndex=takeoff;m_WSGGroundRoutePath=true;}
        else {m_WSGRetryUntil=getMSTime()+1500;LogWSGGroundReject("recorded_drop_approach_blocked",edge);}
        return true;
    }
    G3D::Vector3 start=m_Player->GetVector3();
    destination.z+=0.05f;
    float dx=destination.x-start.x,dy=destination.y-start.y,dz=destination.z-start.z;
    float horizontal=std::sqrt(dx*dx+dy*dy),drop=-dz;
    if (!IsWSGOnNavGround() || drop<2.0f || drop>16.0f || horizontal<1.0f || horizontal>18.0f)
    {m_WSGRetryUntil=getMSTime()+1500;LogWSGGroundReject("recorded_drop_invalid_geometry",destination);return true;}
    // Match the native MoveJump linear spline + parabolic amplitude exactly.
    // A small normal jump, real gravity and ordinary run speed reach the lower
    // ground; speedXY is the spline's 3D speed in this core, not its XY speed.
    float gravity=Movement::gravity,up=7.0f;
    float duration=(up+std::sqrt(up*up+2.0f*gravity*drop))/gravity;
    float allowedSpeed=std::min(7.0f,m_Player->GetSpeed(MOVE_RUN));
    if (duration<=0 || horizontal/duration>allowedSpeed+0.05f)
    {m_WSGRetryUntil=getMSTime()+1500;LogWSGGroundReject("recorded_drop_run_speed",destination);return true;}
    float amplitude=gravity*duration*duration/8.0f;
    float sideX=-dy/horizontal*0.3f,sideY=dx/horizontal*0.3f;
    G3D::Vector3 previous=start;
    for (uint8 i=1;i<=64;++i)
    {
        float u=float(i)/64.0f;
        G3D::Vector3 next(start.x+dx*u,start.y+dy*u,start.z+dz*u+4.0f*amplitude*u*(1.0f-u));
        auto clear=[&](float ox,float oy,float h)
        {return m_Player->GetMap()->isInLineOfSight(previous.x+ox,previous.y+oy,previous.z+h,
            next.x+ox,next.y+oy,next.z+h,m_Player->GetPhases());};
        if (!clear(0,0,0.05f) || !clear(0,0,1.7f) ||
            !clear(sideX,sideY,0.9f) || !clear(-sideX,-sideY,0.9f))
        {m_WSGRetryUntil=getMSTime()+1500;LogWSGGroundReject("recorded_drop_arc_blocked",destination);return true;}
        previous=next;
    }
    ObserveWSGGround();
    m_Player->StopMoving();m_Player->GetMotionMaster()->Clear();
    m_Player->GetMotionMaster()->MoveJump(destination.x,destination.y,destination.z,
        std::sqrt(horizontal*horizontal+dz*dz)/duration,gravity*duration/2.0f);
    m_WSGDropActive=true;m_WSGDropTime=getMSTime();m_WSGRetryUntil=0;
    m_WSGChunkStart=takeoff;m_WSGRouteIndex=landing;m_WSGGroundRoutePath=true;
    m_WSGGoal=Position(destination.x,destination.y,destination.z,0);
    m_WSGProgressPosition=m_Player->GetPosition();m_WSGProgressTime=getMSTime();
    TC_LOG_INFO(LOG_FILTER_PATH_GENERATOR,"WSG_RECORDED_DROP bot=%u variant=%u from=%d to=%d",
        m_Player->GetGUIDLow(),uint32(m_WSGRouteVariant),int(takeoff),int(landing));
    return true;
}

bool BotBGAIMovement::TryWSGTaskRoute(G3D::Vector3 const& goal)
{
    Battleground* bg=m_Player->GetBattleground();
    if (!bg || bg->GetStatus()!=STATUS_IN_PROGRESS ||
        !std::isfinite(goal.x) || !std::isfinite(goal.y) || !std::isfinite(goal.z)) return false;
    // An unreachable selected enemy keeps the existing short pursuit timeout.
    // A strategic route must not prolong combat against a roof/air target.
    Unit* selected=m_Player->GetSelectedUnit();
    if (selected && m_Player->IsValidAttackTarget(selected) &&
        selected->GetExactDist(goal.x,goal.y,goal.z)<=3.0f) return false;
    if (!m_BGAI || m_Player->GetMapId()!=489 || WSGMovementProtected() ||
        m_WSGRouteDirection || m_WSGExecutingExit || m_WSGExitingGraveyard ||
        m_WSGExecutingTaskRoute || !IsWSGOnNavGround()) return false;
    // A failed full walk does not invalidate a commander task. Reconnect to a
    // nearby recorded main-route point, then advance toward that actual task.
    // This also serves defenders leaving their own flag room and combat resets
    // in midfield; neither should depend on the old cross-map X thresholds.
    int16 count=GetWSGRouteCount(1),goalIndex=0;
    float best=std::numeric_limits<float>::max();
    std::vector<int16> candidates;
    for (int16 i=0;i<count;++i)
    {
        float distance=(WSGLegionTunnelRoute[i]-goal).squaredLength();
        if (distance<best) {best=distance;goalIndex=i;}
        float startDistance=(WSGLegionTunnelRoute[i]-m_Player->GetVector3()).squaredLength();
        if (startDistance>1.0f && startDistance<6400.0f) candidates.push_back(i);
    }
    std::sort(candidates.begin(),candidates.end(),[&](int16 a,int16 b)
    {return (WSGLegionTunnelRoute[a]-m_Player->GetVector3()).squaredLength()<
        (WSGLegionTunnelRoute[b]-m_Player->GetVector3()).squaredLength();});
    if (candidates.size()>8) candidates.resize(8);
    // An upper base can be disconnected from its eight nearest field points.
    // Keep both validated tunnel anchors available as a short walking retreat.
    candidates.push_back(1);candidates.push_back(count-2);
    for (int16 entry : candidates)
    {
        std::vector<G3D::Vector3> connector;
        if (!BuildWSGGroundPath(m_Player->GetVector3(),WSGLegionTunnelRoute[entry],connector)) continue;
        float length=0;
        for (size_t i=1;i<connector.size();++i) length+=(connector[i]-connector[i-1]).length();
        if (length>100.0f || !LaunchWSGGroundPath(connector)) continue;
        m_WSGTaskRouting=true;m_WSGTaskRouteGoal=Position(goal.x,goal.y,goal.z,0);
        m_WSGRouteVariant=1;m_WSGRouteDirection=goalIndex>=entry ? 1 : -1;
        m_WSGChunkStart=entry;m_WSGRouteIndex=entry;m_WSGGroundRoutePath=true;
        return true;
    }
    // Combat can end on either recorded graveyard approach. The directed
    // descent is valid for both teams; never try to climb it in reverse.
    for (uint8 variant : {uint8(2),uint8(3)})
    {
        G3D::Vector3 const* route=GetWSGRoute(variant);
        std::vector<int16> upper;
        int16 low=variant==2 ? 0 : 139;
        int16 high=variant==2 ? 62 : GetWSGRouteCount(variant)-1;
        for (int16 i=low;i<=high;++i)
        {
            float distance=(route[i]-m_Player->GetVector3()).squaredLength();
            if (distance>1.0f && distance<1024.0f) upper.push_back(i);
        }
        std::sort(upper.begin(),upper.end(),[&](int16 a,int16 b)
        {return (route[a]-m_Player->GetVector3()).squaredLength()<
            (route[b]-m_Player->GetVector3()).squaredLength();});
        if (upper.size()>8) upper.resize(8);
        for (int16 entry : upper)
        {
            std::vector<G3D::Vector3> connector;
            if (!BuildWSGGroundPath(m_Player->GetVector3(),route[entry],connector)) continue;
            float length=0;
            for (size_t i=1;i<connector.size();++i) length+=(connector[i]-connector[i-1]).length();
            if (length>60.0f || !LaunchWSGGroundPath(connector)) continue;
            m_WSGTaskRouting=false;m_WSGExitingGraveyard=true;m_WSGExitVariant=variant;
            m_WSGRouteVariant=variant;m_WSGRouteDirection=variant==2 ? 1 : -1;
            m_WSGChunkStart=entry;m_WSGRouteIndex=entry;m_WSGGroundRoutePath=true;
            return true;
        }
    }
    return false;
}

bool BotBGAIMovement::TryWSGGraveyardLanding()
{
    Battleground* bg=m_Player->GetBattleground();
    if (!m_BGAI || m_Player->GetMapId()!=489 || !bg ||
        bg->GetStatus()!=STATUS_IN_PROGRESS || WSGMovementProtected() ||
        (m_Player->movespline && !m_Player->movespline->Finalized())) return false;
    WorldSafeLocsEntry const* grave=bg->GetClosestGraveYard(m_Player);
    if (!grave || grave->MapID!=489) return false;
    float dx=m_Player->GetPositionX()-grave->Loc.X,dy=m_Player->GetPositionY()-grave->Loc.Y;
    // RepopAtGraveyard intentionally spawns ghosts two yards above the DBC
    // location. A server-controlled player has no client to finish that fall.
    // Confine this repair to the actual spawn, never arbitrary raised terrain.
    if (dx*dx+dy*dy>9.0f ||
        std::abs(m_Player->GetPositionZ()-(grave->Loc.Z+2.0f))>0.6f) return false;
    dtNavMeshQuery const* query=MMAP::MMapFactory::createOrGetMMapManager()->
        GetNavMeshQuery(489,m_Player->GetInstanceId());
    if (!query) return false;
    float point[3]={m_Player->GetPositionY(),grave->Loc.Z,m_Player->GetPositionX()};
    float extent[3]={0.25f,1.0f,0.25f},ground[3]={};
    dtQueryFilter filter;filter.setIncludeFlags(NAV_GROUND);filter.setExcludeFlags(0);
    dtPolyRef ref=0;
    if (dtStatusFailed(query->findNearestPoly(point,extent,&filter,&ref,ground)) || !ref ||
        std::abs(ground[0]-point[0])>0.05f || std::abs(ground[2]-point[2])>0.05f ||
        std::abs(ground[1]-grave->Loc.Z)>1.0f) return false;
    float drop=m_Player->GetPositionZ()-ground[1];
    if (drop<1.25f || drop>3.5f) return false;
    for (float offset : {0.05f,1.0f,1.7f})
        if (!m_Player->GetMap()->isInLineOfSight(m_Player->GetPositionX(),
            m_Player->GetPositionY(),m_Player->GetPositionZ()+offset,
            m_Player->GetPositionX(),m_Player->GetPositionY(),ground[1]+offset,
            m_Player->GetPhases())) return false;
    m_Player->StopMoving();m_Player->GetMotionMaster()->Clear();
    Movement::MoveSplineInit fall(*m_Player);
    fall.MoveTo(m_Player->GetPositionX(),m_Player->GetPositionY(),ground[1],false);
    fall.SetFall();
    if (fall.Launch()<=0) return false;
    m_WSGDropActive=true;m_WSGDropTime=getMSTime();m_WSGRetryUntil=0;
    m_WSGProgressTime=0;
    TC_LOG_INFO(LOG_FILTER_PATH_GENERATOR,"WSG_GRAVEYARD_LANDING bot=%u grave=%u",
        m_Player->GetGUIDLow(),uint32(grave->ID));
    return true;
}

bool BotBGAIMovement::TryWSGGraveyardExit(G3D::Vector3 const& goal)
{
    if (!m_BGAI || m_Player->GetMapId()!=489 || m_WSGExecutingExit ||
        WSGMovementProtected()) return false;
    // Ordinary recorded routes retain their exact existing transitions.
    if (!m_WSGExitingGraveyard && m_WSGRouteDirection) return false;
    uint8 variant=m_WSGExitVariant;
    if (!m_WSGExitingGraveyard)
    {
        variant=m_Player->GetBGTeamId()==TEAM_ALLIANCE ? 2 : 3;
        G3D::Vector3 const& lip=variant==2 ? WSGLegionAlternativeRoute[43] : WSGLegionHordeAlternativeRoute[166];
        bool upperLip=std::abs(m_Player->GetPositionZ()-lip.z)<=4.0f &&
            m_Player->GetExactDist(lip.x,lip.y,lip.z)<=32.0f;
        Battleground* bg=m_Player->GetBattleground();
        WorldSafeLocsEntry const* grave=bg && bg->GetStatus()==STATUS_IN_PROGRESS ?
            bg->GetClosestGraveYard(m_Player) : nullptr;
        bool lowerSpawn=grave && grave->MapID==489 &&
            std::abs(m_Player->GetPositionZ()-grave->Loc.Z)<=4.0f &&
            m_Player->GetExactDist(grave->Loc.X,grave->Loc.Y,grave->Loc.Z)<=32.0f;
        if (!upperLip && !lowerSpawn) return false;
    }
    if (WaitForWSGGroundSpline()) return m_WSGExitingGraveyard;
    std::vector<G3D::Vector3> direct;
    // A valid walk to our own base needs no jump. A disconnected lower
    // objective needs the same recorded descent regardless of its X direction.
    if (BuildWSGGroundPath(m_Player->GetVector3(),goal,direct))
    {
        if (m_WSGExitingGraveyard)
        {
            m_WSGExitingGraveyard=false;m_WSGExitVariant=0;
            m_WSGRouteVariant=0;m_WSGRouteDirection=0;m_WSGRouteIndex=-1;
            m_WSGChunkStart=-1;m_WSGGroundRoutePath=false;m_WSGWalkFallback=false;
            m_WSGRetryUntil=0;
        }
        return false;
    }
    // After the last directed descent, rejoin the main route in the direction
    // of the real task. Following the entire exit recording can otherwise send
    // a defender across the map and leave it at the wrong tunnel entrance.
    bool descended=m_WSGExitingGraveyard && m_WSGRouteVariant==variant &&
        m_WSGRouteIndex>=0 && (variant==2 ? m_WSGRouteIndex>=62 : m_WSGRouteIndex<=139);
    if (descended)
    {
        int16 count=GetWSGRouteCount(1),goalIndex=0;
        float best=std::numeric_limits<float>::max();
        std::vector<int16> candidates;
        for (int16 i=0;i<count;++i)
        {
            float d=(WSGLegionTunnelRoute[i]-goal).squaredLength();
            if (d<best) {best=d;goalIndex=i;}
            if ((WSGLegionTunnelRoute[i]-m_Player->GetVector3()).squaredLength()<6400.0f)
                candidates.push_back(i);
        }
        std::sort(candidates.begin(),candidates.end(),[&](int16 a,int16 b)
        {return (WSGLegionTunnelRoute[a]-m_Player->GetVector3()).squaredLength()<
            (WSGLegionTunnelRoute[b]-m_Player->GetVector3()).squaredLength();});
        if (candidates.size()>8) candidates.resize(8);
        for (int16 entry : candidates)
        {
            std::vector<G3D::Vector3> connector;
            if (!BuildWSGGroundPath(m_Player->GetVector3(),WSGLegionTunnelRoute[entry],connector)) continue;
            float length=0;
            for (size_t i=1;i<connector.size();++i) length+=(connector[i]-connector[i-1]).length();
            if (length>100.0f || !LaunchWSGGroundPath(connector)) continue;
            m_WSGRouteVariant=1;m_WSGRouteDirection=goalIndex>=entry ? 1 : -1;
            m_WSGChunkStart=entry;m_WSGRouteIndex=entry;m_WSGGroundRoutePath=true;
            return true;
        }
    }
    m_WSGExitingGraveyard=true;m_WSGExitVariant=variant;
    if (!m_WSGRouteDirection || (m_WSGRouteVariant!=variant && m_WSGRouteVariant!=1))
    {
        m_WSGRouteVariant=variant;m_WSGRouteDirection=variant==2 ? 1 : -1;
        m_WSGRouteIndex=-1;m_WSGChunkStart=-1;m_WSGGroundRoutePath=false;
        m_WSGWalkFallback=false;m_WSGRetryUntil=0;
    }
    // Keep strategic ownership intact. This temporary movement stage also
    // serves a GUID chase or escort, then returns to that caller's real goal.
    m_WSGExecutingExit=true;
    MoveWSGTo(goal.x,goal.y,goal.z);
    m_WSGExecutingExit=false;
    return true;
}

void BotBGAIMovement::MoveWSGTo(float x,float y,float z)
{
    ObserveWSGGround();
    if (WSGMovementProtected()) return;
    if (TryWSGGraveyardLanding()) return;
    if (TryWSGGraveyardExit(G3D::Vector3(x,y,z))) return;
    m_WSGObjective=Position(x,y,z,0);m_WSGHasObjective=true;
    if (!m_WSGExecutingExit && !m_WSGExecutingTaskRoute)
        if (Player* carrier=GetWSGEscortCarrier())
        {FollowWSGCarrier(carrier);return;}
    if (WaitForWSGGroundSpline() || (m_WSGRetryUntil && int32(getMSTime()-m_WSGRetryUntil)<0)) return;
    if (m_WSGTaskRouting)
    {
        std::vector<G3D::Vector3> direct;
        bool movedGoal=m_WSGTaskRouteGoal.GetExactDist(x,y,z)>20.0f;
        if (movedGoal || BuildWSGGroundPath(m_Player->GetVector3(),G3D::Vector3(x,y,z),direct))
        {
            m_WSGTaskRouting=false;m_WSGRouteVariant=0;m_WSGRouteDirection=0;
            m_WSGRouteIndex=-1;m_WSGChunkStart=-1;m_WSGGroundRoutePath=false;
            m_WSGWalkFallback=false;m_WSGRetryUntil=0;
            MoveWSGGroundTo(G3D::Vector3(x,y,z));return;
        }
    }
    bool flag=!m_WSGExecutingExit && !m_WSGExecutingTaskRoute && IsWSGFlagCarrier(m_Player);
    if (flag)
    {
        uint8 variant=m_Player->GetBGTeamId()==TEAM_ALLIANCE ? 4 : 5;
        if (!m_WSGWalkFallback && m_WSGRouteVariant!=variant)
        {m_WSGRouteVariant=variant;m_WSGRouteDirection=variant==4?-1:1;m_WSGRouteIndex=-1;m_WSGChunkStart=-1;}
    }
    else if (m_WSGRouteVariant==4 || m_WSGRouteVariant==5)
    {m_WSGRouteVariant=0;m_WSGRouteDirection=0;m_WSGRouteIndex=-1;m_WSGChunkStart=-1;}
    float px=m_Player->GetPositionX(),py=m_Player->GetPositionY();
    if (!m_WSGRouteDirection)
    {
        if (px>1480 && py>1430 && py<1530 && x<1200)
        {m_WSGRouteDirection=1;m_WSGRouteVariant=urand(0,99)<30?2:1;m_WSGRouteIndex=-1;}
        else if (px<950 && py>1400 && py<1475 && x>1200)
        {m_WSGRouteDirection=-1;m_WSGRouteVariant=urand(0,99)<30?3:1;m_WSGRouteIndex=-1;}
        else if (m_Player->GetBGTeamId()==TEAM_ALLIANCE && x<1200 &&
            m_Player->GetExactDist(WSGLegionAlternativeRoute[43].x,
                WSGLegionAlternativeRoute[43].y,WSGLegionAlternativeRoute[43].z)<24.0f)
        {m_WSGRouteDirection=1;m_WSGRouteVariant=2;m_WSGRouteIndex=-1;}
        else if (m_Player->GetBGTeamId()==TEAM_HORDE && x>1200 &&
            m_Player->GetExactDist(WSGLegionHordeAlternativeRoute[166].x,
                WSGLegionHordeAlternativeRoute[166].y,WSGLegionHordeAlternativeRoute[166].z)<24.0f)
        {m_WSGRouteDirection=-1;m_WSGRouteVariant=3;m_WSGRouteIndex=-1;}
    }
    if (!m_WSGRouteDirection)
    {MoveWSGGroundTo(G3D::Vector3(x,y,z));return;}
    G3D::Vector3 const* route=GetWSGRoute(m_WSGRouteVariant);
    int16 count=GetWSGRouteCount(m_WSGRouteVariant),direction=m_WSGRouteDirection;
    int16 begin=m_WSGChunkStart,end=m_WSGRouteIndex;
    if (begin<0 || end<0 || begin>=count || end>=count)
    {begin=0;end=count-1;}
    int16 lo=std::min(begin,end),hi=std::max(begin,end),nearest=lo;
    float distance=std::numeric_limits<float>::max();
    for (int16 i=lo;i<=hi;++i)
    {
        float dx=route[i].x-px,dy=route[i].y-py,dz=route[i].z-m_Player->GetPositionZ();
        float squared=dx*dx+dy*dy+dz*dz;
        if (squared<distance) {nearest=i;distance=squared;}
    }
    int16 index=nearest;
    if (m_WSGGroundRoutePath && m_WSGChunkStart>=0 &&
        IsNearToPosition(m_WSGGoal.GetPositionX(),m_WSGGoal.GetPositionY(),m_WSGGoal.GetPositionZ(),0.45f))
        index=m_WSGRouteIndex+direction;
    else if (IsNearToPosition(route[index].x,route[index].y,route[index].z,0.45f)) index+=direction;
    if (index<0 || index>=count)
    {
        m_WSGTaskRouting=false;
        m_WSGRouteIndex=-1;m_WSGChunkStart=-1;m_WSGRouteDirection=0;m_WSGRouteVariant=0;m_WSGWalkFallback=false;
        MoveWSGGroundTo(G3D::Vector3(x,y,z));return;
    }
    if (HandleWSGRecordedDrop(index)) return;
    int16 first=index,last=index;
    std::vector<G3D::Vector3> points;
    G3D::Vector3 from=m_Player->GetVector3();
    uint8 added=0,skipped=0;
    for (;index>=0 && index<count && added<6;index+=direction)
    {
        int16 takeoff=0,landing=0;
        bool edge=GetWSGRecordedDrop(index,takeoff,landing);
        if (edge && index!=takeoff) break;
        std::vector<G3D::Vector3> leg;
        if (!BuildWSGGroundPath(from,route[index],leg))
        {
            if (!points.empty() || ++skipped>6) break;
            continue; // airborne/unreachable recorded sample; next goal still needs a full nav path
        }
        if (!points.empty() && points.size()+leg.size()-1>256) break;
        if (points.empty()) {points=leg;first=index;}
        else points.insert(points.end(),leg.begin()+1,leg.end());
        from=points.back();last=index;++added;
        if (edge) break; // stop on the lip; launch one native jump next tick
        if (points.size()>96) break;
    }
    if (points.size()<2)
    {
        // Some alternate recordings contain a real player jump/drop. Do not
        // walk it or retry it forever: reconnect to the recorded main route,
        // but only through a complete collision-checked walking path.
        if (m_WSGRouteVariant!=1)
        {
            std::vector<int16> candidates;
            int16 mainCount=GetWSGRouteCount(1);
            for (int16 i=0;i<mainCount;++i)
                if (m_Player->GetExactDist(WSGLegionTunnelRoute[i].x,
                    WSGLegionTunnelRoute[i].y,WSGLegionTunnelRoute[i].z)>3.0f)
                    candidates.push_back(i);
            auto distanceTo=[&](int16 i)
            {return m_Player->GetExactDist(WSGLegionTunnelRoute[i].x,
                WSGLegionTunnelRoute[i].y,WSGLegionTunnelRoute[i].z);};
            std::sort(candidates.begin(),candidates.end(),
                [&](int16 a,int16 b){return distanceTo(a)<distanceTo(b);});
            if (candidates.size()>8) candidates.resize(8);
            // A nearby route point may be on the far side of the same cliff.
            // Include the two base anchors so a walkable retreat is possible.
            candidates.push_back(1);candidates.push_back(mainCount-2);
            for (size_t i=0;i<candidates.size();++i)
            {
                int16 candidate=candidates[i];
                if (!BuildWSGGroundPath(m_Player->GetVector3(),WSGLegionTunnelRoute[candidate],points) ||
                    m_Player->GetExactDist(points.back().x,points.back().y,points.back().z)<=3.0f)
                    continue;
                if (LaunchWSGGroundPath(points))
                {
                    m_WSGRouteVariant=1;m_WSGChunkStart=candidate;m_WSGRouteIndex=candidate;
                    m_WSGGroundRoutePath=true;m_WSGWalkFallback=true;
                    LogWSGGroundReject("recorded_drop_reconnect_main_route",route[nearest]);
                }
                return;
            }
        }
        m_WSGRetryUntil=getMSTime()+1500;
        LogWSGGroundReject("recorded_goal_not_walkable",route[nearest]);return;
    }
    if (LaunchWSGGroundPath(points))
    {m_WSGChunkStart=first;m_WSGRouteIndex=last;m_WSGGroundRoutePath=true;}
}

Player* BotBGAIMovement::GetWSGEscortCarrier() const
{
    if (!m_BGAI || m_Player->GetMapId() != 489 || !targetGuid.IsPlayer() ||
        targetGuid == m_Player->GetGUID() || IsWSGFlagCarrier(m_Player))
        return nullptr;
    Player* carrier = ObjectAccessor::FindPlayer(targetGuid);
    if (!carrier || !carrier->isAlive() || !carrier->IsInWorld() ||
        carrier->GetBattleground() != m_Player->GetBattleground() ||
        carrier->GetBGTeamId() != m_Player->GetBGTeamId() ||
        !IsWSGFlagCarrier(carrier))
        return nullptr;
    return carrier;
}

void BotBGAIMovement::ResetWSGEscort()
{
    if (m_WSGEscortGuid.IsEmpty())
        return;
    if (m_WSGEscortSpline && !WSGMovementProtected())
    {
        m_Player->StopMoving();
        m_Player->GetMotionMaster()->Clear();
    }
    m_WSGTaskRouting=false;
    m_WSGEscortGuid.Clear();
    m_WSGEscortSpline = false;
    m_WSGEscortRetryTick = 0;
    m_WSGRouteIndex = -1;
    m_WSGRouteDirection = 0;
    m_WSGRouteVariant = 0;
    m_MovementTick = 0;
}

void BotBGAIMovement::FollowWSGCarrier(Player* carrier)
{
    if (!carrier || WSGMovementProtected()) return;
    ObserveWSGGround();
    if (m_WSGEscortGuid!=carrier->GetGUID())
    {
        m_WSGTaskRouting=false;
        m_WSGEscortGuid=carrier->GetGUID();m_WSGRouteIndex=-1;
        m_WSGRouteDirection=0;m_WSGRouteVariant=0;m_WSGChunkStart=-1;
        TC_LOG_INFO(LOG_FILTER_GENERAL,"WSG_ESCORT_START bot=%u carrier=%u",m_Player->GetGUIDLow(),carrier->GetGUIDLow());
    }
    if (m_Player->GetDistance(carrier->GetPosition())<=8.0f && m_Player->IsWithinLOSInMap(carrier))
    {
        if (m_WSGEscortSpline) {m_Player->StopMoving();m_Player->GetMotionMaster()->Clear();}
        m_WSGEscortSpline=false;m_WSGProgressTime=0;return;
    }
    if (WaitForWSGGroundSpline()) return;
    MoveWSGGroundTo(carrier->GetVector3());
    m_WSGEscortSpline=m_Player->movespline && !m_Player->movespline->Finalized();
}

bool BotBGAIMovement::RecoverFallenWSGBot()
{
    if (!m_BGAI || m_Player->GetMapId()!=489) return false;
    ObserveWSGGround();
    if (WSGMovementProtected()) return true;
    if (m_WSGRecoveryWait) {--m_WSGRecoveryWait;return true;}
    if (!m_WSGHasSafeGround || uint32(getMSTime()-m_WSGSafeGroundTime)>15000 || IsWSGOnNavGround())
        return false;
    float dx=m_Player->GetPositionX()-m_WSGSafeGround.GetPositionX();
    float dy=m_Player->GetPositionY()-m_WSGSafeGround.GetPositionY();
    if (dx*dx+dy*dy>36.0f || m_Player->GetPositionZ()>=m_WSGSafeGround.GetPositionZ()-6.0f)
        return false;
    m_Player->StopMoving();m_Player->GetMotionMaster()->Clear();
    m_WSGRecoveryWait=1;m_WSGProgressTime=0;
    m_BGAI->SetTeleport(m_WSGSafeGround);
    LogWSGGroundReject("underfloor_return_to_visited_ground",m_WSGSafeGround.GetVector3());
    return true;
}
void BotBGAIMovement::MovementToAIWP(float offset)
{
    if (!pTargetAIWP)
        return;
    if (m_BGAI && m_Player->GetMapId()==30 && pTargetAIWP->mapID!=30) return;

    if (m_MovementTick > 0 &&
        m_Player->GetDistance(pTargetAIWP->GetPosition()) > BOTAI_SEARCH_RANGE)
    {
        --m_MovementTick;
        if (m_MovementTick < 0)
            m_MovementTick = 0;
        return;
    }

    MovementTo(pTargetAIWP->posX, pTargetAIWP->posY, pTargetAIWP->posZ, offset);
}
void BotBGAIMovement::MovementTo(float x, float y, float z, float offset /* = 0 */)
{
	if (m_BGAI && m_BGAI->IsNotSelect(m_Player))
		return;
    // Let a short idle walking leg finish before selecting the next bend.
    if (IsDynamicOutdoorBot() && !m_Player->isInCombat() &&
        m_Player->movespline && !m_Player->movespline->Finalized()) {
        uint32 now=getMSTime();
        if(m_Player->HasUnitState(UNIT_STATE_CASTING | UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING) ||
           m_Player->HasAuraWithMechanic(1 << Mechanics::MECHANIC_ROOT) ||
           m_Player->GetVehicleBase() || m_Player->GetTransport() || m_Player->IsFlying() ||
           m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR)) {
            m_IdleSplineProgressTime=0;return;
        }
        if(!m_IdleSplineProgressTime || m_Player->GetDistance(m_IdleSplineProgressPosition)>0.5f) {
            m_IdleSplineProgressPosition=m_Player->GetPosition();m_IdleSplineProgressTime=now;return;
        }
        if(uint32(now-m_IdleSplineProgressTime)<4000)return;
        // A spline flag is not evidence of actual progress. Retry native safe routing.
        ClearMovement();m_Player->GetMotionMaster()->Clear();
        TC_LOG_INFO(LOG_FILTER_PATH_GENERATOR,"DYNAMIC_BOT_IDLE_RETRY bot=%s map=%u reason=no_spline_progress",m_Player->GetName(),m_Player->GetMapId());
    }else m_IdleSplineProgressTime=0;
    if (m_BGAI && m_Player->GetMapId()==30) {MoveAVGroundTo(x,y,z);return;}
    if (m_BGAI && m_Player->GetMapId()==761) {
        if(auto*commander=dynamic_cast<CommandBFG*>(m_Player->GetMap()->GetCommander(m_Player->GetBGTeamId())))
            if(!commander->BoundCombatDestination(m_Player,x,y,z))return;
        std::vector<G3D::Vector3> path;
        if(BuildBFGGroundLeg(m_Player->GetVector3(),G3D::Vector3(x,y,z),path))MoveBFGRecordedPath(path);
        return;
    }
    if (m_BGAI && m_Player->GetMapId()==529) {MoveABGroundTo(x,y,z);return;}
    if (m_BGAI && m_Player->GetMapId()==489) {MoveWSGTo(x,y,z);return;}
    targetGuid.Clear();
	if (m_MovementTick > 0 && !m_Player->GetSelectedUnit())
	{
		--m_MovementTick;
		return;
	}
	m_MovementTick = 3;
	if (IsNearToPosition(x,y,z,(offset==0)?0.8f:offset*0.9f)) return;

    // The caller owns the destination. Replacing it with a human-trail
    // patrol here makes world travel measure progress toward a different node.
    // Observed trails may inform future route planning, never override a leg.

	uint32 sessionID = 0;
	if (m_Player->GetTypeId() == TypeID::TYPEID_PLAYER)
		sessionID = m_Player->GetSession()->GetAccountId();
	Unit* pVehicle = m_Player->GetVehicleBase();
	PathParameter* pathParam = new PathParameter(sessionID, pVehicle ? pVehicle : m_Player);
	pathParam->targetPosX = x;
	pathParam->targetPosY = y;
	pathParam->targetPosZ = z;
	pathParam->offset = offset;

    Pathfinding path(pathParam, NULL, NULL);
	if (pathParam->offset < 0)
		pathParam->offset *= -1;
	float posx = pathParam->targetPosX + ((pathParam->offset != 0) ? frand(pathParam->offset * (-1), pathParam->offset) : 0);
	float posy = pathParam->targetPosY + ((pathParam->offset != 0) ? frand(pathParam->offset * (-1), pathParam->offset) : 0);
	float posz = pathParam->targetPosZ;
	float const requestedZ = posz;
	if (m_Player->GetMapId() != 489)
        path.UpdateAllowedPositionZ(posx, posy, posz);
	if (IsDynamicOutdoorBot() &&
        (!std::isfinite(posz) || std::abs(posz - requestedZ) > 2.0f))
    {
        delete pathParam;
        m_MovementTick = 0;
        return;
    }
	bool result = path.CalculatePath(posx, posy, posz);
	if (m_BGAI && CommandObjectiveBG::Supports(m_Player->GetBattleground()) &&
	    (!result || path.GetPathType() != PATHFIND_NORMAL))
	{ delete pathParam; m_MovementTick = 0; return; }
    // V12.1: legacy findOK may be forced true later; validate the native result first.
    if(m_GroupAI && m_Player->GetMap() && m_Player->GetMap()->IsDungeon() &&
       (!result || path.GetPathType()!=PATHFIND_NORMAL)){
        delete pathParam;m_MovementTick=0;return;
    }
	if (IsDynamicOutdoorBot() &&
        (!result || !IsSafeDynamicOutdoorPath(path, posx, posy, posz)))
    {
        WarnRejectedDynamicPath(path, posx, posy, posz);
        delete pathParam;
        m_MovementTick = 0;
        return;
    }
	// FELSORIA_BOT_GROUND_V1: Path not find1: a failed ground path is never a direct move.
        bool const freeMovement = m_Player->IsFlying() || m_Player->IsInWater() ||
            m_Player->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING);
        pathParam->findOK = result &&
            !(path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_SHORTCUT)) &&
            (freeMovement || !(path.GetPathType() & PATHFIND_NOT_USING_PATH));

	if (pathParam->findOK)
	{
		pathParam->finishPaths.clear();
		const std::vector<G3D::Vector3>& points = path.GetPath();
		for (std::vector<G3D::Vector3>::const_iterator itPoints = points.begin();
			itPoints != points.end();
			itPoints++)
		{
			pathParam->finishPaths.push_back(*itPoints);
		}
		pathParam->destPosition = path.GetActualEndPosition();
	}

	if (!pathParam->findOK && pathParam->finishPaths.size() > 2)
		pathParam->finishPaths.erase(pathParam->finishPaths.begin());
	ApplyFinishPath(pathParam);
	delete pathParam;
}

void BotBGAIMovement::MovementTo(ObjectGuid guid, float offset /* = 0 */)
{
    if (Player* carrier = GetWSGEscortCarrier())
    {
        if (guid == carrier->GetGUID())
        {
            FollowWSGCarrier(carrier);
            return;
        }
    }
    else
        ResetWSGEscort();
	if (m_BGAI)
	{
		if (m_BGAI->IsNotSelect(m_Player) || guid.IsEmpty())
			return;
	}
	pTargetAIWP = NULL;
	Position pos;
	pos.m_positionX = pos.m_positionY = pos.m_positionZ = 0;
	uint32 sessionID = 0;
	if (guid.IsPlayer())
	{
		Player* player = ObjectAccessor::FindPlayer(guid);
		if (player)
		{
			sessionID = player->GetSession()->GetAccountId();
			pos = player->GetPosition();
		}
		else
			return;
	}
	else if (guid.IsCreature())
	{
		Creature* creature = m_Player->GetMap()->GetCreature(guid);
		if (!creature)
			return;
		pos = creature->GetPosition();
	}
	else
	{
		GameObject* pObject = m_Player->GetMap()->GetGameObject(guid);
		if (!pObject)
			return;
		pos = pObject->GetPosition();
	}
	        // WSG uses the centralized coordinate movement so flags, players and
    // creatures cannot bypass base detection or the manual base routes.
    if (m_Player->GetMapId() == 489 && m_BGAI)
    {
        if (guid.IsPlayer() || guid.IsCreature()) {MoveWSGGroundTo(pos.GetVector3());return;}
        MovementTo(
            pos.GetPositionX(),
            pos.GetPositionY(),
            pos.GetPositionZ(),
            offset);
        return;
    }

    if (m_BGAI && m_Player->GetMapId()==761) {
        if(IsBFGJumpInProgress())return;
        if(guid.IsPlayer()){
            Player*target=ObjectAccessor::FindPlayer(guid);
            if(!target||!target->IsInWorld()||target->GetMap()!=m_Player->GetMap())return;
            if(auto*commander=dynamic_cast<CommandBFG*>(m_Player->GetMap()->GetCommander(m_Player->GetBGTeamId())))
                if(m_Player->IsValidAttackTarget(target)&&!commander->IsCombatTarget(m_Player,target))return;
            if(offset>0&&m_Player->GetDistance(target)<=offset*.9f&&m_Player->IsWithinLOSInMap(target)){
                if(m_ABSpline)ClearMovement();return;
            }
        }
        float x=pos.GetPositionX(),y=pos.GetPositionY(),z=pos.GetPositionZ();
        float dx=m_Player->GetPositionX()-x,dy=m_Player->GetPositionY()-y,d=std::sqrt(dx*dx+dy*dy);
        if(offset>1&&d>.1f){float spacing=std::min(offset*.8f,d);x+=dx/d*spacing;y+=dy/d*spacing;}
        MovementTo(x,y,z);return;
    }
    if (m_BGAI && m_Player->GetMapId()==30) {
        if (guid.IsPlayer()) {
            Player* target=ObjectAccessor::FindPlayer(guid);
            if (!target || !target->IsInWorld() || target->GetMap()!=m_Player->GetMap()) return;
        }
        MoveAVGroundTo(pos.GetPositionX(),pos.GetPositionY(),pos.GetPositionZ());
        return;
    }
    if (IsNearToPosition(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), (offset == 0) ? 1.0f : offset * 0.9f))
		return;
	Unit* pVehicle = m_Player->GetVehicleBase();
	PathParameter* pathParam = new PathParameter(sessionID, pVehicle ? pVehicle : m_Player);
	pathParam->targetPosX = pos.GetPositionX();
	pathParam->targetPosY = pos.GetPositionY();
	pathParam->targetPosZ = pos.GetPositionZ();
	pathParam->offset = offset;

	Pathfinding path(pathParam, NULL, NULL);
	if (pathParam->offset < 0)
		pathParam->offset *= -1;
	float posx = pathParam->targetPosX + ((pathParam->offset != 0) ? frand(pathParam->offset * (-1), pathParam->offset) : 0);
	float posy = pathParam->targetPosY + ((pathParam->offset != 0) ? frand(pathParam->offset * (-1), pathParam->offset) : 0);
	float posz = pathParam->targetPosZ;
	float const requestedZ = posz;
	if (m_Player->GetMapId() != 489)
        path.UpdateAllowedPositionZ(posx, posy, posz);
	if (IsDynamicOutdoorBot() &&
        (!std::isfinite(posz) || std::abs(posz - requestedZ) > 2.0f))
    {
        delete pathParam;
        m_MovementTick = 0;
        return;
    }
	bool result = path.CalculatePath(posx, posy, posz);
	if (m_BGAI && CommandObjectiveBG::Supports(m_Player->GetBattleground()) &&
	    (!result || path.GetPathType() != PATHFIND_NORMAL))
	{ delete pathParam; m_MovementTick = 0; return; }
    // V12.1: legacy findOK may be forced true later; validate the native result first.
    if(m_GroupAI && m_Player->GetMap() && m_Player->GetMap()->IsDungeon() &&
       (!result || path.GetPathType()!=PATHFIND_NORMAL)){
        delete pathParam;m_MovementTick=0;return;
    }
	if (IsDynamicOutdoorBot() &&
        (!result || !IsSafeDynamicOutdoorPath(path, posx, posy, posz)))
    {
        WarnRejectedDynamicPath(path, posx, posy, posz);
        delete pathParam;
        m_MovementTick = 0;
        return;
    }
	// FELSORIA_BOT_GROUND_V1: Path not find2: a failed ground path is never a direct move.
        bool const freeMovement = m_Player->IsFlying() || m_Player->IsInWater() ||
            m_Player->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING);
        pathParam->findOK = result &&
            !(path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_SHORTCUT)) &&
            (freeMovement || !(path.GetPathType() & PATHFIND_NOT_USING_PATH));

	if (pathParam->findOK)
	{
		pathParam->finishPaths.clear();
		const std::vector<G3D::Vector3>& points = path.GetPath();
		for (std::vector<G3D::Vector3>::const_iterator itPoints = points.begin();
			itPoints != points.end();
			itPoints++)
		{
			pathParam->finishPaths.push_back(*itPoints);
		}
		pathParam->destPosition = path.GetActualEndPosition();
	}

	if (!pathParam->findOK && pathParam->finishPaths.size() > 2)
		pathParam->finishPaths.erase(pathParam->finishPaths.begin());
	ApplyFinishPath(pathParam);
	delete pathParam;
}

void BotBGAIMovement::MovementToTarget()
{
    if (m_BGAI && m_Player->GetMapId()==30) {
        if (Unit* target=m_Player->GetSelectedUnit())
            MoveAVGroundTo(target->GetPositionX(),target->GetPositionY(),target->GetPositionZ());
        return;
    }
	if (m_BGAI && m_BGAI->IsNotSelect(m_Player))
		return;
    if (m_BGAI && m_Player->GetMapId()==489)
    {
        if (Unit* target=m_Player->GetSelectedUnit()) MoveWSGGroundTo(target->GetVector3());
        return;
    }
	Unit* pSelect = m_Player->GetSelectedUnit();
	if (pSelect)// && pSelect->ToPlayer())
	{
		//if (IsNearToPosition(pSelect->GetPositionX(), pSelect->GetPositionY(), pSelect->GetPositionZ(), 2.0f * 0.9f))
		//	return;
		Unit* pVehicle = m_Player->GetVehicleBase();
		PathParameter* pathParam = new PathParameter(m_Player->GetSession()->GetAccountId(), pVehicle ? pVehicle : m_Player);
		pathParam->targetPosX = pSelect->GetPositionX();
		pathParam->targetPosY = pSelect->GetPositionY();
		pathParam->targetPosZ = pSelect->GetPositionZ();
		pathParam->offset = 2.0f;

		Pathfinding path(pathParam, NULL, NULL);
		if (pathParam->offset < 0)
			pathParam->offset *= -1;
		float posx = pathParam->targetPosX + ((pathParam->offset != 0) ? frand(pathParam->offset * (-1), pathParam->offset) : 0);
		float posy = pathParam->targetPosY + ((pathParam->offset != 0) ? frand(pathParam->offset * (-1), pathParam->offset) : 0);
		float posz = pathParam->targetPosZ;
		float const requestedZ = posz;
		// WSG combat must preserve the target's actual floor, just like
		// coordinate and GUID movement. Height correction is unreliable here.
		if (m_Player->GetMapId() != 489)
			path.UpdateAllowedPositionZ(posx, posy, posz);
		if (IsDynamicOutdoorBot() &&
            (!std::isfinite(posz) || std::abs(posz - requestedZ) > 2.0f))
        {
            delete pathParam;
            return;
        }
		bool result = path.CalculatePath(posx, posy, posz);
		if (m_BGAI && CommandObjectiveBG::Supports(m_Player->GetBattleground()) &&
		    (!result || path.GetPathType() != PATHFIND_NORMAL))
		{ delete pathParam; m_MovementTick = 0; return; }
    // V12.1: legacy findOK may be forced true later; validate the native result first.
    if(m_GroupAI && m_Player->GetMap() && m_Player->GetMap()->IsDungeon() &&
       (!result || path.GetPathType()!=PATHFIND_NORMAL)){
        delete pathParam;m_MovementTick=0;return;
    }
		if (IsDynamicOutdoorBot() &&
            (!result || !IsSafeDynamicOutdoorPath(path, posx, posy, posz)))
        {
            WarnRejectedDynamicPath(path, posx, posy, posz);
            delete pathParam;
            return;
        }
		// FELSORIA_BOT_GROUND_V1: Path not find3.: a failed ground path is never a direct move.
        bool const freeMovement = m_Player->IsFlying() || m_Player->IsInWater() ||
            m_Player->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING);
        pathParam->findOK = result &&
            !(path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_SHORTCUT)) &&
            (freeMovement || !(path.GetPathType() & PATHFIND_NOT_USING_PATH));

		if (pathParam->findOK)
		{
			pathParam->finishPaths.clear();
			const std::vector<G3D::Vector3>& points = path.GetPath();
			for (std::vector<G3D::Vector3>::const_iterator itPoints = points.begin();
				itPoints != points.end();
				itPoints++)
			{
				pathParam->finishPaths.push_back(*itPoints);
			}
			pathParam->destPosition = path.GetActualEndPosition();
		}

		if (!pathParam->findOK && pathParam->finishPaths.size() > 2)
			pathParam->finishPaths.erase(pathParam->finishPaths.begin());
		ApplyFinishPath(pathParam);
		delete pathParam;
	}
}

bool BotBGAIMovement::MoveObjectiveTo(Position const& goal, float stopRange)
{
    if (!m_BGAI || m_BGAI->IsNotSelect(m_Player) || !CommandObjectiveBG::Supports(m_Player->GetBattleground()) ||
        !m_Player->isAlive() || m_Player->HasUnitState(UNIT_STATE_CASTING | UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING) ||
        m_Player->HasAuraWithMechanic(1 << Mechanics::MECHANIC_ROOT) || m_Player->GetTransport() ||
        m_Player->GetVehicleBase() || m_Player->IsFlying() ||
        m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR)) return false;
    float x = goal.GetPositionX(), y = goal.GetPositionY(), z = goal.GetPositionZ();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        m_Player->GetExactDist(x, y, z) <= stopRange) return false;
    uint32 now = getMSTime();
    if (m_ObjectiveInstance == m_Player->GetInstanceId() && m_ObjectiveMoveAt &&
        uint32(now - m_ObjectiveMoveAt) < 1500 && m_ObjectiveGoal.GetExactDist(x, y, z) < 3.0f &&
        (!m_ObjectiveMoving || (m_Player->movespline && !m_Player->movespline->Finalized())))
        return m_ObjectiveMoving;
    m_ObjectiveMoveAt = now;
    m_ObjectiveInstance = m_Player->GetInstanceId();
    m_ObjectiveGoal = goal;
    m_ObjectiveMoving = false;
    PathParameter parameters(m_Player->GetSession()->GetAccountId(), m_Player);
    parameters.targetPosX = x; parameters.targetPosY = y; parameters.targetPosZ = z;
    parameters.offset = 0;
    Pathfinding path(&parameters, nullptr, nullptr);
    if (!path.CalculatePath(x, y, z, false, false) || path.GetPathType() != PATHFIND_NORMAL ||
        path.GetPath().size() < 2 || path.GetPath().size() > 256) return false;
    G3D::Vector3 end = path.GetActualEndPosition();
    if (!std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z) ||
        (end - G3D::Vector3(x, y, z)).squaredLength() > 4.0f ||
        (path.GetPath().front() - m_Player->GetVector3()).squaredLength() > 9.0f) return false;
    for (auto const& point : path.GetPath())
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return false;
    parameters.findOK = true;
    parameters.finishPaths = path.GetPath();
    parameters.destPosition = end;
    ApplyFinishPath(&parameters);
    m_ObjectiveMoving = true;
    return true;
}

void BotBGAIMovement::ApplyFinishPath(PathParameter* pathParam)
{
    // Only FollowWSGCarrier marks the newly launched path as an escort spline.
    // Combat movement must not be mistaken for an unfinished escort segment.
    m_WSGEscortSpline = false;
	if (m_BGAI && m_BGAI->IsNotSelect(m_Player))
		return;
	if (!pathParam)
		return;
    if (m_BGAI && m_Player->GetMapId()==30) {
        if (pathParam->findOK && pathParam->finishPaths.size()>=2)
            MoveAVGroundTo(pathParam->destPosition.x,pathParam->destPosition.y,pathParam->destPosition.z);
        return;
    }
    if (m_BGAI && m_Player->GetMapId()==489)
    {
        if (!pathParam->findOK || pathParam->finishPaths.size()<2) return;
        MoveWSGGroundTo(pathParam->destPosition);
        return; // Revalidate async/legacy paths; never trim a WSG bend or insert a manual shortcut.
    }
    if(m_BGAI && m_Player->GetMapId()==529) {
        if(pathParam->findOK && pathParam->finishPaths.size()>=2)
            MoveABGroundTo(pathParam->destPosition.x,pathParam->destPosition.y,pathParam->destPosition.z);
        return;
    }
    // V12: legacy class/flee/follow paths obey the same dodge ownership and hazards.
    if(m_GroupAI && m_Player->GetMap() && m_Player->GetMap()->IsDungeon()){
        if(!pathParam->findOK || pathParam->finishPaths.size()<2)return;
        std::vector<FelsoriaMechanics::Vec> route;
        route.push_back({m_Player->GetPositionX(),m_Player->GetPositionY(),m_Player->GetPositionZ()});
        for(auto const& p:pathParam->finishPaths)route.push_back({p.x,p.y,p.z});
        if(!m_GroupAI->AllowsDungeonRoute(route))return;
    }
    // Never trim the first point: MoveSplineInit::Launch replaces it with
    // the current position. Trimming here loses the first real path bend.
    // Failed paths are retried; they cannot become straight moves or teleports.
    if (!pathParam->findOK || pathParam->finishPaths.size() < 2)
    {
        m_MovementTick = 0;
        return;
    }
    lastPathfindSure = 0;
    if (Unit* vehicle = m_Player->GetVehicleBase())
    {
        vehicle->GetMotionMaster()->Clear();
    }
    else
    {
        m_Player->GetMotionMaster()->Clear();
    }
	if (Unit* pVehicle = m_Player->GetVehicleBase())
	{
		pVehicle->GetMotionMaster()->MovePathfinding(pathParam);
	}
	else
		m_Player->GetMotionMaster()->MovePathfinding(pathParam);
}

void BotBGAIMovement::TeleportToValidPosition()
{
    // FELSORIA_BOT_GROUND_V1: no arbitrary relocation after a failed route.
    // In particular, never keep the old height after choosing random X/Y.
    m_MovementTick = 0;
}

void BotBGAIMovement::SyncPosition(Position& /*pos*/, bool immed)
{
    ++m_LastSyncTick;
    if (m_LastSyncTick <= 8 && !immed) return;
    m_LastSyncTick = 0;
    m_MovementTick = 0;
    // FELSORIA_BOT_GROUND_V1: the native spline/teleport owns position updates.
    // A fabricated client landing packet can advertise a different floor.
}

bool BotBGAIMovement::IsNearToPosition(float x, float y, float z, float range)
{
    if (m_BGAI && m_Player->GetMapId()==489)
    {
        float dx=m_Player->GetPositionX()-x,dy=m_Player->GetPositionY()-y,dz=m_Player->GetPositionZ()-z;
        return std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz) && range>=0 &&
            dx*dx+dy*dy+dz*dz<=range*range;
    }
	if (!m_Player->InBattleground())
		return false;
	if (Unit* pVehicle = m_Player->GetVehicleBase())
	{
		float dist = pVehicle->GetDistance(x, y, z);
		if (range < 1.0f)
			range = 1.0f;
		if (dist <= range)
			return true;
	}
	else
	{
		float dist = m_Player->GetDistance(x, y, z);
		if (range < 1.0f)
			range = 1.0f;
		if (dist <= range)
			return true;
	}
	return false;
}

bool BotBGAIMovement::CanMovementTo(float x, float y, float z)
{
	if (!m_BGAI || m_BGAI->IsNotSelect(m_Player))
		return false;

	uint32 sessionID = 0;
	if (m_Player->GetTypeId() == TypeID::TYPEID_PLAYER)
		sessionID = m_Player->GetSession()->GetAccountId();
	Unit* pVehicle = m_Player->GetVehicleBase();
	PathParameter* pathParam = new PathParameter(sessionID, pVehicle ? pVehicle : m_Player);
	pathParam->targetPosX = x;
	pathParam->targetPosY = y;
	pathParam->targetPosZ = z;
	pathParam->offset = 0;

	Pathfinding path(pathParam, NULL, NULL);
	if (pathParam->offset < 0)
		pathParam->offset *= -1;
	float posx = pathParam->targetPosX;
	float posy = pathParam->targetPosY;
	float posz = pathParam->targetPosZ;
	path.UpdateAllowedPositionZ(posx, posy, posz);
	bool result = path.CalculatePath(posx, posy, posz);
	if (!result ||
            (path.GetPathType() & PATHFIND_NOPATH) ||
            (m_Player->GetMapId() == 489 &&
                (path.GetPathType() & PATHFIND_NOT_USING_PATH)))
	{
		if (m_BGAI)
		{
			delete pathParam;
			return false;
		}
	}
	delete pathParam;
	return true;
}

bool BotBGAIMovement::SimulationMovementTo(float x, float y, float z, Position& outPos)
{
	if (!m_BGAI || m_BGAI->IsNotSelect(m_Player))
		return false;

	uint32 sessionID = 0;
	if (m_Player->GetTypeId() == TypeID::TYPEID_PLAYER)
		sessionID = m_Player->GetSession()->GetAccountId();
	Unit* pVehicle = m_Player->GetVehicleBase();
	PathParameter* pathParam = new PathParameter(sessionID, pVehicle ? pVehicle : m_Player);
	pathParam->targetPosX = x;
	pathParam->targetPosY = y;
	pathParam->targetPosZ = z;
	pathParam->offset = 0;

	Pathfinding path(pathParam, NULL, NULL);
	if (pathParam->offset < 0)
		pathParam->offset *= -1;
	float posx = pathParam->targetPosX;
	float posy = pathParam->targetPosY;
	float posz = pathParam->targetPosZ;
	path.UpdateAllowedPositionZ(posx, posy, posz);
	bool result = path.CalculatePath(posx, posy, posz);
	if (!result ||
            (path.GetPathType() & PATHFIND_NOPATH) ||
            (m_Player->GetMapId() == 489 &&
                (path.GetPathType() & PATHFIND_NOT_USING_PATH)))
	{
		if (m_BGAI)
		{
			delete pathParam;
			return false;
		}
	}

	Map* pMap = m_Player->GetMap();
	uint32 size = path.GetPath().size();
	if (size <= 2)
	{
		delete pathParam;
		return false;
	}
	bool hasPos = false;
	for (uint32 i = 0; i < size; i++)
	{
		if (i >= size - 1)
			break;
		const G3D::Vector3& p = path.GetPath()[i];
		const G3D::Vector3& np = path.GetPath()[i + 1];
		if (pMap->isInLineOfSight(p.x, p.y, p.z + 2.0f, np.x, np.y, np.z + 2.0f, m_Player->GetPhases()))
		{
			outPos.m_positionX = p.x;
			outPos.m_positionY = p.y;
			outPos.m_positionZ = p.z;
			hasPos = true;
		}
	}
	if (!hasPos)
	{
		delete pathParam;
		return false;
	}
	//outPos.m_positionZ = pMap->GetHeight(m_Player->GetPhaseMask(), outPos.m_positionX, outPos.m_positionY, outPos.m_positionZ);
	delete pathParam;
	return true;
}

uint32 BotBGAIMovement::GetTargetFindpathPointCount(Player* self, Unit* pTarget)
{
	if (!self || !pTarget)
		return 0;
	uint32 sessionID = 0;
	if (self->GetTypeId() == TypeID::TYPEID_PLAYER)
		sessionID = self->GetSession()->GetAccountId();
	Unit* pVehicle = self->GetVehicleBase();
	PathParameter* pathParam = new PathParameter(sessionID, pVehicle ? pVehicle : self);
	pathParam->targetPosX = pTarget->GetPositionX();
	pathParam->targetPosY = pTarget->GetPositionY();
	pathParam->targetPosZ = pTarget->GetPositionZ();
	pathParam->offset = 0;

	Pathfinding path(pathParam, NULL, NULL);
	if (pathParam->offset < 0)
		pathParam->offset *= -1;
	float posx = pathParam->targetPosX;
	float posy = pathParam->targetPosY;
	float posz = pathParam->targetPosZ;
	path.UpdateAllowedPositionZ(posx, posy, posz);
	bool result = path.CalculatePath(posx, posy, posz);
	if (!result ||
            (path.GetPathType() & PATHFIND_NOPATH) ||
            (self->GetMapId() == 489 &&
                (path.GetPathType() & PATHFIND_NOT_USING_PATH)))
	{
		delete pathParam;
		return 0;
	}

	uint32 size = path.GetPath().size();
	delete pathParam;
	if (size <= 2)
		return 0;
	return size;
}

// FELSORIA_AB_MANUAL_V2_1: the native spline requires adjacent points >=0.1m.
// Use a margin, retaining straight segments and all meaningful recorded bends.
namespace {
void CleanABPoints(std::vector<G3D::Vector3>const&input,std::vector<G3D::Vector3>&out){
    out.clear();
    for(auto const&v:input)if(out.empty()||(out.back()-v).squaredLength()>=0.0625f)out.push_back(v);
}
}
bool BotBGAIMovement::BuildABGroundPath(float x,float y,float z,std::vector<G3D::Vector3>&out)
{
    out.clear();Battleground*bg=m_Player->GetBattleground();
    if(!m_BGAI||m_Player->GetMapId()!=529||!bg||bg->GetStatus()!=STATUS_IN_PROGRESS||
        !m_Player->isAlive()||!m_Player->IsInWorld()||m_Player->IsBeingTeleported()||
        !std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return false;
    if(m_Player->GetDistance(Position(x,y,z,0))<0.35f){out.push_back(m_Player->GetVector3());return true;}
    PathParameter param(m_Player->GetSession()->GetAccountId(),m_Player);
    Pathfinding path(&param,nullptr,nullptr);path.SetUseStraightPath(true);
    bool valid=path.CalculatePath(x,y,z);PathType type=path.GetPathType();
    valid=valid&&(type&PATHFIND_NORMAL)&&!(type&(PATHFIND_NOPATH|PATHFIND_INCOMPLETE|PATHFIND_NOT_USING_PATH|PATHFIND_SHORTCUT|PATHFIND_SHORT));
    auto const&points=path.GetPath();valid=valid&&points.size()>=2;
    if(valid){auto const&end=path.GetActualEndPosition();
        valid=std::abs(end.x-x)<2.5f&&std::abs(end.y-y)<2.5f&&std::abs(end.z-z)<3.0f;
        for(auto const&v:points)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)){valid=false;break;}
    }
    if(!valid)return false;
    out=points;out.front()=m_Player->GetVector3();return true;
}
void BotBGAIMovement::MoveABGroundTo(float x,float y,float z)
{
    if(m_Player->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||
        m_Player->HasAuraType(SPELL_AURA_MOD_ROOT)||m_Player->GetVehicleBase()||
        m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR))return;
    uint32 now=getMSTime();Position goal(x,y,z,0);
    if(m_ABSpline&&!m_ABRecorded&&goal.GetExactDist(&m_ABGoal)<1.0f&&m_Player->movespline&&!m_Player->movespline->Finalized()){
        if(m_Player->GetDistance(m_ABProgress)>0.6f){m_ABProgress=m_Player->GetPosition();m_ABProgressAt=now;return;}
        if(now-m_ABProgressAt<4000)return;
    }
    if(m_Player->GetDistance(goal)<1.1f)return;
    if(m_ABAttemptAt&&now-m_ABAttemptAt<400)return;m_ABAttemptAt=now;
    std::vector<G3D::Vector3>raw,points;
    if(!BuildABGroundPath(x,y,z,raw)){
        if(!m_ABRejectAt||now-m_ABRejectAt>5000){m_ABRejectAt=now;
            TC_LOG_INFO(LOG_FILTER_PATH_GENERATOR,"FELSORIA_AB_PATH_REJECT instance=%u bot=%s x=%.2f y=%.2f z=%.2f",
                m_Player->GetInstanceId(),m_Player->GetName(),x,y,z);}return;
    }
    CleanABPoints(raw,points);if(points.size()<2)return;
    if(!m_ABSpline)m_Player->GetMotionMaster()->Clear();
    Movement::MoveSplineInit init(*m_Player);init.MovebyPath(points);init.SetUncompressed();init.SetWalk(false);
    if(init.Launch()<=0){m_ABSpline=false;return;}
    m_ABGoal=goal;m_ABProgress=m_Player->GetPosition();m_ABProgressAt=now;m_ABSpline=true;m_ABRecorded=false;
}
bool BotBGAIMovement::MoveABRecordedPath(std::vector<G3D::Vector3>const&raw)
{
    Battleground*bg=m_Player->GetBattleground();
    if(!m_BGAI||m_Player->GetMapId()!=529||!bg||bg->GetStatus()!=STATUS_IN_PROGRESS||
        !m_Player->isAlive()||!m_Player->IsInWorld()||m_Player->IsBeingTeleported()||raw.size()<2)return false;
    if(m_Player->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||
        m_Player->HasAuraType(SPELL_AURA_MOD_ROOT)||m_Player->GetVehicleBase()||
        m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR))return true;
    for(auto const&v:raw)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return false;
    auto const&end=raw.back();Position goal(end.x,end.y,end.z,0);uint32 now=getMSTime();
    if(m_Player->GetDistance(goal)<1.1f)return true;
    std::vector<G3D::Vector3>path;CleanABPoints(raw,path);if(path.size()<2)return false;
    Position origin(path.front().x,path.front().y,path.front().z,0);
    bool same=m_ABSpline&&m_ABRecorded&&goal.GetExactDist(&m_ABGoal)<0.5f&&origin.GetExactDist(&m_ABPathStart)<0.1f;
    if(!same)m_ABPathCursor=0;
    G3D::Vector3 current=m_Player->GetVector3();
    if(m_Player->movespline&&!m_Player->movespline->Finalized()){
        auto position=m_Player->movespline->ComputePosition();current=G3D::Vector3(position.x,position.y,position.z);
    }
    G3D::Vector3 nearest;float best=1e20f;unsigned segment=m_ABPathCursor;
    unsigned first=std::min(m_ABPathCursor,unsigned(path.size()-2));
    unsigned last=std::min(unsigned(path.size()-1),first+64);
    for(unsigned i=first;i<last;++i){
        auto delta=path[i+1]-path[i];float sq=delta.squaredLength();
        float t=sq>0.00001f?std::max(0.0f,std::min(1.0f,(current-path[i]).dot(delta)/sq)):0.0f;
        auto point=path[i]+delta*t;float d=(current-point).squaredLength();
        if(d<best-0.00001f){best=d;nearest=point;segment=i;}
    }
    if(best>6.25f||std::abs(current.z-nearest.z)>2.5f)return false;
    m_ABPathCursor=segment;
    bool moving=m_Player->movespline&&!m_Player->movespline->Finalized();
    if(same&&moving&&std::abs(m_ABSpeed-m_Player->GetSpeed(MOVE_RUN))<0.1f){
        if(m_Player->GetDistance(m_ABProgress)>0.6f){m_ABProgress=m_Player->GetPosition();m_ABProgressAt=now;return true;}
        if(now-m_ABProgressAt<4000)return true;
    }
    if(m_ABAttemptAt&&now-m_ABAttemptAt<200)return true;
    std::vector<G3D::Vector3>leg;leg.push_back(current);
    if((current-nearest).squaredLength()>=0.0625f)leg.push_back(nearest);
    for(unsigned i=segment+1;i<path.size();++i)
        if((leg.back()-path[i]).squaredLength()>=0.0625f)leg.push_back(path[i]);
    if(leg.size()<2)return true;
    m_ABAttemptAt=now;
    // Launch computes the current spline position itself. Sending StopMoving
    // first introduces a stop packet and a visible pause at every replacement.
    if(!m_ABSpline)m_Player->GetMotionMaster()->Clear();
    Movement::MoveSplineInit init(*m_Player);init.MovebyPath(leg);init.SetUncompressed();init.SetWalk(false);
    if(init.Launch()<=0){
        m_ABSpline=false;
        TC_LOG_INFO(LOG_FILTER_PATH_GENERATOR,"FELSORIA_AB_SPLINE_REJECT instance=%u bot=%s points=%u",
            m_Player->GetInstanceId(),m_Player->GetName(),uint32(leg.size()));return false;
    }
    m_ABGoal=goal;m_ABPathStart=origin;m_ABSpeed=m_Player->GetSpeed(MOVE_RUN);
    m_ABProgress=m_Player->GetPosition();m_ABProgressAt=now;m_ABSpline=true;m_ABRecorded=true;return true;
}

// FELSORIA_AV_TACTICS_V1_5: validate the actual floor and each short segment.
bool BotBGAIMovement::GetAVFloor(G3D::Vector3 const& point,float& floor) const
{
 if(!m_Player->GetMap()||!FelsoriaAVSafety::Finite({point.x,point.y,point.z}))return false;
 auto*map=m_Player->GetMap();
 // Map::GetHeight adds two yards internally. Begin the first ray just above
 // the feet, so a low ceiling is not mistaken for the walking floor.
 float nearFloor=map->GetHeight(m_Player->GetPhases(),point.x,point.y,point.z-1.5f,true,7.0f);
 if(std::isfinite(nearFloor)&&nearFloor>-99999.0f&&std::abs(nearFloor-point.z)<.5f){floor=nearFloor;return true;}
 float upperFloor=map->GetHeight(m_Player->GetPhases(),point.x,point.y,point.z+.5f,true,8.0f);
 return FelsoriaAVSafety::LocalFloor(point.z,nearFloor,upperFloor,floor);
}

bool BotBGAIMovement::ValidateAVGroundPath(std::vector<G3D::Vector3>const&raw,std::vector<G3D::Vector3>&out)
{
 out.clear();m_AVRejectReason="invalid_ground_input";
 if(raw.empty()||raw.size()>4096||!m_Player->GetMap())return false;
 float floor=0;if(!GetAVFloor(raw.front(),floor)){m_AVRejectReason="start_floor";return false;}
 out.push_back(raw.front());
 for(unsigned i=1;i<raw.size();++i){
  auto const&a=raw[i-1];auto const&b=raw[i];
  if(!FelsoriaAVSafety::SafeStep({a.x,a.y,a.z},{b.x,b.y,b.z})){m_AVRejectReason="raw_step";out.clear();return false;}
  // Rechecking a previously sampled path must not subdivide 1.25-yard
  // segments again because of coordinate rounding at AV world positions.
  float dx=b.x-a.x,dy=b.y-a.y;
  unsigned samples=std::max(1u,unsigned(std::ceil(std::max(0.0f,std::sqrt(dx*dx+dy*dy)-.001f)/1.25f)));
  for(unsigned j=1;j<=samples;++j){float f=float(j)/samples;
   G3D::Vector3 p(a.x+(b.x-a.x)*f,a.y+(b.y-a.y)*f,a.z+(b.z-a.z)*f);
   if(!GetAVFloor(p,floor)){m_AVRejectReason="sample_floor";out.clear();return false;}
   p.z=floor+.05f;
   auto const&previous=out.back();
   if(!FelsoriaAVSafety::SafeStep({previous.x,previous.y,previous.z},{p.x,p.y,p.z})){
    m_AVRejectReason="projected_step";out.clear();return false;
   }
   // Check the standing-player corridor. The mesh path already follows
   // walkable polygons; a low ray can clip their stair risers.
   if(!m_Player->GetMap()->isInLineOfSight(previous.x,previous.y,previous.z+2.0f,p.x,p.y,p.z+2.0f,m_Player->GetPhases())){
    m_AVRejectReason="ground_los";out.clear();return false;
   }
   // MSVC rejects adjacent spline points below 0.1 yard. A 0.25-yard
   // margin prevents near-duplicate joins from repeatedly failing Launch().
   if((previous-p).squaredLength()>=.0625f)out.push_back(p);
  }
 }
 m_AVRejectReason="none";return true;
}

bool BotBGAIMovement::RecoverFallenAVBot()
{
 if(!m_BGAI||m_Player->GetMapId()!=30||!m_Player->GetBattleground()||
    m_Player->GetBattleground()->GetStatus()!=STATUS_IN_PROGRESS||!m_Player->isAlive()||
    !m_Player->IsInWorld()||m_Player->IsBeingTeleported()||m_Player->GetVehicleBase()||m_Player->GetTransport())return false;
 uint32 now=getMSTime();
 if(m_AVSafeInstance!=m_Player->GetInstanceId()){
  m_AVSafeInstance=m_Player->GetInstanceId();m_AVHasSafeGround=false;m_AVBelowSince=0;m_AVRecoveryUntil=0;
 }
 auto current=m_Player->GetVector3();float floor=0;
 bool onLocalFloor=GetAVFloor(current,floor)&&std::abs(current.z-floor)<1.0f;
 bool recent=m_AVHasSafeGround&&now-m_AVSafeAt<15000;
 float dx=current.x-m_AVSafeGround.GetPositionX(),dy=current.y-m_AVSafeGround.GetPositionY();
 bool below=false;
 if(!onLocalFloor&&recent&&dx*dx+dy*dy<=64.0f&&current.z<m_AVSafeGround.GetPositionZ()-2.0f){
  float upper=m_Player->GetMap()->GetHeight(m_Player->GetPhases(),current.x,current.y,m_AVSafeGround.GetPositionZ()+.25f,true,10.0f);
  below=std::isfinite(upper)&&upper>current.z+1.5f&&std::abs(upper-m_AVSafeGround.GetPositionZ())<2.5f;
 }
 if(below){
  if(m_AVRecoveryUntil&&int32(m_AVRecoveryUntil-now)>0)return true;
  if(!m_AVBelowSince){
   m_AVBelowSince=now?now:1;ClearMovement();m_Player->GetMotionMaster()->Clear();return true;
  }
  if(now-m_AVBelowSince<500)return true;
  // Return only to a nearby floor point this bot actually occupied and which
  // still exists in its current phase. Keep its combat state and attackers.
  auto safe=m_AVSafeGround.GetVector3();
  if(!GetAVFloor(safe,floor)||std::abs(floor-safe.z)>1.0f)return true;
  ClearMovement();m_Player->StopMoving();m_Player->GetMotionMaster()->Clear();
  m_BGAI->SetTeleport(m_AVSafeGround,true);m_AVRecoveryUntil=now+5000;m_AVBelowSince=0;
  TC_LOG_WARN(LOG_FILTER_BATTLEGROUND,"FELSORIA_AV_GROUND_RECOVERY instance=%u bot=%s fromZ=%.3f toZ=%.3f",m_Player->GetInstanceId(),m_Player->GetName(),current.z,safe.z);
  return true;
 }
 m_AVBelowSince=0;
 if(!m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR)&&onLocalFloor){
  m_AVSafeGround=m_Player->GetPosition();m_AVSafeAt=now;m_AVHasSafeGround=true;
 }
 return false;
}

bool BotBGAIMovement::BuildAVGroundPath(float x,float y,float z,std::vector<G3D::Vector3>&out)
{
 out.clear();auto*bg=m_Player->GetBattleground();
 if(!m_BGAI||m_Player->GetMapId()!=30||!bg||
    (bg->GetStatus()!=STATUS_WAIT_JOIN&&bg->GetStatus()!=STATUS_IN_PROGRESS)||
    !m_Player->isAlive()||!m_Player->IsInWorld()||m_Player->IsBeingTeleported()||
    !FelsoriaAVSafety::Finite({x,y,z})||!m_Player->GetSession())return false;
 G3D::Vector3 start=m_Player->GetVector3();
 if(m_Player->movespline&&!m_Player->movespline->Finalized()){
  auto v=m_Player->movespline->ComputePosition();start={v.x,v.y,v.z};
 }
 PathParameter param(m_Player->GetSession()->GetAccountId(),m_Player);
 param.posX=start.x;param.posY=start.y;param.posZ=start.z;
 Pathfinding path(&param,nullptr,nullptr);path.SetUseStraightPath(false);
 if(!path.CalculatePath(x,y,z,false,false)||path.GetPathType()!=PATHFIND_NORMAL)return false;
 auto raw=path.GetPath();
 if(raw.size()<2||raw.size()>4096||(raw.front()-start).squaredLength()>6.25f||
    !FelsoriaAVSafety::Finite({raw.back().x,raw.back().y,raw.back().z})||
    std::abs(raw.back().x-x)>3.0f||std::abs(raw.back().y-y)>3.0f||std::abs(raw.back().z-z)>4.0f)return false;
 raw.front()=start;
 if(!ValidateAVGroundPath(raw,out)){out.clear();return false;}
 if(bg->GetStatus()==STATUS_WAIT_JOIN){
  TeamId team=m_Player->GetBGTeamId();if(team>=TEAM_NEUTRAL){out.clear();return false;}
  auto*spawn=bg->GetTeamStartPosition(team);if(!spawn){out.clear();return false;}
  auto const&gate=BG_AV_DoorPositons[team];
  for(auto const&p:out)if(!FelsoriaAVSafety::InsideStartingCave(
      {spawn->GetPositionX(),spawn->GetPositionY(),spawn->GetPositionZ()},
      {gate[0],gate[1],gate[2]},{p.x,p.y,p.z})){out.clear();return false;}
 }
 return !out.empty();
}

void BotBGAIMovement::MoveAVGroundTo(float x,float y,float z)
{
 auto*bg=m_Player->GetBattleground();
 if(!m_BGAI||m_Player->GetMapId()!=30||!bg||!m_Player->isAlive()||!m_Player->IsInWorld()||m_Player->IsBeingTeleported()||
    (bg->GetStatus()!=STATUS_WAIT_JOIN&&bg->GetStatus()!=STATUS_IN_PROGRESS))return;
 if(m_Player->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||
    m_Player->HasAuraType(SPELL_AURA_MOD_ROOT)||m_Player->GetVehicleBase()||m_Player->GetTransport()||
    m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR))return;
 if(!FelsoriaAVMovementAllowed(m_Player,x,y,z))return;
 bool preparing=bg->GetStatus()==STATUS_WAIT_JOIN;
 if(preparing){
  TeamId team=m_Player->GetBGTeamId();if(team>=TEAM_NEUTRAL)return;
  auto*spawn=bg->GetTeamStartPosition(team);if(!spawn)return;auto const&gate=BG_AV_DoorPositons[team];
  if(m_AVReadyInstance!=m_Player->GetInstanceId()){
   m_AVReadyInstance=m_Player->GetInstanceId();m_AVReadyChosen=false;m_AVReadyAttempt=0;
  }
  if(m_AVReadyChosen){x=m_AVReadyGoal.GetPositionX();y=m_AVReadyGoal.GetPositionY();z=m_AVReadyGoal.GetPositionZ();}
  else{
   unsigned seed=m_Player->GetGUIDLow()^(m_AVReadyInstance*0x9e3779b9u)^(m_AVReadyAttempt*0x85ebca6bu);
   auto ready=FelsoriaAVSafety::ReadyPoint({spawn->GetPositionX(),spawn->GetPositionY(),spawn->GetPositionZ()},
       {gate[0],gate[1],gate[2]},seed);
   float floor=0;
   if(!GetAVFloor({ready.x,ready.y,ready.z},floor)){++m_AVReadyAttempt;return;}
   x=ready.x;y=ready.y;z=floor+.05f;
  }
 }
 if(!FelsoriaAVSafety::Finite({x,y,z}))return;
 uint32 now=getMSTime();Position goal(x,y,z,0);
 bool running=m_Player->movespline&&!m_Player->movespline->Finalized();
 bool same=m_AVSpline&&!m_AVRecorded&&m_AVInstance==m_Player->GetInstanceId()&&m_AVPreparing==preparing&&
     goal.GetExactDist(&m_AVGoal)<3.0f&&std::abs(m_AVSpeed-m_Player->GetSpeed(MOVE_RUN))<.1f;
 if(same&&running){
  if(m_Player->GetDistance(m_AVProgress)>.6f){m_AVProgress=m_Player->GetPosition();m_AVProgressAt=now;}
  bool continuation=m_Player->movespline->Duration()-m_Player->movespline->GetTimePassed()<=750&&m_Player->GetDistance(goal)>4.0f;
  if(now-m_AVProgressAt<4000&&!continuation)return;
 }
 if(m_Player->GetDistance(goal)<1.1f)return;
 if(m_AVAttemptAt&&now-m_AVAttemptAt<400)return;m_AVAttemptAt=now;
 std::vector<G3D::Vector3>raw;
 if(!BuildAVGroundPath(x,y,z,raw)){
  if(preparing&&!m_AVReadyChosen)++m_AVReadyAttempt;
  if(!same){m_Player->StopMoving();m_Player->GetMotionMaster()->Clear();m_AVSpline=false;}
  return;
 }
 if(preparing&&!m_AVReadyChosen){m_AVReadyGoal=goal;m_AVReadyChosen=true;}
 std::vector<G3D::Vector3>chunk;chunk.push_back(raw.front());float length=0;
 for(unsigned i=1;i<raw.size()&&chunk.size()<64;++i){
  float step=(raw[i]-chunk.back()).length();if(length+step>65.0f&&chunk.size()>1)break;
  chunk.push_back(raw[i]);length+=step;
 }
 if(chunk.size()<2)return;
 if(!m_AVSpline)m_Player->GetMotionMaster()->Clear();
 Movement::MoveSplineInit init(*m_Player);init.MovebyPath(chunk);init.SetUncompressed();init.SetWalk(false);
 if(init.Launch()<=0){m_AVSpline=false;return;}
 m_AVSpline=true;m_AVRecorded=false;m_AVPreparing=preparing;m_AVInstance=m_Player->GetInstanceId();
 m_AVGoal=goal;m_AVProgress=m_Player->GetPosition();m_AVProgressAt=now;m_AVSpeed=m_Player->GetSpeed(MOVE_RUN);
}

bool BotBGAIMovement::BuildAVGroundLeg(G3D::Vector3 const&start,G3D::Vector3 const&goal,std::vector<G3D::Vector3>&out)
{
 out.clear();m_AVRejectReason="invalid_leg_context";auto*bg=m_Player->GetBattleground();
 if(!m_BGAI||m_Player->GetMapId()!=30||!bg||bg->GetStatus()!=STATUS_IN_PROGRESS||
    !m_Player->isAlive()||!m_Player->IsInWorld()||m_Player->IsBeingTeleported()||!m_Player->GetSession()||
    !FelsoriaAVSafety::Finite({start.x,start.y,start.z})||!FelsoriaAVSafety::Finite({goal.x,goal.y,goal.z}))return false;
 if((start-goal).squaredLength()<.0625f){float floor=0;if(!GetAVFloor(start,floor)){m_AVRejectReason="start_floor";return false;}out.push_back(start);m_AVRejectReason="none";return true;}
 PathParameter param(m_Player->GetSession()->GetAccountId(),m_Player);
 param.posX=start.x;param.posY=start.y;param.posZ=start.z;param.targetPosX=goal.x;param.targetPosY=goal.y;param.targetPosZ=goal.z;
 Pathfinding path(&param,nullptr,nullptr);path.SetUseStraightPath(false);path.SetPathLengthLimit(360.0f);
 bool normal=path.CalculatePath(goal.x,goal.y,goal.z,false,false)&&path.GetPathType()==PATHFIND_NORMAL;
 m_AVRejectReason="native_path_type";
 if(normal){
  auto raw=path.GetPath();m_AVRejectReason="native_path_endpoints";
  if(raw.size()>=2&&raw.size()<=4096&&(raw.front()-start).squaredLength()<=6.25f&&(raw.back()-goal).squaredLength()<=4.0f){
   raw.front()=start;if(ValidateAVGroundPath(raw,out)&&!out.empty())return true;
  }
 }
 // A very short join or a recorded road edge need not fail because Detour
 // returns a degenerate path, a wrong-height polygon, or an incomplete seam.
 // Authorize it only with positive walking-floor and collision checks along
 // the entire local segment. Longer paths still require a complete mesh path.
 if((start-goal).squaredLength()<=144.0f&&FelsoriaAVSafety::SafeStep({start.x,start.y,start.z},{goal.x,goal.y,goal.z})){
  std::vector<G3D::Vector3>local{start,goal};
  if(ValidateAVGroundPath(local,out)&&!out.empty()){
   // Without a complete mesh corridor also require low obstacle clearance.
   // A waist-high fence must not become a shortcut just because the ray
   // above the character's feet passes over it.
   bool clear=true;
   for(unsigned i=1;i<out.size();++i){auto const&a=out[i-1];auto const&b=out[i];
    if(!m_Player->GetMap()->isInLineOfSight(a.x,a.y,a.z+.6f,b.x,b.y,b.z+.6f,m_Player->GetPhases())){clear=false;break;}
   }
   if(clear)return true;
   m_AVRejectReason="local_obstacle";
  }
 }
 out.clear();return false;
}

bool BotBGAIMovement::MoveAVRecordedChunk(std::vector<G3D::Vector3>const&path)
{
 auto*bg=m_Player->GetBattleground();
 if(!m_BGAI||m_Player->GetMapId()!=30||!bg||bg->GetStatus()!=STATUS_IN_PROGRESS||!m_Player->isAlive()||
    !m_Player->IsInWorld()||m_Player->IsBeingTeleported()||path.size()<2||path.size()>64||
    m_Player->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||
    m_Player->HasAuraType(SPELL_AURA_MOD_ROOT)||m_Player->GetVehicleBase()||m_Player->GetTransport()||
    m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR))return false;
 G3D::Vector3 current=m_Player->GetVector3();
 if(m_Player->movespline&&!m_Player->movespline->Finalized()){
  auto v=m_Player->movespline->ComputePosition();current={v.x,v.y,v.z};
 }
 if((path.front()-current).squaredLength()>6.25f)return false;
 auto raw=path;raw.front()=current;std::vector<G3D::Vector3>validated;
 if(!ValidateAVGroundPath(raw,validated))return false;
 if(validated.size()<2||validated.size()>64){m_AVRejectReason="spline_point_limit";return false;}
 float distance=0;for(unsigned i=1;i<validated.size();++i)distance+=(validated[i]-validated[i-1]).length();
 if(distance>90.0f)return false;
 // A live AV spline already belongs to us. Clearing it here sends a stop
 // before every extension and can reconnect a stale first point across a bend.
 if(!m_AVSpline)m_Player->GetMotionMaster()->Clear();
 Movement::MoveSplineInit init(*m_Player);init.MovebyPath(validated);init.SetUncompressed();init.SetWalk(false);
 if(init.Launch()<=0){m_AVSpline=false;return false;}
 m_AVSpline=true;m_AVRecorded=true;m_AVPreparing=false;m_AVInstance=m_Player->GetInstanceId();
 auto const&end=validated.back();m_AVGoal=Position(end.x,end.y,end.z,0);
 m_AVProgress=m_Player->GetPosition();m_AVProgressAt=getMSTime();m_AVSpeed=m_Player->GetSpeed(MOVE_RUN);return true;
}

bool BotBGAIMovement::MoveBFGRecordedPath(std::vector<G3D::Vector3>const&raw)
{
    if(m_BFGRecoveryHop&&IsBFGJumpInProgress())return true;
    if(m_BFGWaterActive){if(ContinueBFGWaterCrossing())return true;if(std::string(m_BFGMovementReason).find("water_reject")==0)return false;}
    m_BFGMovementReason="spline_context";Battleground*bg=m_Player->GetBattleground();
    if(!m_BGAI||m_Player->GetMapId()!=761||!bg||(bg->GetStatus()!=STATUS_WAIT_JOIN&&bg->GetStatus()!=STATUS_IN_PROGRESS)||
        !m_Player->isAlive()||!m_Player->IsInWorld()||m_Player->IsBeingTeleported()||raw.size()<2)return false;
    if(m_Player->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||
        m_Player->HasAuraType(SPELL_AURA_MOD_ROOT)||m_Player->GetVehicleBase()||
        m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR)){m_BFGMovementReason="movement_blocked";return true;}
    for(auto const&v:raw)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return false;
    auto const&end=raw.back();Position goal(end.x,end.y,end.z,0);uint32 now=getMSTime();
    if(m_Player->GetDistance(goal)<1.1f){m_BFGMovementReason="arrived";return true;}
    std::vector<G3D::Vector3>path;CleanABPoints(raw,path);if(path.size()<2)return false;
    Position origin(path.front().x,path.front().y,path.front().z,0);
    bool same=m_ABSpline&&m_ABRecorded&&goal.GetExactDist(&m_ABGoal)<0.5f&&origin.GetExactDist(&m_ABPathStart)<0.1f;
    if(!same)m_ABPathCursor=0;
    G3D::Vector3 current=m_Player->GetVector3();
    if(m_Player->movespline&&!m_Player->movespline->Finalized()){
        auto position=m_Player->movespline->ComputePosition();current=G3D::Vector3(position.x,position.y,position.z);
    }
    G3D::Vector3 nearest;float best=1e20f;unsigned segment=m_ABPathCursor;
    unsigned first=std::min(m_ABPathCursor,unsigned(path.size()-2));
    unsigned last=std::min(unsigned(path.size()-1),first+64);
    for(unsigned i=first;i<last;++i){
        auto delta=path[i+1]-path[i];float sq=delta.squaredLength();
        float t=sq>0.00001f?std::max(0.0f,std::min(1.0f,(current-path[i]).dot(delta)/sq)):0.0f;
        auto point=path[i]+delta*t;float d=(current-point).squaredLength();
        if(d<best-0.00001f){best=d;nearest=point;segment=i;}
    }
    if(best>6.25f||std::abs(current.z-nearest.z)>2.5f){m_BFGMovementReason="off_recorded_path";return false;}
    m_ABPathCursor=segment;
    FelsoriaGilneasWater::Span water;bool crossing=FelsoriaGilneasWater::FindSpan(path,segment,water);
    if(crossing&&(current-path[water.takeoff]).squaredLength()<1.0f){
        auto const&landing=path[water.landing];
        m_BFGWaterActive=true;m_BFGHopActive=false;m_BFGWaterReverse=water.reverse;
        m_BFGWaterStarted=now;m_BFGWaterInstance=m_Player->GetInstanceId();m_BFGWaterLanding=water.landing;
        m_BFGWaterGoal=Position(landing.x,landing.y,landing.z,0);
        m_ABGoal=goal;m_ABPathStart=origin;m_ABRecorded=true;m_ABSpline=true;
        return ContinueBFGWaterCrossing();
    }
    bool moving=m_Player->movespline&&!m_Player->movespline->Finalized();
    if(same&&moving&&std::abs(m_ABSpeed-m_Player->GetSpeed(MOVE_RUN))<0.1f){
        if(m_Player->GetDistance(m_ABProgress)>0.6f){m_ABProgress=m_Player->GetPosition();m_ABProgressAt=now;return true;}
        if(now-m_ABProgressAt<4000)return true;
    }
    if(m_ABAttemptAt&&now-m_ABAttemptAt<200)return true;
    std::vector<G3D::Vector3>leg;leg.push_back(current);
    if((current-nearest).squaredLength()>=0.0625f)leg.push_back(nearest);
    unsigned stop=crossing?water.takeoff:unsigned(path.size()-1);
    for(unsigned i=segment+1;i<=stop;++i)
        if((leg.back()-path[i]).squaredLength()>=0.0625f)leg.push_back(path[i]);
    if(leg.size()<2)return true;
    m_ABAttemptAt=now;
    // Launch computes the current spline position itself. Sending StopMoving
    // first introduces a stop packet and a visible pause at every replacement.
    if(!m_ABSpline)m_Player->GetMotionMaster()->Clear();
    Movement::MoveSplineInit init(*m_Player);init.MovebyPath(leg);init.SetUncompressed();init.SetWalk(false);
    if(init.Launch()<=0){
        m_ABSpline=false;m_BFGMovementReason="spline_launch_failed";
        if(sConfigMgr->GetBoolDefault("Felsoria.Gilneas.Debug",false))TC_LOG_INFO(LOG_FILTER_PATH_GENERATOR,"FELSORIA_BFG_SPLINE_REJECT instance=%u bot=%s points=%u",
            m_Player->GetInstanceId(),m_Player->GetName(),uint32(leg.size()));return false;
    }
    m_ABGoal=goal;m_ABPathStart=origin;m_ABSpeed=m_Player->GetSpeed(MOVE_RUN);
    m_ABProgress=m_Player->GetPosition();m_ABProgressAt=now;m_ABSpline=true;m_ABRecorded=true;m_BFGMovementReason="spline_launched";return true;
}


bool BotBGAIMovement::BuildBFGGroundLeg(G3D::Vector3 const&start,G3D::Vector3 const&goal,std::vector<G3D::Vector3>&out)
{
    out.clear();m_BFGMovementReason="invalid_context";auto*bg=m_Player->GetBattleground();
    if(!m_BGAI||m_Player->GetMapId()!=761||!bg||
        (bg->GetStatus()!=STATUS_WAIT_JOIN&&bg->GetStatus()!=STATUS_IN_PROGRESS)||
        !m_Player->isAlive()||!m_Player->IsInWorld()||m_Player->IsBeingTeleported()||
        m_Player->GetVehicleBase()||m_Player->GetTransport()||!m_Player->GetMap())return false;
    auto pointIsFinite=[](G3D::Vector3 const&v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    if(!pointIsFinite(start)||!pointIsFinite(goal)){m_BFGMovementReason="invalid_coordinates";return false;}
    using FelsoriaGilneasGround::Sample;
    auto floorProbe=[&](Sample const&v,float&floor){
        auto*map=m_Player->GetMap();
        float lower=map->GetHeight(m_Player->GetPhases(),v.x,v.y,v.z-1.5f,true,8.0f);
        float upper=map->GetHeight(m_Player->GetPhases(),v.x,v.y,v.z+.25f,true,8.0f);
        bool a=std::isfinite(lower)&&lower>-99999.0f&&std::abs(lower-v.z)<=2.2f;
        bool b=std::isfinite(upper)&&upper>-99999.0f&&std::abs(upper-v.z)<=2.2f;
        if(!a&&!b)return false;
        floor=a&&(!b||std::abs(lower-v.z)<=std::abs(upper-v.z))?lower:upper;return true;
    };
    PathParameter param(m_Player->GetSession()->GetAccountId(),m_Player);
    param.posX=start.x;param.posY=start.y;param.posZ=start.z;
    param.targetPosX=goal.x;param.targetPosY=goal.y;param.targetPosZ=goal.z;
    Pathfinding path(&param,nullptr,nullptr);path.SetUseStraightPath(false);path.SetPathLengthLimit(360.0f);
    bool calculated=path.CalculatePath(goal.x,goal.y,goal.z,false,false);
    PathType type=path.GetPathType();m_BFGMovementReason="native_path_type";
    bool complete=calculated&&(type&PATHFIND_NORMAL)&&
        !(type&(PATHFIND_NOPATH|PATHFIND_INCOMPLETE|PATHFIND_NOT_USING_PATH|PATHFIND_SHORTCUT|PATHFIND_SHORT));
    if(complete){
        auto const&raw=path.GetPath();m_BFGMovementReason="native_endpoints";
        bool valid=raw.size()>=2&&raw.size()<=4096&&(raw.front()-start).squaredLength()<=6.25f&&
            (raw.back()-goal).squaredLength()<=6.25f;
        if(valid)for(auto const&v:raw){
            float floor=0;
            if(!pointIsFinite(v)||!floorProbe({v.x,v.y,v.z},floor)){valid=false;m_BFGMovementReason="native_floor";break;}
        }
        if(valid){out=raw;out.front()=start;out.back()=goal;m_BFGMovementReason="native_clear";return true;}
    }
    // A missing/incomplete mesh must not prohibit a 2-yard waiting move or
    // a 1-yard recorded-trail join. Require sampled floor AND two collision
    // rays throughout the entire short local connector instead.
    auto corridor=[&](Sample const&a,Sample const&b){
        auto*map=m_Player->GetMap();
        return map->isInLineOfSight(a.x,a.y,a.z+.6f,b.x,b.y,b.z+.6f,m_Player->GetPhases())&&
            map->isInLineOfSight(a.x,a.y,a.z+1.8f,b.x,b.y,b.z+1.8f,m_Player->GetPhases());
    };
    std::vector<Sample>local;char const*localReason="none";
    if(!FelsoriaGilneasGround::BuildLocal({start.x,start.y,start.z},{goal.x,goal.y,goal.z},floorProbe,corridor,local,localReason)){
        m_BFGMovementReason=localReason;return false;
    }
    for(auto const&v:local)out.emplace_back(v.x,v.y,v.z);
    m_BFGMovementReason="local_clear";return true;
}

bool BotBGAIMovement::IsBFGJumpInProgress()const
{
    bool active=(m_BFGWaterActive&&m_Player&&m_Player->GetInstanceId()==m_BFGWaterInstance&&uint32(getMSTime()-m_BFGWaterStarted)<18000)||
        (m_BFGRecoveryHop&&m_Player&&m_Player->GetInstanceId()==m_BFGRecoveryInstance&&uint32(getMSTime()-m_BFGRecoveryHopAt)<3500)||
        (m_BFGStartSettling&&m_Player&&m_Player->GetInstanceId()==m_BFGStartSettleInstance&&uint32(getMSTime()-m_BFGStartSettleAt)<2500);
    return active&&m_Player&&m_Player->isAlive()&&m_Player->IsInWorld()&&
        m_Player->GetMapId()==761&&
        m_Player->GetBattleground()&&m_Player->GetBattleground()->GetStatus()==STATUS_IN_PROGRESS&&
        !m_Player->IsBeingTeleported();
}

bool BotBGAIMovement::ContinueBFGWaterCrossing()
{
    if(!m_BFGWaterActive)return false;
    uint32 now=getMSTime();
    auto reject=[&](char const*reason){m_BFGWaterActive=false;m_BFGHopActive=false;m_ABSpline=false;m_BFGMovementReason=reason;return false;};
    if(!IsBFGJumpInProgress())return reject("water_reject_context_or_timeout");
    if(m_BFGHopActive){
        if(m_Player->movespline&&!m_Player->movespline->Finalized()){
            if(now-m_BFGHopStarted>3000)return reject("water_reject_hop_timeout");
            m_BFGMovementReason="water_jump_in_flight";return true;
        }
        m_BFGHopActive=false;
    }
    if(m_Player->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||
       m_Player->HasAuraType(SPELL_AURA_MOD_ROOT)){
        m_BFGMovementReason="water_wait_cc";return true;
    }
    if(m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR)){
        m_BFGMovementReason="water_wait_landing";return true;
    }
    if(m_Player->GetDistance(m_BFGWaterGoal)<.9f){
        m_BFGWaterActive=false;m_ABPathCursor=m_BFGWaterLanding;
        m_ABProgress=m_Player->GetPosition();m_ABProgressAt=now;
        m_BFGMovementReason="water_complete";return false;
    }
    using FelsoriaGilneas::Point;
    auto current=m_Player->GetVector3();Point destination;
    float speed=std::min(7.0f,m_Player->GetSpeed(MOVE_RUN)),duration=.8f;
    if(speed<=.1f){m_BFGMovementReason="water_wait_speed";return true;}
    if(!FelsoriaGilneasWater::NextHop({current.x,current.y,current.z},m_BFGWaterReverse,std::min(4.6f,speed*duration),destination))
        return reject("water_reject_corridor");
    float floor=0,surface=m_Player->GetWaterOrGroundLevel(destination.x,destination.y,destination.z+1.0f,&floor,true);
    if(!std::isfinite(surface)||surface<=-99999.0f||std::abs(surface-destination.z)>2.0f)
        return reject("water_reject_surface");
    destination.z=surface+.02f;
    float dx=destination.x-current.x,dy=destination.y-current.y,dz=destination.z-current.z;
    float horizontal=std::sqrt(dx*dx+dy*dy);
    if(horizontal<.15f||horizontal>speed*duration+.15f||std::abs(dz)>2.5f)return reject("water_reject_geometry");
    float amplitude=Movement::gravity*duration*duration/8.0f;
    G3D::Vector3 previous=current;
    for(unsigned sample=1;sample<=24;++sample){
        float u=float(sample)/24.0f;
        G3D::Vector3 next(current.x+dx*u,current.y+dy*u,current.z+dz*u+4.0f*amplitude*u*(1.0f-u));
        for(float height:{.15f,1.6f})if(!m_Player->GetMap()->isInLineOfSight(previous.x,previous.y,previous.z+height,
            next.x,next.y,next.z+height,m_Player->GetPhases()))return reject("water_reject_arc");
        previous=next;
    }
    m_Player->StopMoving();m_Player->GetMotionMaster()->Clear();
    m_Player->GetMotionMaster()->MoveJump(destination.x,destination.y,destination.z,
        std::sqrt(horizontal*horizontal+dz*dz)/duration,Movement::gravity*duration/2.0f);
    m_BFGHopActive=true;m_BFGHopStarted=now;m_BFGMovementReason="water_jump_launched";
    return true;
}

void BotBGAIMovement::ResetBFGAfterResurrection()
{
    m_BFGWaterActive=false;m_BFGHopActive=false;m_BFGRecoveryHop=false;m_BFGStartSettling=false;
    m_BFGStartSettleAt=0;
    m_BFGRecoveryHopAt=0;m_ABPathCursor=0;m_ABRecorded=false;m_ABAttemptAt=0;
    ClearMovement();
    m_BFGMovementReason="revived_reset";
}

FelsoriaGilneasRecovery::MoveResult BotBGAIMovement::SettleBFGStartRecoveryFloor()
{
    using FelsoriaGilneasRecovery::MoveResult;
    auto reject=[&](char const*reason){m_BFGStartSettling=false;m_BFGMovementReason=reason;return MoveResult::Rejected;};
    if(!m_BGAI||!m_Player||!m_Player->IsPlayerBot()||m_Player->GetMapId()!=761||
        !m_Player->isAlive()||!m_Player->IsInWorld()||m_Player->IsBeingTeleported()||
        m_Player->GetTransport()||m_Player->GetVehicleBase()||!m_Player->GetMap())return reject("start_floor_context");
    auto*bg=m_Player->GetBattleground();
    if(!bg||bg->GetStatus()!=STATUS_IN_PROGRESS)return reject("start_floor_context");
    uint32 now=getMSTime();
    if(m_BFGStartSettling){
        if(m_Player->GetInstanceId()!=m_BFGStartSettleInstance)return reject("start_floor_instance_changed");
        if(m_Player->movespline&&!m_Player->movespline->Finalized()){
            if(uint32(now-m_BFGStartSettleAt)>=2500)return reject("start_floor_fall_timeout");
            m_BFGMovementReason="start_floor_falling";return MoveResult::Running;
        }
        m_BFGStartSettling=false;
        float dx=m_Player->GetPositionX()-m_BFGStartSettleLanding.GetPositionX();
        float dy=m_Player->GetPositionY()-m_BFGStartSettleLanding.GetPositionY();
        float dz=m_Player->GetPositionZ()-m_BFGStartSettleLanding.GetPositionZ();
        if(dx*dx+dy*dy+dz*dz>.16f)return reject("start_floor_not_landed");
        m_BFGMovementReason="start_floor_settled";return MoveResult::Finished;
    }
    float x=m_Player->GetPositionX(),y=m_Player->GetPositionY(),z=m_Player->GetPositionZ(),floor=0;
    unsigned grave=FelsoriaGilneasRecovery::Graveyard({x,y,z});
    if(grave<3||grave>=5)return MoveResult::Finished;
    if(!ProbeBFGRecoveryFloor(x,y,z,floor))return reject("start_floor_missing");
    float drop=z-floor;
    if(drop<-.15f||drop>2.25f)return reject("start_floor_offset_rejected");
    if(drop<=.35f)return MoveResult::Finished;
    // Player graveyard teleporting intentionally adds two yards. Bots have no
    // client to settle that offset. Use the server fall spline, not MoveFall(),
    // whose Player branch only sets a flag and waits for client movement.
    // The vertical column and the landing floor must both be verified.
    float landingZ=floor+.03f;
    for(float height:{.08f,1.7f})if(!m_Player->GetMap()->isInLineOfSight(
        x,y,z+height,x,y,landingZ+height,m_Player->GetPhases()))return reject("start_floor_column_blocked");
    m_Player->StopMoving();m_Player->GetMotionMaster()->Clear();m_ABSpline=false;
    Movement::MoveSplineInit init(*m_Player);
    init.MoveTo(x,y,landingZ,false);init.SetFall();init.SetUncompressed();init.SetWalk(false);
    if(init.Launch()<=0)return reject("start_floor_fall_launch_failed");
    m_BFGStartSettling=true;m_BFGStartSettleAt=now;m_BFGStartSettleInstance=m_Player->GetInstanceId();
    m_BFGStartSettleLanding=Position(x,y,landingZ,0);m_BFGMovementReason="start_floor_fall_launched";
    return MoveResult::Running;
}

bool BotBGAIMovement::ProbeBFGRecoveryFloor(float x,float y,float fromZ,float&floor)const
{
    if(!m_Player||!m_Player->GetMap()||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(fromZ))return false;
    floor=m_Player->GetMap()->GetHeight(m_Player->GetPhases(),x,y,fromZ+2.5f,true,FelsoriaGilneasRecovery::MaximumDrop+6.0f);
    return std::isfinite(floor)&&floor>-99999.0f&&floor-fromZ<=2.5f&&fromZ-floor<=FelsoriaGilneasRecovery::MaximumDrop;
}

bool BotBGAIMovement::CanBFGRecoveryWalk(FelsoriaGilneas::Point const&a,FelsoriaGilneas::Point const&b)const
{
    using namespace FelsoriaGilneasRecovery;
    float distance=Flat(a,b);if(distance>12.0f)return false;
    unsigned count=std::max(1u,unsigned(std::ceil(distance/.7f)));Point previous=a;
    for(unsigned i=0;i<=count;++i){
        float u=float(i)/float(count);Point next{a.x+(b.x-a.x)*u,a.y+(b.y-a.y)*u,a.z+(b.z-a.z)*u};float floor=0;
        if(!ProbeBFGRecoveryFloor(next.x,next.y,next.z,floor)||std::abs(floor-next.z)>1.0f)return false;
        next.z=floor+.03f;
        if(i){
            if(std::abs(next.z-previous.z)>.55f*Flat(next,previous)+.2f)return false;
            for(float h:{.15f,1.7f})if(!m_Player->GetMap()->isInLineOfSight(previous.x,previous.y,previous.z+h,
                next.x,next.y,next.z+h,m_Player->GetPhases()))return false;
        }
        previous=next;
    }
    return true;
}

bool BotBGAIMovement::ProbeBFGRecoveryJump(FelsoriaGilneas::Point const&a,FelsoriaGilneas::Point const&b,
    float&duration,float&speedXY,float&speedZ)const
{
    using namespace FelsoriaGilneasRecovery;
    float horizontal=Flat(a,b),drop=a.z-b.z,floor=0;
    if(horizontal<.25f||horizontal>16.0f||drop< -1.0f||drop>MaximumDrop||
        !ProbeBFGRecoveryFloor(b.x,b.y,b.z,floor)||std::abs(floor-b.z)>.6f)return false;
    float gravity=Movement::gravity,up=7.0f,discriminant=up*up+2*gravity*drop;
    if(discriminant<=0)return false;
    float run=std::min(7.0f,m_Player->GetSpeed(MOVE_RUN));
    if(run<.1f)return false;
    duration=std::max((up+std::sqrt(discriminant))/gravity,horizontal/run);
    // The actual LH/Mines recordings span about 10.5 yards between grounded
    // samples. The old hard eight-yard cap rejected both exits. Keep normal
    // horizontal speed, bounded lift/time and the complete collision arc.
    float requiredLift=(gravity*duration*duration*.5f-drop)/duration;
    if(duration<=0||duration>2.7f||requiredLift>11.5f)return false;
    float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,amplitude=gravity*duration*duration/8;
    float sideX=-dy/horizontal*.3f,sideY=dx/horizontal*.3f;Point previous=a;
    unsigned boundary=FelsoriaGilneasExits::BoundaryJump(a,b);
    for(unsigned i=1;i<=48;++i){
        float u=float(i)/48;Point next{a.x+dx*u,a.y+dy*u,a.z+dz*u+4*amplitude*u*(1-u)};
        auto clear=[&](float ox,float oy,float h){
            if(boundary<3)return m_Player->GetMap()->isInLineOfSightForBFGExit(
                previous.x+ox,previous.y+oy,previous.z+h,next.x+ox,next.y+oy,next.z+h,m_Player->GetPhases(),boundary);
            return m_Player->GetMap()->isInLineOfSight(
                previous.x+ox,previous.y+oy,previous.z+h,next.x+ox,next.y+oy,next.z+h,m_Player->GetPhases());
        };
        if(!clear(0,0,.08f)||!clear(0,0,1.7f)||!clear(sideX,sideY,.9f)||!clear(-sideX,-sideY,.9f))return false;
        previous=next;
    }
    // The native jump spline uses its 3D velocity and a symmetric parabola.
    // These arguments reproduce the arc that was just checked against VMAP.
    speedXY=std::sqrt(horizontal*horizontal+dz*dz)/duration;speedZ=gravity*duration/2;
    return true;
}

bool BotBGAIMovement::CanBFGRecoveryJump(FelsoriaGilneas::Point const&a,FelsoriaGilneas::Point const&b)const
{
    float duration=0,xy=0,z=0;return ProbeBFGRecoveryJump(a,b,duration,xy,z);
}

FelsoriaGilneasRecovery::MoveResult BotBGAIMovement::MoveBFGEgressPath(
    std::vector<FelsoriaGilneasRecovery::Step>const&path,unsigned&cursor)
{
    using namespace FelsoriaGilneasRecovery;
    auto reject=[&](char const*reason){m_BFGRecoveryHop=false;m_BFGMovementReason=reason;return MoveResult::Rejected;};
    auto*bg=m_Player->GetBattleground();uint32 now=getMSTime();
    if(!m_BGAI||m_Player->GetMapId()!=761||!bg||bg->GetStatus()!=STATUS_IN_PROGRESS||!m_Player->isAlive()||
        !m_Player->IsInWorld()||m_Player->IsBeingTeleported()||m_Player->GetTransport()||m_Player->GetVehicleBase())return reject("egress_context");
    if(m_BFGRecoveryHop){
        if(m_Player->GetInstanceId()!=m_BFGRecoveryInstance||now-m_BFGRecoveryHopAt>3500)return reject("egress_jump_timeout");
        if(m_Player->movespline&&!m_Player->movespline->Finalized()){m_BFGMovementReason="egress_jump_in_flight";return MoveResult::Running;}
        m_BFGRecoveryHop=false;
        if(m_Player->GetDistance(m_BFGRecoveryLanding)>1.5f)return reject("egress_jump_not_landed");
        ++cursor;m_ABSpline=false;
    }
    if(m_Player->HasUnitState(UNIT_STATE_CASTING|UNIT_STATE_STUNNED|UNIT_STATE_CONFUSED|UNIT_STATE_FLEEING)||
        m_Player->HasAuraType(SPELL_AURA_MOD_ROOT)||m_Player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR)){
        m_BFGMovementReason="egress_wait_cc_or_landing";return MoveResult::Running;
    }
    auto v=m_Player->GetVector3();Point current{v.x,v.y,v.z};
    while(cursor<path.size()&&FelsoriaGilneas::Distance(current,path[cursor].point)<.85f)++cursor;
    cursor=WalkingProgress(path,cursor,current);
    if(cursor>=path.size()){m_BFGMovementReason="egress_complete";return MoveResult::Finished;}
    auto const&step=path[cursor];
    if(step.jump){
        float duration=0,xy=0,z=0;if(!ProbeBFGRecoveryJump(current,step.point,duration,xy,z))return reject("egress_jump_blocked");
        m_Player->StopMoving();m_Player->GetMotionMaster()->Clear();m_ABSpline=false;
        m_Player->GetMotionMaster()->MoveJump(step.point.x,step.point.y,step.point.z,xy,z);
        m_BFGRecoveryHop=true;m_BFGRecoveryHopAt=now;m_BFGRecoveryInstance=m_Player->GetInstanceId();
        m_BFGRecoveryLanding=Position(step.point.x,step.point.y,step.point.z,0);
        m_BFGMovementReason=FelsoriaGilneasExits::BoundaryJump(current,step.point)<3?"egress_boundary_jump_launched":"egress_jump_launched";
        return MoveResult::Running;
    }
    unsigned begin=cursor?cursor-1:0;
    while(begin&&!path[begin].jump)--begin;
    std::vector<G3D::Vector3>ground;
    for(unsigned i=begin;i<path.size()&&(i==begin||!path[i].jump);++i){auto const&p=path[i].point;ground.emplace_back(p.x,p.y,p.z);}
    if(ground.size()<2||!MoveBFGRecordedPath(ground))return reject("egress_ground_rejected");
    m_BFGMovementReason="egress_walk";return MoveResult::Running;
}

