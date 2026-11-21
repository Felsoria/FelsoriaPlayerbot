/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2006-2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* ScriptData
SDName: Stormwind_City
SD%Complete: 0
SDComment:
SDCategory: Stormwind City
EndScriptData */

/* ContentData
EndContentData */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ObjectMgr.h"
#include "ScriptedGossip.h"
#include "ScriptedEscortAI.h"

class npc_102585_jace_for_dh_questline : public CreatureScript
{
public:
    npc_102585_jace_for_dh_questline() : CreatureScript("npc_102585_jace_for_dh_questline") { }
    // Felsoria: preserve local Jace 100675 gossip credit + scene
    // when the DH questline script is attached to the existing Stormwind Jace.
    bool OnGossipSelect(Player* player, Creature* /*creature*/, uint32 /*sender*/, uint32 /*action*/) override
    {
        uint32 menuId = player->PlayerTalkClass->GetGossipMenu().GetMenuId();
        player->PlayerTalkClass->ClearMenus();

        if (menuId != 19134)
            return false;

        player->KilledMonsterCredit(111585);
        player->CastSpell(player, 225500, true);
        player->PlayerTalkClass->SendCloseGossip();
        return true;
    }

    struct npc_102585_jace_for_dh_questlineAI : ScriptedAI
    {
        npc_102585_jace_for_dh_questlineAI(Creature* creature) : ScriptedAI(creature) {}

        EventMap events;
        // Felsoria Pack40: Second Sight visual reveal
        void UpdateSecondSightVisual(bool reveal)
        {
            CreatureTemplate const* demonTemplate = sObjectMgr->GetCreatureTemplate(100993);
            if (!demonTemplate)
                return;

            int32 demonDisplay = demonTemplate->GetFirstValidModelId();
            if (demonDisplay <= 0)
                return;

            std::list<Creature*> guards;
            GetCreatureListWithEntryInGrid(guards, me, 1756, 35.0f);
            GetCreatureListWithEntryInGrid(guards, me, 100449, 35.0f);
            GetCreatureListWithEntryInGrid(guards, me, 100754, 35.0f);
            GetCreatureListWithEntryInGrid(guards, me, 108376, 35.0f);
            GetCreatureListWithEntryInGrid(guards, me, 108397, 35.0f);

            guards.sort([this](Creature* left, Creature* right)
            {
                return me->GetDistance(left) < me->GetDistance(right);
            });

            uint8 revealed = 0;
            for (Creature* guard : guards)
            {
                if (!guard || !guard->isAlive())
                    continue;

                if (reveal && revealed < 6)
                {
                    guard->SetDisplayId(uint32(demonDisplay));
                    ++revealed;
                }
                else
                    guard->SetDisplayId(guard->GetNativeDisplayId());
            }
        }

        void MoveInLineOfSight(Unit* who) override
        {
            if (events.Empty())
                events.ScheduleEvent(EVENT_1, 2000);

            if (who->GetTypeId() != TYPEID_PLAYER)
                return;

            Player* player = who->ToPlayer();

            if (player->GetQuestStatus(39691) != QUEST_STATUS_INCOMPLETE)
                return;

            player->KilledMonsterCredit(102585);
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);

            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                case EVENT_1:
                    events.ScheduleEvent(EVENT_1, 2000);

                    std::list<Player*> list;
                    me->GetPlayerListInGrid(list, 20.0f);

                    bool revealSecondSight = false;
                    for (Player* player : list)
                    {
                        QuestStatus status = player->GetQuestStatus(44471);
                        if (!player->HasAura(188501))
                            continue;

                        if (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE)
                            revealSecondSight = true;

                        if (status == QUEST_STATUS_INCOMPLETE)
                            player->KilledMonsterCredit(102563);
                    }

                    UpdateSecondSightVisual(revealSecondSight);

                    break;
                }
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_102585_jace_for_dh_questlineAI(creature);
    }
};

void AddSC_stormwind_city()
{
    new npc_102585_jace_for_dh_questline();
}
