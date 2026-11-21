#include "BotWorldTravel.h"
#include "BotBGAIMovement.h"
#include "PlayerBotMgr.h"
#include "PlayerBotSession.h"
#include "ObjectMgr.h"
#include "Creature.h"
#include "Player.h"
#include "Map.h"
#include "DB2Stores.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <utility>

namespace
{
    enum Kind : uint8 { HUB = 1, HUNT = 2, SCENIC = 4 };
    struct Node
    {
        struct Point { float x, y, z; };
        float x, y, z;
        uint32 phase, area, faction;
        std::set<uint32> phaseIds;
        uint8 kind;
        uint16 count;
        uint8 minLevel, maxLevel;
        bool quest;
        std::vector<Point> local;
    };
    struct Graph
    {
        uint64 serial = 0;
        std::vector<Node> nodes;
        std::vector<std::vector<std::pair<uint16, float>>> links;
    };
    struct Context
    {
        uint32 map, zone, instance, phase;
        std::set<uint32> phaseIds;
        uint32 faction;
        uint8 playerClass, team, level;
        bool operator<(Context const& rhs) const
        {
            return std::tie(map, zone, instance, phase, phaseIds, faction, playerClass, team, level) <
                std::tie(rhs.map, rhs.zone, rhs.instance, rhs.phase, rhs.phaseIds,
                    rhs.faction, rhs.playerClass, rhs.team, rhs.level);
        }
    };
    struct RouteKey
    {
        Context context;
        uint64 serial;
        uint32 from, to;
        bool operator<(RouteKey const& rhs) const
        {
            return std::tie(context, serial, from, to) <
                std::tie(rhs.context, rhs.serial, rhs.from, rhs.to);
        }
    };

    std::mutex graphLock;
    std::map<std::pair<uint32, uint32>, std::shared_ptr<Graph const>> graphs;
    std::map<RouteKey, std::vector<uint32>> routeCache;
    uint64 nextGraphSerial = 0;

    float Distance(Node const& a, Node const& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    std::shared_ptr<Graph const> GetGraph(uint32 map, uint32 zone)
    {
        std::lock_guard<std::mutex> guard(graphLock);
        std::pair<uint32, uint32> key(map, zone);
        auto found = graphs.find(key);
        if (found != graphs.end())
            return found->second;

        std::shared_ptr<Graph> graph(new Graph);
        graph->serial = ++nextGraphSerial;
        uint32 candidateCount = 0;
        // ObjectMgr's loaded world spawns are stable after world startup.
        // Cluster by authored position, phase, kind and surface height; never
        // average a village and a cliff into an unwalkable destination.
        for (auto const& entry : *sObjectMgr->GetCreatures())
        {
            CreatureData const& data = entry.second;
            // Some world rows store the sub-area but have a missing/stale zoneId.
            // Use the same bounded area-parent lookup as dynamic placement.
            bool inZone = data.zoneId == zone;
            uint32 area = data.areaId;
            for (uint8 depth = 0; !inZone && area && depth < 8; ++depth)
            {
                inZone = area == zone;
                AreaTableEntry const* entry = sAreaTableStore.LookupEntry(area);
                if (!entry || entry->ParentAreaID == area) break;
                area = entry->ParentAreaID;
            }
            if (data.mapid != map || !inZone || !data.phaseMask ||
                !std::isfinite(data.posX) || !std::isfinite(data.posY) ||
                !std::isfinite(data.posZ))
                continue;
            CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(data.id);
            if (!creature || !(creature->InhabitType & INHABIT_GROUND) || creature->VehicleId)
                continue;
            uint32 const flags = data.npcflag | creature->npcflag;
            uint8 kind = 0;
            if (flags & (UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_VENDOR |
                UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_REPAIR | UNIT_NPC_FLAG_FLIGHTMASTER))
                kind = HUB;
            else if (creature->maxlevel && creature->minlevel &&
                !(flags & (UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_TRAINER)) &&
                !(creature->unit_flags & UNIT_FLAG_NON_ATTACKABLE))
                kind = HUNT;
            else if (data.movementType == 2)
                kind = SCENIC;
            if (!kind)
                continue;

            bool merged = false;
            for (Node& node : graph->nodes)
            {
                if (node.kind == kind && node.phase == data.phaseMask &&
                    node.phaseIds == data.PhaseID && node.faction == creature->faction &&
                    node.area == data.areaId && std::abs(node.z - data.posZ) < 6.0f &&
                    std::hypot(node.x - data.posX, node.y - data.posY) < 45.0f)
                {
                    if (node.count < UINT16_MAX) ++node.count;
                    node.minLevel = std::min<uint8>(node.minLevel,
                        creature->ScaleLevelMin ? creature->ScaleLevelMin : creature->minlevel);
                    node.maxLevel = std::max<uint8>(node.maxLevel,
                        creature->ScaleLevelMax ? creature->ScaleLevelMax : creature->maxlevel);
                    node.quest = node.quest || (flags & UNIT_NPC_FLAG_QUESTGIVER);
                    bool distinct = true;
                    for (Node::Point const& point : node.local)
                        if (std::hypot(point.x - data.posX, point.y - data.posY) < 7.0f)
                            { distinct = false; break; }
                    if (distinct && node.local.size() < 5)
                        node.local.push_back({data.posX, data.posY, data.posZ});
                    merged = true;
                    break;
                }
            }
            if (!merged)
            {
                // Reservoir selection keeps distant portions of a large zone
                // represented instead of truncating the unordered spawn map.
                ++candidateCount;
                uint32 slot = graph->nodes.size() < 256 ? uint32(graph->nodes.size()) :
                    urand(0, candidateCount - 1);
                if (slot >= 256) continue;
                Node candidate{data.posX, data.posY, data.posZ, data.phaseMask,
                    data.areaId, creature->faction, data.PhaseID, kind, 1,
                    creature->ScaleLevelMin ? creature->ScaleLevelMin : creature->minlevel,
                    creature->ScaleLevelMax ? creature->ScaleLevelMax : creature->maxlevel,
                    bool(flags & UNIT_NPC_FLAG_QUESTGIVER)};
                candidate.local.push_back({data.posX, data.posY, data.posZ});
                if (slot == graph->nodes.size()) graph->nodes.push_back(std::move(candidate));
                else graph->nodes[slot] = std::move(candidate);
            }
        }

        graph->links.resize(graph->nodes.size());
        for (uint16 i = 0; i < graph->nodes.size(); ++i)
        {
            std::vector<std::pair<float, uint16>> nearbyNodes;
            for (uint16 j = 0; j < graph->nodes.size(); ++j)
            {
                if (i == j || !(graph->nodes[i].phase & graph->nodes[j].phase) ||
                    std::abs(graph->nodes[i].z - graph->nodes[j].z) > 32.0f)
                    continue;
                float d = Distance(graph->nodes[i], graph->nodes[j]);
                if (d < 220.0f)
                    nearbyNodes.emplace_back(d, j);
            }
            std::sort(nearbyNodes.begin(), nearbyNodes.end());
            for (size_t n = 0; n < nearbyNodes.size() && n < 8; ++n)
                graph->links[i].emplace_back(nearbyNodes[n].second, nearbyNodes[n].first);
        }
        if (graphs.size() >= 32)
        {
            graphs.erase(graphs.begin());
            routeCache.clear();
        }
        graphs[key] = graph;
        return graph;
    }

    bool Eligible(Player const* player, Node const& node)
    {
        if (!(node.phase & player->GetPhaseMask()))
            return false;
        if (!node.phaseIds.empty())
        {
            bool overlap = false;
            for (uint32 phase : node.phaseIds)
                if (player->GetPhases().count(phase)) { overlap = true; break; }
            if (!overlap) return false;
        }
        FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(node.faction);
        FactionTemplateEntry const* mine = sFactionTemplateStore.LookupEntry(player->getFaction());
        if (faction && mine &&
            ((node.kind == HUB && faction->IsHostileTo(*mine)) ||
             (node.kind == HUNT && faction->IsFriendlyTo(*mine)))) return false;
        uint8 required = PlayerBotMgr::GetDynamicAreaRequiredClass(player->GetMapId(),
            player->GetZoneId(), node.area);
        if ((required && required != player->getClass()) ||
            !PlayerBotMgr::IsDynamicRaceAllowedAt(player->GetMapId(), player->GetZoneId(), node.area,
                player->getRace(), uint8(player->GetTeamId())))
            return false;
        if (node.kind == HUNT && (node.minLevel > player->getLevel() + 3 ||
            node.maxLevel + 8 < player->getLevel()))
            return false;
        return true;
    }

    bool ValidGround(Player const* player, Node const& node)
    {
        float z = player->GetMap()->GetHeight(player->GetPhases(), node.x, node.y, node.z + 2.0f);
        if (!std::isfinite(z) || z <= INVALID_HEIGHT || std::abs(z - node.z) >= 2.0f ||
            player->GetMap()->IsInWater(node.x, node.y, z) ||
            player->GetMap()->GetZoneId(node.x, node.y, z) != player->GetZoneId())
            return false;
        uint8 required = PlayerBotMgr::GetDynamicAreaRequiredClass(player->GetMapId(),
            player->GetZoneId(), player->GetMap()->GetAreaId(node.x, node.y, z));
        uint32 const area = player->GetMap()->GetAreaId(node.x, node.y, z);
        return (!required || required == player->getClass()) &&
            PlayerBotMgr::IsDynamicRaceAllowedAt(player->GetMapId(), player->GetZoneId(), area,
                player->getRace(), uint8(player->GetTeamId()));
    }

    std::vector<uint32> Route(Graph const& graph, Player const* player,
        uint32 from, uint32 to, uint32 blockedFrom, uint32 blockedTo)
    {
        if (from >= graph.nodes.size() || to >= graph.nodes.size()) return {};
        Context context{player->GetMapId(), player->GetZoneId(), player->GetInstanceId(),
            player->GetPhaseMask(), player->GetPhases(), player->getFaction(), player->getClass(),
            uint8(player->GetTeamId()), uint8(player->getLevel())};
        RouteKey key{context, graph.serial, from, to};
        if (blockedFrom == blockedTo)
        {
            std::lock_guard<std::mutex> guard(graphLock);
            auto hit = routeCache.find(key);
            if (hit != routeCache.end()) return hit->second;
        }
        size_t const size = graph.nodes.size();
        std::vector<float> costs(size, std::numeric_limits<float>::infinity());
        std::vector<int> previous(size, -1);
        std::vector<bool> seen(size, false);
        costs[from] = 0;
        for (size_t step = 0; step < size; ++step)
        {
            uint32 current = uint32(size);
            for (uint32 n = 0; n < size; ++n)
                if (!seen[n] && (current == size || costs[n] < costs[current])) current = n;
            if (current == size || !std::isfinite(costs[current])) break;
            if (current == to) break;
            seen[current] = true;
            for (auto const& link : graph.links[current])
            {
                uint32 next = link.first;
                if ((current == blockedFrom && next == blockedTo) ||
                    !Eligible(player, graph.nodes[next]) || seen[next]) continue;
                float cost = costs[current] + link.second;
                if (cost < costs[next]) { costs[next] = cost; previous[next] = int(current); }
            }
        }
        std::vector<uint32> path;
        if (std::isfinite(costs[to]))
        {
            for (int n = int(to); n >= 0; n = previous[n])
                path.push_back(uint32(n));
            std::reverse(path.begin(), path.end());
        }
        if (blockedFrom == blockedTo)
        {
            std::lock_guard<std::mutex> guard(graphLock);
            if (routeCache.size() >= 1024) routeCache.erase(routeCache.begin());
            routeCache[key] = path;
        }
        return path;
    }
}

BotWorldTravel::BotWorldTravel(Player* owner) : _owner(owner) { }

void BotWorldTravel::Reset()
{
    _active = false;
    _nextDecision = 0;
    _target = Target();
    _recovery = 0;
    _blockedFrom = _blockedTo = _blockedUntil = 0;
    _graphIdentity.reset();
    _phaseIds.clear();
}

void BotWorldTravel::Observe(bool interrupted)
{
    if (!_active) return;
    if (!_owner->IsInWorld() || _owner->GetMapId() != _map ||
        _owner->GetZoneId() != _zone || _owner->GetInstanceId() != _instance ||
        _owner->GetPhaseMask() != _phase || _owner->GetPhases() != _phaseIds)
    {
        _clearOldMovement = true;
        Reset();
        return;
    }
    if (_owner->GetGroup())
    {
        Reset();
        return;
    }
    if (interrupted && !_target.paused)
    {
        _target.paused = true;
        _target.pauseAt = getMSTime();
    }
}

bool BotWorldTravel::Update(BotBGAIMovement* movement)
{
    if (!movement || !_owner->IsInWorld() || !_owner->GetMap() ||
        !_owner->GetSession() || _owner->GetGroup() || _owner->InBattleground() ||
        _owner->InArena() || _owner->isInCombat() || !_owner->isAlive() ||
        _owner->isUsingLfg() || _owner->GetSession()->HasBGSchedule() ||
        _owner->IsFlying() || _owner->isInFlight() || _owner->IsBeingTeleported() ||
        _owner->GetTransport())
        return false;
    PlayerBotSession* session = dynamic_cast<PlayerBotSession*>(_owner->GetSession());
    if (!session || !session->DynamicPlacementComplete() ||
        !sPlayerBotMgr->GetDynamicBotTargetLevel(session->GetAccountId()) ||
        (_owner->GetMap()->IsDungeon() &&
            PlayerBotMgr::GetDynamicAreaRequiredClass(_owner->GetMapId(),
                _owner->GetZoneId(), _owner->GetAreaId()) != _owner->getClass()))
        return false;

    uint32 now = getMSTime();
    if (_clearOldMovement)
    {
        movement->ClearMovement();
        _clearOldMovement = false;
    }
    if (!_active || _map != _owner->GetMapId() || _zone != _owner->GetZoneId() ||
        _instance != _owner->GetInstanceId() || _phase != _owner->GetPhaseMask() ||
        _phaseIds != _owner->GetPhases())
    {
        Reset();
        _active = true;
        _map = _owner->GetMapId(); _zone = _owner->GetZoneId();
        _instance = _owner->GetInstanceId(); _phase = _owner->GetPhaseMask();
        _phaseIds = _owner->GetPhases();
    }
    if (_target.paused)
    {
        if (_target.started) _target.started += now - _target.pauseAt;
        if (_target.until) _target.until += now - _target.pauseAt;
        _target.paused = false;
        _target.lastProgress = now;
        _nextDecision = 0; // Resume the retained route in this update.
    }
    // A retry timer without a destination must yield to the local walker.
    if (_nextDecision && int32(now - _nextDecision) < 0)
        return !_target.route.empty();
    _nextDecision = now + 2000;
    std::shared_ptr<Graph const> graph = _graphIdentity ?
        std::static_pointer_cast<Graph const>(_graphIdentity) : GetGraph(_map, _zone);
    _graphIdentity = graph;
    if (graph->nodes.empty())
    {
        _nextDecision = now + 5000;
        return false;
    }

    auto nearest = [&]() -> uint32
    {
        uint32 result = uint32(graph->nodes.size());
        std::vector<std::pair<float, uint32>> candidates;
        for (uint32 n = 0; n < graph->nodes.size(); ++n)
        {
            Node const& node = graph->nodes[n];
            float d = std::hypot(_owner->GetPositionX() - node.x,
                _owner->GetPositionY() - node.y);
            if (std::abs(_owner->GetPositionZ() - node.z) < 35.0f &&
                Eligible(_owner, node)) candidates.emplace_back(d, n);
        }
        std::sort(candidates.begin(), candidates.end());
        for (size_t n = 0; n < candidates.size() && n < 6; ++n)
            if (ValidGround(_owner, graph->nodes[candidates[n].second]))
                return candidates[n].second;
        return result;
    };

    bool const arrived = !_target.route.empty() && _target.progress >= _target.route.size();
    if (_target.route.empty() || (arrived && _target.until &&
        int32(now - _target.until) >= 0) ||
        (_target.started && now - _target.started > 600000))
    {
        uint32 start = nearest();
        if (start >= graph->nodes.size())
        {
            _target = Target();
            _nextDecision = now + 5000;
            return false;
        }
        _target = Target();
        // Prefer hubs and relevant creatures, but vary intent over time.
        uint8 wanted = uint8(urand(0, 3));
        _target.activity = wanted == 0 ? TOWN : wanted == 1 ? QUESTING_AREA :
            wanted == 2 ? GRIND : EXPLORE;
        std::vector<std::pair<float, uint32>> candidates;
        for (uint32 n = 0; n < graph->nodes.size(); ++n)
        {
            Node const& node = graph->nodes[n];
            if (n == start || !Eligible(_owner, node))
                continue;
            bool preferred = _target.activity == EXPLORE ||
                (_target.activity == TOWN && node.kind == HUB) ||
                (_target.activity == QUESTING_AREA && node.kind == HUB && node.quest) ||
                (_target.activity == GRIND && node.kind == HUNT);
            float distance = std::hypot(_owner->GetPositionX() - node.x,
                _owner->GetPositionY() - node.y);
            if (distance < 25.0f || distance > 800.0f) continue;
            float score = (preferred ? 2.0f : 0.0f) + frand(0.0f, 1.0f) + std::min(distance, 400.0f) / 800.0f;
            candidates.emplace_back(score, n);
        }
        std::sort(candidates.begin(), candidates.end(), std::greater<std::pair<float, uint32>>());
        uint32 goal = uint32(graph->nodes.size());
        bool blocked = _blockedUntil && int32(now - _blockedUntil) < 0;
        if (!blocked) _blockedFrom = _blockedTo = _blockedUntil = 0;
        for (size_t n = 0; n < candidates.size() && n < 8; ++n)
        {
            uint32 candidate = candidates[n].second;
            if (!ValidGround(_owner, graph->nodes[candidate])) continue;
            std::vector<uint32> path = Route(*graph, _owner, start, candidate,
                blocked ? _blockedFrom : start, blocked ? _blockedTo : start);
            if (path.size() < 2) continue;
            // Graph edges express intent, not reachability. Reject an unsafe
            // first leg before claiming movement ownership.
            float firstDistance = std::hypot(_owner->GetPositionX() - graph->nodes[start].x,
                _owner->GetPositionY() - graph->nodes[start].y);
            uint32 first = path[firstDistance > 8.0f ? 0 : 1];
            Node const& leg = graph->nodes[first];
            if (!movement->CanDynamicOutdoorMovementTo(leg.x, leg.y, leg.z)) continue;
            goal = candidate;
            _target.route.swap(path);
            break;
        }
        if (goal >= graph->nodes.size())
        {
            // An isolated/single-node town is still useful for local activity.
            // Otherwise the existing safe local walker takes over immediately.
            if (graph->nodes[start].local.size() > 1 &&
                std::hypot(_owner->GetPositionX() - graph->nodes[start].x,
                    _owner->GetPositionY() - graph->nodes[start].y) < 8.0f)
            {
                goal = start;
                _target.route.push_back(start);
            }
            else
            {
                _target = Target();
                _nextDecision = now + 5000;
                return false;
            }
        }
        movement->ClearMovement();
        _target.destination = goal;
        Node const& destination = graph->nodes[goal];
        _target.activity = destination.kind == HUB ?
            (destination.quest ? QUESTING_AREA : TOWN) :
            destination.kind == HUNT ? GRIND : EXPLORE;
        float firstDistance = std::hypot(_owner->GetPositionX() - graph->nodes[start].x,
            _owner->GetPositionY() - graph->nodes[start].y);
        _target.progress = firstDistance > 8.0f ? 0 : 1;
        _target.started = _target.lastProgress = now;
        _target.until = 0;
        _target.closest = std::numeric_limits<float>::infinity();
        _target.paused = false;
        _recovery = 0;
    }

    if (_target.progress >= _target.route.size())
    {
        // Local activity persists at the destination while existing combat AI
        // selects nearby creatures. A later decision chooses a new destination.
        if (!_target.until) _target.until = now +
            (_target.activity == TOWN ? urand(15000, 35000) : urand(20000, 45000));
        Node const& area = graph->nodes[_target.destination];
        if (area.local.size() < 2)
        {
            // Do not call a minute of standing at one spawn an activity.
            _target = Target();
            _nextDecision = now + 5000;
            return false;
        }
        if (area.local.size() > 1 && (!_target.localTick ||
            int32(now - _target.localTick) >= 0))
        {
            _target.localPoint = uint8((_target.localPoint + 1) % area.local.size());
            Node::Point const& point = area.local[_target.localPoint];
            float ground = _owner->GetMap()->GetHeight(_owner->GetPhases(),
                point.x, point.y, point.z + 2.0f);
            uint8 requiredClass = std::isfinite(ground) && ground > INVALID_HEIGHT ?
                PlayerBotMgr::GetDynamicAreaRequiredClass(_map, _zone,
                    _owner->GetMap()->GetAreaId(point.x, point.y, ground)) : 0;
            if (std::isfinite(ground) && ground > INVALID_HEIGHT &&
                std::abs(ground - point.z) < 2.0f &&
                !_owner->GetMap()->IsInWater(point.x, point.y, ground) &&
                _owner->GetMap()->GetZoneId(point.x, point.y, ground) == _zone &&
                (!requiredClass || requiredClass == _owner->getClass()) &&
                PlayerBotMgr::IsDynamicRaceAllowedAt(_map, _zone,
                    _owner->GetMap()->GetAreaId(point.x, point.y, ground),
                    _owner->getRace(), uint8(_owner->GetTeamId())) &&
                std::hypot(_owner->GetPositionX() - point.x,
                    _owner->GetPositionY() - point.y) < 75.0f &&
                movement->CanDynamicOutdoorMovementTo(point.x, point.y, point.z))
            {
                movement->MovementTo(point.x, point.y, point.z);
                _target.localTick = now + urand(3000, 7000);
            }
            else
            {
                _target = Target();
                _nextDecision = now + 5000;
                return false;
            }
        }
        return true;
    }
    Node const& node = graph->nodes[_target.route[_target.progress]];
    if (!Eligible(_owner, node) || !ValidGround(_owner, node))
    {
        _target = Target();
        _nextDecision = now + 5000;
        movement->ClearMovement();
        return false;
    }
    float d = std::hypot(_owner->GetPositionX() - node.x, _owner->GetPositionY() - node.y);
    if (d < 8.0f && std::abs(_owner->GetPositionZ() - node.z) < 4.0f)
    {
        ++_target.progress;
        _target.lastProgress = now;
        _target.closest = std::numeric_limits<float>::infinity();
        _recovery = 0;
        return true;
    }
    // Measure actual travel rather than movement-command issuance. Combat can
    // pause the route indefinitely without triggering recovery.
    if (!std::isfinite(_target.closest))
        _target.closest = d;
    else if (d + 3.0f < _target.closest)
    {
        _target.closest = d;
        _target.lastProgress = now;
        _recovery = 0;
    }
    if (now - _target.lastProgress > 8000)
    {
        _target.lastProgress = now;
        if (++_recovery == 1)
            movement->ClearMovement(); // Retry current navmesh section once.
        else
        {
            uint32 previous = _target.route[_target.progress ? _target.progress - 1 : 0];
            _blockedFrom = previous; _blockedTo = _target.route[_target.progress];
            _blockedUntil = now + 120000;
            uint32 start = nearest();
            if (start >= graph->nodes.size()) start = previous;
            _target.route = Route(*graph, _owner, start, _target.destination,
                _blockedFrom, _blockedTo);
            _target.progress = std::hypot(_owner->GetPositionX() - graph->nodes[start].x,
                _owner->GetPositionY() - graph->nodes[start].y) > 8.0f ? 0 : 1;
            _target.closest = std::numeric_limits<float>::infinity();
            if (_target.route.empty() || _recovery >= 4)
            {
                _target.route.clear();
                _target.until = now;
                _nextDecision = now + 5000;
                movement->ClearMovement();
                return false;
            }
        }
    }
    if (!_target.route.empty() && _target.progress < _target.route.size())
    {
        Node const& next = graph->nodes[_target.route[_target.progress]];
        movement->MovementTo(next.x, next.y, next.z);
    }
    return true;
}
