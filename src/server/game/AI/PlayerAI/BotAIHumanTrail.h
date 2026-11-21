#ifndef BOT_AI_HUMAN_TRAIL_H
#define BOT_AI_HUMAN_TRAIL_H

#include "Common.h"
#include "G3D/Vector3.h"

class Player;

// Recent, observed walking positions only. No generated route coordinates.
namespace BotAIHumanTrail
{
    void Record(Player const* owner);
    bool NextWalkingPoint(Player const* bot, float targetX, float targetY,
        float targetZ, G3D::Vector3& next);
}

#endif
