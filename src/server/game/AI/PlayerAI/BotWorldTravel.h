#ifndef BOT_WORLD_TRAVEL_H
#define BOT_WORLD_TRAVEL_H

#include "Common.h"
#include <cstddef>
#include <memory>
#include <set>
#include <vector>

class Player;
class BotBGAIMovement;

// Per-field-bot intent. The shared, immutable zone graph and routes are owned
// by BotWorldTravel.cpp; this object never holds a Creature or Map pointer.
class BotWorldTravel
{
public:
    enum Activity : uint8 { TRAVEL, QUESTING_AREA, GRIND, TOWN, EXPLORE };

    explicit BotWorldTravel(Player* owner);
    void Observe(bool interrupted);
    bool Update(BotBGAIMovement* movement);
    void Reset();
    Activity GetActivity() const
    {
        return !_target.route.empty() && _target.progress < _target.route.size() ?
            TRAVEL : _target.activity;
    }

private:
    struct Target
    {
        Activity activity = EXPLORE;
        uint32 destination = 0;
        std::vector<uint32> route;
        size_t progress = 0;
        uint32 started = 0;
        uint32 lastProgress = 0;
        uint32 until = 0;
        float closest = 0.0f;
        uint32 localTick = 0;
        uint32 pauseAt = 0;
        uint8 localPoint = 0;
        bool paused = false;
    };

    Player* _owner;
    Target _target;
    uint32 _map = 0, _zone = 0, _instance = 0, _phase = 0;
    uint32 _nextDecision = 0;
    std::set<uint32> _phaseIds;
    std::shared_ptr<void const> _graphIdentity;
    uint8 _recovery = 0;
    uint32 _blockedFrom = 0, _blockedTo = 0, _blockedUntil = 0;
    bool _active = false;
    bool _clearOldMovement = false;
};

#endif
