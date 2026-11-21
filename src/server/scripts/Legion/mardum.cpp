#include "ObjectAccessor.h"
#include <iterator>
#include "SpellPackets.h"
#include <algorithm>
#include <cmath>
#include "MovementPackets.h"
#include "MovementTypedefs.h"
#include "MoveSplineInitArgs.h"
#include "MoveSpline.h"
#include "PathGenerator.h"
#include "DatabaseEnv.h"
#include "TemporarySummon.h"
#include "ScriptedEscortAI.h"
#include "CreatureTextMgr.h"
#include "MapManager.h"
#include "ScriptMgr.h"
#include "GameObjectAI.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "DB2Structure.h"
#include "DB2Stores.h"
#include "SpellTargetInfo.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "CollectionMgr.h"
#include "PhaseMgr.h"

class sceneTrigger_dh_init : public SceneTriggerScript
{
public:
    sceneTrigger_dh_init() : SceneTriggerScript("sceneTrigger_dh_init")
    {}

    enum data
    {
        NPC_CONV = 705,
        SPELL_CUEILLIDANTH = 191667,
    };

    bool OnTrigger(Player* player, SpellScene const* /*trigger*/, std::string type) override
    {
        if (type == "CUEILLIDANTH")
        {
            // Felsoria - Mardum Illidan intro only once per character
            // Quest 40076 is a TRACKING quest, so it is automatically rewarded on completion.
            if (player->GetQuestRewardStatus(40076) ||
                player->GetQuestStatus(40076) == QUEST_STATUS_COMPLETE)
                return true;

            //player->CastSpell(player, CUEILLIDANTH, false);
            Conversation* conversation = new Conversation;
            if (!conversation->CreateConversation(sObjectMgr->GetGenerator<HighGuid::Conversation>()->Generate(), NPC_CONV, player, NULL, *player))
                delete conversation;
            player->CompleteQuest(40076);

            // Felsoria Pack47 - refresh phase after DH intro
            // Phase 7000 is now allowed only after Q40076 is rewarded.
            PhaseUpdateData introPhaseUpdate;
            introPhaseUpdate.AddQuestUpdate(40076);
            player->GetPhaseMgr().NotifyConditionChanged(introPhaseUpdate);
        }
        return true;
    }
};

// 244898
// FELSORIA_BANNER_FINISH_V1
class go_q40077 : public GameObjectScript
{
public:
    go_q40077() : GameObjectScript("go_q40077") { }
    struct go_q40077AI : public GameObjectAI
    {
        go_q40077AI(GameObject* go) : GameObjectAI(go) { }
        bool GossipHello(Player* player) override
        {
            return player->GetQuestStatus(40077) != QUEST_STATUS_INCOMPLETE;
        }
        bool GossipUse(Player* player) override
        {
            if (go->GetEntry() != 244898 || player->GetMapId() != 1481 ||
                player->GetQuestStatus(40077) != QUEST_STATUS_INCOMPLETE ||
                player->GetReqKillOrCastCurrentCount(40077, 244898) ||
                !player->GetAnyDataContainer().GetValue<ObjectGuid>("Felsoria.Banner.PendingGuid", ObjectGuid::Empty).IsEmpty())
                return true;
            if (!sSpellMgr->GetSpellScene(1116))
            {
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "MARDUM_BANNER: missing scene MiscValue=1116; quest unchanged");
                return true;
            }
            player->GetAnyDataContainer().Set<ObjectGuid>("Felsoria.Banner.PendingGuid", go->GetGUID());
            Position scenePos = player->GetPosition();
            player->SendSpellScene(1116, nullptr, true, &scenePos);
            // Credit and phase refresh belong to SceneCompleted, after cleanup.
            return true;
        }
    };
    GameObjectAI* GetAI(GameObject* go) const override { return new go_q40077AI(go); }
};

//93011
class npc_q40077 : public CreatureScript
{
public:
    npc_q40077() : CreatureScript("npc_q40077") { }


    enum data
    {
        QUEST    = 40077,
        NPC_CONV = 922,
    };

    bool OnQuestAccept(Player* player, Creature* creature, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST)
        {
            /* //ToDo: how load npc for converstation?
            player->GetMap()->LoadGrid(1170.74f, 3204.71f); //voice

            Conversation* conversation = new Conversation;
            if (!conversation->CreateConversation(sObjectMgr->GetGenerator<HighGuid::Conversation>()->Generate(), NPC_CONV, player, NULL, *player))
                delete conversation; */
            player->SummonCreature(93011, creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), 1.64f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);

            // Felsoria Pack47 - remove static starting group immediately
            // Q40077 is now active, so Phase 7000 must disappear before
            // the personal Kayn + Illidari movement group starts.
            PhaseUpdateData questPhaseUpdate;
            questPhaseUpdate.AddQuestUpdate(QUEST);
            player->GetPhaseMgr().NotifyConditionChanged(questPhaseUpdate);
        }

        return true;
    }
    
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q40077AI(creature);
    }

    struct npc_q40077AI : public ScriptedAI
    {
        npc_q40077AI(Creature* creature) : ScriptedAI(creature), pPlayer(nullptr)
        {
            timer = 0;
            movein = false;
        }

        Player* pPlayer;
        uint32 timer;
        bool movein;
        
        void IsSummonedBy(Unit* summoner) override
        {
            Player* player = summoner->ToPlayer();
            if (!player)
                return;
            timer = 9000;
            movein = true;
            
            me->AddPlayerInPersonnalVisibilityList(summoner->GetGUID());
            
            me->SummonCreature(98228, 1182.35f, 3202.90f, 52.60f, 2.28f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);
            me->SummonCreature(98227, 1177.00f, 3203.07f, 52.44f, 1.29f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);
            me->SummonCreature(99918, 1172.92f, 3207.82f, 52.47f, 5.89f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);
            me->SummonCreature(98290, 1171.48f, 3203.68f, 51.39f, 0.18f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);
            me->SummonCreature(98292, 1170.73f, 3204.70f, 52.62f, 0.01f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);
            
            player->GetMap()->LoadGrid(1170.74f, 3204.71f); //voice

            Conversation* conversation = new Conversation;
            if (!conversation->CreateConversation(sObjectMgr->GetGenerator<HighGuid::Conversation>()->Generate(), NPC_CONV, player, NULL, *player))
                delete conversation;
        }
        
        void UpdateAI(uint32 diff) override
        {
            if (movein)
            {
                if (timer <= diff)
                {
                    me->GetMotionMaster()->MovePath(10267107, false);
                    movein = false;
                    me->DespawnOrUnsummon(17000);
                } else timer -= diff;
            }
        }
    };

};

class npc_q40077_1 : public CreatureScript
{
public:
    npc_q40077_1() : CreatureScript("npc_q40077_1") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q40077_1AI(creature);
    }

    struct npc_q40077_1AI : public ScriptedAI
    {
        npc_q40077_1AI(Creature* creature) : ScriptedAI(creature)
        {
            timer = 0;
            movein = false;
        }
        
        uint32 timer;
        bool movein;
        
        void IsSummonedBy(Unit* summoner) override
        {
            timer = 9000;
            movein = true;
        }
        
        void UpdateAI(uint32 diff) override
        {
            if (movein)
            {
                if (timer <= diff)
                {
                    uint32 path = 0;
                    switch(me->GetEntry())
                    {
                        case 98228:
                            path = 10267108;
                            break;
                        case 98227:
                            path = 10267109;
                            break;
                        case 99918:
                            path = 10267110;
                            break;
                        case 98292:
                            path = 10267111;
                            break;
                        case 98290:
                            path = 10267112;
                            break;
                        default:
                            break;                        
                    }
                    me->GetMotionMaster()->MovePath(path, false);
                    me->DespawnOrUnsummon(17000);
                    movein = false;
                } else timer -= diff;
            }
        }        
    };

};

// Q40378 - Enter the Illidari: Ashtongue
// Scene 1053 / spell 189261
class sceneTrigger_q40378_ashtongue : public SceneTriggerScript
{
public:
    sceneTrigger_q40378_ashtongue() : SceneTriggerScript("sceneTrigger_q40378_ashtongue") { }

    bool OnTrigger(Player* player, SpellScene const* /*trigger*/, std::string type) override
    {
        if (!player || player->GetQuestStatus(40378) != QUEST_STATUS_INCOMPLETE)
            return true;

        // The client scene fires this trigger when the Felsaber is revealed.
        // Q40378 StorageIndex 4 / ObjectID 101534 is the hidden scene credit.
        if (type == "SEEFELSABERCREDIT")
        {
            if (!player->GetQuestObjectiveData(40378, 101534))
                player->KilledMonsterCredit(101534);
        }

        return true;
    }
};
// 241751
class go_q40378 : public GameObjectScript
{
public:
    go_q40378() : GameObjectScript("go_q40378") { }

    struct go_q40378AI : public GameObjectAI
    {
        go_q40378AI(GameObject* go) : GameObjectAI(go) { }

        enum data
        {
            QUEST = 40378,
            SCENE = 189261,
            CREDIT_PORTAL = 88872,
            CREDIT_FIRST_GUARDIAN = 97831,
        };

        bool GossipHello(Player* player) override
        {
            if (player->GetQuestStatus(QUEST) != QUEST_STATUS_INCOMPLETE)
                return true;

            // Felsoria Pack53 v2:
            // Pack53 v1 returned immediately as soon as 88872 was already complete.
            // That made partially progressed characters impossible to repair:
            // objective 0 = 1, objective 1 = 0 -> portal did absolutely nothing.
            //
            // Handle each required opening objective independently instead.

            bool portalDone   = player->GetQuestObjectiveData(QUEST, CREDIT_PORTAL) != 0;
            bool guardianDone = player->GetQuestObjectiveData(QUEST, CREDIT_FIRST_GUARDIAN) != 0;
            bool hiddenDone   = player->GetQuestObjectiveData(QUEST, 101534) != 0;

            // Fully processed opening sequence: do not replay it.
            if (portalDone && guardianDone && hiddenDone)
                return true;

            player->Dismount();

            if (!portalDone)
                player->KilledMonsterCredit(CREDIT_PORTAL);

            if (!guardianDone)
                player->KilledMonsterCredit(CREDIT_FIRST_GUARDIAN);

            // The hidden 101534 objective is supposed to be produced by
            // SEEFELSABERCREDIT from scene 1053. If it is still missing,
            // start/restart that scene and recreate its temporary actors.
            if (!hiddenDone)
            {
                player->CastSpell(player, SCENE, true);

                player->SummonCreature(
                    99916,
                    1023.25f, 2849.71f, 5.42f, 1.75f,
                    TEMPSUMMON_MANUAL_DESPAWN,
                    0,
                    player->GetGUID(),
                    NULL);

                if (Creature* mount = player->SummonCreature(
                    101518,
                    1016.71f, 2849.31f, 5.48f, 1.38f,
                    TEMPSUMMON_MANUAL_DESPAWN,
                    0,
                    player->GetGUID(),
                    NULL))
                {
                    mount->SetVisible(false);
                }
            }

            return true;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_q40378AI(go);
    }
};


/*
before OBJECT_FIELD_DYNAMIC_FLAGS: 4294902018
after OBJECT_FIELD_DYNAMIC_FLAGS: 2473853184

*/

//244439, 244440, 244441, 243873
class go_q39279 : public GameObjectScript
{
public:
    go_q39279() : GameObjectScript("go_q39279") { }

    struct go_q39279AI : public GameObjectAI
    {
        go_q39279AI(GameObject* go) : GameObjectAI(go)
        {

        }

        enum data
        {
            QUEST = 39279,
            GO_244439 = 244439,
            GO_244440 = 244440,
            GO_244441 = 244441,
            GO_243873 = 243873,
            NPC_CONV_GO_244439 = 558,
            NPC_CONV_GO_244440 = 583,
        };

        bool GossipUse(Player* player) override
        { 
            if (player->GetReqKillOrCastCurrentCount(QUEST, go->GetEntry()))
                return true;
            return false;
        }
        bool GossipHello(Player* player) override
        {
            if (player->GetReqKillOrCastCurrentCount(QUEST, go->GetEntry()))
                return true;

            switch (go->GetEntry())
            {
                case GO_244439:
                {
                    Conversation* conversation = new Conversation;
                    if (!conversation->CreateConversation(sObjectMgr->GetGenerator<HighGuid::Conversation>()->Generate(), NPC_CONV_GO_244439, player, NULL, *player))
                        delete conversation;
                    break;
                }
                case GO_244440:
                {
                    Conversation* conversation = new Conversation;
                    if (!conversation->CreateConversation(sObjectMgr->GetGenerator<HighGuid::Conversation>()->Generate(), NPC_CONV_GO_244440, player, NULL, *player))
                        delete conversation;
                    break;
                }
                case GO_244441:
                case GO_243873:
                    break;
            }

            return false;
        }

    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_q39279AI(go);
    }
};

/*
ClientToServer: CMSG_SPELL_CLICK (0x348C) Length: 16 ConnIdx: 2 Time: 02/06/2016 22:40:30.961 Number: 17163
SpellClickUnitGUID: Full: 0x202090B9205EDD800000100002B64D15; HighType: Creature; Low: 45501717; Map: 1481; Entry: 97142;
TryAutoDismount: True

conv - 581
*/

// Destroying Fel Spreader - 191827
class spell_legion_q39279 : public SpellScriptLoader
{
public:
    spell_legion_q39279() : SpellScriptLoader("spell_legion_q39279") { }

    class spell_legion_q39279_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_legion_q39279_SpellScript);

        enum data
        {
            QUEST = 39279,
            NPC_CONV = 581
        };

        void HandleScriptEffect(SpellEffIndex effIndex)
        {
            PreventHitDefaultEffect(EFFECT_0);
            if (Unit* caster = GetCaster())
            {
                Player *player = caster->ToPlayer();
                if (!player)
                    return;

                if (Unit * target = GetHitUnit())
                {
                    target->AddToHideList(caster->GetGUID());
                    target->DestroyForPlayer(player);
                    // FELSORIA_KAYN_FIRST_FEL_SPREADER_V1
                    bool firstFelSpreader = target->GetEntry() == 97142 &&
                        player->GetQuestStatus(QUEST) == QUEST_STATUS_INCOMPLETE &&
                        player->GetQuestObjectiveData(QUEST, 97142) == 0;
                    player->KilledMonsterCredit(target->GetEntry());
                    if (!firstFelSpreader || player->GetQuestObjectiveData(QUEST, 97142) == 0)
                        return;

                    Conversation* conversation = new Conversation;
                    if (!conversation->CreateConversation(sObjectMgr->GetGenerator<HighGuid::Conversation>()->Generate(), NPC_CONV, player, NULL, *player))
                        delete conversation;
                }
            }
        }

        void Register() override
        {
            OnEffectHitTarget += SpellEffectFn(spell_legion_q39279_SpellScript::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_DUMMY);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_legion_q39279_SpellScript();
    }
};

//94410
class npc_q40378 : public CreatureScript
{
public:
    npc_q40378() : CreatureScript("npc_q40378") { }
    
    bool OnQuestAccept(Player* player, Creature* creature, Quest const* quest) override
    {
        if (quest->GetQuestId() == 39049) 
            sCreatureTextMgr->SendChat(creature, 1, player->GetGUID());
        return true;
    }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q40378AI(creature);
    }

    enum data
    {
        QUEST = 40378,
    };

    struct npc_q40378AI : public ScriptedAI
    {
        npc_q40378AI(Creature* creature) : ScriptedAI(creature)
        {
            timer = 1000;
        }
        
        uint32 timer;

        void Reset() override
        {
            if (Creature* target = me->FindNearestCreature(105316, 40.0f))
                me->CastSpell(target, 188437, true); // visual
        }

        void MoveInLineOfSight(Unit* who) override
        {
            Player *player = who->ToPlayer();
            if (!player || !me->IsWithinDistInMap(who, 100.0f))
                return;

            if (player->GetQuestStatus(QUEST) != QUEST_STATUS_INCOMPLETE)
                return;

            if (player->GetQuestObjectiveData(QUEST, me->GetEntry()))
                return;

            player->KilledMonsterCredit(me->GetEntry());
            sCreatureTextMgr->SendChat(me, 0, player->GetGUID());
            return;
        }
        
        void UpdateAI(uint32 diff) override
        {
            if (timer <= diff)
            {
                if (Creature* target = me->FindNearestCreature(105316, 40.0f))
                    me->CastSpell(target, 188437, true); // visual
                timer = 3000;
            } else
                timer -= diff;
                
        }
    };
};


class conversation_announcer : public CreatureScript
{
public:
    conversation_announcer() : CreatureScript("conversation_announcer") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new conversation_announcerAI(creature);
    }

    struct conversation_announcerAI : public ScriptedAI
    {
        conversation_announcerAI(Creature* creature) : ScriptedAI(creature)
        {
            SetCanSeeEvenInPassiveMode(true);
            if (me->GetEntry() == 101748)
                events.RescheduleEvent(EVENT_3, 6000);
        }

        void Reset() override
        {
            conversationEntry = 0;
            targetGUID.Clear();
            //me->SetReactState(REACT_PASSIVE);
        }

        enum events
        {
            EVENT_1 = 1,
            EVENT_2_ANNOUNCER6 = 2,
            EVENT_CLEAR = 3,
            EVENT_3 = 4,
        };

        enum npcs
        {
            NPC_ANNOUNCER_1 = 101748, //583
            NPC_ANNOUNCER_2 = 101781, //1542
            NPC_ANNOUNCER_3 = 100510, //531
        };

        uint32 conversationEntry;
        ObjectGuid targetGUID;
        GuidSet m_player_for_event;
        EventMap events;
        void MoveInLineOfSight(Unit* who) override
        {
            if (who->GetTypeId() != TYPEID_PLAYER || who->IsOnVehicle())
                return;

            GuidSet::iterator itr = m_player_for_event.find(who->GetGUID());
            if (itr != m_player_for_event.end())
                return;

            if (!me->IsWithinDistInMap(who, 100.0f))
                return;

            uint32 eTimer = 1000;
            /*
            area 7742
            [1] Object Guid: Full: 0x1C2090B9200084C000109C00004B0C80; HighType: Conversation; Low: 4918400; Map: 1481; Entry: 531;
            [1] Stationary Position: X: 1561.098 Y: 2623.586 Z: 30.56277

            area 7705
            [0] Object Guid: Full: 0x1C2090B92000BAC000109C00004B0CE6; HighType: Conversation; Low: 4918502; Map: 1481; Entry: 747;
            [0] Stationary Position: X: 1429.753 Y: 2362.509 Z: 61.22379
            */
            switch (me->GetEntry())
            {
                case NPC_ANNOUNCER_1:
                    if (me->GetAreaId() == 7742) //Quest 38766
                    {
                        conversationEntry = 531;
                        events.RescheduleEvent(EVENT_1, eTimer);
                    }
                    break;
                case NPC_ANNOUNCER_2:
                    conversationEntry = 1542;
                    events.RescheduleEvent(EVENT_2, eTimer);
                    break;
                default:
                    break;
            }

            if (!conversationEntry)
                return;

            m_player_for_event.insert(who->GetGUID());
            //events.RescheduleEvent(EVENT_CLEAR, 300000);
            targetGUID = who->GetGUID();
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);

            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_1:
                    {
                        if (Player* player = sObjectAccessor->FindPlayer(targetGUID))
                        {
                            Conversation* conversation = new Conversation;
                            if (!conversation->CreateConversation(sObjectMgr->GetGenerator<HighGuid::Conversation>()->Generate(), conversationEntry, player, NULL, *player))
                                delete conversation;
                        }
                        break;
                    }
                    case EVENT_2:
                    {
                        if (Player* player = sObjectAccessor->FindPlayer(targetGUID))
                        {
                            player->SendSpellScene(conversationEntry, nullptr, true, player);
                        }
                        break;
                    }

                    case EVENT_CLEAR:
                        m_player_for_event.clear();
                        break;
                    case EVENT_3:
                        DoCast(200680);
                        events.RescheduleEvent(EVENT_3, 10000);
                        break;
                }
            }
        }
    };
};

//93105
class npc_q39049 : public CreatureScript
{
public:
    npc_q39049() : CreatureScript("npc_q39049") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q39049AI(creature);
    }

    enum data
    {
        QUEST       = 39049,
        CREDIT      = 105946,
        SPELL_LEARN = 195447,
        
        SPELL_1     = 194519,  // 2 + 5
        SPELL_2     = 195058,  // 5
        SPELL_PHASE = 192665, // 53%
        SPELL_INF   = 192709,
    };

    struct npc_q39049AI : public Scripted_NoMovementAI
    {
        npc_q39049AI(Creature* creature) : Scripted_NoMovementAI(creature), summons(me), phase2(false)
        {
            creature->SetCorpseDelay(30);
            creature->SetRespawnDelay(15);
        }

        EventMap events;
        SummonList summons;
        bool phase2;
        
        void Reset() override
        {
            phase2 = false;
            me->SetReactState(REACT_DEFENSIVE);
            SetFlyMode(false);
            summons.DespawnAll();
            events.Reset();
        }

        void EnterCombat(Unit* victim) override
        {
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_0);
            events.RescheduleEvent(EVENT_1, 2000);
            events.RescheduleEvent(EVENT_2, 5000);
        }

        void JustDied(Unit* attacker) override
        {
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_2);

            Player *player = attacker->ToPlayer();
            if (!player)
                return;

            if (player->GetQuestStatus(QUEST) != QUEST_STATUS_INCOMPLETE)
                return;

            player->KilledMonsterCredit(CREDIT);
            player->CastSpell(player, SPELL_LEARN, false);
        }
        
        void JustSummoned(Creature* summon) override
        { 
            summons.Summon(summon);
            // summon->CastSpell(summon, 195051, false);
            summon->GetMotionMaster()->MoveRotate(20000, ROTATE_DIRECTION_RIGHT);
        }
        
        void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType dmgType) override
        {
            if (me->HealthBelowPct(53) && !phase2)
            {
                phase2 = true;
                me->SetReactState(REACT_PASSIVE);
                DoCast(SPELL_PHASE);
                sCreatureTextMgr->SendChat(me, TEXT_GENERIC_1);
                summons.DespawnAll();
                SetFlyMode(true);
                me->GetMotionMaster()->MovePoint(0, 593.33f, 2433.02f, -64.88f);
                events.CancelEvent(EVENT_1);
                events.CancelEvent(EVENT_2);
                events.RescheduleEvent(EVENT_3, 3000);
                events.RescheduleEvent(EVENT_4, 15000);
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim() || me->HasUnitState(UNIT_STATE_CASTING))
                return;

            events.Update(diff);
            
            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_1:
                        DoCast(SPELL_1);
                        events.RescheduleEvent(EVENT_1, 5000);
                        break;
                    case EVENT_2:
                        {
                            float radius = 5.0f; // растояние от кастера
                            float coneAngle = 0.0f; // угол куда поставится моб с АТ
                            Position position;
                            me->GetNearPosition(position, radius, coneAngle);
                            me->CastSpell(position.GetPositionX(), position.GetPositionY(), position.GetPositionZ(), 195061, true);
                            coneAngle = 2.0f; // угол куда поставится моб с АТ
                            me->GetNearPosition(position, radius, coneAngle);
                            me->CastSpell(position.GetPositionX(), position.GetPositionY(), position.GetPositionZ(), 195061, true);
                            coneAngle = 4.0f; // угол куда поставится моб с АТ
                            me->GetNearPosition(position, radius, coneAngle);
                            me->CastSpell(position.GetPositionX(), position.GetPositionY(), position.GetPositionZ(), 195061, true);
                        }
                        break;
                    case EVENT_3:
                        if (Creature* inf = me->FindNearestCreature(96159, 300.0f))
                            inf->CastSpell(inf, SPELL_INF);
                        events.RescheduleEvent(EVENT_3, 2500);
                        break;
                    case EVENT_4:
                        me->RemoveAura(SPELL_PHASE);
                        events.CancelEvent(EVENT_3);
                        events.RescheduleEvent(EVENT_1, 3000);
                        events.RescheduleEvent(EVENT_2, 6000);
                        me->SetReactState(REACT_DEFENSIVE);
                        SetFlyMode(false);
                        me->GetMotionMaster()->MovePoint(1, 592.58f, 2432.88f, -71.28f);
                        break;
                }
            }
            DoMeleeAttackIfReady();
        }
    };
};

//242989 244916 242987 242990
// FELSORIA_SET_THEM_FREE_CAST_V1
namespace
{
    uint32 FelsoriaPrisonerForCage(uint32 entry)
    {
        switch (entry)
        {
            case 242989: return 94400;
            case 244916: return 94377;
            case 242987: return 93117;
            case 242990: return 93230;
            default: return 0;
        }
    }

    bool FelsoriaCanUnlockCage(Player* player, GameObject* cage)
    {
        uint32 prisonerEntry = cage ? FelsoriaPrisonerForCage(cage->GetEntry()) : 0;
        return player && cage && prisonerEntry && player->isAlive() &&
            player->GetMapId() == 1481 && cage->GetMapId() == 1481 &&
            player->GetQuestStatus(38759) == QUEST_STATUS_INCOMPLETE &&
            player->HasItemCount(128227, 1, false) &&
            !player->GetQuestObjectiveData(38759, prisonerEntry) &&
            cage->IsWithinDistInMap(player, 5.0f);
    }

    void FelsoriaFinishCageUnlock(Player* player, GameObject* cage)
    {
        if (!FelsoriaCanUnlockCage(player, cage))
            return;
        uint32 prisonerEntry = FelsoriaPrisonerForCage(cage->GetEntry());
        if (Creature* c = cage->FindNearestCreature(prisonerEntry, 10.0f))
        {
                if (auto temp = player->SummonCreature(prisonerEntry, c->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 35000, 0, player->GetGUID()))
                {
                    player->KilledMonsterCredit(prisonerEntry);
                    c->DestroyForPlayer(player);
                    cage->SetGoState(GO_STATE_ACTIVE);
                    ObjectGuid playerGuid = player->GetGUID();
                    temp->AddDelayedEvent(500, [temp, playerGuid]()-> void
                    {
                        if (auto player = ObjectAccessor::FindPlayer(playerGuid))
                            temp->AI()->Talk(1, player->GetGUID());

                        temp->AddDelayedEvent(4000, [temp]() -> void
                        {
                            switch (temp->GetEntry())
                            {
                            case 94377:
                                temp->GetMotionMaster()->MovePath(1467061408, false);
                                break;
                            case 94400:
                                temp->GetMotionMaster()->MovePath(1467061409, false);
                                break;
                            case 93230:
                                temp->GetMotionMaster()->MovePath(1467061410, false);
                                break;
                            case 93117:
                                temp->GetMotionMaster()->MovePath(1467061411, false);
                                break;
                            }
                        });
                    });
                }
        }
    }
}

class go_q38759 : public GameObjectScript
{
public:
    go_q38759() : GameObjectScript("go_q38759") { }

    struct go_q38759AI : public GameObjectAI
    {
        go_q38759AI(GameObject* go) : GameObjectAI(go) { }

        bool StartUnlock(Player* player)
        {
            if (!FelsoriaCanUnlockCage(player, go) || player->IsNonMeleeSpellCast(false))
                return true;
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(188889);
            if (!spellInfo)
                return true;
            SpellCastTargets targets;
            targets.SetGOTarget(go);
            TriggerCastData triggerData;
            triggerData.casttime = 1500;
            player->CastSpell(targets, spellInfo, nullptr, triggerData);
            return true; // Never let a use/report packet release prisoners directly.
        }

        bool GossipHello(Player* player) override { return StartUnlock(player); }
        bool GossipUse(Player* player) override { return StartUnlock(player); }
    };

    GameObjectAI* GetAI(GameObject* go) const override { return new go_q38759AI(go); }
};

class spell_felsoria_cage_unlock_v1 : public SpellScriptLoader
{
public:
    spell_felsoria_cage_unlock_v1() : SpellScriptLoader("spell_felsoria_cage_unlock_v1") { }
    class spell_felsoria_cage_unlock_v1_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_felsoria_cage_unlock_v1_SpellScript);
        SpellCastResult CheckUnlock()
        {
            Player* player = GetCaster()->ToPlayer();
            if (!player)
                return SPELL_FAILED_BAD_TARGETS;
            if (!player->HasItemCount(128227, 1, false))
                return SPELL_FAILED_ITEM_NOT_FOUND;
            return FelsoriaCanUnlockCage(player, GetExplTargetGObj()) ? SPELL_CAST_OK : SPELL_FAILED_BAD_TARGETS;
        }
        void SuppressDefaultOpen(SpellEffIndex effIndex)
        {
            PreventHitDefaultEffect(effIndex);
        }
        void FinishUnlock()
        {
            FelsoriaFinishCageUnlock(GetCaster()->ToPlayer(), GetExplTargetGObj());
        }
        void Register() override
        {
            OnCheckCast += SpellCheckCastFn(spell_felsoria_cage_unlock_v1_SpellScript::CheckUnlock);
            OnEffectHitTarget += SpellEffectFn(spell_felsoria_cage_unlock_v1_SpellScript::SuppressDefaultOpen, EFFECT_ALL, SPELL_EFFECT_ANY);
            AfterCast += SpellCastFn(spell_felsoria_cage_unlock_v1_SpellScript::FinishUnlock);
        }
    };
    SpellScript* GetSpellScript() const override { return new spell_felsoria_cage_unlock_v1_SpellScript(); }
};

// FELSORIA_FELSABER_PERSISTENT_UNLOCK_V3
namespace
{
    bool FelsoriaHasAcceptedFelsaber(Player* player)
    {
        return player && player->getClass() == CLASS_DEMON_HUNTER &&
            (player->GetQuestRewardStatus(40378) ||
             player->GetQuestStatus(40378) == QUEST_STATUS_COMPLETE ||
             player->GetQuestObjectiveData(40378, 101518) != 0);
    }
}

class spell_felsoria_starter_mount_lock_v3 : public SpellScriptLoader
{
public:
    spell_felsoria_starter_mount_lock_v3() : SpellScriptLoader("spell_felsoria_starter_mount_lock_v3") { }
    class spell_felsoria_starter_mount_lock_v3_AuraScript : public AuraScript
    {
        PrepareAuraScript(spell_felsoria_starter_mount_lock_v3_AuraScript);
        void RemoveObsoleteLock(AuraEffect const*, AuraEffectHandleModes)
        {
            if (FelsoriaHasAcceptedFelsaber(GetTarget()->ToPlayer()))
                Remove();
        }
        void Register() override
        {
            AfterEffectApply += AuraEffectApplyFn(spell_felsoria_starter_mount_lock_v3_AuraScript::RemoveObsoleteLock,
                EFFECT_0, SPELL_AURA_MOD_FLYING_RESTRICTIONS, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
        }
    };
    AuraScript* GetAuraScript() const override { return new spell_felsoria_starter_mount_lock_v3_AuraScript(); }
};

class player_felsoria_felsaber_unlock_v3 : public PlayerScript
{
public:
    player_felsoria_felsaber_unlock_v3() : PlayerScript("player_felsoria_felsaber_unlock_v3") { }

    // FELSORIA_FELSABER_LOGIN_SYNC_V5
    static void RestoreUnlock(Player* player, char const* stage = nullptr, bool syncClient = false)
    {
        if (!player || player->IsPlayerBot() || player->getClass() != CLASS_DEMON_HUNTER)
            return;
        bool accepted = FelsoriaHasAcceptedFelsaber(player);
        bool hadLock = player->HasAura(179665);
        if (accepted)
        {
            player->RemoveAura(179665);
            // Aura type 336 uses HandleNoImmediateEffect in this core.
            player->UpdateMount();
        }
        uint32 locationZone = 0, locationArea = 0;
        player->GetZoneAndAreaId(locationZone, locationArea);
        if (accepted && syncClient && player->IsInWorld())
        {
            // Initialize a stale/zero login area from the real map position.
            // No movement, teleport, or mount flag override.
            if (locationZone && locationArea &&
                (player->GetCurrentZoneID() != locationZone || player->GetCurrentAreaID() != locationArea))
                player->UpdateZone(locationZone, locationArea);
            player->RemoveAura(179665);
            player->UpdateMount();
            // Full authoritative aura snapshot: RemoveAura cannot send a removal
            // for an aura already absent on the server but retained by the client.
            player->SendAurasForTarget(player);
        }
        if (stage)
        {
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(player->GetCurrentAreaID());
            MountEntry const* mount = sDB2Manager.GetMount(200175);
            auto restrictions = player->GetAuraEffectsByType(SPELL_AURA_MOD_FLYING_RESTRICTIONS);
            (void)area; (void)mount; (void)restrictions; // FELSORIA_P13_QUIET_FELSABER_LOGIN
        }
    }

    void OnLogin(Player* player) override
    {
        if (!player || player->IsPlayerBot() || player->getClass() != CLASS_DEMON_HUNTER)
            return;
        RestoreUnlock(player, "login");
        ObjectGuid guid = player->GetGUID();
        // One check on the next player event update, one bounded final check.
        // Resolve the GUID at execution; do not retain a raw Player pointer.
        player->AddDelayedEvent(1, [guid]() -> void
        {
            if (Player* current = ObjectAccessor::FindPlayer(guid))
                RestoreUnlock(current, "post-login", true);
        });
        player->AddDelayedEvent(1000, [guid]() -> void
        {
            if (Player* current = ObjectAccessor::FindPlayer(guid))
                RestoreUnlock(current, "post-login-final", true);
        });
    }
    void OnMapChanged(Player* player) override { RestoreUnlock(player); }
    void OnUpdateZone(Player* player, uint32, uint32) override { RestoreUnlock(player); }
};

//99914
// FELSORIA_COILSKAR_MYSTIC_DEATH_V1
// FELSORIA_COILSKAR_MYSTIC_DEATH_V2
// FELSORIA_COILSKAR_TRACE_V3
// FELSORIA_COILSKAR_TARGET_V4
// FELSORIA_PERSONAL_SACRIFICE_COILSKAR_V1
// FELSORIA_PERSONAL_SACRIFICE_LOGIN_V1
class player_felsoria_personal_sacrifice_v1 : public PlayerScript
{
public:
    player_felsoria_personal_sacrifice_v1() : PlayerScript("player_felsoria_personal_sacrifice_v1") { }
    void OnLogin(Player* player) override
    {
        uint32 mask = 0;
        if (player->getClass() == CLASS_DEMON_HUNTER)
            if (QueryResult rows = CharacterDatabase.PQuery(
                "SELECT npc_entry FROM felsoria_personal_sacrifice WHERE guid=%u", uint32(player->GetGUIDLow())))
                do
                {
                    uint32 entry = rows->Fetch()[0].GetUInt32();
                    if (entry == 99914) mask |= 1u;
                    if (entry == 99915) mask |= 2u;
                } while (rows->NextRow());
        player->GetAnyDataContainer().Set<uint32>("Felsoria.PersonalSacrifice.Mask", mask);
        if (!mask) return;
        // Initial visibility may already have been sent before OnLogin.
        ObjectGuid guid = player->GetGUID();
        player->AddDelayedEvent(1, [guid]()
        {
            Player* owner = ObjectAccessor::FindPlayer(guid);
            if (!owner || owner->GetMapId() != 1481) return;
            for (uint32 entry : {99914u, 99915u})
            {
                std::list<Creature*> nearby;
                GetCreatureListWithEntryInGrid(nearby, owner, entry, 300.0f);
                for (Creature* npc : nearby)
                    if (owner->HaveAtClient(npc)) npc->SendUpdateToPlayer(owner);
            }
        });
    }
};

class npc_q40379 : public CreatureScript
{
public:
    npc_q40379() : CreatureScript("npc_q40379") { }
    bool OnGossipSelect(Player* player, Creature* creature, uint32, uint32 action) override
    {
        TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "COILSKAR_TRACE: gossip guid=%u action=%u entry=%u alive=%u quest=%u credit=%u casting=%u",
            uint32(player->GetGUIDLow()), action, creature->GetEntry(), uint32(creature->isAlive()),
            uint32(player->GetQuestStatus(40379)), uint32(player->GetQuestObjectiveData(40379, 99914)), uint32(player->IsNonMeleeSpellCast(false)));
        player->PlayerTalkClass->ClearMenus();
        if (action == 1)
        {
            player->CLOSE_GOSSIP_MENU();
            if (creature->GetEntry() == 99914 && creature->isAlive() &&
                player->GetQuestStatus(40379) == QUEST_STATUS_INCOMPLETE &&
                !player->GetQuestObjectiveData(40379, 99914) &&
                !player->IsNonMeleeSpellCast(false))
            {
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "COILSKAR_TRACE: gossip dispatch spell=196724");
                // Spell 196724 has no explicit unit target in this client data.
                // Bind only the NPC selected by this player, only for this cast.
                player->GetAnyDataContainer().Set<ObjectGuid>("Felsoria.CoilskarMystic.PendingTarget", creature->GetGUID());
                player->CastSpell(creature, 196724, false);
                // Load() consumes the binding synchronously during CastSpell.
                // Clear it also when spell preparation fails before Load().
                player->GetAnyDataContainer().Remove("Felsoria.CoilskarMystic.PendingTarget");
            }
            else
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "COILSKAR_TRACE: gossip blocked by precondition");
        }
        return true;
    }
};

class spell_felsoria_coilskar_mystic_sacrifice_v1 : public SpellScriptLoader
{
public:
    spell_felsoria_coilskar_mystic_sacrifice_v1() : SpellScriptLoader("spell_felsoria_coilskar_mystic_sacrifice_v1") { }
    class sacrifice_SpellScript : public SpellScript
    {
        PrepareSpellScript(sacrifice_SpellScript);
        ObjectGuid _mysticGuid;
        void TraceStage(char const* stage)
        {
            Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
            auto target = GetExplTargetUnit();
            TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "COILSKAR_TRACE: stage=%s casterPlayer=%u guid=%u targetEntry=%u targetCreature=%u finishSuccess=%u map=%u quest=%u credit=%u",
                stage, uint32(player != nullptr), player ? uint32(player->GetGUIDLow()) : 0,
                target ? uint32(target->GetEntry()) : 0, uint32(target && target->ToCreature() != nullptr),
                uint32(GetSpell()->FelsoriaFinishedSuccessfully()), player ? player->GetMapId() : 0,
                player ? uint32(player->GetQuestStatus(40379)) : 0,
                player ? uint32(player->GetQuestObjectiveData(40379, 99914)) : 0);
        }
        bool Load() override
        {
            _mysticGuid = ObjectGuid::Empty;
            if (Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr)
            {
                _mysticGuid = player->GetAnyDataContainer().GetValue<ObjectGuid>("Felsoria.CoilskarMystic.PendingTarget", ObjectGuid::Empty);
                player->GetAnyDataContainer().Remove("Felsoria.CoilskarMystic.PendingTarget");
                Creature* bound = ObjectAccessor::GetCreature(*player, _mysticGuid);
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "COILSKAR_TRACE: binding player=%u entry=%u found=%u",
                    uint32(player->GetGUIDLow()), bound ? bound->GetEntry() : 0, uint32(bound != nullptr));
            }
            TraceStage("load");
            return true;
        }
        void BeforeStart() { TraceStage("before-start"); }
        void Launched() { TraceStage("on-cast"); }
        void AfterLaunch() { TraceStage("after-cast"); }
        void FinishSacrifice()
        {
            Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
            if (!GetSpell()->FelsoriaFinishedSuccessfully() || !player || player->GetMapId() != 1481)
                return;
            Creature* original = ObjectAccessor::GetCreature(*player, _mysticGuid);
            if (!original || original->GetEntry() != 99914 || !original->isAlive() ||
                player->GetQuestStatus(40379) != QUEST_STATUS_INCOMPLETE ||
                player->GetQuestObjectiveData(40379, 99914))
                return;
            // Only this viewer receives dead-looking fields for the original GUID.
            // Never mutate shared health/death/flags and never create a clone.
            uint32 mask = player->GetAnyDataContainer().GetValue<uint32>("Felsoria.PersonalSacrifice.Mask", 0);
            player->GetAnyDataContainer().Set<uint32>("Felsoria.PersonalSacrifice.Mask", mask | 1u);
            CharacterDatabase.PExecute("REPLACE INTO felsoria_personal_sacrifice (guid, npc_entry) VALUES (%u, %u)",
                uint32(player->GetGUIDLow()), uint32(99914));
            player->KilledMonsterCredit(99914);
            original->SendUpdateToPlayer(player);
            TC_LOG_INFO(LOG_FILTER_WORLDSERVER, "PERSONAL_SACRIFICE: player=%u entry=99914 same NPC GUID, personal death sent",
                uint32(player->GetGUIDLow()));
        }
        void Register() override
        {
            BeforeStartCast += SpellCastFn(sacrifice_SpellScript::BeforeStart);
            OnCast += SpellCastFn(sacrifice_SpellScript::Launched);
            AfterCast += SpellCastFn(sacrifice_SpellScript::AfterLaunch);
            OnFinishCast += SpellCastFn(sacrifice_SpellScript::FinishSacrifice);
        }
    };
    SpellScript* GetSpellScript() const override { return new sacrifice_SpellScript(); }
};

class go_q40379 : public GameObjectScript
{
public:
    go_q40379() : GameObjectScript("go_q40379") { }

    struct go_q40379AI : public GameObjectAI
    {
        go_q40379AI(GameObject* go) : GameObjectAI(go)
        {

        }

        enum data
        {
            QUEST  = 40379,
            SPELL  = 190793,
            CREDIT = 94406,
            CREDIT_REQUARE = 99914,
        };

        bool GossipUse(Player* player) override
        {
            if (player->GetReqKillOrCastCurrentCount(QUEST, CREDIT) || !player->GetReqKillOrCastCurrentCount(QUEST, CREDIT_REQUARE))
                return true;
            return false;
        }

        bool GossipHello(Player* player) override
        {
            if (player->GetReqKillOrCastCurrentCount(QUEST, CREDIT) || !player->GetReqKillOrCastCurrentCount(QUEST, CREDIT_REQUARE))
                return true;

            player->KilledMonsterCredit(CREDIT);
            player->CastSpell(player, SPELL, false);
            player->SummonCreature(99917, 756.76f, 2401.41f, -60.91f, 1.06f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);
            //    targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
            return false;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_q40379AI(go);
    }
};

//243335
class go_q39050 : public GameObjectScript
{
public:
    go_q39050() : GameObjectScript("go_q39050") { }

    struct go_q39050AI : public GameObjectAI
    {
        go_q39050AI(GameObject* go) : GameObjectAI(go)
        {

        }

        enum data
        {
            QUEST = 39050,
            SPELL = 188539,
            CREDIT = 100722,
        };

        bool GossipHello(Player* player) override
        {
            if (player->GetQuestStatus(QUEST) == QUEST_STATUS_INCOMPLETE)
            {
                player->KilledMonsterCredit(CREDIT);
                player->CastSpell(player, SPELL, false);
            }
            return false;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_q39050AI(go);
    }
};

// 99915
// FELSORIA_SHIVARRA_SACRIFICE_V1
// FELSORIA_PERSONAL_SACRIFICE_SHIVARRA_V1
class spell_felsoria_shivarra_sacrifice_v1 : public SpellScriptLoader
{
public:
    spell_felsoria_shivarra_sacrifice_v1() : SpellScriptLoader("spell_felsoria_shivarra_sacrifice_v1") { }
    class sacrifice_SpellScript : public SpellScript
    {
        PrepareSpellScript(sacrifice_SpellScript);
        ObjectGuid _mysticGuid;
        void TraceStage(char const* stage)
        {
            Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
            auto target = GetExplTargetUnit();
            TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "SHIVARRA_TRACE: stage=%s casterPlayer=%u guid=%u targetEntry=%u targetCreature=%u finishSuccess=%u map=%u quest=%u credit=%u",
                stage, uint32(player != nullptr), player ? uint32(player->GetGUIDLow()) : 0,
                target ? uint32(target->GetEntry()) : 0, uint32(target && target->ToCreature() != nullptr),
                uint32(GetSpell()->FelsoriaFinishedSuccessfully()), player ? player->GetMapId() : 0,
                player ? uint32(player->GetQuestStatus(38765)) : 0,
                player ? uint32(player->GetQuestObjectiveData(38765, 99915)) : 0);
        }
        bool Load() override
        {
            _mysticGuid = ObjectGuid::Empty;
            if (Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr)
            {
                _mysticGuid = player->GetAnyDataContainer().GetValue<ObjectGuid>("Felsoria.ShivarraSacrifice.PendingTarget", ObjectGuid::Empty);
                player->GetAnyDataContainer().Remove("Felsoria.ShivarraSacrifice.PendingTarget");
                Creature* bound = ObjectAccessor::GetCreature(*player, _mysticGuid);
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "SHIVARRA_TRACE: binding player=%u entry=%u found=%u",
                    uint32(player->GetGUIDLow()), bound ? bound->GetEntry() : 0, uint32(bound != nullptr));
            }
            TraceStage("load");
            return true;
        }
        void BeforeStart() { TraceStage("before-start"); }
        void Launched() { TraceStage("on-cast"); }
        void AfterLaunch() { TraceStage("after-cast"); }
        void FinishSacrifice()
        {
            Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
            if (!GetSpell()->FelsoriaFinishedSuccessfully() || !player || player->GetMapId() != 1481)
                return;
            Creature* original = ObjectAccessor::GetCreature(*player, _mysticGuid);
            if (!original || original->GetEntry() != 99915 || !original->isAlive() ||
                player->GetQuestStatus(38765) != QUEST_STATUS_INCOMPLETE ||
                player->GetQuestObjectiveData(38765, 99915))
                return;
            // Only this viewer receives dead-looking fields for the original GUID.
            // Never mutate shared health/death/flags and never create a clone.
            uint32 mask = player->GetAnyDataContainer().GetValue<uint32>("Felsoria.PersonalSacrifice.Mask", 0);
            player->GetAnyDataContainer().Set<uint32>("Felsoria.PersonalSacrifice.Mask", mask | 2u);
            CharacterDatabase.PExecute("REPLACE INTO felsoria_personal_sacrifice (guid, npc_entry) VALUES (%u, %u)",
                uint32(player->GetGUIDLow()), uint32(99915));
            player->KilledMonsterCredit(99915);
            original->SendUpdateToPlayer(player);
            TC_LOG_INFO(LOG_FILTER_WORLDSERVER, "PERSONAL_SACRIFICE: player=%u entry=99915 same NPC GUID, personal death sent",
                uint32(player->GetGUIDLow()));
        }
        void Register() override
        {
            BeforeStartCast += SpellCastFn(sacrifice_SpellScript::BeforeStart);
            OnCast += SpellCastFn(sacrifice_SpellScript::Launched);
            AfterCast += SpellCastFn(sacrifice_SpellScript::AfterLaunch);
            OnFinishCast += SpellCastFn(sacrifice_SpellScript::FinishSacrifice);
        }
    };
    SpellScript* GetSpellScript() const override { return new sacrifice_SpellScript(); }
};

class npc_q38765 : public CreatureScript
{
public:
    npc_q38765() : CreatureScript("npc_q38765") { }

    enum Credit
    {
        QUEST = 38765,
        SPELL = 196724,
        SPELL_1= 196731,
    };
    
    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (player->GetQuestStatus(QUEST) == QUEST_STATUS_INCOMPLETE && !player->GetReqKillOrCastCurrentCount(QUEST, creature->GetEntry()))
        {
            player->ADD_GOSSIP_ITEM_DB(19016, 0, GOSSIP_SENDER_MAIN, 2);
            player->ADD_GOSSIP_ITEM_DB(19016, 1, GOSSIP_SENDER_MAIN, 1);
            player->SEND_GOSSIP_MENU(27770, creature->GetGUID());
        }
        return true;
    }
    
    
    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        switch(action)
        {
            case 1:
                player->ADD_GOSSIP_ITEM_DB(19132, 0, GOSSIP_SENDER_MAIN, 3);  // меня
                player->ADD_GOSSIP_ITEM_DB(19132, 1, GOSSIP_SENDER_MAIN, 4);
                player->SEND_GOSSIP_MENU(27999, creature->GetGUID());
                break;
            case 2:
                player->ADD_GOSSIP_ITEM_DB(19133, 0, GOSSIP_SENDER_MAIN, 5);  // его
                player->ADD_GOSSIP_ITEM_DB(19133, 1, GOSSIP_SENDER_MAIN, 4);
                player->SEND_GOSSIP_MENU(28000, creature->GetGUID());
                break;
            case 4:
                player->ADD_GOSSIP_ITEM_DB(19016, 0, GOSSIP_SENDER_MAIN, 2);
                player->ADD_GOSSIP_ITEM_DB(19016, 1, GOSSIP_SENDER_MAIN, 1);
                player->SEND_GOSSIP_MENU(27770, creature->GetGUID());
                break;
                
            case 3:
                // This alternative sacrifices the player, not the NPC.
                player->GetAnyDataContainer().Set<uint32>("Felsoria.PersonalSacrifice.Mask",
                    player->GetAnyDataContainer().GetValue<uint32>("Felsoria.PersonalSacrifice.Mask", 0) & ~2u);
                CharacterDatabase.PExecute("DELETE FROM felsoria_personal_sacrifice WHERE guid=%u AND npc_entry=99915",
                    uint32(player->GetGUIDLow()));
                creature->CastSpell(player, SPELL, false);
                creature->Kill(player);
                player->KilledMonsterCredit(creature->GetEntry());
                player->SummonCreature(99915, creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetOrientation(), TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);
                //    targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                // talk
                player->CLOSE_GOSSIP_MENU();
                break;
            case 5:
                player->CLOSE_GOSSIP_MENU();
                if (creature->GetEntry() == 99915 && creature->isAlive() &&
                    player->GetMapId() == 1481 &&
                    player->GetQuestStatus(QUEST) == QUEST_STATUS_INCOMPLETE &&
                    !player->GetQuestObjectiveData(QUEST, 99915) &&
                    !player->IsNonMeleeSpellCast(false))
                {
                    sCreatureTextMgr->SendChat(creature, 1, player->GetGUID());
                    player->GetAnyDataContainer().Set<ObjectGuid>("Felsoria.ShivarraSacrifice.PendingTarget", creature->GetGUID());
                    player->CastSpell(creature, SPELL_1, false);
                    player->GetAnyDataContainer().Remove("Felsoria.ShivarraSacrifice.PendingTarget");
                }
                break;
            }
        

        return true;
    }
    
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q38765AI(creature);
    }
    struct npc_q38765AI : public ScriptedAI
    {
        npc_q38765AI(Creature* creature) : ScriptedAI(creature) { }
        
        GuidSet m_player_for_event;
        
        void IsSummonedBy(Unit* summoner) override
        {
            // The sacrifice corpse must not run the living helper summon script.
            if (TempSummon* summon = me->ToTempSummon())
                if (summon->GetSummonType() == TEMPSUMMON_CORPSE_TIMED_DESPAWN)
                    return;
            if (summoner->GetTypeId() == TYPEID_PLAYER)
            {
                me->RemoveAllAuras();
                me->CastSpell(summoner, SPELL, false);
                sCreatureTextMgr->SendChat(me, 2, summoner->GetGUID());
                me->DespawnOrUnsummon(2000);
                me->HandleEmoteCommand(16);
            }
            else 
                me->DespawnOrUnsummon();
        }
        
        void MoveInLineOfSight(Unit* who) override
        {
            if (who->GetTypeId() != TYPEID_PLAYER || !me->IsWithinDistInMap(who, 15.0f))
                return;

            GuidSet::iterator itr = m_player_for_event.find(who->GetGUID());
            if (itr != m_player_for_event.end())
                return;

            if (who->ToPlayer()->GetQuestStatus(38765) != QUEST_STATUS_INCOMPLETE)
                return;
           

            m_player_for_event.insert(who->GetGUID());
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_0, who->GetGUID());
        }
    };
};

//241757
class go_q38765 : public GameObjectScript
{
public:
    go_q38765() : GameObjectScript("go_q38765") { }

    struct go_q38765AI : public GameObjectAI
    {
        go_q38765AI(GameObject* go) : GameObjectAI(go)
        {

        }

        enum data
        {
            QUEST = 38765,
            SPELL = 190851,
            CREDIT = 94407,
        };

        bool GossipHello(Player* player) override
        {
            if (player->GetReqKillOrCastCurrentCount(QUEST, CREDIT))
                return true;

            player->KilledMonsterCredit(CREDIT);
            player->CastSpell(player, SPELL, false);
            return false;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_q38765AI(go);
    }
};

//! 96675
class npc_questgiver_93759 : public CreatureScript
{
public:
    npc_questgiver_93759() : CreatureScript("npc_questgiver_93759") {}
    enum data
    {
        QUEST = 40379,
        QUEST2 = 39050,
        QUEST_03 = 38766,
        CONVERSATION = 735
    };

    bool OnQuestAccept(Player* player, Creature* creature, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_03) // La lecon du parchemin brulant
        {
            Conversation* conversation = new Conversation;
            if (!conversation->CreateConversation(sObjectMgr->GetGenerator<HighGuid::Conversation>()->Generate(), CONVERSATION, player, NULL, *player))
                delete conversation;
        }
        else if (quest->GetQuestId() == QUEST2)
        {
            sCreatureTextMgr->SendChat(creature, TEXT_GENERIC_1, player->GetGUID());
        }

        return true;
    }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_questgiver_93759AI(creature);
    }
    struct npc_questgiver_93759AI : public ScriptedAI
    {
        npc_questgiver_93759AI(Creature* creature) : ScriptedAI(creature)
        {
            DoCast(188485);
        }
        GuidSet m_player_for_event;
        void MoveInLineOfSight(Unit* who) override
        {
            if (who->GetTypeId() != TYPEID_PLAYER || !me->IsWithinDistInMap(who, 20.0f))
                return;

            GuidSet::iterator itr = m_player_for_event.find(who->GetGUID());
            if (itr != m_player_for_event.end())
                return;

            if (who->ToPlayer()->GetQuestStatus(QUEST) != QUEST_STATUS_COMPLETE)
                return;

            m_player_for_event.insert(who->GetGUID());
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_0, who->GetGUID());
        }
    };
};

//93221 Doom Commander Beliash
// FELSORIA_BELIASH_MOVEMENT_TRACE_V1
// Movement/lifecycle repair. Damage values and spell cadence are unchanged
// until the loaded Legion spell data and combat log identify the bad effect.
// FELSORIA_BELIASH_RETREAT_V2
// Ground area 195402 belongs to the retreat, jump spell 196625 needs a destination.
class npc_q93221_beliash : public CreatureScript
{
public:
    npc_q93221_beliash() : CreatureScript("npc_q93221_beliash") { }
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q93221_beliashAI(creature);
    }

    struct npc_q93221_beliashAI : public ScriptedAI
    {
        EventMap events;
        ObjectGuid TyrannaGuid;
        bool retreating = false;
        bool resumeChase = false;
        uint32 retreatTimeout = 0;

        npc_q93221_beliashAI(Creature* creature) : ScriptedAI(creature)
        {
            creature->SetCorpseDelay(30);
            creature->SetRespawnDelay(15);
            SetCombatMovement(true);
        }

        enum data
        {
            QUEST = 38766,
            SPELL_AT_DEATH = 210093,
            CREDIT = 106003,
            SPELL_VOLLEY = 196677,
            SPELL_EXISTING_GROUND = 195402,
            SPELL_RETREAT = 196625
        };

        void RemoveTyranna()
        {
            if (Creature* tyranna = ObjectAccessor::GetCreature(*me, TyrannaGuid))
                tyranna->DespawnOrUnsummon();
            TyrannaGuid = ObjectGuid::Empty;
        }

        void Reset() override
        {
            events.Reset();
            retreating = resumeChase = false;
            retreatTimeout = 0;
            me->RemoveAllAreaObjects();
            RemoveTyranna();
            SetCombatMovement(true);
            if (Creature* tyranna = me->SummonCreature(95048, 1599.33f, 2704.51f, 19.23f, 2.53f))
                TyrannaGuid = tyranna->GetGUID();
        }

        void EnterCombat(Unit* victim) override
        {
            // A pet pull must start the same encounter as a direct player pull.
            Player* player = victim ? victim->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
            if (Creature* tyranna = ObjectAccessor::GetCreature(*me, TyrannaGuid))
            {
                if (player && tyranna->AI())
                    tyranna->AI()->Talk(TEXT_GENERIC_0, player->GetGUID());
                tyranna->DespawnOrUnsummon(5000);
            }
            events.RescheduleEvent(EVENT_1, 3000);
            events.RescheduleEvent(EVENT_2, 2000);
            events.RescheduleEvent(EVENT_3, 8000);
        }

        void EnterEvadeMode() override
        {
            events.Reset();
            retreating = resumeChase = false;
            retreatTimeout = 0;
            me->RemoveAllAreaObjects();
            // Clears combat/threat and returns home; the base invokes Reset().
            ScriptedAI::EnterEvadeMode();
        }

        void JustDied(Unit* killer) override
        {
            events.Reset();
            retreating = resumeChase = false;
            retreatTimeout = 0;
            me->RemoveAllAreaObjects();
            RemoveTyranna();
            Player* player = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
            if (player)
                Talk(TEXT_GENERIC_1, player->GetGUID());
            std::list<Player*> players;
            me->GetPlayerListInGrid(players, 60.0f);
            for (Player* plr : players)
                if (plr->GetQuestStatus(QUEST) == QUEST_STATUS_INCOMPLETE)
                {
                    plr->KilledMonsterCredit(CREDIT);
                    plr->CastSpell(plr, SPELL_AT_DEATH, true);
                }
        }

        bool StartRetreat()
        {
            Unit* victim = me->getVictim();
            if (!victim || me->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED))
                return false;
            // Away from victim first, then sideways if a wall or cliff blocks it.
            float away = me->GetAngle(victim) - me->GetOrientation() + 3.14159265f;
            float const offsets[] = {0.0f, 0.78539816f, -0.78539816f, 1.57079633f, -1.57079633f};
            Position home = me->GetHomePosition();
            for (float offset : offsets)
            {
                Position dest;
                me->GetFirstCollisionPosition(dest, 12.0f, away + offset);
                float distance = me->GetExactDist(&dest);
                float dz = dest.GetPositionZ() - me->GetPositionZ();
                // Positive comparisons reject non-finite coordinates as well.
                if (!(distance >= 4.0f && distance <= 16.0f && dz >= -3.0f && dz <= 3.0f &&
                    home.GetExactDist(&dest) <= 45.0f) ||
                    !me->IsWithinLOS(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ()))
                    continue;
                // Leave the blaze at the departure point, then jump with a real destination.
                retreating = true;
                retreatTimeout = 4000;
                me->CastSpell(me, SPELL_EXISTING_GROUND, true);
                me->CastSpell(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), SPELL_RETREAT, true);
                return true;
            }
            // Do not create a stationary pile of damage when there is no safe landing point.
            return false;
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type == EFFECT_MOTION_TYPE && id == SPELL_RETREAT && retreating)
            {
                retreating = false;
                retreatTimeout = 0;
                resumeChase = true;
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;
            events.Update(diff);
            if (retreating)
            {
                if (retreatTimeout > diff)
                {
                    retreatTimeout -= diff;
                    return;
                }
                retreating = false;
                retreatTimeout = 0;
                resumeChase = true;
            }
            if (resumeChase)
            {
                resumeChase = false;
                me->GetMotionMaster()->MoveChase(me->getVictim());
            }
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
            if (uint32 id = events.ExecuteEvent())
            {
                switch (id)
                {
                    case EVENT_1:
                        DoCast(SPELL_VOLLEY);
                        events.RescheduleEvent(EVENT_1, 17000);
                        break;
                    case EVENT_2:
                        Talk(TEXT_GENERIC_0);
                        break;
                    case EVENT_3:
                        events.RescheduleEvent(EVENT_3, StartRetreat() ? 12000 : 2000);
                        if (retreating)
                            return;
                        break;
                }
            }
            DoMeleeAttackIfReady();
        }
    };
};
//96441 Fel Lord Caza
// FELSORIA_CAZA_HELPER_V2: spawn-scoped combat fix and explicit departure failure.
namespace FelsoriaCazaHelperV1
{
    char const* const ActiveKey = "Felsoria.CazaHelper.Departing";
    bool Done(Player* player)
    {
        return player->GetQuestRewardStatus(39495) || player->GetQuestObjectiveData(39495, 106014) != 0;
    }
    bool IsHelper(Creature* npc)
    {
        return npc && npc->GetEntry() == 101790 && npc->GetDBTableGUIDLow() == 368164;
    }
    void PrepareCombat(Creature* npc)
    {
        if (!IsHelper(npc))
            return;
        // The uploaded creature row 368164 has 0x20008300, including 0x200.
        // Only this persistent helper loses NPC immunity; player immunity stays.
        npc->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
        npc->SetReactState(REACT_DEFENSIVE);
    }
    uint32 WalkOut(Player* player, Creature* npc)
    {
        if (!IsHelper(npc) || !npc->IsAlive() || !npc->GetTransGUID().IsEmpty())
            return 0;
        // Existing npc_q39495 exit coordinate, reached only through a complete navmesh path.
        PathGenerator path(npc);
        // Remove the v1's extra 220-yard cap; retain the generator's own limit.
        bool calculated = path.CalculatePath(1223.62f, 1649.79f, 101.29f, false);
        uint32 type = uint32(path.GetPathType());
        if (!calculated || type != uint32(PATHFIND_NORMAL) || path.GetPath().size() < 2)
        {
            TC_LOG_ERROR(LOG_FILTER_WORLDSERVER,
                "CAZA_HELPER_V2 PATH_FAIL calculated=%u type=%u points=%u start=%.3f,%.3f,%.3f dest=1223.620,1649.790,101.290",
                uint32(calculated), type, uint32(path.GetPath().size()),
                npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ());
            return 0;
        }
        Movement::MoveSplineInitArgs args;
        args.path = path.GetPath();
        args.path[0] = G3D::Vector3(npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ());
        args.velocity = 7.0f;
        args.HasVelocity = true;
        args.walk = false;
        args.initialOrientation = npc->GetOrientation();
        args.splineId = Movement::splineIdGen.NewId();
        args.TransformForTransport = false;
        args.flags.smoothGroundPath = false;
        if (!args.Validate())
        {
            TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "CAZA_HELPER_V2 SPLINE_FAIL reason=validation");
            return 0;
        }
        Movement::MoveSpline spline;
        spline.Initialize(args);
        if (spline.Duration() <= 0 || spline.Duration() > 45000)
        {
            TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "CAZA_HELPER_V2 SPLINE_FAIL reason=duration ms=%d", spline.Duration());
            return 0;
        }
        WorldPackets::Movement::MonsterMove packet;
        packet.MoverGUID = npc->GetGUID();
        packet.Pos = npc->GetPosition();
        packet.InitializeSplineData(spline);
        player->SendDirectMessage(packet.Write());
        TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "CAZA_HELPER_V2 DEPART_START points=%u ms=%d",
            uint32(args.path.size()), spline.Duration());
        return uint32(spline.Duration());
    }
    void Depart(Player* player, ObjectGuid guid)
    {
        player->AddDelayedEvent(3000, [player, guid]()
        {
            uint32 duration = 0;
            if (player->GetMapId() == 1481 && Done(player))
                if (Creature* npc = ObjectAccessor::GetCreature(*player, guid))
                    duration = WalkOut(player, npc);
            if (!duration)
            {
                // Value 2 means failed departure, not completed departure.
                // The existing Object.cpp guard keeps this player from being silently hidden.
                if (player->GetMapId() == 1481 && Done(player))
                {
                    player->GetAnyDataContainer().Set<uint32>(ActiveKey, 2);
                    TC_LOG_ERROR(LOG_FILTER_WORLDSERVER,
                        "CAZA_HELPER_V2 DEPART_FAILED helper kept visible; see preceding PATH_FAIL/SPLINE_FAIL");
                }
                else
                    player->GetAnyDataContainer().Set<uint32>(ActiveKey, 0);
                player->UpdateObjectVisibility();
                return;
            }
            player->AddDelayedEvent(duration + 200, [player, guid]()
            {
                player->GetAnyDataContainer().Set<uint32>(ActiveKey, 0);
                if (player->GetMapId() == 1481 && Done(player))
                    if (Creature* npc = ObjectAccessor::GetCreature(*player, guid))
                        if (IsHelper(npc))
                            npc->DestroyForPlayer(player);
                player->UpdateObjectVisibility();
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "CAZA_HELPER_V2 DEPART_END");
            });
        });
    }
}

class npc_q39495_caza : public CreatureScript
{
public:
    npc_q39495_caza() : CreatureScript("npc_q39495_caza") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q39495_cazaAI(creature);
    }

    struct npc_q39495_cazaAI : public ScriptedAI
    {
        EventMap events;
        ObjectGuid playerGuid;

        npc_q39495_cazaAI(Creature* creature) : ScriptedAI(creature)
        {
            creature->SetCorpseDelay(30);
            creature->SetRespawnDelay(15);
        }

        enum data
        {
            QUEST = 39495,
            SPELL_AT_DEATH = 210107,
            CREDIT = 106014,
            SPELL_1 = 197002, // 7
            SPELL_2 = 196876, // 16 (+ talk)
            SPELL_3 = 197180,
            
        };

        void EnterCombat(Unit* victim) override
        {
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_0, victim->GetGUID());
            events.RescheduleEvent(EVENT_1, 7000);
            events.RescheduleEvent(EVENT_2, 15000);
            events.RescheduleEvent(EVENT_3, 9000);

            RememberPlayer(victim);
            // Prefer the persistent helper from the screenshot, not an entrance-scene summon.
            std::list<Creature*> helpers;
            GetCreatureListWithEntryInGrid(helpers, me, 101790, 30.0f);
            Creature* helper = nullptr;
            for (Creature* candidate : helpers)
                if (FelsoriaCazaHelperV1::IsHelper(candidate) && candidate->IsAlive())
                {
                    helper = candidate;
                    break;
                }
            if (!helper)
                helper = me->FindNearestCreature(101790, 20.0f, true);
            if (helper)
            {
                FelsoriaCazaHelperV1::PrepareCombat(helper);
                helper->AI()->AttackStart(me);
                if (FelsoriaCazaHelperV1::IsHelper(helper))
                    TC_LOG_ERROR(LOG_FILTER_WORLDSERVER,
                        "CAZA_HELPER_V2 COMBAT_START victim_is_caza=%u immune_npc=%u distance=%.3f",
                        uint32(helper->getVictim() == me),
                        uint32(helper->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC)),
                        helper->GetDistance(me));
            }
            else
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "CAZA_HELPER_V2 COMBAT_FAIL no living helper within search range");
        }

        void Reset() override
        {
            events.Reset();
            playerGuid = ObjectGuid::Empty;
        }

        void RememberPlayer(Unit* source)
        {
            if (!source || !playerGuid.IsEmpty())
                return;
            if (Player* player = source->GetCharmerOrOwnerPlayerOrPlayerItself())
                if (player->GetQuestStatus(QUEST) == QUEST_STATUS_INCOMPLETE)
                    playerGuid = player->GetGUID();
        }

        void DamageTaken(Unit* attacker, uint32& /*damage*/, DamageEffectType /*type*/) override
        {
            RememberPlayer(attacker);
        }
        
        void JustDied(Unit* killer) override
        {
            Player* player = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
            // The helper has no player owner: recover the actual player who engaged Caza.
            if (!player || player->GetQuestStatus(QUEST) != QUEST_STATUS_INCOMPLETE)
                player = ObjectAccessor::GetPlayer(*me, playerGuid);
            if (!player || player->GetMapId() != me->GetMapId() || player->GetDistance(me) > 100.0f ||
                player->GetQuestStatus(QUEST) != QUEST_STATUS_INCOMPLETE)
                return;
            ObjectGuid helperGuid = ObjectGuid::Empty;
            std::list<Creature*> helpers;
            GetCreatureListWithEntryInGrid(helpers, me, 101790, 40.0f);
            for (Creature* helper : helpers)
                if (FelsoriaCazaHelperV1::IsHelper(helper) && helper->IsAlive())
                {
                    helperGuid = helper->GetGUID();
                    break;
                }
            player->GetAnyDataContainer().Set<uint32>(FelsoriaCazaHelperV1::ActiveKey, helperGuid.IsEmpty() ? 0 : 1);
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_2, player->GetGUID());
            player->KilledMonsterCredit(CREDIT);
            player->CastSpell(player, SPELL_AT_DEATH, false);
            if (!helperGuid.IsEmpty())
                FelsoriaCazaHelperV1::Depart(player, helperGuid);
            else
                player->UpdateObjectVisibility();
        }
        
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim() || me->HasUnitState(UNIT_STATE_CASTING))
                return;

            events.Update(diff);
            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_1:
                        DoCast(SPELL_1);
                        events.RescheduleEvent(EVENT_1, 12000);
                        break;
                    case EVENT_2:
                    {
                        std::list<Creature*> targets;
                        GetCreatureListWithEntryInGrid(targets, me, 100061, 50.0f);
                        std::list<Creature*>::const_iterator itr = targets.begin();
                        if (!targets.empty())
                        {
                            std::advance(itr, urand(0, targets.size() - 1));

                            if (Creature* target = (*itr))
                                if (target->IsInWorld())
                                    me->CastSpell(target, SPELL_2);
                        }
                        Talk(1);
                        events.RescheduleEvent(EVENT_2, 15000);
                    }
                        break;   
                    case EVENT_3:
                        DoCast(SPELL_3);
                        events.RescheduleEvent(EVENT_3, 9000);
                        break;
                }                        
            }
            DoMeleeAttackIfReady();
        }
    };
};

// 243965 243968 243967
// Q: 38727
class go_q38727 : public GameObjectScript
{
public:
    go_q38727() : GameObjectScript("go_q38727") { }
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_q38727AI(go);
    }

    struct go_q38727AI : public GameObjectAI
    {
        go_q38727AI(GameObject* go) : GameObjectAI(go)
        {

        }

        enum data
        {
            QUEST = 38727,
            CREDIT = 94407,
        };

        bool GossipHello(Player* player) override
        {
            uint32 credit1 = 0;
            uint32 credit2 = 0;

            switch (go->GetEntry())
            {
                case 243965:
                    credit1 = 93762;
                    credit2 = 96692;
                    if (player->GetQuestObjectiveData(QUEST, credit1))
                        return true;
                    
                    if (Creature* t = go->FindNearestCreature(93762, 50.0f))
                    {
                        t->AddToHideList(player->GetGUID());
                        t->DestroyForPlayer(player);
                    }
                    
                    if (Creature* targ = player->SummonCreature(93762, 1800.39f, 1569.82f, 87.04f, 2.61f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                      //  targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                        targ->DespawnOrUnsummon(20000);
                    }
                    
                    if (Creature* targ = go->SummonCreature(96503, 1838.30f, 1527.21f, 87.45f, 2.20f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                      //  targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                        targ->GetMotionMaster()->MovePath(10267118, false);
                        targ->DespawnOrUnsummon(10000);
                    }
                    if (Creature* targ = go->SummonCreature(96561, 1850.74f, 1533.68f, 91.67f, 2.45f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                       // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                        targ->GetMotionMaster()->MovePoint(0, 1832.96f, 1548.18f, 88.65f);
                        targ->DespawnOrUnsummon(10000);
                    }
                    if (Creature* targ = go->SummonCreature(96503, 1856.08f, 1554.44f, 94.00f, 2.88f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                      //  targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                        targ->GetMotionMaster()->MovePath(10267119, false);
                        targ->DespawnOrUnsummon(10000);
                    }
                    if (Creature* targ = go->SummonCreature(96503, 1848.49f, 1579.76f, 89.97f, 3.55f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                       // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                        targ->GetMotionMaster()->MovePath(10267120, false);
                        targ->DespawnOrUnsummon(10000);
                    }
                    break;
                case 243968:
                {
                    credit1 = 96732;
                    credit2 = 96734;
                    if (player->GetQuestObjectiveData(QUEST, credit1))
                        return true;
                    
                    if (Creature* t = go->FindNearestCreature(96732, 50.0f))
                    {
                        t->AddToHideList(player->GetGUID());
                        t->DestroyForPlayer(player);
                    }
                    
                    if (Creature* targ = player->SummonCreature(96732, 1382.39f, 1452.39f, 37.0f, 1.22061f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                       // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                        targ->DespawnOrUnsummon(6000);
                    }
                    if (Creature* targ = player->SummonCreature(96562, 1342.57f, 1421.92f, 39.34f, 0.33f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                       // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                      //  targ->CastSpell(targ, 114943);
                        targ->GetMotionMaster()->MovePath(10267114, false);
                        targ->DespawnOrUnsummon(9000);
                    }
                    if (Creature* targ = player->SummonCreature(96562, 1350.38f, 1410.88f, 39.14f, 0.87f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                      //  targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                     //   targ->CastSpell(targ, 114943);
                        targ->GetMotionMaster()->MovePath(10267115, false);
                        targ->DespawnOrUnsummon(9000);
                    }
                    if (Creature* targ = player->SummonCreature(96562, 1354.43f, 1402.99f, 39.50f, 0.96f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                       // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                      //  targ->CastSpell(targ, 114943);
                        targ->GetMotionMaster()->MovePath(10267116, false);
                        targ->DespawnOrUnsummon(9000);
                    }
                }
                    break;
                case 243967:
                {
                    credit1 = 96731;
                    credit2 = 96733;
                    
                    if (player->GetQuestObjectiveData(QUEST, credit1))
                        return true;
                    
                    if (Creature* t = go->FindNearestCreature(96731, 100.0f))
                    {
                        t->AddToHideList(player->GetGUID());
                        t->DestroyForPlayer(player);
                    }
                    
                    if (Creature* targ = player->SummonCreature(96731, 1524.58f, 1248.48f, 70.8699f, 1.72f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                      //  targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                        targ->GetMotionMaster()->MovePath(10267117, false);
                        targ->DespawnOrUnsummon(12000);
                    }
                    if (Creature* targ = player->SummonCreature(96563, 1538.09f, 1212.63f, 71.13f, 2.07f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                       // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                        targ->DespawnOrUnsummon(12000);
                        targ->CastSpell(targ, 191537);
                    }
                    if (Creature* targ = player->SummonCreature(96564, 1524.91f, 1212.91f, 71.02f, 1.38f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                    {
                       // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                        targ->DespawnOrUnsummon(12000);
                        targ->CastSpell(targ, 191537);
                    }
                }
                    break;
            }

            player->KilledMonsterCredit(credit1);
            player->KilledMonsterCredit(credit2); //Hack. Special event.
            return false;
        }
    };
};

//103429
class npc_q40222_progres1 : public CreatureScript
{
public:
    npc_q40222_progres1() : CreatureScript("npc_q40222_progres1") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q40222_progres1AI(creature);
    }

    struct npc_q40222_progres1AI : public ScriptedAI
    {
        npc_q40222_progres1AI(Creature* creature) : ScriptedAI(creature)
        {
            SetCanSeeEvenInPassiveMode(true);
        }

        void Reset() override
        {
            me->SetReactState(REACT_PASSIVE);
        }


        enum data
        {
            CREDIT = 103429,
            QUEST = 40222,
        };
        void MoveInLineOfSight(Unit* who) override
        {
            if (who->GetTypeId() != TYPEID_PLAYER || !me->IsWithinDistInMap(who, 50.0f))
                return;

            if (who->ToPlayer()->GetQuestObjectiveData(QUEST, CREDIT))
                return;

            who->ToPlayer()->KilledMonsterCredit(CREDIT);
        }
    };
};
//98986 Prolifica
class npc_q39495_prolifica : public CreatureScript
{
public:
    npc_q39495_prolifica() : CreatureScript("npc_q39495_prolifica") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q39495_prolificaAI(creature);
    }

    //! Scripted_NoMovementAI HACK!
    struct npc_q39495_prolificaAI : public Scripted_NoMovementAI
    {
        EventMap events;
        ObjectGuid playerGuid;

        npc_q39495_prolificaAI(Creature* creature) : Scripted_NoMovementAI(creature)
        {
            creature->SetCorpseDelay(30);
            creature->SetRespawnDelay(15);
            if(GameObject* gob = me->FindNearestGameObject(GO, 100.0f))
                gob->SetPhaseMask(1, true);  // invis
        }

        enum data
        {
            GO = 245169,
            SPELL_1 = 197217,
            SPELL_2 = 197240,
        };
        
        void Reset() override
        {
            events.Reset();
            if(GameObject* gob = me->FindNearestGameObject(GO, 100.0f))
                gob->SetPhaseMask(1, true);  // invis
        }

        void EnterCombat(Unit* victim) override
        {
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_0, victim->GetGUID());
            if(GameObject* gob = me->FindNearestGameObject(GO, 100.0f))
                gob->SetPhaseMask(2, true);  // invis
            events.RescheduleEvent(EVENT_1, 13000);
            events.RescheduleEvent(EVENT_2, 5000); // 20
        }

        void JustDied(Unit* killer) override
        {
            Player *pl = killer->ToPlayer();
            if (!pl)
                return;
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_2, pl->GetGUID());
            
            Conversation* conversation = new Conversation;
            if (!conversation->CreateConversation(sObjectMgr->GetGenerator<HighGuid::Conversation>()->Generate(), 585, pl, NULL, *pl))
                delete conversation;
            if (Creature* tr = pl->SummonCreature(103432, 1807.34f, 1245.43f, 87.27f, 4.85f, TEMPSUMMON_MANUAL_DESPAWN, 0, pl->GetGUID(), NULL))
            {
              //  tr->AddPlayerInPersonnalVisibilityList(pl->GetGUID());
                tr->GetMotionMaster()->MovePoint(0, 1814.97f, 1211.79f, 83.19f);
            }
            if (Creature* tr = pl->SummonCreature(103432, 1805.83f, 1241.35f, 87.03f, 4.85f, TEMPSUMMON_MANUAL_DESPAWN, 0, pl->GetGUID(), NULL))
            {
              //  tr->AddPlayerInPersonnalVisibilityList(pl->GetGUID());
                tr->GetMotionMaster()->MovePoint(0, 1808.91f, 1204.31f, 82.97f);
            }
            if (Creature* tr = pl->SummonCreature(96278, 1809.17f, 1238.59f, 85.88f, 4.85f, TEMPSUMMON_MANUAL_DESPAWN, 0, pl->GetGUID(), NULL))
            {
              //  tr->AddPlayerInPersonnalVisibilityList(pl->GetGUID());
                tr->GetMotionMaster()->MovePoint(0, 1811.69f, 1195.42f, 80.83f);
            }

        }
        
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim() || me->HasUnitState(UNIT_STATE_CASTING))
                return;

            events.Update(diff);
            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_1:
                        DoCast(SPELL_1);
                        events.RescheduleEvent(EVENT_1, 13000);
                        break;
                    case EVENT_2:
                        DoCast(SPELL_2);
                        events.RescheduleEvent(EVENT_2, 20000);
                        break;
                }
            }
            DoMeleeAttackIfReady();
        }
    };
};

//93802 tyranna
class npc_q38728_tyranna : public CreatureScript
{
public:
    npc_q38728_tyranna() : CreatureScript("npc_q38728_tyranna") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q38728_tyrannaAI(creature);
    }

    struct npc_q38728_tyrannaAI : public ScriptedAI
    {
        EventMap events;
        ObjectGuid playerGuid;
        
        uint32 healthPct;
        SummonList summons;
        uint32 count;
        std::unordered_set<uint32> currentPlayers;

        npc_q38728_tyrannaAI(Creature* creature) : ScriptedAI(creature), healthPct(0), summons(me), count(0)
        {
            creature->SetCorpseDelay(30);
            creature->SetRespawnDelay(15);
        }

        enum data
        {
            SPELL_1 = 197627,  // 8
            SPELL_2 = 197414,  // phase -35%
            SPELL_QUEENS_BITE = 197486,
            NPC_ADD = 100333,
            
        };
        
        void Reset() override
        {
            healthPct = 65;
            summons.DespawnAll();
            events.Reset();
            count = 0;
            currentPlayers.clear();
        }
        
        void JustSummoned(Creature* summon) override
        {
            summons.Summon(summon);
            DoZoneInCombat(summon, 30.0f);
        }

        void EnterCombat(Unit* victim) override
        {
            events.RescheduleEvent(EVENT_1, 8000);
            events.RescheduleEvent(EVENT_4, 10000);
        }

        void AttackedBy(Unit* attacker) override
        {
            // for every player in combat with this boss add 800k HP
            if (attacker->IsPlayer() && currentPlayers.count(attacker->GetGUIDLow()) == 0)
            {
                currentPlayers.insert(attacker->GetGUIDLow());
                me->SetMaxHealth(me->GetMaxHealth() + 800000);
                me->SetHealth(me->GetHealth() + 800000);
            }
        }

        void JustDied(Unit* killer) override
        {
            Talk(3);

            // remove Queen's Bite Transformation if any
            me->CastSpell(me, 208121, true);

            //TODO: Threat does not properly reset for the npc demon hunters due to the faction change
        }
        
        void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType dmgType) override
        {
            if (me->HasAura(SPELL_2))
                damage = 0;

            if (me->HealthBelowPct(healthPct))
            {
                if (healthPct == 65)
                    healthPct -= 30;
                else 
                    healthPct = 0;
                
                events.RescheduleEvent(EVENT_2, 500);
                events.CancelEvent(EVENT_1);
                events.CancelEvent(EVENT_4);
            }
        }
        
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;            

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_1:
                        DoCast(SPELL_1);
                        events.RescheduleEvent(EVENT_1, 8000);
                        break;
                    case EVENT_2:
                        me->SetReactState(REACT_PASSIVE);
                        DoCast(SPELL_2);
                        Talk(1);
                        Talk(2);
                        count = 0;
                        events.RescheduleEvent(EVENT_3, 3000);
                        events.RescheduleEvent(EVENT_1, 27000);
                        events.RescheduleEvent(EVENT_4, 23000);
                        break;
                    case EVENT_3:
                        count++;
                        
                        for (int8 i = 0; i < 6; i++)
                            me->SummonCreature(NPC_ADD, me->GetPositionX()+irand(-10, 10), me->GetPositionY()+irand(-10, 10), me->GetPositionZ());
                        
                        if (count < 3)
                            events.RescheduleEvent(EVENT_3, 5000);
                        else
                            me->SetReactState(REACT_DEFENSIVE);
                        break;
                    case EVENT_4:
                        events.RescheduleEvent(EVENT_4, 55000);
                        Talk(0);

                        std::list<Unit*> targetList;
                        me->GetAttackableUnitListInRange(targetList, 20.0f);
                        targetList.remove_if([](WorldObject* target)
                        {
                            if (target->GetEntry() == 97244)
                                return false;
                            if (target->GetEntry() == 97959)
                                return false;
                            if (target->GetEntry() == 97962)
                                return false;
                            if (target->GetEntry() == 98712)
                                return false;

                            return true;
                        });

                        if (!targetList.empty())
                        {
                            std::vector<Unit*> targetVector(targetList.begin(), targetList.end());
                            targetVector[urand(0, targetVector.size() - 1)]->CastSpell(me, SPELL_QUEENS_BITE, true);
                        }

                        break;
                }
            }

            DoMeleeAttackIfReady();
        }
    };
};

class spell_legion_197486 : public SpellScriptLoader
{
public:
    spell_legion_197486() : SpellScriptLoader("spell_legion_197486") { }

    class spell_legion_197486_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_legion_197486_SpellScript);

        void RemoveVehicleEnter()
        {
            if (Unit* caster = GetCaster())
                if (Creature* tyranna = caster->FindNearestCreature(93802, 150.0f, true))
                    tyranna->RemoveAura(197493);
        }

        void AddVehicleEnter(SpellEffIndex /*p_EffIndex*/)
        {
            if (Unit* caster = GetCaster())
                if (Creature* tyranna = caster->FindNearestCreature(93802, 150.0f, true))
                    caster->CastSpell(tyranna, 197493, true);
        }

        void Register() override
        {
            //TODO: this is still a mess...
            //OnEffectHitTarget += SpellEffectFn(spell_legion_197486_SpellScript::AddVehicleEnter, EFFECT_0, SPELL_EFFECT_KNOCK_BACK);
            //AfterHit += SpellHitFn(spell_legion_197486_SpellScript::RemoveVehicleEnter);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_legion_197486_SpellScript();
    }
};

class spell_legion_208121 : public SpellScriptLoader
{
public:
    spell_legion_208121() : SpellScriptLoader("spell_legion_208121") { }

    class spell_legion_208121_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_legion_208121_SpellScript);

        //TODO: debug this...
        void CorrectTargetsEff0(std::list<WorldObject*>& targets)
        {
            targets.remove_if([this](WorldObject* target)
            {
                if (Creature* creature = target->ToCreature())
                    if (creature->HasAura(this->GetSpellInfo()->GetEffect(0)->TriggerSpell))
                        return false;

                return true;
            });
        }

        void CorrectTargetsEff1(std::list<WorldObject*>& targets)
        {
            targets.remove_if([this](WorldObject* target)
            {
                if (Creature* creature = target->ToCreature())
                    if (creature->HasAura(this->GetSpellInfo()->GetEffect(1)->TriggerSpell))
                        return false;

                return true;
            });
        }

        void Register() override
        {
            OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_legion_208121_SpellScript::CorrectTargetsEff0, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
            OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_legion_208121_SpellScript::CorrectTargetsEff1, EFFECT_1, TARGET_UNIT_SRC_AREA_ENTRY);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_legion_208121_SpellScript();
    }
};

class spell_legion_197523 : public SpellScriptLoader
{
public:
    spell_legion_197523() : SpellScriptLoader("spell_legion_197523") { }

    class spell_legion_197523_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_legion_197523_SpellScript);

        void HandleScriptEffect(SpellEffIndex effIndex)
        {
            if (Unit* unit = GetCaster())
            {
                if (unit->GetEntry() == 97244 || unit->GetEntry() == 97959)
                    unit->CastSpell(unit, 197505);
                else
                    unit->CastSpell(unit, 197598);
            }
        }

        void Register() override
        {
            OnEffectHitTarget += SpellEffectFn(spell_legion_197523_SpellScript::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_legion_197523_SpellScript();
    }
};

class spell_legion_197505_197598 : public SpellScriptLoader
{
public:
    spell_legion_197505_197598() : SpellScriptLoader("spell_legion_197505_197598") { }

    class spell_legion_197505_197598_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_legion_197505_197598_AuraScript);

            void OnTick(AuraEffect const* aurEff)
            {
                Unit* caster = GetCaster();
                if(!caster)
                    return;

                //TODO: use dbc values here instead of hardcoded
                //if (caster->GetHealthPct() < aurEff->GetBaseAmount())
                //    caster->RemoveAura(aurEff->GetBase());
                if (caster->GetHealthPct() < 50.0f)
                {
                    caster->RemoveAura(197505);
                    caster->RemoveAura(197598);
                }
            }

            void Register() override
            {
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_legion_197505_197598_AuraScript::OnTick, EFFECT_2, SPELL_AURA_PERIODIC_DUMMY);
            }
        };

    AuraScript* GetAuraScript() const override
    {
        return new spell_legion_197505_197598_AuraScript();
    }
};

//101760
class npc_q38728_progres1 : public CreatureScript
{
public:
    npc_q38728_progres1() : CreatureScript("npc_q38728_progres1") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q38728_progres1AI(creature);
    }

    struct npc_q38728_progres1AI : public ScriptedAI
    {
        npc_q38728_progres1AI(Creature* creature) : ScriptedAI(creature)
        {
            SetCanSeeEvenInPassiveMode(true);
        }

        void Reset() override
        {
            me->SetReactState(REACT_PASSIVE);
        }


        enum data
        {
            CREDIT = 101760,
            QUEST = 38728,
        };
        void MoveInLineOfSight(Unit* who) override
        {
            if (who->GetTypeId() != TYPEID_PLAYER || !me->IsWithinDistInMap(who, 20.0f))
                return;

            if (who->ToPlayer()->GetQuestObjectiveData(QUEST, CREDIT))
                return;

            WorldLocation loc;
            loc.m_mapId = 1481;
            loc.m_positionX = 1467.47f;
            loc.m_positionY = 1412.78f;
            loc.m_positionZ = 243.96f;
            loc.SetOrientation(who->GetOrientation());
            who->ToPlayer()->SetHomebind(loc, 7749);
            who->ToPlayer()->SendBindPointUpdate();

            who->ToPlayer()->KilledMonsterCredit(CREDIT);
        }
    };
};

//245728
class go_q38729 : public GameObjectScript
{
public:
    go_q38729() : GameObjectScript("go_q38729") { }

    struct go_q38729AI : public GameObjectAI
    {
        go_q38729AI(GameObject* go) : GameObjectAI(go)
        {

        }

        enum data
        {
            QUEST = 38729,
            CREDIT = 100651,
        };

        bool GossipHello(Player* player) override
        {
            if (player->GetReqKillOrCastCurrentCount(QUEST, CREDIT))
                return true;

            player->KilledMonsterCredit(CREDIT);
            return false;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_q38729AI(go);
    }
};

// 245045
class go_q39262 : public GameObjectScript
{
public:
    go_q39262() : GameObjectScript("go_q39262") { }

    struct go_q39262AI : public GameObjectAI
    {
        go_q39262AI(GameObject* go) : GameObjectAI(go), ow(nullptr)
        {
            check = false;
            timer = 0;
        }

        bool check;
        uint32 timer;
        Player* ow;
        
        void UpdateAI(uint32 diff) override
        {
            std::list<Player*> list;
            list.clear();
            go->GetPlayerListInGrid(list, 15.0f);
            if (!list.empty())
            {
               for (std::list<Player*>::const_iterator itr = list.begin(); itr != list.end(); ++itr)
               {
                   if ((*itr)->HasAura(188501))
                       (*itr)->KilledMonsterCredit(96437);
               }
            }
            
            if (check)
                if (timer <= diff)
                {
                    check = false;
                    if (ow)
                        go->DestroyForPlayer(ow);
                    go->SetLootState(GO_JUST_DEACTIVATED);
                } else
                    timer -= diff;
        }
        
        void IsSummonedBy(Unit* summoner)
        {
            if (summoner->GetTypeId() == TYPEID_PLAYER)
            {
                ow = summoner->ToPlayer();
                check = true;
                timer = 5500;
            }
            else
                go->SetLootState(GO_JUST_DEACTIVATED);
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_q39262AI(go);
    }
};

// 99916
class npc_q40378_1 : public CreatureScript
{
public:
    npc_q40378_1() : CreatureScript("npc_q40378_1") {}

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q40378_1AI(creature);
    }
    struct npc_q40378_1AI : public ScriptedAI
    {
        npc_q40378_1AI(Creature* creature) : ScriptedAI(creature) 
        {
            check = false;
        }
        
        GuidSet m_player_for_event;
        bool check;
        
        void IsSummonedBy(Unit* summoner) override
        {
            if (summoner->GetTypeId() == TYPEID_PLAYER)
            {
                sCreatureTextMgr->SendChat(me, TEXT_GENERIC_1, summoner->GetGUID());
                check = true;
                me->GetMotionMaster()->MovePoint(1, 913.92f, 2848.63f, -0.69f);
                me->Mount(64385);
                me->DespawnOrUnsummon(10000);
            }
            else 
                me->DespawnOrUnsummon();
        }
        
        void MoveInLineOfSight(Unit* who) override
        {
            if (who->GetTypeId() != TYPEID_PLAYER || !me->IsWithinDistInMap(who, 10.0f))
                return;

            GuidSet::iterator itr = m_player_for_event.find(who->GetGUID());
            if (itr != m_player_for_event.end())
                return;

            if (who->ToPlayer()->GetQuestStatus(40378) != QUEST_STATUS_INCOMPLETE)
                return;
            
            if (check)
                return;

            m_player_for_event.insert(who->GetGUID());
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_0, who->GetGUID());
        }
    };
};


class npc_q40378_2 : public CreatureScript
{
public:
    npc_q40378_2() : CreatureScript("npc_q40378_2") {}

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q40378_2AI(creature);
    }
    struct npc_q40378_2AI : public ScriptedAI
    {
        npc_q40378_2AI(Creature* creature) : ScriptedAI(creature), timer(0)
        {
            check = false;
        }

        uint32 timer;
        bool check;
        
        void IsSummonedBy(Unit* summoner) override
        {
            if (summoner->GetTypeId() == TYPEID_PLAYER)
            {
                timer = 16000;
                check = true;
            }
            else 
                me->DespawnOrUnsummon();
        }
        
        void UpdateAI(uint32 diff) override
        {
            if (check)
            {
                if (timer <= diff)
                {
                    me->SetVisible(true);
                    check = false;
                } else
                    timer -= diff;
            }
        }
        
        void OnSpellClick(Unit* /*clicker*/) override
        {
            me->DespawnOrUnsummon(10000);
        }
    };
};

// 100982
class npc_q40379_1: public CreatureScript
{
public:
    npc_q40379_1() : CreatureScript("npc_q40379_1") {}

    bool OnQuestAccept(Player* player, Creature* creature, Quest const* quest) override
    {
        if (quest->GetQuestId() == 40379) 
            player->SummonCreature(100982, 826.90f, 2758.64f, -30.50f, 1.62f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);
              //  target->AddPlayerInPersonnalVisibilityList(player->GetGUID());
        return true;
    }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q40379_1AI(creature);
    }
    struct npc_q40379_1AI : public ScriptedAI
    {
        npc_q40379_1AI(Creature* creature) : ScriptedAI(creature) {}
        
        
        void IsSummonedBy(Unit* summoner) override
        {
            if (summoner->GetTypeId() == TYPEID_PLAYER)
            {
                sCreatureTextMgr->SendChat(me, 0, summoner->GetGUID());
                me->Mount(64385);
                me->GetMotionMaster()->MovePath(10267113, false);
                me->DespawnOrUnsummon(10000);
            }
            else 
                me->DespawnOrUnsummon();
        }
    };
};


class npc_q38759_1 : public CreatureScript
{
public:
    npc_q38759_1() : CreatureScript("npc_q38759_1") {}

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q38759_1AI(creature);
    }
    struct npc_q38759_1AI : public ScriptedAI
    {
        npc_q38759_1AI(Creature* creature) : ScriptedAI(creature) { }
        
        GuidSet m_player_for_event;
        
        void MoveInLineOfSight(Unit* who) override
        {
            if (auto owner = me->GetAnyOwner())
                if (owner->IsPlayer())
                    return;

            if (who->GetTypeId() != TYPEID_PLAYER || !me->IsWithinDistInMap(who, 15.0f))
                return;

            GuidSet::iterator itr = m_player_for_event.find(who->GetGUID());
            if (itr != m_player_for_event.end())
                return;

            if (who->ToPlayer()->GetQuestStatus(38759) != QUEST_STATUS_INCOMPLETE)
                return;
           

            m_player_for_event.insert(who->GetGUID());
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_0, who->GetGUID());
        }
    };
};

// 99917
class npc_q40379_2 : public CreatureScript
{
public:
    npc_q40379_2() : CreatureScript("npc_q40379_2") {}

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q40379_2AI(creature);
    }
    struct npc_q40379_2AI : public ScriptedAI
    {
        npc_q40379_2AI(Creature* creature) : ScriptedAI(creature) { }
        
        GuidSet m_player_for_event;
        
        void IsSummonedBy(Unit* summoner) override
        {
            if (summoner->GetTypeId() == TYPEID_PLAYER)
            {
                sCreatureTextMgr->SendChat(me, 2, summoner->GetGUID());
                me->Mount(64385);
                me->GetMotionMaster()->MovePoint(0, 825.57f, 2488.19f, -59.78f);
                me->DespawnOrUnsummon(10000);
            }
            else 
                me->DespawnOrUnsummon();
        }
        
        void MoveInLineOfSight(Unit* who) override
        {
            if (who->GetTypeId() != TYPEID_PLAYER || !me->IsWithinDistInMap(who, 15.0f))
                return;

            GuidSet::iterator itr = m_player_for_event.find(who->GetGUID());
            if (itr != m_player_for_event.end())
                return;

            if (who->ToPlayer()->GetQuestStatus(40379) != QUEST_STATUS_INCOMPLETE)
                return;
           

            m_player_for_event.insert(who->GetGUID());
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_0, who->GetGUID());
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_1, who->GetGUID());
        }
    };
};

class npc_96654 : public CreatureScript
{
public:
    npc_96654() : CreatureScript("npc_96654") {}

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_96654AI(creature);
    }
    struct npc_96654AI : public ScriptedAI
    {
        npc_96654AI(Creature* creature) : ScriptedAI(creature) { }
        
        GuidSet m_player_for_event;
                
        void MoveInLineOfSight(Unit* who) override
        {
            if (who->GetTypeId() != TYPEID_PLAYER || !me->IsWithinDistInMap(who, 10.0f))
                return;

            GuidSet::iterator itr = m_player_for_event.find(who->GetGUID());
            if (itr != m_player_for_event.end())
                return;
            m_player_for_event.insert(who->GetGUID());
            sCreatureTextMgr->SendChat(me, TEXT_GENERIC_0, who->GetGUID());
        }
    };
};

// 96436
class npc_96436 : public CreatureScript
{
public:
    npc_96436() : CreatureScript("npc_96436") {}
    
    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        player->CLOSE_GOSSIP_MENU();

        sCreatureTextMgr->SendChat(creature, TEXT_GENERIC_0, player->GetGUID());
        player->KilledMonsterCredit(96436);      

        return true;
    }
    
    bool OnQuestAccept(Player* player, Creature* creature, Quest const* quest) override
    {
        if (quest->GetQuestId() == 39495)
        {
            if (Creature* Jais = player->SummonCreature(96436, creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetOrientation(), TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
            {
              //  Jais->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                Jais->CastSpell(Jais, 194326);
            }
            
            if (Creature* targ = player->SummonCreature(101788, 1267.46f, 1627.08f, 104.10f, 2.66f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
            {
               // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                targ->CastSpell(targ, 194326);
            }
            if (Creature* targ = player->SummonCreature(101787, 1262.82f, 1624.67f, 104.28f, 2.59f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
            {
               // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                targ->CastSpell(targ, 194326);
            }
            if (Creature* targ = player->SummonCreature(101790, 1256.11f, 1623.56f, 103.58f, 2.43f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
            {
              //  targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                targ->CastSpell(targ, 194326);
            }
            if (Creature* targ = player->SummonCreature(101789, 1260.16f, 1641.68f, 100.90f, 3.04f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
            {
              //  targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                targ->CastSpell(targ, 194326);
            }
            if (Creature* targ = player->SummonCreature(101787, 1262.44f, 1646.57f, 100.38f, 3.39f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
            {
               // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                targ->CastSpell(targ, 194326);
                targ->AddDelayedEvent(3800, [targ]()-> void
                {
                    targ->CastSpell(targ, 194366);
                });
            }

            player->SummonCreature(96504, 1270.82f, 1629.88f, 103.63f, 2.80f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);            
            player->SummonCreature(96503, 1260.15f, 1622.65f, 104.51f, 2.54f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);            
            player->SummonCreature(96500, 1264.43f, 1643.56f, 100.36f, 3.09f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);            
            player->SummonCreature(96500, 1262.93f, 1649.17f, 100.10f, 3.39f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);            
            player->SummonCreature(96501, 1265.69f, 1645.30f, 99.90f, 3.22f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL);
            
            if (GameObject* targ = player->SummonGameObject(245045, 1237.15f, 1642.62f,    103.152f, 5.80559f, 0.0f, 0.0f, 0.0f, 0.0f, DAY, player->GetGUID()))
            {
                targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                ObjectGuid playerGuid = player->GetGUID();
                targ->AddDelayedEvent(3800, [targ, playerGuid]() -> void
                {
                    targ->SetLootState(GO_READY);
                    if (auto player = ObjectAccessor::FindPlayer(playerGuid))
                        targ->UseDoorOrButton(10000, false, player);
                });
            }
            
        }

        return true;
    }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_96436AI(creature);
    }
    struct npc_96436AI : public ScriptedAI
    {
        npc_96436AI(Creature* creature) : ScriptedAI(creature) 
        {
            check = false;
            timer = 0;
        }
        
        bool check;
        uint32 timer;
        
        void IsSummonedBy(Unit* summoner) override
        {
            if (summoner->GetTypeId() == TYPEID_PLAYER)
            {
                timer = 5500;
                check = true;
            }
            else 
                me->DespawnOrUnsummon();
        }
        
        void UpdateAI(uint32 diff) override
        {
            if (check)
                if (timer <= diff)
                {
                    Talk(1);
                    me->GetMotionMaster()->MovePoint(0, 1223.62f, 1649.79f, 101.29f);
                    me->DespawnOrUnsummon(4000);
                    check = false;
                } else timer -= diff;
        }

    };
};

// 96501, 96500, 96503, 96504, 101787, 101789, 101790, 101788
class npc_q39495 : public CreatureScript
{
public:
    npc_q39495() : CreatureScript("npc_q39495") {}

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q39495AI(creature);
    }
    struct npc_q39495AI : public ScriptedAI
    {
        npc_q39495AI(Creature* creature) : ScriptedAI(creature)
        {
            check = false;
            timer = 0;
            FelsoriaCazaHelperV1::PrepareCombat(me);
        }
        
        bool check;
        uint32 timer;
        EventMap events;
        
        void IsSummonedBy(Unit* summoner) override
        {
            if (summoner->GetTypeId() == TYPEID_PLAYER)
            {
                timer = 5500;
                check = true;
            }
            else 
                me->DespawnOrUnsummon();
        }
        
        void Reset() override
        {
            events.Reset();
            FelsoriaCazaHelperV1::PrepareCombat(me);
            if (FelsoriaCazaHelperV1::IsHelper(me))
                SetCombatMovement(true);
        }
        void EnterCombat(Unit* who) override
        {
            if (me->GetEntry() == 96501)
            {
                events.RescheduleEvent(EVENT_1, 3000); // 200880
                events.RescheduleEvent(EVENT_2, 11000); // 197744
            }
            if (me->GetEntry() == 101787)
            {
                events.RescheduleEvent(EVENT_3, 34000); // 197641
                events.RescheduleEvent(EVENT_4, 12000); // 197639
            }
                
        }

        void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType dmgType) override
        {
            damage /= 20;
        }

        void UpdateAI(uint32 diff) override
        {
            if (check)
                if (timer <= diff)
                {
                    me->GetMotionMaster()->MovePoint(0, 1223.62f, 1649.79f, 101.29f);
                    me->DespawnOrUnsummon(4000);
                    check = false;
                } else timer -= diff;
                
            if (!UpdateVictim() || me->HasUnitState(UNIT_STATE_CASTING))
                return;            

            events.Update(diff);
            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_1:
                        DoCast(200880);
                        events.RescheduleEvent(EVENT_1, 3000);
                        break;
                    case EVENT_2:
                        DoCast(197744);
                        events.RescheduleEvent(EVENT_2, 11000);
                        break;
                    case EVENT_3:
                        DoCast(197641);
                        events.RescheduleEvent(EVENT_3, 34000); 
                        break;
                    case EVENT_4:
                        DoCast(197639);
                        events.RescheduleEvent(EVENT_4, 12000);
                        break;
                }
            }
            DoMeleeAttackIfReady();
            
        }

    };
};

// 93762 96732 96731
class npc_q38727 : public CreatureScript
{
public:
    npc_q38727() : CreatureScript("npc_q38727") {}

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_q38727AI(creature);
    }
    struct npc_q38727AI : public ScriptedAI
    {
        npc_q38727AI(Creature* creature) : ScriptedAI(creature) 
        {
            check1 = false;
            check2 = false;
            timer = 0;
        }
        
        bool check1;
        bool check2;
        uint32 timer;
        
        void IsSummonedBy(Unit* summoner) override
        {
            if (summoner->GetTypeId() == TYPEID_PLAYER)
            {
                if (me->GetEntry() == 93762)
                {
                    check1 = true;
                    timer = 9000;
                }
                if (me->GetEntry() == 96732)
                {
                    check2 = true;
                    timer = 5000;
                }
            }
            else 
                me->DespawnOrUnsummon();
        }
        
        void UpdateAI(uint32 diff) override
        {
            if (check2)
                if (timer <= diff)
                {
                    DoCast(191454);
                    check2 = false;
                } else timer -= diff;
            if (check1)
                if (timer <= diff)
                {
                    DoCast(191568);
                    DoCast(198908);
                    check1 = false;
                } else timer -= diff;
        }

    };
};

// 93127
class npc_93127 : public CreatureScript
{
public:
    npc_93127() : CreatureScript("npc_93127") {}
    
    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        if (action == 1)
        {
            player->CLOSE_GOSSIP_MENU();

            player->CastSpell(player, 195020, true);
            sCreatureTextMgr->SendChat(creature, TEXT_GENERIC_3, player->GetGUID());  
        }

        return true;
    }
    
    bool OnQuestAccept(Player* player, Creature* creature, Quest const* quest) override
    {
        switch(quest->GetQuestId()) 
        {
            case 38813:
                sCreatureTextMgr->SendChat(creature, TEXT_GENERIC_0, player->GetGUID());  
                break;
            case 38727:
                sCreatureTextMgr->SendChat(creature, TEXT_GENERIC_1, player->GetGUID());  
                break;
            case 39516:
                sCreatureTextMgr->SendChat(creature, TEXT_GENERIC_2, player->GetGUID());  
                break;
            case 39663:
            {
                if (Creature* targ = player->SummonCreature(93127, creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetOrientation(), TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                {
                   // targ->AddPlayerInPersonnalVisibilityList(player->GetGUID());
                    sCreatureTextMgr->SendChat(targ, TEXT_GENERIC_4, player->GetGUID());  
                    targ->Mount(64385);
                    targ->GetMotionMaster()->MovePath(10267121, false);
                    targ->DespawnOrUnsummon(25000);
                } 
                if (Creature* targ = player->SummonCreature(99045, 1454.29f, 1763.44f, 54.52f, 5.56f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                {
                  //  targ->AddPlayerInPersonnalVisibilityList(player->GetGUID()); 
                    targ->Mount(64385);
                    targ->GetMotionMaster()->MovePath(10267121, false);
                    targ->DespawnOrUnsummon(25000);
                } 
                if (Creature* targ = player->SummonCreature(96420, 1453.28f, 1762.33f, 54.52f, 0.26f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                {
                  //  targ->AddPlayerInPersonnalVisibilityList(player->GetGUID()); 
                    targ->Mount(64385);
                    targ->GetMotionMaster()->MovePath(10267121, false);
                    targ->DespawnOrUnsummon(25000);
                } 
                if (Creature* targ = player->SummonCreature(96655, 1458.17f, 1763.06f, 54.52f, 3.16f, TEMPSUMMON_MANUAL_DESPAWN, 0, player->GetGUID(), NULL))
                {
                  //  targ->AddPlayerInPersonnalVisibilityList(player->GetGUID()); 
                    targ->Mount(64385);
                    targ->GetMotionMaster()->MovePath(10267121, false);
                    targ->DespawnOrUnsummon(25000);
                } 
            }
                break;
            case 39262:
            {
                Conversation* conversation = new Conversation;
                if (!conversation->CreateConversation(sObjectMgr->GetGenerator<HighGuid::Conversation>()->Generate(), 486, player, NULL, *player))
                    delete conversation;
            }
                break;
            default:
                break;
        }

        return true;
    }
};

class npc_c100161 : public CreatureScript
{
public:
    npc_c100161() : CreatureScript("npc_c100161") {}

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_c100161AI(creature);
    }

    struct npc_c100161AI : public ScriptedAI
    {
        npc_c100161AI(Creature* creature) : ScriptedAI(creature)
        {
            me->SetReactState(REACT_PASSIVE);
            me->setActive(true);
        }

        uint32 timer = 2000;

        void UpdateAI(uint32 diff) override
        {
            if (timer <= diff)
            {
                auto pos = me->GetPosition();
                me->GetMap()->LoadGrid(pos);
                const auto& players = me->GetMap()->GetPlayers();
                for (auto & playerItr : players)
                    if (auto player = playerItr.getSource())
                        if (player->GetAreaId() == 7705)
                            if (me->GetDistance2d(player) <= 270.0f)
                                if (player->GetQuestStatus(38766) == QUEST_STATE_COMPLETE)
                                    me->CastSpell(player, 191669);

                timer = urand(5000, 10000);
            }
            else
                timer -= diff;

        }

    };
};




// FELSORIA_FELSABER_AFTERCAST_V1
// Spell 200255: reward only after successful acceptance cast.
class spell_felsoria_felsaber_aftercast_v1 : public SpellScriptLoader
{
public:
    spell_felsoria_felsaber_aftercast_v1()
        : SpellScriptLoader("spell_felsoria_felsaber_aftercast_v1") { }

    class spell_felsoria_felsaber_aftercast_v1_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_felsoria_felsaber_aftercast_v1_SpellScript);

        void HandleAfterCast()
        {
            Player* player = GetCaster()->ToPlayer();
            if (!player)
                return;

            // Apprentice Riding: at least 75; never downgrade trained riding.
            if (!player->HasSpell(33388))
                player->learnSpell(33388, false);
            if (player->GetBaseSkillValue(SKILL_RIDING) < 75)
                player->SetSkill(SKILL_RIDING, 1, 75, 75);

            CollectionMgr* collection = player->GetCollectionMgr();
            if (!collection || (!collection->HasMount(200175) && !collection->AddMount(200175)))
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER,
                    "FELSABER_REWARD: collection registration failed for spell 200175; check Mount.db2.");
            if (!player->HasSpell(200175))
                player->learnSpell(200175, false);

            // Apply the mount aura to the accepting player. This is separate
            // from learning the mount and from a second spell-location check.
            // FELSORIA_FELSABER_CLEAR_179665_V2
            // The confirmed DH starter restriction must end when the gift is accepted.
            player->RemoveAura(179665);
            player->RemoveAurasByType(SPELL_AURA_MOUNTED);
            if (player->IsMounted())
                player->Dismount();
            player->AddAura(200175, player);

            if (!player->GetQuestObjectiveData(40378, 101518) &&
                player->GetQuestStatus(40378) == QUEST_STATUS_INCOMPLETE)
                player->KilledMonsterCredit(101518);

            if (player->GetBaseSkillValue(SKILL_RIDING) < 75 ||
                !player->HasAura(200175) || !player->IsMounted())
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER,
                    "FELSABER_REWARD: riding=%u aura=%u mounted=%u; reward incomplete.",
                    uint32(player->GetBaseSkillValue(SKILL_RIDING)),
                    uint32(player->HasAura(200175)), uint32(player->IsMounted()));
        }

        void Register() override
        {
            AfterCast += SpellCastFn(spell_felsoria_felsaber_aftercast_v1_SpellScript::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_felsoria_felsaber_aftercast_v1_SpellScript();
    }
};

// FELSORIA_ILLIDARI_TITLE_V1
class player_felsoria_illidari_title_v1 : public PlayerScript
{
public:
    player_felsoria_illidari_title_v1() : PlayerScript("player_felsoria_illidari_title_v1") { }

    void OnLogin(Player* player, bool firstLogin) override
    {
        if (!player || player->getClass() != CLASS_DEMON_HUNTER)
            return;

        // CharTitles ID is not the title bit/selected-title index.
        CharTitlesEntry const* title = sCharTitlesStore.LookupEntry(458);
        if (!title || title->MaskID < 0 || uint32(title->MaskID) >= KNOWN_TITLES_SIZE * 64)
        {
            static bool reported = false;
            if (!reported)
            {
                reported = true;
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER,
                    "FELSORIA_ILLIDARI_TITLE: CharTitles entry 458 missing or invalid; verify Legion DB2 data. No title fields changed.");
            }
            return;
        }

        bool changed = false;
        if (!player->HasTitle(title))
        {
            player->SetTitle(title);
            changed = true;
        }
        // Existing characters keep their selected title, including "none".
        if (firstLogin && !player->GetUInt32Value(PLAYER_FIELD_PLAYER_TITLE))
        {
            player->SetUInt32Value(PLAYER_FIELD_PLAYER_TITLE, uint32(title->MaskID));
            changed = true;
        }
        if (changed)
            player->SaveToDB();
    }
};

// FELSORIA_CAPTAINS_TRACE_V1: observation only; preserves quest and scene behavior.
class spell_felsoria_captains_trace_v1 : public SpellScriptLoader
{
public:
    spell_felsoria_captains_trace_v1() : SpellScriptLoader("spell_felsoria_captains_trace_v1") { }
    class trace_SpellScript : public SpellScript
    {
        PrepareSpellScript(trace_SpellScript);
        void Observe()
        {
            Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
            if (!player || player->GetMapId() != 1481)
                return;
            uint32 spellId = GetSpellInfo()->Id;
            uint32 entry = spellId == 191315 ? 90247 : spellId == 191400 ? 93693 : 94435;
            uint32 sceneId = spellId == 191315 ? 1079 : spellId == 191400 ? 1094 : 1095;
            Creature* captain = player->FindNearestCreature(entry, 50.0f, true);
            SpellScene const* scene = sSpellMgr->GetSpellScene(sceneId);
            TC_LOG_ERROR(LOG_FILTER_WORLDSERVER,
                "CAPTAINS_TRACE: cast player=%s spell=%u quest=%u credit=%u captain=%u found=%u scene=%u package=%u launched=%u aura=%u pos=%.3f,%.3f,%.3f",
                player->GetGUID().ToString().c_str(), spellId, uint32(player->GetQuestStatus(38813)),
                uint32(player->GetQuestObjectiveData(38813, entry)), entry, uint32(captain != nullptr),
                sceneId, scene ? scene->SceneScriptPackageID : 0,
                uint32(player->HasSceneStatus(sceneId, SCENE_LAUNCH)), uint32(player->HasAura(spellId)),
                player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                SpellEffectInfo const* effect = GetSpellInfo()->GetEffect(i);
                if (!effect || !effect->Effect)
                    continue;
                TC_LOG_ERROR(LOG_FILTER_WORLDSERVER,
                    "CAPTAINS_TRACE: effect spell=%u index=%u type=%u aura=%u misc=%i trigger=%u",
                    spellId, uint32(i), uint32(effect->Effect), uint32(effect->ApplyAuraName),
                    effect->MiscValue, effect->TriggerSpell);
            }
        }
        void Register() override { AfterCast += SpellCastFn(trace_SpellScript::Observe); }
    };
    SpellScript* GetSpellScript() const override { return new trace_SpellScript(); }
};

class scene_felsoria_captains_trace_v1 : public SceneTriggerScript
{
public:
    scene_felsoria_captains_trace_v1() : SceneTriggerScript("scene_felsoria_captains_trace_v1") { }
    bool OnTrigger(Player* player, SpellScene const* scene, std::string event) override
    {
        if (player && scene && player->GetMapId() == 1481)
            TC_LOG_ERROR(LOG_FILTER_WORLDSERVER,
                "CAPTAINS_TRACE: event player=%s scene=%u package=%u event=%s quest=%u",
                player->GetGUID().ToString().c_str(), scene->MiscValue, scene->SceneScriptPackageID,
                event.c_str(), uint32(player->GetQuestStatus(38813)));
        return false; // Existing database event dispatch must still run.
    }
};

// FELSORIA_CAPTAINS_NATIVE_V2: original GUID, personal departure; native scenes preserved.
namespace FelsoriaCaptainsNativeV2
{
    char const* const ActiveKey = "Felsoria.Captains.Native.Active";
    char const* const FailedKey = "Felsoria.Captains.Native.Failed";
    uint32 Bit(uint32 entry) { return entry == 90247 ? 1u : entry == 93693 ? 2u : entry == 94435 ? 4u : 0u; }
    uint64 Spawn(uint32 entry) { return entry == 90247 ? 368098u : entry == 93693 ? 368085u : entry == 94435 ? 368054u : 0u; }
    bool Briefed(Player* player, uint32 entry)
    {
        return player->GetQuestRewardStatus(38813) || player->GetQuestObjectiveData(38813, entry) != 0;
    }
    void Clear(Player* player, uint32 bit)
    {
        uint32 mask = player->GetAnyDataContainer().GetValue<uint32>(ActiveKey, 0);
        player->GetAnyDataContainer().Set<uint32>(ActiveKey, mask & ~bit);
    }
    void Fail(Player* player, uint32 bit)
    {
        uint32 mask = player->GetAnyDataContainer().GetValue<uint32>(FailedKey, 0);
        player->GetAnyDataContainer().Set<uint32>(FailedKey, mask | bit);
        Clear(player, bit);
        player->UpdateObjectVisibility();
    }
    // Native client Path.db2 14979; ordered PathNode/Location coordinates.
    G3D::Vector3 const Ashtongue[] = {
        G3D::Vector3(1417.991333f, 1702.439209f, 54.482819f),
        G3D::Vector3(1414.795166f, 1704.012207f, 54.484940f),
        G3D::Vector3(1409.456665f, 1704.852417f, 54.485592f),
        G3D::Vector3(1400.564209f, 1703.869751f, 54.485592f),
        G3D::Vector3(1391.998291f, 1698.845459f, 54.935802f),
        G3D::Vector3(1383.413208f, 1692.739624f, 56.763901f),
        G3D::Vector3(1371.338501f, 1684.086792f, 58.945171f),
        G3D::Vector3(1359.725708f, 1671.395874f, 61.242702f),
        G3D::Vector3(1351.840332f, 1655.684082f, 61.966995f),
        G3D::Vector3(1340.597290f, 1632.694458f, 61.008938f),
    };
    // Native client Path.db2 15106; ordered PathNode/Location coordinates.
    G3D::Vector3 const Coilskar[] = {
        G3D::Vector3(1426.184082f, 1808.612915f, 61.154934f),
        G3D::Vector3(1440.170166f, 1799.298584f, 57.197666f),
        G3D::Vector3(1454.763916f, 1795.413208f, 54.026451f),
        G3D::Vector3(1469.807251f, 1798.578125f, 52.558475f),
        G3D::Vector3(1482.779541f, 1799.675415f, 51.085072f),
        G3D::Vector3(1500.791626f, 1800.038208f, 47.670479f),
        G3D::Vector3(1520.855957f, 1800.652832f, 45.864590f),
        G3D::Vector3(1535.413208f, 1801.232666f, 44.873413f),
    };

    uint32 Walk(Player* player, Creature* npc, uint32 entry)
    {
        Movement::MoveSplineInitArgs args;
        args.path.push_back(G3D::Vector3(npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ()));
        if (entry == 90247)
            args.path.insert(args.path.end(), std::begin(Ashtongue), std::end(Ashtongue));
        else if (entry == 93693)
            args.path.insert(args.path.end(), std::begin(Coilskar), std::end(Coilskar));
        else
            return 0;
        args.velocity = entry == 90247 ? 7.2f : 7.6f;
        args.HasVelocity = true;
        args.walk = false;
        args.initialOrientation = npc->GetOrientation();
        args.splineId = Movement::splineIdGen.NewId();
        args.TransformForTransport = false;
        args.flags.smoothGroundPath = false;
        if (!args.Validate())
            return 0;
        Movement::MoveSpline spline;
        spline.Initialize(args);
        if (spline.Duration() <= 0 || spline.Duration() > 30000)
            return 0;
        WorldPackets::Movement::MonsterMove packet;
        packet.MoverGUID = npc->GetGUID();
        packet.Pos = npc->GetPosition();
        packet.InitializeSplineData(spline);
        player->SendDirectMessage(packet.Write());
        return uint32(spline.Duration());
    }
    void Start(Player* player, ObjectGuid guid, uint32 entry)
    {
        uint32 bit = Bit(entry);
        Creature* npc = ObjectAccessor::GetCreature(*player, guid);
        if (player->GetMapId() != 1481 || !Briefed(player, entry) || !npc || !npc->IsAlive() ||
            npc->GetEntry() != entry || npc->GetDBTableGUIDLow() != Spawn(entry) || !npc->GetTransGUID().IsEmpty())
        {
            Fail(player, bit);
            return;
        }
        uint32 duration = 0;
        if (entry == 94435)
        {
            // SceneScriptGlobalText: demonicTeleportVisualID = 53121 (a visual, not a spell).
            WorldPackets::Spells::PlaySpellVisual visual;
            visual.Source = guid;
            visual.Target = guid;
            visual.TargetPosition = npc->GetPosition();
            visual.SpellVisualID = 53121;
            visual.TravelSpeed = 0.0f;
            player->SendDirectMessage(visual.Write());
            duration = 500;
        }
        else
            duration = Walk(player, npc, entry);
        if (!duration)
        {
            Fail(player, bit);
            TC_LOG_ERROR(LOG_FILTER_WORLDSERVER, "CAPTAINS_NATIVE: invalid departure spline entry=%u", entry);
            return;
        }
        player->AddDelayedEvent(duration + (entry == 94435 ? 0 : 200), [player, guid, entry, bit]()
        {
            Clear(player, bit);
            if (player->GetMapId() == 1481 && Briefed(player, entry))
                if (Creature* captain = ObjectAccessor::GetCreature(*player, guid))
                    if (captain->GetEntry() == entry && captain->GetDBTableGUIDLow() == Spawn(entry))
                        captain->DestroyForPlayer(player);
            player->UpdateObjectVisibility();
        });
    }
}
class spell_felsoria_captains_native_v2 : public SpellScriptLoader
{
public:
    spell_felsoria_captains_native_v2() : SpellScriptLoader("spell_felsoria_captains_native_v2") { }
    class native_SpellScript : public SpellScript
    {
        PrepareSpellScript(native_SpellScript);
        void Depart()
        {
            Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
            if (!player || player->GetMapId() != 1481 || player->getClass() != CLASS_DEMON_HUNTER ||
                player->GetQuestStatus(38813) != QUEST_STATUS_INCOMPLETE)
                return;
            uint32 spell = GetSpellInfo()->Id;
            uint32 entry = spell == 191315 ? 90247 : spell == 191400 ? 93693 : spell == 191402 ? 94435 : 0;
            if (!entry || FelsoriaCaptainsNativeV2::Briefed(player, entry))
                return;
            Creature* captain = player->FindNearestCreature(entry, 15.0f, true);
            if (!captain || captain->GetDBTableGUIDLow() != FelsoriaCaptainsNativeV2::Spawn(entry))
                return;
            uint32 bit = FelsoriaCaptainsNativeV2::Bit(entry);
            uint32 mask = player->GetAnyDataContainer().GetValue<uint32>(FelsoriaCaptainsNativeV2::ActiveKey, 0);
            if (mask & bit)
                return;
            player->GetAnyDataContainer().Set<uint32>(FelsoriaCaptainsNativeV2::ActiveKey, mask | bit);
            uint32 failed = player->GetAnyDataContainer().GetValue<uint32>(FelsoriaCaptainsNativeV2::FailedKey, 0);
            player->GetAnyDataContainer().Set<uint32>(FelsoriaCaptainsNativeV2::FailedKey, failed & ~bit);
            ObjectGuid guid = captain->GetGUID();
            // Ashtongue 8.50+2.75+0.40+0.40; Coilskar 9+1.75; Shivarra 8.50+3+0.20.
            uint32 delay = entry == 90247 ? 12050 : entry == 93693 ? 10750 : 11700;
            player->AddDelayedEvent(delay, [player, guid, entry]() { FelsoriaCaptainsNativeV2::Start(player, guid, entry); });
        }
        void Register() override { AfterCast += SpellCastFn(native_SpellScript::Depart); }
    };
    SpellScript* GetSpellScript() const override { return new native_SpellScript(); }
};

#include "FelsoriaMardumReinforcements.inc"

void AddSC_Mardum()
{
    new npc_felsoria_mardum_reinforcement_v1();
    new player_felsoria_mardum_reinforcements_v1();
    new spell_felsoria_captains_native_v2();
    // Retired observation-only captain trace hooks. Native captain scripts stay registered.

    new player_felsoria_illidari_title_v1();
    new spell_felsoria_coilskar_mystic_sacrifice_v1();
    new spell_felsoria_cage_unlock_v1();
    new spell_felsoria_starter_mount_lock_v3();
    new player_felsoria_felsaber_unlock_v3();
    new spell_felsoria_felsaber_aftercast_v1();
    new sceneTrigger_dh_init();
    new sceneTrigger_q40378_ashtongue();
    new go_q40077();
    new npc_q40077();
    new npc_q40077_1();
    new go_q40378();
    new go_q39279();
    new spell_legion_q39279();
    // spell_legion_197486 has no enabled hooks; periodic effect is handled natively.
    new spell_legion_197505_197598();
    new spell_legion_197523();
    new spell_legion_208121();
    new npc_q40378();
    new conversation_announcer();
    new npc_q39049();
    new go_q38759();
    new player_felsoria_personal_sacrifice_v1();
    new npc_q40379();
    new go_q40379();
    new go_q39050();
    new spell_felsoria_shivarra_sacrifice_v1();
    new npc_q38765();
    new go_q38765();
    new npc_questgiver_93759();
    new npc_q93221_beliash();
    new npc_q39495_caza();
    new go_q38727();
    new npc_q40222_progres1();
    new npc_q39495_prolifica();
    new npc_q38728_tyranna();
    new npc_q38728_progres1();
    new go_q38729();
    new go_q39262();
    new npc_q40378_1();
    new npc_q40378_2();
    new npc_q40379_1();
    new npc_q38759_1();
    new npc_q40379_2();
    new npc_96654();
    new npc_96436();
    new npc_q39495();
    new npc_q38727();
    new npc_93127();
    new npc_c100161();
}