#ifndef FELSORIA_STELLAGOSA_STATE_H
#define FELSORIA_STELLAGOSA_STATE_H

#include "Player.h"

// FELSORIA_STELLAGOSA_STATE_V1: existing quest objectives persist the one-shot state.
namespace FelsoriaStellagosaState
{
    constexpr uint32 QuestId = 37450;
    constexpr uint32 Drake = 90546;
    constexpr uint32 Lock = 239455;
    constexpr uint32 Key = 120359;
    constexpr uint32 Released = 105635;

    inline bool Active(Player const* player)
    {
        auto status = player->GetQuestStatus(QuestId);
        return !player->GetQuestRewardStatus(QuestId) &&
            (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE);
    }

    inline bool Freed(Player const* player)
    {
        return player->GetQuestRewardStatus(QuestId) ||
            (Active(player) && player->GetQuestObjectiveData(QuestId, Lock) >= 3 &&
             player->GetQuestObjectiveData(QuestId, Released) >= 1);
    }

    inline bool Ready(Player const* player)
    {
        return Active(player) && player->isAlive() &&
            player->GetQuestObjectiveData(QuestId, Drake) >= 1 &&
            player->GetQuestObjectiveData(QuestId, Lock) >= 3 &&
            !player->GetQuestObjectiveData(QuestId, Released) &&
            player->HasItemCount(Key, 1, false);
    }
}

#endif
