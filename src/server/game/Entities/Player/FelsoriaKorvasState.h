// FELSORIA_KORVAS_NATIVE_STATE_V9
#ifndef FELSORIA_KORVAS_STATE_V9_H
#define FELSORIA_KORVAS_STATE_V9_H
#include "Player.h"
namespace FelsoriaKorvasState
{
    inline bool StationaryVisible(Player const* player)
    {
        if (player->GetQuestRewardStatus(37449) || player->GetQuestRewardStatus(37656))
            return false;
        auto fel = player->GetQuestStatus(37656);
        if (fel == QUEST_STATUS_INCOMPLETE || fel == QUEST_STATUS_COMPLETE)
            return false;
        for (uint32 id : {36920u, 40815u, 44140u})
            if (player->GetQuestStatus(id) == QUEST_STATUS_COMPLETE || player->GetQuestRewardStatus(id))
                return true;
        return false;
    }
}
#endif
