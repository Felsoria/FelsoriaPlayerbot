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

/* ContentData
go_cat_figurine (the "trap" version of GO, two different exist)
go_northern_crystal_pylon
go_eastern_crystal_pylon
go_western_crystal_pylon
go_barov_journal
go_ethereum_prison
go_ethereum_stasis
go_sacred_fire_of_life
go_shrine_of_the_birds
go_southfury_moonstone
go_field_repair_bot_74A
go_orb_of_command
go_resonite_cask
go_tablet_of_madness
go_tablet_of_the_seven
go_tele_to_dalaran_crystal
go_tele_to_violet_stand
go_scourge_cage
go_jotunheim_cage
go_table_theka
go_soulwell
go_bashir_crystalforge
go_ethereal_teleport_pad
go_soulwell
go_dragonflayer_cage
go_tadpole_cage
go_amberpine_outhouse
go_hive_pod
go_gjalerbron_cage
go_large_gjalerbron_cage
go_veil_skith_cage
EndContentData */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "GameObjectAI.h"
#include "Spell.h"
#include "QuestData.h"

/*######
## go_cat_figurine
######*/

enum CatFigurine
{
    SPELL_SUMMON_GHOST_SABER    = 5968,
};

class go_cat_figurine : public GameObjectScript
{
public:
    go_cat_figurine() : GameObjectScript("go_cat_figurine") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        player->CastSpell(player, SPELL_SUMMON_GHOST_SABER, true);
        return false;
    }
};

/*######
## go_crystal_pylons (3x)
######*/
class go_northern_crystal_pylon : public GameObjectScript
{
public:
    go_northern_crystal_pylon() : GameObjectScript("go_northern_crystal_pylon") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER)
        {
            player->PrepareQuestMenu(go->GetGUID());
            player->SendPreparedQuest(go->GetGUID());
        }

        if (player->GetQuestStatus(4285) == QUEST_STATUS_INCOMPLETE)
            player->AreaExploredOrEventHappens(4285);

        return true;
    }
};

class go_eastern_crystal_pylon : public GameObjectScript
{
public:
    go_eastern_crystal_pylon() : GameObjectScript("go_eastern_crystal_pylon") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER)
        {
            player->PrepareQuestMenu(go->GetGUID());
            player->SendPreparedQuest(go->GetGUID());
        }

        if (player->GetQuestStatus(4287) == QUEST_STATUS_INCOMPLETE)
            player->AreaExploredOrEventHappens(4287);

        return true;
    }
};

class go_western_crystal_pylon : public GameObjectScript
{
public:
    go_western_crystal_pylon() : GameObjectScript("go_western_crystal_pylon") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER)
        {
            player->PrepareQuestMenu(go->GetGUID());
            player->SendPreparedQuest(go->GetGUID());
        }

        if (player->GetQuestStatus(4288) == QUEST_STATUS_INCOMPLETE)
            player->AreaExploredOrEventHappens(4288);

        return true;
    }
};

/*######
## go_barov_journal
######*/

class go_barov_journal : public GameObjectScript
{
public:
    go_barov_journal() : GameObjectScript("go_barov_journal") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        if (player->HasSkill(SKILL_TAILORING) && player->GetBaseSkillValue(SKILL_TAILORING) >= 280 && !player->HasSpell(26086))
            player->CastSpell(player, 26095, false);

        return true;
    }
};

/*######
## go_field_repair_bot_74A
######*/

class go_field_repair_bot_74A : public GameObjectScript
{
public:
    go_field_repair_bot_74A() : GameObjectScript("go_field_repair_bot_74A") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        if (player->HasSkill(SKILL_ENGINEERING) && player->GetBaseSkillValue(SKILL_ENGINEERING) >= 300 && !player->HasSpell(22704))
            player->CastSpell(player, 22864, false);

        return true;
    }
};

/*######
## go_gilded_brazier (Paladin First Trail quest (9678))
######*/

enum GildedBrazier
{
    NPC_STILLBLADE  = 17716,
};

class go_gilded_brazier : public GameObjectScript
{
public:
    go_gilded_brazier() : GameObjectScript("go_gilded_brazier") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
        {
            if (player->GetQuestStatus(9678) == QUEST_STATUS_INCOMPLETE)
            {
                if (Creature* Stillblade = player->SummonCreature(NPC_STILLBLADE, 8106.11f, -7542.06f, 151.775f, 3.02598f, TEMPSUMMON_DEAD_DESPAWN, 60000))
                    Stillblade->AI()->AttackStart(player);
            }
        }
        return true;
    }
};

/*######
## go_orb_of_command
######*/

class go_orb_of_command : public GameObjectScript
{
public:
    go_orb_of_command() : GameObjectScript("go_orb_of_command") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        if (player->GetQuestRewardStatus(7761))
            player->CastSpell(player, 23460, true);

        return true;
    }
};

/*######
## go_tablet_of_madness
######*/

class go_tablet_of_madness : public GameObjectScript
{
public:
    go_tablet_of_madness() : GameObjectScript("go_tablet_of_madness") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        if (player->HasSkill(SKILL_ALCHEMY) && player->GetSkillValue(SKILL_ALCHEMY) >= 300 && !player->HasSpell(24266))
            player->CastSpell(player, 24267, false);

        return true;
    }
};

/*######
## go_tablet_of_the_seven
######*/

class go_tablet_of_the_seven : public GameObjectScript
{
public:
    go_tablet_of_the_seven() : GameObjectScript("go_tablet_of_the_seven") { }

    //TODO: use gossip option ("Transcript the Tablet") instead, if Trinity adds support.
    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (go->GetGoType() != GAMEOBJECT_TYPE_QUESTGIVER)
            return true;

        if (player->GetQuestStatus(4296) == QUEST_STATUS_INCOMPLETE)
            player->CastSpell(player, 15065, false);

        return true;
    }
};

/*#####
## go_jump_a_tron
######*/

class go_jump_a_tron : public GameObjectScript
{
public:
    go_jump_a_tron() : GameObjectScript("go_jump_a_tron") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        if (player->GetQuestStatus(10111) == QUEST_STATUS_INCOMPLETE)
            player->CastSpell(player, 33382, true);

        return true;
    }
};

/*######
## go_ethereum_prison
######*/

enum EthereumPrison
{
    SPELL_REP_LC        = 39456,
    SPELL_REP_SHAT      = 39457,
    SPELL_REP_CE        = 39460,
    SPELL_REP_CON       = 39474,
    SPELL_REP_KT        = 39475,
    SPELL_REP_SPOR      = 39476
};

const uint32 NpcPrisonEntry[] =
{
    22810, 22811, 22812, 22813, 22814, 22815,               //good guys
    20783, 20784, 20785, 20786, 20788, 20789, 20790         //bad guys
};

class go_ethereum_prison : public GameObjectScript
{
public:
    go_ethereum_prison() : GameObjectScript("go_ethereum_prison") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {    
        for (uint8 n = 0; n <= 12; n++)
        {
            if (Creature* cre = player->FindNearestCreature(NpcPrisonEntry[n], 20.0f, true))
                return false;
        }
        
        int Random = rand() % (sizeof(NpcPrisonEntry) / sizeof(uint32));

        if (Creature* creature = player->SummonCreature(NpcPrisonEntry[Random], go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), go->GetAngle(player),
            TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30000))
        {
            if (!creature->IsHostileTo(player))
            {
                if (FactionTemplateEntry const* pFaction = creature->getFactionTemplateEntry())
                {
                    uint32 Spell = 0;

                    switch (pFaction->Faction)
                    {
                        case 1011: Spell = SPELL_REP_LC; break;
                        case 935: Spell = SPELL_REP_SHAT; break;
                        case 942: Spell = SPELL_REP_CE; break;
                        case 933: Spell = SPELL_REP_CON; break;
                        case 989: Spell = SPELL_REP_KT; break;
                        case 970: Spell = SPELL_REP_SPOR; break;
                    }

                    if (Spell)
                        creature->CastSpell(player, Spell, false);
                    else
                        TC_LOG_ERROR(LOG_FILTER_TSCR, "go_ethereum_prison summoned Creature (entry %u) but faction (%u) are not expected by script.", creature->GetEntry(), creature->getFaction());
                }
            }
        }

        return false;
    }
};

/*######
## go_ethereum_stasis
######*/

const uint32 NpcStasisEntry[] =
{
    22825, 20888, 22827, 22826, 22828
};

class go_ethereum_stasis : public GameObjectScript
{
public:
    go_ethereum_stasis() : GameObjectScript("go_ethereum_stasis") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        int Random = rand() % (sizeof(NpcStasisEntry) / sizeof(uint32));

        player->SummonCreature(NpcStasisEntry[Random], go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), go->GetAngle(player),
            TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30000);

        return false;
    }
};

/*######
## go_resonite_cask
######*/

enum ResoniteCask
{
    NPC_GOGGEROC    = 11920
};

class go_resonite_cask : public GameObjectScript
{
public:
    go_resonite_cask() : GameObjectScript("go_resonite_cask") { }

    bool OnGossipHello(Player* /*player*/, GameObject* go) override
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
            go->SummonCreature(NPC_GOGGEROC, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 300000);

        return false;
    }
};

/*######
## go_sacred_fire_of_life
######*/

#define NPC_ARIKARA  10882

class go_sacred_fire_of_life : public GameObjectScript
{
public:
    go_sacred_fire_of_life() : GameObjectScript("go_sacred_fire_of_life") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
            player->SummonCreature(NPC_ARIKARA, -5008.338f, -2118.894f, 83.657f, 0.874f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30000);

        return true;
    }
};

/*######
## go_shrine_of_the_birds
######*/

enum ShrineOfTheBirds
{
    NPC_HAWK_GUARD      = 22992,
    NPC_EAGLE_GUARD     = 22993,
    NPC_FALCON_GUARD    = 22994,
    GO_SHRINE_HAWK      = 185551,
    GO_SHRINE_EAGLE     = 185547,
    GO_SHRINE_FALCON    = 185553
};

class go_shrine_of_the_birds : public GameObjectScript
{
public:
    go_shrine_of_the_birds() : GameObjectScript("go_shrine_of_the_birds") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        uint32 BirdEntry = 0;

        float fX, fY, fZ;
        go->GetClosePoint(fX, fY, fZ, go->GetObjectSize(), INTERACTION_DISTANCE);

        switch (go->GetEntry())
        {
            case GO_SHRINE_HAWK:
                BirdEntry = NPC_HAWK_GUARD;
                break;
            case GO_SHRINE_EAGLE:
                BirdEntry = NPC_EAGLE_GUARD;
                break;
            case GO_SHRINE_FALCON:
                BirdEntry = NPC_FALCON_GUARD;
                break;
        }

        if (BirdEntry)
            player->SummonCreature(BirdEntry, fX, fY, fZ, go->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 60000);

        return false;
    }
};

/*######
## go_southfury_moonstone
######*/

enum Southfury
{
    NPC_RIZZLE                  = 23002,
    SPELL_BLACKJACK             = 39865, //stuns player
    SPELL_SUMMON_RIZZLE         = 39866
};

class go_southfury_moonstone : public GameObjectScript
{
public:
    go_southfury_moonstone() : GameObjectScript("go_southfury_moonstone") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        //implicitTarget=48 not implemented as of writing this code, and manual summon may be just ok for our purpose
        //player->CastSpell(player, SPELL_SUMMON_RIZZLE, false);

        if (Creature* creature = player->SummonCreature(NPC_RIZZLE, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_DEAD_DESPAWN, 0))
            creature->CastSpell(player, SPELL_BLACKJACK, false);

        return false;
    }
};

/*######
## go_tele_to_dalaran_crystal
######*/

enum DalaranCrystal
{
    QUEST_LEARN_LEAVE_RETURN    = 12790,
    QUEST_TELE_CRYSTAL_FLAG     = 12845
};

#define GO_TELE_TO_DALARAN_CRYSTAL_FAILED   "This teleport crystal cannot be used until the teleport crystal in Dalaran has been used at least once."

class go_tele_to_dalaran_crystal : public GameObjectScript
{
public:
    go_tele_to_dalaran_crystal() : GameObjectScript("go_tele_to_dalaran_crystal") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        if (player->GetQuestRewardStatus(QUEST_LEARN_LEAVE_RETURN) || player->GetQuestStatus(QUEST_LEARN_LEAVE_RETURN) == QUEST_STATUS_INCOMPLETE)
            return false;

        player->GetSession()->SendNotification(GO_TELE_TO_DALARAN_CRYSTAL_FAILED);

        return true;
    }
};

/*######
## go_tele_to_violet_stand
######*/

class go_tele_to_violet_stand : public GameObjectScript
{
public:
    go_tele_to_violet_stand() : GameObjectScript("go_tele_to_violet_stand") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        if (player->GetQuestRewardStatus(QUEST_LEARN_LEAVE_RETURN) || player->GetQuestStatus(QUEST_LEARN_LEAVE_RETURN) == QUEST_STATUS_INCOMPLETE)
            return false;

        return true;
    }
};

/*######
## go_fel_crystalforge
######*/

#define GOSSIP_FEL_CRYSTALFORGE_TEXT 31000
#define GOSSIP_FEL_CRYSTALFORGE_ITEM_TEXT_RETURN 31001
#define GOSSIP_FEL_CRYSTALFORGE_ITEM_1 "Purchase 1 Unstable Flask of the Beast for the cost of 10 Apexis Shards"
#define GOSSIP_FEL_CRYSTALFORGE_ITEM_5 "Purchase 5 Unstable Flask of the Beast for the cost of 50 Apexis Shards"
#define GOSSIP_FEL_CRYSTALFORGE_ITEM_RETURN "Use the fel crystalforge to make another purchase."

enum FelCrystalforge
{
    SPELL_CREATE_1_FLASK_OF_BEAST   = 40964,
    SPELL_CREATE_5_FLASK_OF_BEAST   = 40965,
};

class go_fel_crystalforge : public GameObjectScript
{
public:
    go_fel_crystalforge() : GameObjectScript("go_fel_crystalforge") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER) /* != GAMEOBJECT_TYPE_QUESTGIVER) */
            player->PrepareQuestMenu(go->GetGUID()); /* return true*/

        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_FEL_CRYSTALFORGE_ITEM_1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_FEL_CRYSTALFORGE_ITEM_5, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

        player->SEND_GOSSIP_MENU(GOSSIP_FEL_CRYSTALFORGE_TEXT, go->GetGUID());

        return true;
    }

    bool OnGossipSelect(Player* player, GameObject* go, uint32 /*sender*/, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        switch (action)
        {
            case GOSSIP_ACTION_INFO_DEF:
                player->CastSpell(player, SPELL_CREATE_1_FLASK_OF_BEAST, false);
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_FEL_CRYSTALFORGE_ITEM_RETURN, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+2);
                player->SEND_GOSSIP_MENU(GOSSIP_FEL_CRYSTALFORGE_ITEM_TEXT_RETURN, go->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF + 1:
                player->CastSpell(player, SPELL_CREATE_5_FLASK_OF_BEAST, false);
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_FEL_CRYSTALFORGE_ITEM_RETURN, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+2);
                player->SEND_GOSSIP_MENU(GOSSIP_FEL_CRYSTALFORGE_ITEM_TEXT_RETURN, go->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF + 2:
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_FEL_CRYSTALFORGE_ITEM_1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_FEL_CRYSTALFORGE_ITEM_5, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                player->SEND_GOSSIP_MENU(GOSSIP_FEL_CRYSTALFORGE_TEXT, go->GetGUID());
                break;
        }
        return true;
    }
};

/*######
## go_bashir_crystalforge
######*/

#define GOSSIP_BASHIR_CRYSTALFORGE_TEXT 31100
#define GOSSIP_BASHIR_CRYSTALFORGE_ITEM_TEXT_RETURN 31101
#define GOSSIP_BASHIR_CRYSTALFORGE_ITEM_1 "Purchase 1 Unstable Flask of the Sorcerer for the cost of 10 Apexis Shards"
#define GOSSIP_BASHIR_CRYSTALFORGE_ITEM_5 "Purchase 5 Unstable Flask of the Sorcerer for the cost of 50 Apexis Shards"
#define GOSSIP_BASHIR_CRYSTALFORGE_ITEM_RETURN "Use the bashir crystalforge to make another purchase."

enum BashirCrystalforge
{
    SPELL_CREATE_1_FLASK_OF_SORCERER   = 40968,
    SPELL_CREATE_5_FLASK_OF_SORCERER   = 40970,
};

class go_bashir_crystalforge : public GameObjectScript
{
public:
    go_bashir_crystalforge() : GameObjectScript("go_bashir_crystalforge") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER) /* != GAMEOBJECT_TYPE_QUESTGIVER) */
            player->PrepareQuestMenu(go->GetGUID()); /* return true*/

        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_BASHIR_CRYSTALFORGE_ITEM_1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_BASHIR_CRYSTALFORGE_ITEM_5, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

        player->SEND_GOSSIP_MENU(GOSSIP_BASHIR_CRYSTALFORGE_TEXT, go->GetGUID());

        return true;
    }

    bool OnGossipSelect(Player* player, GameObject* go, uint32 /*sender*/, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        switch (action)
        {
            case GOSSIP_ACTION_INFO_DEF:
                player->CastSpell(player, SPELL_CREATE_1_FLASK_OF_SORCERER, false);
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_BASHIR_CRYSTALFORGE_ITEM_RETURN, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+2);
                player->SEND_GOSSIP_MENU(GOSSIP_BASHIR_CRYSTALFORGE_ITEM_TEXT_RETURN, go->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF + 1:
                player->CastSpell(player, SPELL_CREATE_5_FLASK_OF_SORCERER, false);
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_BASHIR_CRYSTALFORGE_ITEM_RETURN, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+2);
                player->SEND_GOSSIP_MENU(GOSSIP_BASHIR_CRYSTALFORGE_ITEM_TEXT_RETURN, go->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF + 2:
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_BASHIR_CRYSTALFORGE_ITEM_1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_BASHIR_CRYSTALFORGE_ITEM_5, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                player->SEND_GOSSIP_MENU(GOSSIP_BASHIR_CRYSTALFORGE_TEXT, go->GetGUID());
                break;
        }
        return true;
    }
};

/*######
## matrix_punchograph
######*/

enum MatrixPunchograph
{
    ITEM_WHITE_PUNCH_CARD = 9279,
    ITEM_YELLOW_PUNCH_CARD = 9280,
    ITEM_BLUE_PUNCH_CARD = 9282,
    ITEM_RED_PUNCH_CARD = 9281,
    ITEM_PRISMATIC_PUNCH_CARD = 9316,
    SPELL_YELLOW_PUNCH_CARD = 11512,
    SPELL_BLUE_PUNCH_CARD = 11525,
    SPELL_RED_PUNCH_CARD = 11528,
    SPELL_PRISMATIC_PUNCH_CARD = 11545,
    MATRIX_PUNCHOGRAPH_3005_A = 142345,
    MATRIX_PUNCHOGRAPH_3005_B = 142475,
    MATRIX_PUNCHOGRAPH_3005_C = 142476,
    MATRIX_PUNCHOGRAPH_3005_D = 142696,
};

class go_matrix_punchograph : public GameObjectScript
{
public:
    go_matrix_punchograph() : GameObjectScript("go_matrix_punchograph") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        switch (go->GetEntry())
        {
            case MATRIX_PUNCHOGRAPH_3005_A:
                if (player->HasItemCount(ITEM_WHITE_PUNCH_CARD))
                {
                    player->DestroyItemCount(ITEM_WHITE_PUNCH_CARD, 1, true);
                    player->CastSpell(player, SPELL_YELLOW_PUNCH_CARD, true);
                }
                break;
            case MATRIX_PUNCHOGRAPH_3005_B:
                if (player->HasItemCount(ITEM_YELLOW_PUNCH_CARD))
                {
                    player->DestroyItemCount(ITEM_YELLOW_PUNCH_CARD, 1, true);
                    player->CastSpell(player, SPELL_BLUE_PUNCH_CARD, true);
                }
                break;
            case MATRIX_PUNCHOGRAPH_3005_C:
                if (player->HasItemCount(ITEM_BLUE_PUNCH_CARD))
                {
                    player->DestroyItemCount(ITEM_BLUE_PUNCH_CARD, 1, true);
                    player->CastSpell(player, SPELL_RED_PUNCH_CARD, true);
                }
                break;
            case MATRIX_PUNCHOGRAPH_3005_D:
                if (player->HasItemCount(ITEM_RED_PUNCH_CARD))
                {
                    player->DestroyItemCount(ITEM_RED_PUNCH_CARD, 1, true);
                    player->CastSpell(player, SPELL_PRISMATIC_PUNCH_CARD, true);
                }
                break;
            default:
                break;
        }
        return false;
    }
};

/*######
## go_scourge_cage
######*/

enum ScourgeCage
{
    NPC_SCOURGE_PRISONER = 25610
};

class go_scourge_cage : public GameObjectScript
{
public:
    go_scourge_cage() : GameObjectScript("go_scourge_cage") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (Creature* pNearestPrisoner = go->FindNearestCreature(NPC_SCOURGE_PRISONER, 5.0f, true))
        {
            go->SetGoState(GO_STATE_ACTIVE);
            player->KilledMonsterCredit(NPC_SCOURGE_PRISONER, pNearestPrisoner->GetGUID());
            pNearestPrisoner->DisappearAndDie();
        }

        return true;
    }
};

/*######
## go_arcane_prison
######*/

enum ArcanePrison
{
    QUEST_PRISON_BREAK                  = 11587,
    SPELL_ARCANE_PRISONER_KILL_CREDIT   = 45456
};

class go_arcane_prison : public GameObjectScript
{
public:
    go_arcane_prison() : GameObjectScript("go_arcane_prison") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (player->GetQuestStatus(QUEST_PRISON_BREAK) == QUEST_STATUS_INCOMPLETE)
        {
            go->SummonCreature(25318, 3485.089844f, 6115.7422188f, 70.966812f, 0, TEMPSUMMON_TIMED_DESPAWN, 60000);
            player->CastSpell(player, SPELL_ARCANE_PRISONER_KILL_CREDIT, true);
            return true;
        }
        return false;
    }
};

/*######
## go_blood_filled_orb
######*/

#define NPC_ZELEMAR  17830

class go_blood_filled_orb : public GameObjectScript
{
public:
    go_blood_filled_orb() : GameObjectScript("go_blood_filled_orb") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
            player->SummonCreature(NPC_ZELEMAR, -369.746f, 166.759f, -21.50f, 5.235f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30000);

        return true;
    }
};

/*######
## go_jotunheim_cage
######*/

enum JotunheimCage
{
    NPC_EBON_BLADE_PRISONER_HUMAN   = 30186,
    NPC_EBON_BLADE_PRISONER_NE      = 30194,
    NPC_EBON_BLADE_PRISONER_TROLL   = 30196,
    NPC_EBON_BLADE_PRISONER_ORC     = 30195,

    SPELL_SUMMON_BLADE_KNIGHT_H     = 56207,
    SPELL_SUMMON_BLADE_KNIGHT_NE    = 56209,
    SPELL_SUMMON_BLADE_KNIGHT_ORC   = 56212,
    SPELL_SUMMON_BLADE_KNIGHT_TROLL = 56214
};

class go_jotunheim_cage : public GameObjectScript
{
public:
    go_jotunheim_cage() : GameObjectScript("go_jotunheim_cage") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        Creature* pPrisoner = go->FindNearestCreature(NPC_EBON_BLADE_PRISONER_HUMAN, 5.0f, true);
        if (!pPrisoner)
        {
            pPrisoner = go->FindNearestCreature(NPC_EBON_BLADE_PRISONER_TROLL, 5.0f, true);
            if (!pPrisoner)
            {
                pPrisoner = go->FindNearestCreature(NPC_EBON_BLADE_PRISONER_ORC, 5.0f, true);
                if (!pPrisoner)
                    pPrisoner = go->FindNearestCreature(NPC_EBON_BLADE_PRISONER_NE, 5.0f, true);
            }
        }
        if (!pPrisoner || !pPrisoner->isAlive())
            return false;

        pPrisoner->DisappearAndDie();
        player->KilledMonsterCredit(NPC_EBON_BLADE_PRISONER_HUMAN, ObjectGuid::Empty);
        switch (pPrisoner->GetEntry())
        {
            case NPC_EBON_BLADE_PRISONER_HUMAN:
                player->CastSpell(player, SPELL_SUMMON_BLADE_KNIGHT_H, true);
                break;
            case NPC_EBON_BLADE_PRISONER_NE:
                player->CastSpell(player, SPELL_SUMMON_BLADE_KNIGHT_NE, true);
                break;
            case NPC_EBON_BLADE_PRISONER_TROLL:
                player->CastSpell(player, SPELL_SUMMON_BLADE_KNIGHT_TROLL, true);
                break;
            case NPC_EBON_BLADE_PRISONER_ORC:
                player->CastSpell(player, SPELL_SUMMON_BLADE_KNIGHT_ORC, true);
                break;
        }
        return true;
    }
};

enum TableTheka
{
    GOSSIP_TABLE_THEKA = 1653,

    QUEST_SPIDER_GOLD = 2936
};

class go_table_theka : public GameObjectScript
{
public:
    go_table_theka() : GameObjectScript("go_table_theka") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (player->GetQuestStatus(QUEST_SPIDER_GOLD) == QUEST_STATUS_INCOMPLETE)
            player->AreaExploredOrEventHappens(QUEST_SPIDER_GOLD);

        player->SEND_GOSSIP_MENU(GOSSIP_TABLE_THEKA, go->GetGUID());

        return true;
    }
};

/*######
## go_inconspicuous_landmark
######*/

enum InconspicuousLandmark
{
    SPELL_SUMMON_PIRATES_TREASURE_AND_TRIGGER_MOB    = 11462,
    ITEM_CUERGOS_KEY                                 = 9275,
};

class go_inconspicuous_landmark : public GameObjectScript
{
public:
    go_inconspicuous_landmark() : GameObjectScript("go_inconspicuous_landmark") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        if (player->HasItemCount(ITEM_CUERGOS_KEY))
            return false;

        player->CastSpell(player, SPELL_SUMMON_PIRATES_TREASURE_AND_TRIGGER_MOB, true);

        return true;
    }
};

/*######
## go_ethereal_teleport_pad
######*/

enum EtherealTeleportPad
{
    NPC_IMAGE_WIND_TRADER               = 20518,
    ITEM_TELEPORTER_POWER_PACK          = 28969,
};

class go_ethereal_teleport_pad : public GameObjectScript
{
public:
    go_ethereal_teleport_pad() : GameObjectScript("go_ethereal_teleport_pad") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (!player->HasItemCount(ITEM_TELEPORTER_POWER_PACK))
            return false;

        go->SummonCreature(NPC_IMAGE_WIND_TRADER, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), go->GetAngle(player), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 60000);

        return true;
    }
};

/*######
## go_soulwell
######*/

enum SoulWellData
{
    GO_SOUL_WELL                        = 181621,
    SPELL_HEALTH_STONE                  = 23517
};

class go_soulwell : public GameObjectScript
{
    public:
        go_soulwell() : GameObjectScript("go_soulwell") { }

        struct go_soulwellAI : public GameObjectAI
        {
            go_soulwellAI(GameObject* go) : GameObjectAI(go)
            {
                _stoneId = 0;
                _stoneSpell = SPELL_HEALTH_STONE;
                if (_stoneSpell == 0) // Should never happen
                    return;

                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(_stoneSpell);
                if (!spellInfo)
                    return;

                _stoneId = spellInfo->Effects[EFFECT_0]->ItemType;
            }

            /// Due to the fact that this GameObject triggers CMSG_GAMEOBJECT_USE
            /// _and_ CMSG_GAMEOBJECT_REPORT_USE, this GossipHello hook is called
            /// twice. The script's handling is fine as it won't remove two charges
            /// on the well. We have to find how to segregate REPORT_USE and USE.
            bool GossipHello(Player* player) override
            {
                Unit* owner = go->GetOwner();
                if (_stoneSpell == 0 || _stoneId == 0)
                    return true;

                if (!owner || owner->GetTypeId() != TYPEID_PLAYER || !player->IsInSameRaidWith(owner->ToPlayer()))
                    return true;

                // Don't try to add a stone if we already have one.
                if (Item* item = player->GetItemByEntry(_stoneId))
                {
                    item->SetSpellCharges(1, -3);
                    return true;
                }

                player->CastSpell(player, _stoneSpell, true);

                return false;
            }

        private:
            uint32 _stoneSpell;
            uint32 _stoneId;
        };

        GameObjectAI* GetAI(GameObject* go) const override
        {
            return new go_soulwellAI(go);
        }
};

/*######
## Quest 11255: Prisoners of Wyrmskull
## go_dragonflayer_cage
######*/

enum PrisonersOfWyrmskull
{
    QUEST_PRISONERS_OF_WYRMSKULL                  = 11255,
    NPC_PRISONER_PRIEST                           = 24086,
    NPC_PRISONER_MAGE                             = 24088,
    NPC_PRISONER_WARRIOR                          = 24089,
    NPC_PRISONER_PALADIN                          = 24090
};

class go_dragonflayer_cage : public GameObjectScript
{
public:
    go_dragonflayer_cage() : GameObjectScript("go_dragonflayer_cage") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (player->GetQuestStatus(QUEST_PRISONERS_OF_WYRMSKULL) != QUEST_STATUS_INCOMPLETE)
            return true;

        Creature* pPrisoner = go->FindNearestCreature(NPC_PRISONER_PRIEST, 2.0f);
        if (!pPrisoner)
        {
            pPrisoner = go->FindNearestCreature(NPC_PRISONER_MAGE, 2.0f);
            if (!pPrisoner)
            {
                pPrisoner = go->FindNearestCreature(NPC_PRISONER_WARRIOR, 2.0f);
                if (!pPrisoner)
                    pPrisoner = go->FindNearestCreature(NPC_PRISONER_PALADIN, 2.0f);
            }
        }

        if (!pPrisoner || !pPrisoner->isAlive())
            return true;

        Quest const* qInfo = sQuestDataStore->GetQuestTemplate(QUEST_PRISONERS_OF_WYRMSKULL);
        if (qInfo)
        {
            //TODO: prisoner should help player for a short period of time
            player->KilledMonsterCredit(qInfo->Objectives[0].ObjectID, ObjectGuid::Empty);
            pPrisoner->DisappearAndDie();
        }
        return true;
    }
};

/*######
## Quest 11560: Oh Noes, the Tadpoles!
## go_tadpole_cage
######*/

enum Tadpoles
{
    QUEST_OH_NOES_THE_TADPOLES                    = 11560,
    NPC_WINTERFIN_TADPOLE                         = 25201
};

class go_tadpole_cage : public GameObjectScript
{
public:
    go_tadpole_cage() : GameObjectScript("go_tadpole_cage") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (player->GetQuestStatus(QUEST_OH_NOES_THE_TADPOLES) == QUEST_STATUS_INCOMPLETE)
        {
            Creature* pTadpole = go->FindNearestCreature(NPC_WINTERFIN_TADPOLE, 1.0f);
            if (pTadpole)
            {
                go->UseDoorOrButton();
                pTadpole->DisappearAndDie();
                player->KilledMonsterCredit(NPC_WINTERFIN_TADPOLE, ObjectGuid::Empty);
                //FIX: Summon minion tadpole
            }
        }
        return true;
    }
};

/*######
## go_amberpine_outhouse
######*/

#define GOSSIP_USE_OUTHOUSE "Use the outhouse."
#define GO_ANDERHOLS_SLIDER_CIDER_NOT_FOUND "Quest item Anderhol's Slider Cider not found."

enum AmberpineOuthouse
{
    ITEM_ANDERHOLS_SLIDER_CIDER     = 37247,
    NPC_OUTHOUSE_BUNNY              = 27326,
    QUEST_DOING_YOUR_DUTY           = 12227,
    SPELL_INDISPOSED                = 53017,
    SPELL_INDISPOSED_III            = 48341,
    SPELL_CREATE_AMBERSEEDS         = 48330,
    GOSSIP_OUTHOUSE_INUSE           = 12775,
    GOSSIP_OUTHOUSE_VACANT          = 12779
};

class go_amberpine_outhouse : public GameObjectScript
{
public:
    go_amberpine_outhouse() : GameObjectScript("go_amberpine_outhouse") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        QuestStatus status = player->GetQuestStatus(QUEST_DOING_YOUR_DUTY);
        if (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_REWARDED)
        {
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_USE_OUTHOUSE, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
            player->SEND_GOSSIP_MENU(GOSSIP_OUTHOUSE_VACANT, go->GetGUID());
            return true;
        }
        else
            player->SEND_GOSSIP_MENU(GOSSIP_OUTHOUSE_INUSE, go->GetGUID());
            return true;
    }

    bool OnGossipSelect(Player* player, GameObject* go, uint32 /*sender*/, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        if (action == GOSSIP_ACTION_INFO_DEF +1)
        {
            player->CLOSE_GOSSIP_MENU();
            Creature* target = GetClosestCreatureWithEntry(player, NPC_OUTHOUSE_BUNNY, 3.0f);
            if (target)
            {
                target->AI()->SetData(1, player->getGender());
                go->CastSpell(target, SPELL_INDISPOSED_III);
            }
            go->CastSpell(player, SPELL_INDISPOSED);
            if (player->HasItemCount(ITEM_ANDERHOLS_SLIDER_CIDER, 1))
                go->CastSpell(player, SPELL_CREATE_AMBERSEEDS);
            return true;
        }
        else
        {
            player->CLOSE_GOSSIP_MENU();
            player->GetSession()->SendNotification(GO_ANDERHOLS_SLIDER_CIDER_NOT_FOUND);
            return false;
        }
    }
};

/*######
## Quest 1126: Hive in the Tower
## go_hive_pod
######*/

enum Hives
{
    QUEST_HIVE_IN_THE_TOWER                       = 9544,
    NPC_HIVE_AMBUSHER                             = 13301
};

class go_hive_pod : public GameObjectScript
{
public:
    go_hive_pod() : GameObjectScript("go_hive_pod") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        player->SendLoot(go->GetGUID(), LOOT_CORPSE);
        go->SummonCreature(NPC_HIVE_AMBUSHER, go->GetPositionX()+1, go->GetPositionY(), go->GetPositionZ(), go->GetAngle(player), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 60000);
        go->SummonCreature(NPC_HIVE_AMBUSHER, go->GetPositionX(), go->GetPositionY()+1, go->GetPositionZ(), go->GetAngle(player), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 60000);
        return true;
    }
};

class go_massive_seaforium_charge : public GameObjectScript
{
    public:
        go_massive_seaforium_charge() : GameObjectScript("go_massive_seaforium_charge") { }

        bool OnGossipHello(Player* /*player*/, GameObject* go) override
        {
            go->SetLootState(GO_JUST_DEACTIVATED);
            return true;
        }
};

/*######
## go_gjalerbron_cage
######*/

enum OfKeysAndCages
{
    QUEST_ALLIANCE_OF_KEYS_AND_CAGES    = 11231,
    QUEST_HORDE_OF_KEYS_AND_CAGES       = 11265,
    NPC_GJALERBRON_PRISONER             = 24035,
    SAY_FREE                            = 0,
};

class go_gjalerbron_cage : public GameObjectScript
{
    public:
        go_gjalerbron_cage() : GameObjectScript("go_gjalerbron_cage") { }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            if ((player->GetTeamId() == TEAM_ALLIANCE && player->GetQuestStatus(QUEST_ALLIANCE_OF_KEYS_AND_CAGES) == QUEST_STATUS_INCOMPLETE) ||
                (player->GetTeamId() == TEAM_HORDE && player->GetQuestStatus(QUEST_HORDE_OF_KEYS_AND_CAGES) == QUEST_STATUS_INCOMPLETE))
            {
                if (Creature* prisoner = go->FindNearestCreature(NPC_GJALERBRON_PRISONER, 5.0f))
                {
                    go->UseDoorOrButton();

                    if (player)
                        player->KilledMonsterCredit(NPC_GJALERBRON_PRISONER, ObjectGuid::Empty);

                    prisoner->AI()->Talk(SAY_FREE);
                    prisoner->DespawnOrUnsummon(6000);
                }
            }
            return true;
        }
};

/*########
## go_large_gjalerbron_cage
#####*/

class go_large_gjalerbron_cage : public GameObjectScript
{
    public:
        go_large_gjalerbron_cage() : GameObjectScript("go_large_gjalerbron_cage") { }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            if ((player->GetTeamId() == TEAM_ALLIANCE && player->GetQuestStatus(QUEST_ALLIANCE_OF_KEYS_AND_CAGES) == QUEST_STATUS_INCOMPLETE) ||
                (player->GetTeamId() == TEAM_HORDE && player->GetQuestStatus(QUEST_HORDE_OF_KEYS_AND_CAGES) == QUEST_STATUS_INCOMPLETE))
            {
                std::list<Creature*> prisonerList;
                GetCreatureListWithEntryInGrid(prisonerList, go, NPC_GJALERBRON_PRISONER, INTERACTION_DISTANCE);
                for (std::list<Creature*>::const_iterator itr = prisonerList.begin(); itr != prisonerList.end(); ++itr)
                {
                    go->UseDoorOrButton();
                    player->KilledMonsterCredit(NPC_GJALERBRON_PRISONER, (*itr)->GetGUID());
                    (*itr)->DespawnOrUnsummon(6000);
                    (*itr)->AI()->Talk(SAY_FREE);
                }
            }
            return false;
        }
};

/*########
#### go_veil_skith_cage
#####*/

enum MissingFriends
{
   QUEST_MISSING_FRIENDS    = 10852,
   NPC_CAPTIVE_CHILD        = 22314,
   SAY_FREE_0               = 0,
};

class go_veil_skith_cage : public GameObjectScript
{
    public:
       go_veil_skith_cage() : GameObjectScript("go_veil_skith_cage") { }

       bool OnGossipHello(Player* player, GameObject* go) override
       {
           if (player->GetQuestStatus(QUEST_MISSING_FRIENDS) == QUEST_STATUS_INCOMPLETE)
           {
               std::list<Creature*> childrenList;
               GetCreatureListWithEntryInGrid(childrenList, go, NPC_CAPTIVE_CHILD, INTERACTION_DISTANCE);
               for (std::list<Creature*>::const_iterator itr = childrenList.begin(); itr != childrenList.end(); ++itr)
               {
                   go->UseDoorOrButton();
                   player->KilledMonsterCredit(NPC_CAPTIVE_CHILD, (*itr)->GetGUID());
                   (*itr)->DespawnOrUnsummon(5000);
                   (*itr)->GetMotionMaster()->MovePoint(1, go->GetPositionX()+5, go->GetPositionY(), go->GetPositionZ());
                   (*itr)->AI()->Talk(SAY_FREE_0);
                   (*itr)->GetMotionMaster()->Clear();
               }
           }
           return false;
       }
};


/*######
## go_midsummer_bonfire
######*/

enum MidsummerBonfire
{
    STAMP_OUT_BONFIRE_QUEST_COMPLETE    = 45458,
};

class go_midsummer_bonfire : public GameObjectScript
{
public:
    go_midsummer_bonfire() : GameObjectScript("go_midsummer_bonfire") { }

    bool OnGossipSelect(Player* player, GameObject* /*go*/, uint32 /*sender*/, uint32 /*action*/) override
    {
        player->CastSpell(player, STAMP_OUT_BONFIRE_QUEST_COMPLETE, true);
        player->CLOSE_GOSSIP_MENU();
        return false;
    }
};

class go_sikthik_cage : public GameObjectScript
{
public:
    go_sikthik_cage() : GameObjectScript("go_sikthik_cage") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        if (player->GetQuestStatus(31688) == QUEST_STATUS_INCOMPLETE && player->GetReqKillOrCastCurrentCount(31688, -214734) >= 7)
        {
            player->CastSpell(player, 128920, true);
            return false;
        }
        return false;
    }
};

// Quest: Clean Up in Stormwind - 29144 and Clean Up in Undercity - 29375
class go_forsaken_stink_bomb : public GameObjectScript
{
    public:
        go_forsaken_stink_bomb() : GameObjectScript("go_forsaken_stink_bomb") { }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            go->SetLootState(GO_JUST_DEACTIVATED);
            player->CastSpell(player, 97157, true);
            return true;
        }
};

// 251168 GO: 5 crystals for mount
class go_ephemeral_crystal : public GameObjectScript
{
public:
    go_ephemeral_crystal() : GameObjectScript("go_ephemeral_crystal") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (player->HasAura(215327))
            {
                sGameEventMgr->StopEvent(99, true);
                player->CastSpell(player, 215345);
                return true;
            } 
            
        if (!player->HasAura(215316))
            if (!player->HasAura(215325) && !player->HasAura(215326) && !player->HasAura(215327))
                {
                    player->CastSpell(player, 215316);
                    return true;
                }

        if (player->HasAura(215316))
        {
            player->CastSpell(player, 215325);
            player->RemoveAura(215316);
            return true;
        }

        if (player->HasAura(215325))
        {
            player->CastSpell(player, 215326);
            player->RemoveAura(215325);
            return true;
        }

        if (player->HasAura(215326))
        {
            player->CastSpell(player, 215327);
            player->RemoveAura(215326);
            return true;
        }
        return false;
    }
};

// FELSORIA_SCYTHE_OF_SOULS_V1: only the quest owner's current event can advance.
class go_azsuna_soul_gem : public GameObjectScript
{
public:
    go_azsuna_soul_gem() : GameObjectScript("go_azsuna_soul_gem") {}
    bool OnGossipHello(Player* player, GameObject* go) override
    {
        uint32 entry = go->GetEntry();
        if (entry != 239338 && entry != 239332 && entry != 237017 && entry != 240012)
            return false;
        if (player->GetQuestStatus(37660) != QUEST_STATUS_INCOMPLETE)
            return true;
        GuidList copy = *player->GetSummonList(90401);
        bool hasHelper = false;
        for (ObjectGuid const& guid : copy)
            if (Creature* helper = ObjectAccessor::GetCreature(*player, guid))
                if (helper->AI()->GetGUID() == player->GetGUID())
                {
                    hasHelper = true;
                    if ((helper->AI()->GetData(0) == entry || (entry == 240012 && (player->GetPhaseMask() & 2))) &&
                        (entry != 237017 || helper->AI()->GetGUID(1) == go->GetGUID()) &&
                        helper->IsInMap(player) && helper->IsWithinDist(player, 80.0f) && go->IsWithinDist(player, 10.0f))
                    {
                        helper->AI()->DoAction(entry);
                        if ((entry == 239338 || entry == 239332) && helper->AI()->GetData(0) == 0 &&
                            player->GetQuestObjectiveData(37660, entry))
                            go->DestroyForPlayer(player);
                        return true;
                    }
                }
        // Resume after relog without requiring the phased-out questgiver.
        if (!hasHelper && (entry == 239338 || entry == 239332) && go->IsWithinDist(player, 10.0f))
        {
            player->SummonCreature(90401, player->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 15 * MINUTE * IN_MILLISECONDS);
            return true;
        }
        // A player who relogs inside can always leave via the existing exit gem.
        if (entry == 240012 && (player->GetPhaseMask() & 2))
        {
            player->RemoveAurasDueToSpell(179183);
            player->CastSpell(player, 179182, true);
            player->SetPhaseMask(1, true);
            if (player->GetQuestObjectiveData(37660, 89673))
                player->QuestObjectiveSatisfy(240012, 1, QUEST_OBJECTIVE_GAMEOBJECT);
        }
        return true;
    }
};

struct go_toy_train_set : public GameObjectAI
{
    go_toy_train_set(GameObject* go) : GameObjectAI(go), pulseTimer(3 * IN_MILLISECONDS) {}

    uint32 pulseTimer = 0;

    void UpdateAI(uint32 diff) override
    {
        if (diff < pulseTimer)
            pulseTimer -= diff;
        else
        {
            go->CastSpell(nullptr, 61551);
            pulseTimer = 6 * IN_MILLISECONDS;
        }
    }
};

enum BrewfestMusic
{
    EVENT_BREWFESTDWARF01 = 11810, // 1.35 min
    EVENT_BREWFESTDWARF02 = 11812, // 1.55 min
    EVENT_BREWFESTDWARF03 = 11813, // 0.23 min
    EVENT_BREWFESTGOBLIN01 = 11811, // 1.08 min
    EVENT_BREWFESTGOBLIN02 = 11814, // 1.33 min
    EVENT_BREWFESTGOBLIN03 = 11815 // 0.28 min
};

enum BrewfestMusicTime
{
    EVENT_BREWFESTDWARF01_TIME = 95000,
    EVENT_BREWFESTDWARF02_TIME = 155000,
    EVENT_BREWFESTDWARF03_TIME = 23000,
    EVENT_BREWFESTGOBLIN01_TIME = 68000,
    EVENT_BREWFESTGOBLIN02_TIME = 93000,
    EVENT_BREWFESTGOBLIN03_TIME = 28000
};

enum BrewfestMusicAreas
{
    SILVERMOON = 3430, // Horde
    UNDERCITY = 1497,
    ORGRIMMAR_1 = 1296,
    ORGRIMMAR_2 = 14,
    THUNDERBLUFF = 1638,
    IRONFORGE_1 = 809, // Alliance
    IRONFORGE_2 = 1,
    STORMWIND = 12,
    EXODAR = 3557,
    DARNASSUS = 1657
};

enum BrewfestMusicEvents
{
    EVENT_BM_SELECT_MUSIC = 1,
    EVENT_BM_START_MUSIC = 2
};

struct go_brewfest_music : public GameObjectAI
{
    go_brewfest_music(GameObject* go) : GameObjectAI(go)
    {
        events.ScheduleEvent(EVENT_BM_SELECT_MUSIC, 1000);
        events.ScheduleEvent(EVENT_BM_START_MUSIC, 2000);
    }

    EventMap events;
    uint32 rnd = 0;
    uint32 musicTime = 1000;

    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
            case EVENT_BM_SELECT_MUSIC:
                if (IsHolidayActive(HOLIDAY_BREWFEST))
                    rnd = urand(0, 2);

                events.ScheduleEvent(EVENT_BM_SELECT_MUSIC, musicTime);
                break;
            case EVENT_BM_START_MUSIC:
                if (IsHolidayActive(HOLIDAY_BREWFEST))
                {
                    if (go->GetAreaId() == SILVERMOON || go->GetAreaId() == UNDERCITY || go->GetAreaId() == ORGRIMMAR_1 || go->GetAreaId() == ORGRIMMAR_2 || go->GetAreaId() == THUNDERBLUFF)
                    {
                        switch (rnd)
                        {
                        case 0:
                            go->PlayDirectSound(EVENT_BREWFESTGOBLIN01);
                            musicTime = EVENT_BREWFESTGOBLIN01_TIME;
                            break;
                        case 1:
                            go->PlayDirectSound(EVENT_BREWFESTGOBLIN02);
                            musicTime = EVENT_BREWFESTGOBLIN02_TIME;
                            break;
                        case 2:
                            go->PlayDirectSound(EVENT_BREWFESTGOBLIN03);
                            musicTime = EVENT_BREWFESTGOBLIN03_TIME;
                            break;
                        }
                    }
                    if (go->GetAreaId() == IRONFORGE_1 || go->GetAreaId() == IRONFORGE_2 || go->GetAreaId() == STORMWIND || go->GetAreaId() == EXODAR || go->GetAreaId() == DARNASSUS)
                    {
                        switch (rnd)
                        {
                        case 0:
                            go->PlayDirectSound(EVENT_BREWFESTDWARF01);
                            musicTime = EVENT_BREWFESTDWARF01_TIME;
                            break;
                        case 1:
                            go->PlayDirectSound(EVENT_BREWFESTDWARF02);
                            musicTime = EVENT_BREWFESTDWARF02_TIME;
                            break;
                        case 2:
                            go->PlayDirectSound(EVENT_BREWFESTDWARF03);
                            musicTime = EVENT_BREWFESTDWARF03_TIME;
                            break;
                        }
                    }
                }
                events.ScheduleEvent(EVENT_BM_START_MUSIC, 5000);
                break;
            default:
                break;
            }
        }
    }
};

struct go_midsummer_music : public GameObjectAI
{
    go_midsummer_music(GameObject* go) : GameObjectAI(go)
    {
        events.ScheduleEvent(EVENT_1, 1000);
    }

    EventMap events;

    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
            case EVENT_1:
            {
                if (IsHolidayActive(HOLIDAY_FIRE_FESTIVAL))
                {
                    std::list<Player*> playersNearby;
                    go->GetPlayerListInGrid(playersNearby, go->GetMap()->GetVisibilityRange());
                    for (auto& player : playersNearby)
                    {
                        if (player->GetTeamId() == TEAM_HORDE)
                            go->PlayDirectSound(12325, player);
                        else
                            go->PlayDirectSound(12319, player);
                    }
                }
                events.ScheduleEvent(EVENT_1, 5000);
                break;
            }
            default:
                break;
            }
        }
    }
};

struct go_darkmoon_faire_music : public GameObjectAI
{
    go_darkmoon_faire_music(GameObject* go) : GameObjectAI(go)
    {
        events.ScheduleEvent(EVENT_1, 1000);
    }

    EventMap events;

    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
            case EVENT_1:
                if (IsHolidayActive(HOLIDAY_DARKMOON_FAIRE))
                    go->PlayDirectSound(8440);

                events.ScheduleEvent(EVENT_1, 5000);
                break;
            default:
                break;
            }
        }
    }
};

struct go_pirate_day_music : public GameObjectAI
{
    go_pirate_day_music(GameObject* go) : GameObjectAI(go)
    {
        events.ScheduleEvent(EVENT_1, 1000);
    }

    EventMap events;

    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
            case EVENT_1:
                if (IsHolidayActive(HOLIDAY_PIRATES_DAY))
                    go->PlayDirectSound(12845);

                events.ScheduleEvent(EVENT_1, 5000);
                break;
            default:
                break;
            }
        }
    }
};

#include "SmartAI.h"
#include "CreatureTextMgr.h"
#include "ObjectAccessor.h"
#include "MotionMaster.h"
#include "TemporarySummon.h"
#include <set>

// FELSORIA_KORVAS_NATIVE_COMBAT_V9
#include "FelsoriaKorvasState.h"
#include <map>
#include <mutex>
// FELSORIA_FEL_DIALOGUE_V1: progression belongs to the rescuing player, not the spawn.
namespace FelsoriaFelDialogue
{
    constexpr uint32 QuestId = 37656;
    constexpr uint32 Korvas = 90474;
    constexpr uint32 Hunter = 90487;

    // Camp center from existing Kayn spawn 271100, map 1220, area 7364.
    Position StandCenter() { return Position(-103.434f, 6961.33f, 12.5469f, 2.02271f); }
    bool NearStand(Player* player, float range)
    {
        return player->GetMapId() == 1220 && player->IsWithinDist3d(
            -103.434f, 6961.33f, 12.5469f, range);
    }
    bool Finished(Player* player)
    {
        if (player->GetQuestRewardStatus(37449))
            return true;
        auto status = player->GetQuestStatus(37449);
        return (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE) &&
            player->GetQuestObjectiveData(37449, 90621) >= 1 &&
            player->GetQuestObjectiveData(37449, 112175) >= 1;
    }
    Position StandPosition(Creature* reference)
    {
        Position pos = StandCenter();
        std::list<Creature*> leaders;
        GetCreatureListWithEntryInGrid(leaders, reference, 89362, 60.0f);
        for (Creature* leader : leaders)
            if (leader->IsWithinDist3d(-103.434f, 6961.33f, 12.5469f, 5.0f))
            {
                // Use the core's ground-aware near-position instead of occupying Kayn's spawn.
                leader->GetNearPosition(pos, 3.0f, 1.5708f);
                return pos;
            }
        // Fallback beside the verified center, within the same flat camp platform.
        pos.Relocate(-105.434f, 6962.33f, 12.5469f, 2.02271f);
        return pos;
    }

    struct KorvasAI : SmartAI
    {
        explicit KorvasAI(Creature* creature) : SmartAI(creature) {}
        ObjectGuid ownerGuid, originalGuid, harvesterGuid, speakerGuid;
        std::set<ObjectGuid> rescuedHunters, rescuedHarvesters;
        uint32 previousCount = 0, stage = 0, timer = 0, combatLogTimer = 0;
        bool following = false, initialized = false, finished = false;
        bool mountedFollow = false;
        float unmountedRunRate = 1.0f, mountRunBaseSpeed = 0.0f;
        bool HasRelatedQuest(Player* player) const
        {
            auto status = player->GetQuestStatus(QuestId);
            return !Finished(player) &&
                (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE ||
                 player->GetQuestRewardStatus(QuestId));
        }
        void RestoreRescues()
        {
            for (ObjectGuid const& guid : rescuedHunters)
                if (Creature* hunter = ObjectAccessor::GetCreature(*me, guid))
                    hunter->UpdateObjectVisibility(true);
            for (ObjectGuid const& guid : rescuedHarvesters)
                if (GameObject* go = ObjectAccessor::GetGameObject(*me, guid))
                    go->UpdateObjectVisibility(true);
            rescuedHunters.clear();
            rescuedHarvesters.clear();
        }
        // FELSORIA_KORVAS_DIALOGUE_DIRECT_V5: this DB keys text by (Entry, Text), not by group.
        void Speak(Creature* speaker, uint32 ordinal, bool hunter)
        {
            static char const* const questions[] = {
                "Illidari, what happened here?",
                "They have tortured all of them.",
                "What did they do? What did the demons offer you?",
                "Who did this to you?",
                "Did anyone break?",
                "Where did Cyana Nightglaive go? I must find her!",
                "A warden working for the Legion? I look forward to killing her."
            };
            static char const* const answers[] = {
                "I didn't break.",
                "They did... things to me.",
                "The demons promised us unimaginable power.",
                "A warden was trying to turn us. I heard screaming.",
                "Cyana Nightglaive turned against us. She joined the Legion.",
                "Nightglaive went southwest with a warden named Cordana Felsong."
            };
            static uint32 const sounds[] = {57181, 57182, 57183, 57005, 57185, 57186, 57187};
            if (!speaker || !speaker->IsInWorld() || ordinal >= (hunter ? 6u : 7u))
                return;
            CreatureTextEntry line{};
            line.entry = hunter ? Hunter : Korvas;
            line.group = uint8(40 + ordinal);
            line.id = 0;
            line.text = hunter ? answers[ordinal] : questions[ordinal];
            line.type = CHAT_MSG_MONSTER_SAY;
            line.lang = LANG_UNIVERSAL;
            line.probability = 100.0f;
            line.sound = hunter ? 0 : sounds[ordinal];
            // SendText builds normal localized NPC chat packets without a loaded text-group lookup.
            sCreatureTextMgr->SendText(speaker, &line, ownerGuid);
            TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_SPEECH player=%u entry=%u ordinal=%u hunter=%u",
                Owner() ? Owner()->GetGUIDLow() : 0, line.entry, ordinal + 1, uint32(hunter));
        }
        void SyncFollow(Player* player)
        {
            // GM toggles and summon properties can leave the companion on friendly faction 35.
            if (me->getFaction() != player->getFaction())
            {
                TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_KORVAS_FACTION_V8 player=%u old=%u new=%u",
                    player->GetGUIDLow(), me->getFaction(), player->getFaction());
                me->setFaction(player->getFaction());
            }
            if (me->GetPhaseMask() != player->GetPhaseMask())
                me->SetPhaseMask(player->GetPhaseMask(), true);
            if (me->GetPhases() != player->GetPhases())
                me->SetPhaseId(player->GetPhases(), true);
            if (TempSummon* summon = me->ToTempSummon())
                summon->SetTempSummonType(TEMPSUMMON_MANUAL_DESPAWN);
            bool done = Finished(player);
            bool active = HasRelatedQuest(player);
            if (!initialized || active != following || done != finished)
            {
                Cancel();
                if (!active)
                    RestoreRescues();
                me->CombatStop(true);
                me->ClearUnitState(UNIT_STATE_EVADE);
                me->StopMoving();
                me->GetMotionMaster()->Clear();
                me->SetReactState(active ? REACT_DEFENSIVE : REACT_PASSIVE);
                following = active;
                finished = done;
                initialized = true;
                if (!finished)
                    me->SetFlag(UNIT_FIELD_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
                if (active)
                    me->GetMotionMaster()->MoveFollow(player, 2.0f, 3.14159f);
                else if (finished)
                {
                    Position pos = StandPosition(me);
                    me->SetHomePosition(pos);
                    me->RemoveFlag(UNIT_FIELD_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
                    if (NearStand(player, 30.0f) && me->IsWithinDist(player, 40.0f))
                        me->GetMotionMaster()->MovePoint(7007, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
                    else
                        me->NearTeleportTo(pos);
                }

                TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_KORVAS_STATE_V7 player=%u following=%u finished=%u",
                    player->GetGUIDLow(), uint32(following), uint32(finished));
            }
            if (active)
                // Creature::canCreatureAttack can use home instead of the summon owner.
                me->SetHomePosition(player->GetPosition());
            SyncMount(player);
        }
        // FELSORIA_KORVAS_MOUNT_V10: ride only while following outside combat.
        void SyncMount(Player* player)
        {
            bool ride = following && !finished && me->isAlive() && player->isAlive() &&
                player->IsMounted() && !player->isInCombat() && !me->isInCombat();
            if (ride)
            {
                if (!mountedFollow)
                {
                    // Mount() changes the model/flag; it does not grant movement speed.
                    unmountedRunRate = me->GetSpeedRate(MOVE_RUN);
                    mountRunBaseSpeed = unmountedRunRate > 0.0f ?
                        me->GetSpeed(MOVE_RUN) / unmountedRunRate : 0.0f;
                    mountedFollow = true;
                }
                if (!me->IsMounted())
                    me->Mount(67597); // This companion's original mount display.
                if (mountRunBaseSpeed > 0.0f)
                    me->SetSpeed(MOVE_RUN, player->GetSpeed(MOVE_RUN) / mountRunBaseSpeed);
            }
            else
            {
                if (me->IsMounted())
                    me->Dismount();
                if (mountedFollow)
                {
                    me->SetSpeed(MOVE_RUN, unmountedRunRate);
                    mountedFollow = false;
                }
            }
        }
        char const* RejectTarget(Unit const* target) const
        {
            Player* player = Owner();
            if (!following || finished) return "not_escorting";
            if (!player || !player->isAlive()) return "owner_unavailable";
            if (player->isGameMaster()) return "owner_gm_on";
            if (!target) return "no_owner_target";
            if (target->IsPlayer()) return "player_target";
            if (!target->isAlive() || !target->IsInWorld()) return "target_dead_or_gone";
            if (!me->IsInMap(target)) return "different_map";
            if (!player->InSamePhase(target)) return "owner_phase_rejected";
            if (!me->InSamePhase(target)) return "helper_phase_rejected";
            if (!player->IsWithinDist(target, 40.0f) || !me->IsWithinDist(target, 60.0f)) return "leash";
            if (!me->IsValidAttackTarget(target)) return "core_attack_target_rejected";
            if (player->getVictim() != target && player->getAttackerForHelper() != target &&
                target->getVictim() != player && target->getVictim() != me)
                return "unrelated_target";
            // Temporary LOS/path changes must not tear down an existing chase.
            return nullptr;
        }
        bool CanAIAttack(Unit const* target) const override { return !RejectTarget(target); }
        // FELSORIA_KORVAS_COMBAT_RESUME_V11: combat may clear the victim before our update.
        void ResumeFollow(Player* player)
        {
            if (!following || finished || !me->isAlive() || !player->isAlive() || me->getVictim())
                return;
            MovementGeneratorType movement = me->GetMotionMaster()->GetCurrentMovementGeneratorType();
            // Do not evade out of crowd control or interrupt scripted point movement.
            if (movement != IDLE_MOTION_TYPE && movement != CHASE_MOTION_TYPE &&
                movement != HOME_MOTION_TYPE && movement != FOLLOW_MOTION_TYPE)
                return;
            // Unit::AttackStop can clear the victim without an evade callback.
            if (me->isInCombat())
                EnterEvadeMode();
            if (me->getVictim() || me->isInCombat() ||
                me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
                return;
            me->ClearUnitState(UNIT_STATE_EVADE);
            SetRun(true);
            me->GetMotionMaster()->MoveFollow(player, 2.0f, 3.14159f);
            TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_KORVAS_RESUME_V11 player=%u helper=%u previousMotion=%u",
                player->GetGUIDLow(), me->GetGUIDLow(), uint32(movement));
        }
        void AssistOwner(Player* player)
        {
            if (!following || !player->isAlive() || player->isGameMaster())
            {
                if (me->isInCombat())
                    EnterEvadeMode();
                return;
            }
            if (player->isInCombat() && me->IsMounted())
                me->Dismount();
            Unit* target = me->getVictim();
            if (target && RejectTarget(target))
            {
                EnterEvadeMode();
                target = nullptr;
            }
            if (!target && player->isInCombat())
            {
                target = player->getVictim();
                if (RejectTarget(target))
                    target = player->getAttackerForHelper();
                if (!RejectTarget(target))
                {
                    SetRun(true);
                    SmartAI::AttackStart(target);
                }
            }
            if (!me->getVictim())
            {
                // A live attacker of the companion is still real combat, not stale state.
                Unit* attacker = me->getAttackerForHelper();
                if (!RejectTarget(attacker))
                {
                    SetRun(true);
                    SmartAI::AttackStart(attacker);
                }
            }
            if (!me->getVictim())
            {
                // Recover after the core has already cleared the combat target.
                ResumeFollow(player);
                SyncMount(player);
            }
            bool swing = false;
            if (Unit* victim = me->getVictim())
                if (me->IsWithinLOSInMap(victim))
                    swing = DoMeleeAttackIfReady();
            if (!combatLogTimer && (player->isInCombat() || me->isInCombat()))
            {
                Unit* candidate = me->getVictim();
                if (!candidate) candidate = player->getAttackerForHelper();
                char const* rejected = RejectTarget(candidate);
                TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_KORVAS_COMBAT_V9 player=%u helper=%u target=%u victim=%u faction=%u ownerFaction=%u react=%u flags=%u flags2=%u reason=%s los=%u swing=%u targetEntry=%u distance=%.2f mounted=%u min=%.2f max=%.2f",
                    player->GetGUIDLow(), me->GetGUIDLow(), candidate ? candidate->GetGUIDLow() : 0,
                    me->getVictim() ? me->getVictim()->GetGUIDLow() : 0, me->getFaction(), player->getFaction(),
                    uint32(me->GetReactState()), me->GetUInt32Value(UNIT_FIELD_FLAGS), me->GetUInt32Value(UNIT_FIELD_FLAGS_2),
                    rejected ? rejected : "ok", uint32(candidate && me->IsWithinLOSInMap(candidate)), uint32(swing),
                    candidate ? candidate->GetEntry() : 0, candidate ? me->GetDistance(candidate) : -1.0f,
                    uint32(me->IsMounted()), me->GetFloatValue(UNIT_FIELD_MIN_DAMAGE), me->GetFloatValue(UNIT_FIELD_MAX_DAMAGE));
                combatLogTimer = 5000;
            }
        }
        void DamageDealt(Unit* target, uint32& damage, DamageEffectType type) override
        {
            SmartAI::DamageDealt(target, damage, type);
            if (damage && target && !combatLogTimer)
            {
                TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_KORVAS_DAMAGE_V9 helper=%u target=%u damage=%u",
                    me->GetGUIDLow(), target->GetGUIDLow(), damage);
                combatLogTimer = 5000;
            }
        }

        void IsSummonedBy(Unit* summoner) override
        {
            if (Player* player = summoner->ToPlayer())
            {
                ownerGuid = player->GetGUID();
                me->SetPhaseMask(player->GetPhaseMask(), true);
                me->SetPhaseId(player->GetPhases(), true);
                GuidList* helpers = player->GetSummonList(Korvas);
                GuidList previous = *helpers;
                for (ObjectGuid const& guid : previous)
                    if (guid != me->GetGUID())
                        if (Creature* existing = ObjectAccessor::GetCreature(*player, guid))
                            if (existing->IsInWorld() && existing->isAlive() && existing->GetAnyOwner() == player)
                            {
                                me->DespawnOrUnsummon();
                                return;
                            }
                helpers->remove(me->GetGUID());
                helpers->push_back(me->GetGUID());
                TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_HELPER player=%u helper=%u registered=1",
                    player->GetGUIDLow(), me->GetGUIDLow());
                me->AddPlayerInPersonnalVisibilityList(ownerGuid);
                if (CreatureTemplate const* identity = sObjectMgr->GetCreatureTemplate(90255))
                    me->SetName(identity->Name[0]);
            }
            SmartAI::IsSummonedBy(summoner);
            SetAutoAttack(true);
            SetCombatMove(true);
            if (Player* player = Owner())
                SyncFollow(player);
        }
        Player* Owner() const
        {
            Player* player = ObjectAccessor::GetPlayer(*me, ownerGuid);
            return player && me->GetAnyOwner() == player && me->IsInMap(player) ? player : nullptr;
        }
        void EnterEvadeMode() override
        {
            SmartAI::EnterEvadeMode();
            // A companion resumes following rather than returning to its old summon position.
            me->ClearUnitState(UNIT_STATE_EVADE);
            if (Player* player = Owner())
                if (following && player->isAlive() && !me->getVictim())
                    me->GetMotionMaster()->MoveFollow(player, 2.0f, 3.14159f);
        }
        void Cancel(bool restore = true)
        {
            if (Creature* speaker = ObjectAccessor::GetCreature(*me, speakerGuid))
                speaker->DespawnOrUnsummon();
            if (restore && stage)
            {
                if (Creature* original = ObjectAccessor::GetCreature(*me, originalGuid))
                    original->UpdateObjectVisibility(true);
                if (GameObject* harvester = ObjectAccessor::GetGameObject(*me, harvesterGuid))
                    harvester->UpdateObjectVisibility(true);
            }
            speakerGuid = originalGuid = harvesterGuid = ObjectGuid::Empty;
            stage = timer = 0;
        }
        bool Start(Player* player, GameObject* harvester, Creature* hunter)
        {
            if (player != Owner() || !player->isAlive() || stage ||
                player->GetQuestStatus(QuestId) != QUEST_STATUS_INCOMPLETE ||
                !me->IsWithinDist(player, 60.0f) || !harvester->IsWithinDist(player, 10.0f) ||
                !hunter->isAlive() || !hunter->IsInMap(player) || !hunter->InSamePhase(player) ||
                !hunter->IsWithinDist(harvester, 10.0f) ||
                rescuedHunters.count(hunter->GetGUID()) || rescuedHarvesters.count(harvester->GetGUID()))
            {
                TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_REJECT player=%u stage=%u owner=%u alive=%u helperNear=%u goNear=%u hunterPhase=%u duplicate=%u",
                    player->GetGUIDLow(), stage, uint32(player == Owner()), uint32(player->isAlive()),
                    uint32(me->IsWithinDist(player, 60.0f)), uint32(harvester->IsWithinDist(player, 10.0f)),
                    uint32(hunter->InSamePhase(player)), uint32(rescuedHunters.count(hunter->GetGUID()) || rescuedHarvesters.count(harvester->GetGUID())));
                return false;
            }
            uint32 count = player->GetQuestObjectiveData(QuestId, Hunter);
            if (count >= 6)
                return false;
            Creature* speaker = player->SummonCreature(Hunter, hunter->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 30000);
            if (!speaker)
            {
                TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_REJECT player=%u reason=speaker_summon_failed", player->GetGUIDLow());
                return false;
            }
            speaker->SetDisplayId(hunter->GetDisplayId());
            speaker->AddPlayerInPersonnalVisibilityList(ownerGuid);
            previousCount = count;
            originalGuid = hunter->GetGUID();
            harvesterGuid = harvester->GetGUID();
            speakerGuid = speaker->GetGUID();
            // Hide only for this player; other players retain the shared rescue.
            hunter->DestroyForPlayer(player);
            harvester->DestroyForPlayer(player);
            stage = 1;
            timer = 3500;
            Speak(me, previousCount, false);
            TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_BEGIN player=%u ordinal=%u hunter=%u", player->GetGUIDLow(), previousCount + 1, hunter->GetGUIDLow());
            return true;
        }
        void UpdateAI(uint32 diff) override
        {
            combatLogTimer = combatLogTimer > diff ? combatLogTimer - diff : 0;
            Player* player = Owner();
            if (!player)
            {
                Cancel();
                me->DespawnOrUnsummon();
                return;
            }
            SyncFollow(player);
            if (finished)
                return;
            if (!following)
            {
                Cancel();
                RestoreRescues();
                me->DespawnOrUnsummon();
                return;
            }
            GetScript()->OnUpdate(diff);
            // This personal companion owns victim selection; generic SelectVictim can evade it.
            AssistOwner(player);
            if (!stage)
            {
                if (player && player->GetQuestStatus(QuestId) != QUEST_STATUS_INCOMPLETE &&
                    player->GetQuestStatus(QuestId) != QUEST_STATUS_COMPLETE)
                {
                    RestoreRescues();
                }
                return;
            }
            Creature* speaker = ObjectAccessor::GetCreature(*me, speakerGuid);
            Creature* original = ObjectAccessor::GetCreature(*me, originalGuid);
            GameObject* harvester = ObjectAccessor::GetGameObject(*me, harvesterGuid);
            if (!player || !player->isAlive() || player->GetQuestStatus(QuestId) != QUEST_STATUS_INCOMPLETE ||
                uint32(player->GetQuestObjectiveData(QuestId, Hunter)) != previousCount ||
                !me->IsWithinDist(player, 80.0f) || !speaker || !speaker->isAlive() || !original || !harvester)
            {
                // No credit was committed: make the shared objects visible again on cancellation.
                if (original)
                    original->UpdateObjectVisibility(true);
                if (harvester)
                    harvester->UpdateObjectVisibility(true);
                TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_CANCEL player=%u reason=actor_or_state_changed", player ? player->GetGUIDLow() : 0);
                Cancel();
                return;
            }
            if (timer > diff)
            {
                timer -= diff;
                return;
            }
            if (stage == 1)
            {
                Speak(speaker, previousCount, true);
                stage = 2;
                timer = 5000;
                return;
            }
            if (stage == 2 && previousCount == 5)
            {
                Speak(me, 6, false);
                stage = 3;
                timer = 3500;
                return;
            }
            // Commit exactly once after the ordered dialogue, only to its owner.
            rescuedHunters.insert(originalGuid);
            rescuedHarvesters.insert(harvesterGuid);
            player->QuestObjectiveSatisfy(Hunter, 1);
            TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_RESCUE player=%u ordinal=%u hunter=%u",
                player->GetGUIDLow(), previousCount + 1, original->GetGUIDLow());
            Cancel(false);
        }
    };
}

class npc_felsoria_fel_korvas : public CreatureScript
{
public:
    npc_felsoria_fel_korvas() : CreatureScript("npc_felsoria_fel_korvas") {}
    CreatureAI* GetAI(Creature* creature) const override { return new FelsoriaFelDialogue::KorvasAI(creature); }
};

class npc_felsoria_korvas_stationary_v9 : public CreatureScript
{
public:
    npc_felsoria_korvas_stationary_v9() : CreatureScript("npc_felsoria_korvas_stationary_v9") {}
    CreatureAI* GetAI(Creature* creature) const override { return new SmartAI(creature); }
    bool OnQuestAccept(Player* player, Creature* creature, Quest const* quest) override
    {
        if (creature->GetMapId() != 1220 || creature->GetDBTableGUIDLow() != 269594 || quest->GetQuestId() != 37656)
            return false;
        bool exists = false;
        GuidList helpers = *player->GetSummonList(90474);
        for (ObjectGuid const& guid : helpers)
            if (Creature* helper = ObjectAccessor::GetCreature(*player, guid))
                if (helper->isAlive() && helper->GetAnyOwner() == player)
                    exists = true;
        if (!exists)
            if (Creature* helper = player->SummonCreature(90474, player->GetPosition(), TEMPSUMMON_MANUAL_DESPAWN, 0))
                helper->setFaction(player->getFaction());
        creature->DestroyForPlayer(player);
        player->UpdateVisibilityForPlayer();
        TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_KORVAS_ACCEPT_V9 player=%u native=%u", player->GetGUIDLow(), creature->GetGUIDLow());
        return true;
    }
};

class go_felsoria_fel_harvester : public GameObjectScript
{
public:
    go_felsoria_fel_harvester() : GameObjectScript("go_felsoria_fel_harvester") {}
    bool OnGossipHello(Player* player, GameObject* go) override
    {
        using namespace FelsoriaFelDialogue;
        TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_USE player=%u go=%u entry=%u questStatus=%u",
            player->GetGUIDLow(), go->GetGUIDLow(), go->GetEntry(), uint32(player->GetQuestStatus(QuestId)));
        if (player->GetQuestStatus(QuestId) != QUEST_STATUS_INCOMPLETE || !player->isAlive() ||
            !go->IsInMap(player) || !go->InSamePhase(player) || !go->IsWithinDist(player, 10.0f))
            return true;
        GuidList helpers = *player->GetSummonList(Korvas);
        std::list<Creature*> nearbyHelpers;
        GetCreatureListWithEntryInGrid(nearbyHelpers, player, Korvas, 60.0f);
        for (Creature* helper : nearbyHelpers)
            if (helper->GetAnyOwner() == player)
                helpers.push_back(helper->GetGUID());
        TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_CLICK player=%u go=%u entry=%u helpers=%u",
            player->GetGUIDLow(), go->GetGUIDLow(), go->GetEntry(), uint32(helpers.size()));
        for (ObjectGuid const& guid : helpers)
            if (Creature* helper = ObjectAccessor::GetCreature(*player, guid))
                if (helper->GetAnyOwner() == player)
                    if (auto ai = dynamic_cast<KorvasAI*>(helper->AI()))
                    {
                        std::list<Creature*> hunters;
                        // Search in the player's phase, then retain the original 10-yard GO radius.
                        GetCreatureListWithEntryInGrid(hunters, player, Hunter, 20.0f);
                        Creature* nearest = nullptr;
                        for (Creature* hunter : hunters)
                            if (hunter->isAlive() && !hunter->GetAnyOwner() && hunter->InSamePhase(player) &&
                                go->IsWithinDist(hunter, 10.0f) && !ai->rescuedHunters.count(hunter->GetGUID()) &&
                                (!nearest || go->GetDistance(hunter) < go->GetDistance(nearest)))
                                nearest = hunter;
                        if (nearest)
                            ai->Start(player, go, nearest);
                        else
                            TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_REJECT player=%u reason=no_eligible_prisoner candidates=%u", player->GetGUIDLow(), uint32(hunters.size()));
                        return true;
                    }
        TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_FEL_REJECT player=%u reason=no_owned_helper", player->GetGUIDLow());
        return true;
    }
};

// FELSORIA_KORVAS_RECOVERY_V7: rebuild this player's personal actor after login/grid unload.
class player_felsoria_korvas_lifecycle : public PlayerScript
{
public:
    player_felsoria_korvas_lifecycle() : PlayerScript("player_felsoria_korvas_lifecycle") {}
    std::map<ObjectGuid, uint32> timers;
    std::map<ObjectGuid, bool> stationaryVisibility;
    std::mutex timerLock;
    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> guard(timerLock);
        timers.erase(player->GetGUID());
        stationaryVisibility.erase(player->GetGUID());
    }
    void OnUpdate(Player* player, uint32 diff) override
    {
        using namespace FelsoriaFelDialogue;
        if (!player->IsInWorld() || player->GetMapId() != 1220 || player->GetCurrentZoneID() != 7334)
            return;
        {
            std::lock_guard<std::mutex> guard(timerLock);
            uint32& timer = timers[player->GetGUID()];
            if (timer > diff) { timer -= diff; return; }
            timer = 2000;
        }
        if (!player->isAlive())
            return;
        auto status = player->GetQuestStatus(37449);
        if (status == QUEST_STATUS_INCOMPLETE && NearStand(player, 15.0f) &&
            player->GetQuestObjectiveData(37449, 90621) >= 1 &&
            player->GetQuestObjectiveData(37449, 112175) == 0)
        {
            // Meet back at Illidari Stand: require the real kill and physical return.
            player->QuestObjectiveSatisfy(112175, 1);
            TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_KORVAS_STAND_CREDIT_V7 player=%u", player->GetGUIDLow());
        }
        bool visible = FelsoriaKorvasState::StationaryVisible(player);
        bool refresh = false;
        {
            std::lock_guard<std::mutex> guard(timerLock);
            auto previous = stationaryVisibility.find(player->GetGUID());
            refresh = previous == stationaryVisibility.end() || previous->second != visible;
            stationaryVisibility[player->GetGUID()] = visible;
        }
        if (refresh && player->IsWithinDist3d(-412.979f, 7113.75f, 1.65625f, 120.0f))
            player->UpdateVisibilityForPlayer();
        bool done = Finished(player);
        bool available = player->GetQuestRewardStatus(36920) ||
            player->GetQuestRewardStatus(40815) || player->GetQuestRewardStatus(44140);
        if (!done && !available)
            return;
        auto fel = player->GetQuestStatus(QuestId);
        bool escort = !done && (fel == QUEST_STATUS_INCOMPLETE || fel == QUEST_STATUS_COMPLETE ||
            player->GetQuestRewardStatus(QuestId));
        if (done && !NearStand(player, 80.0f))
            return;
        if (!done && !escort)
            return; // The persistent DB spawn, never a waiting personal summon, supplies quests.
        GuidList helpers = *player->GetSummonList(Korvas);
        for (ObjectGuid const& guid : helpers)
            if (Creature* helper = ObjectAccessor::GetCreature(*player, guid))
                if (helper->isAlive() && helper->GetAnyOwner() == player && helper->IsInWorld())
                    return;
        std::list<Creature*> nearby;
        GetCreatureListWithEntryInGrid(nearby, player, Korvas, 120.0f);
        for (Creature* helper : nearby)
            if (helper->isAlive() && helper->GetAnyOwner() == player)
                return;
        Position pos = done ? StandCenter() : player->GetPosition();
        if (Creature* helper = player->SummonCreature(Korvas, pos, TEMPSUMMON_MANUAL_DESPAWN, 0))
        {
            helper->setFaction(player->getFaction());
            TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_KORVAS_RECOVER_V7 player=%u helper=%u finished=%u",
                player->GetGUIDLow(), helper->GetGUIDLow(), uint32(done));
        }
    }
};

// FELSORIA_STELLAGOSA_RELEASE_V1
#include "FelsoriaStellagosaState.h"

namespace FelsoriaStellagosaRelease
{
    constexpr uint32 NativeSpawn = 338233;
    constexpr uint32 FlightPoint = 37450;

    bool NearCage(Player const* player)
    {
        return player->GetMapId() == 1220 &&
            player->IsWithinDist3d(-736.529f, 7336.21f, 26.2122f, 90.0f);
    }

    struct DrakeAI : ScriptedAI
    {
        explicit DrakeAI(Creature* creature) : ScriptedAI(creature)
        {
            me->SetReactState(REACT_PASSIVE);
        }
        ObjectGuid ownerGuid;
        uint32 stage = 0, timer = 0;

        void MoveInLineOfSight(Unit* unit) override
        {
            if (me->GetDBTableGUIDLow() != NativeSpawn || !unit)
                return;
            if (Player* player = unit->ToPlayer())
                if (FelsoriaStellagosaState::Active(player) && player->isAlive() &&
                    me->IsInMap(player) && me->InSamePhase(player) &&
                    me->IsWithinDist(player, 25.0f) && me->IsWithinLOSInMap(player) &&
                    !player->GetQuestObjectiveData(37450, 90546))
                    player->QuestObjectiveSatisfy(90546, 1);
        }

        void IsSummonedBy(Unit* summoner) override
        {
            Player* player = summoner ? summoner->ToPlayer() : nullptr;
            if (!player || !FelsoriaStellagosaState::Ready(player) || !NearCage(player))
            {
                me->DespawnOrUnsummon();
                return;
            }
            // Resolve the reviewed existing objective before committing anything.
            Quest const* quest = sQuestDataStore->GetQuestTemplate(37450);
            QuestObjective const* released = nullptr;
            if (quest)
                for (QuestObjective const& objective : quest->GetObjectives())
                    if (objective.ObjectID == 105635 && objective.StorageIndex == 3 &&
                        objective.Type == QUEST_OBJECTIVE_MONSTER && objective.Amount == 1)
                        released = &objective;
            if (!released)
            {
                TC_LOG_ERROR(LOG_FILTER_GENERAL, "FELSORIA_STELLAGOSA_RELEASE_V1 missing reviewed objective 105635");
                me->DespawnOrUnsummon();
                return;
            }
            ownerGuid = player->GetGUID();
            me->SetPhaseMask(player->GetPhaseMask(), true);
            me->SetPhaseId(player->GetPhases(), true);
            me->AddPlayerInPersonnalVisibilityList(ownerGuid);
            me->SetCanFly(true);
            me->SetDisableGravity(true);
            me->RemoveAurasDueToSpell(65612);
            // Directly update this optional objective even if the last lock completed the quest.
            // QuestObjectiveSatisfy ignores QUEST_STATUS_COMPLETE in this core.
            player->SetQuestObjectiveData(quest, released, 1);
            player->SendQuestUpdateAddCredit(quest, me->GetGUID(), *released, 1);
            if (player->CanCompleteQuest(37450))
                player->CompleteQuest(37450);
            player->UpdateVisibilityForPlayer();
            Talk(0, ownerGuid);
            stage = 1;
            timer = 1500;
            TC_LOG_INFO(LOG_FILTER_GENERAL, "FELSORIA_STELLAGOSA_RELEASE_V1 player=%u actor=%u locks=3",
                player->GetGUIDLow(), me->GetGUIDLow());
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (stage == 3 && type == POINT_MOTION_TYPE && id == FlightPoint)
                me->DespawnOrUnsummon(500);
        }

        void UpdateAI(uint32 diff) override
        {
            if (ownerGuid.IsEmpty())
                return; // The shared captive has no periodic release event.
            Player* player = ObjectAccessor::GetPlayer(*me, ownerGuid);
            if (!player || !me->IsInMap(player) ||
                (!FelsoriaStellagosaState::Active(player) && !player->GetQuestRewardStatus(37450)))
            {
                me->DespawnOrUnsummon();
                return;
            }
            // Nearby native chain casters must not attach the departing personal actor.
            me->RemoveAurasDueToSpell(65612);
            if (timer > diff) { timer -= diff; return; }
            if (stage == 1)
            {
                Talk(1, ownerGuid);
                stage = 2;
                timer = 2500;
            }
            else if (stage == 2)
            {
                me->SetWalk(false);
                me->GetMotionMaster()->MovePoint(FlightPoint, -685.12f, 7294.91f, 36.18f, false);
                // Preserve Kor'vas' ownership and combat AI; this is only her scene reaction.
                GuidList helpers = *player->GetSummonList(90474);
                for (ObjectGuid const& guid : helpers)
                    if (Creature* helper = ObjectAccessor::GetCreature(*player, guid))
                        if (helper->GetAnyOwner() == player && helper->IsInMap(me) &&
                            helper->IsWithinDist(me, 70.0f))
                        {
                            CreatureTextEntry line{};
                            line.entry = 90474;
                            line.text = "Stellagosa, no... come back. Damn!";
                            line.type = CHAT_MSG_MONSTER_YELL;
                            line.lang = LANG_UNIVERSAL;
                            line.probability = 100.0f;
                            sCreatureTextMgr->SendText(helper, &line, ownerGuid);
                            break;
                        }
                stage = 3;
                timer = 15000; // Bounded cleanup if movement cannot finish.
            }
            else
                me->DespawnOrUnsummon();
        }
    };
}

class npc_felsoria_stellagosa_release_v1 : public CreatureScript
{
public:
    npc_felsoria_stellagosa_release_v1() : CreatureScript("npc_felsoria_stellagosa_release_v1") {}
    CreatureAI* GetAI(Creature* creature) const override { return new FelsoriaStellagosaRelease::DrakeAI(creature); }
};

class player_felsoria_stellagosa_release_v1 : public PlayerScript
{
public:
    player_felsoria_stellagosa_release_v1() : PlayerScript("player_felsoria_stellagosa_release_v1") {}
    struct State { uint32 timer = 0; bool initialized = false, freed = false; };
    std::map<ObjectGuid, State> states;
    std::mutex stateLock;
    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> guard(stateLock);
        states.erase(player->GetGUID());
    }
    void OnUpdate(Player* player, uint32 diff) override
    {
        using namespace FelsoriaStellagosaState;
        if (!player->IsInWorld() || !FelsoriaStellagosaRelease::NearCage(player))
            return;
        bool refresh = false;
        {
            std::lock_guard<std::mutex> guard(stateLock);
            State& state = states[player->GetGUID()];
            if (state.timer > diff) { state.timer -= diff; return; }
            state.timer = 500;
            bool freed = Freed(player);
            refresh = !state.initialized || freed != state.freed;
            state.initialized = true;
            state.freed = freed;
        }
        if (refresh)
            player->UpdateVisibilityForPlayer();
        if (!Ready(player))
            return;
        // Only a visible, phased, native captive can start this player's release.
        std::list<Creature*> captives;
        GetCreatureListWithEntryInGrid(captives, player, Drake, 90.0f);
        for (Creature* captive : captives)
            if (captive->GetDBTableGUIDLow() == FelsoriaStellagosaRelease::NativeSpawn &&
                captive->isAlive() && captive->InSamePhase(player))
            {
                player->SummonCreature(Drake, captive->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 25000);
                return;
            }
    }
};

void AddSC_go_scripts()
{
    new npc_felsoria_stellagosa_release_v1;
    new player_felsoria_stellagosa_release_v1;
    new npc_felsoria_fel_korvas;
    new npc_felsoria_korvas_stationary_v9;
    new player_felsoria_korvas_lifecycle;
    new go_felsoria_fel_harvester;
    new go_cat_figurine;
    new go_northern_crystal_pylon;
    new go_eastern_crystal_pylon;
    new go_western_crystal_pylon;
    new go_barov_journal;
    new go_field_repair_bot_74A;
    new go_gilded_brazier;
    //new go_orb_of_command;
    new go_shrine_of_the_birds;
    new go_southfury_moonstone;
    new go_tablet_of_madness;
    new go_tablet_of_the_seven;
    new go_jump_a_tron;
    new go_ethereum_prison;
    new go_ethereum_stasis;
    new go_resonite_cask;
    new go_sacred_fire_of_life;
    new go_tele_to_dalaran_crystal;
    new go_tele_to_violet_stand;
    new go_fel_crystalforge;
    new go_bashir_crystalforge;
    new go_matrix_punchograph;
    new go_scourge_cage;
    new go_arcane_prison;
    new go_blood_filled_orb;
    new go_jotunheim_cage;
    new go_table_theka;
    new go_inconspicuous_landmark;
    new go_ethereal_teleport_pad;
    new go_soulwell;
    new go_tadpole_cage;
    new go_dragonflayer_cage;
    new go_amberpine_outhouse;
    new go_hive_pod;
    new go_massive_seaforium_charge;
    new go_gjalerbron_cage;
    new go_large_gjalerbron_cage;
    new go_veil_skith_cage;
    new go_midsummer_bonfire;
    new go_sikthik_cage;
    new go_forsaken_stink_bomb;
    new go_ephemeral_crystal;
    new go_azsuna_soul_gem;
    RegisterGameObjectAI(go_toy_train_set);
    RegisterGameObjectAI(go_brewfest_music);
    RegisterGameObjectAI(go_midsummer_music);
    RegisterGameObjectAI(go_darkmoon_faire_music);
    RegisterGameObjectAI(go_pirate_day_music);
}
