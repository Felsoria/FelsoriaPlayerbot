/*
    Dungeon : Halls of Valor 100-110
*/

#include "Group.h"
#include "halls_of_valor.h"
#include "PrecompiledHeaders/ScriptPCH.h"
#include "WorldPacket.h"
#include "InstancePackets.h"

DoorData const doorData[] =
{
    {GO_HYMDALL_ENTER_DOOR,         DATA_HYMDALL,         DOOR_TYPE_ROOM,       BOUNDARY_NONE},
    {GO_HYMDALL_EXIT_DOOR,          DATA_HYMDALL,         DOOR_TYPE_PASSAGE,    BOUNDARY_NONE},
    {GO_HYRJA_DOOR,                 DATA_HYRJA,           DOOR_TYPE_ROOM,       BOUNDARY_NW},
    {GO_GATES_OF_GLORY_DOOR,        DATA_HYRJA,           DOOR_TYPE_PASSAGE,    BOUNDARY_NONE},
    {GO_GATES_OF_GLORY_DOOR,        DATA_FENRYR,          DOOR_TYPE_PASSAGE,    BOUNDARY_NONE},
    {GO_ODYN_AND_SKOVALD_DOOR,      DATA_SKOVALD,         DOOR_TYPE_ROOM,       BOUNDARY_NONE},
    {GO_ODYN_AND_SKOVALD_DOOR,      DATA_ODYN,            DOOR_TYPE_ROOM,       BOUNDARY_NONE},
    {0, 0, DOOR_TYPE_ROOM, BOUNDARY_NONE}, // LoadDoorData uses a zero-entry sentinel.
};

class instance_halls_of_valor : public InstanceMapScript
{
public:
    instance_halls_of_valor() : InstanceMapScript("instance_halls_of_valor", 1477) { }

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_halls_of_valor_InstanceMapScript(map);
    }

    struct instance_halls_of_valor_InstanceMapScript : public InstanceScript
    {
        instance_halls_of_valor_InstanceMapScript(Map* map) : InstanceScript(map) {}

        WorldLocation loc_res_pla;

        ObjectGuid HymdallChestGUID;
        ObjectGuid hyrjaGUID;
        ObjectGuid feryrGUID;
        ObjectGuid skovaldGUID;
        ObjectGuid aegisGUID;
        ObjectGuid OdynGUID;
        ObjectGuid OdynChestGUID;
        ObjectGuid HymdallGUID;

        std::map<uint32, ObjectGuid> runicBrandGUIDconteiner;

        bool StartEvent;
        uint32 fenryrEventDone;
        uint32 skovaldEventDone;
        uint32 const GoRunicColour[5] =
        {
            GO_RUNIC_BRAND_PURE, GO_RUNIC_BRAND_RED, GO_RUNIC_BRAND_YELLOW,
            GO_RUNIC_BRAND_BLUE, GO_RUNIC_BRAND_GREEN
        };
        uint32 checkTimer = 1000;
        uint32 checkTimerAura = 1000;

        void Initialize() override
        {
            SetBossNumber(MAX_ENCOUNTER);
            LoadDoorData(doorData);

            StartEvent = false;
            fenryrEventDone = 0;
            skovaldEventDone = 0;
            checkTimer = 1000;
            checkTimerAura = 1000;
            runicBrandGUIDconteiner.clear();
        }

        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_HYRJA:
                    hyrjaGUID = creature->GetGUID();
                    break;
                case NPC_BOSS_FENRYR:    
                    feryrGUID = creature->GetGUID();
                    creature->SetVisible(fenryrEventDone == DONE);
                    creature->SetReactState(fenryrEventDone == DONE ? REACT_AGGRESSIVE : REACT_PASSIVE);
                    break;
                case NPC_GOD_KING_SKOVALD:
                    skovaldGUID = creature->GetGUID();
                    break;
                case NPC_AEGIS_OF_AGGRAMAR:
                    aegisGUID = creature->GetGUID();
                    break;
                case NPC_ODYN:
                    OdynGUID = creature->GetGUID();
                    break;
                case NPC_HYMDALL:
                    HymdallGUID = creature->GetGUID();
                    break;
            }
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_HYMDALL_ENTER_DOOR:
                case GO_HYMDALL_EXIT_DOOR:
                case GO_HYRJA_DOOR:
                case GO_GATES_OF_GLORY_DOOR:
                case GO_ODYN_AND_SKOVALD_DOOR:
                    AddDoor(go, true);
                    break;
                case GO_HYMDALL_CHEST:
                    HymdallChestGUID = go->GetGUID();
                    RestoreRewardChest(go, DATA_HYMDALL);
                    break;
                case GO_ODYN_CHEST:
                    OdynChestGUID = go->GetGUID();
                    RestoreRewardChest(go, DATA_ODYN);
                    break;
                case GO_RUNIC_BRAND_PURE:
                    runicBrandGUIDconteiner[go->GetEntry()] = go->GetGUID();
                    break;
                case GO_RUNIC_BRAND_RED:
                    runicBrandGUIDconteiner[go->GetEntry()] = go->GetGUID();
                    break;
                case GO_RUNIC_BRAND_YELLOW:
                    runicBrandGUIDconteiner[go->GetEntry()] = go->GetGUID();
                    break;
                case GO_RUNIC_BRAND_BLUE:
                    runicBrandGUIDconteiner[go->GetEntry()] = go->GetGUID();
                    break;
                case GO_RUNIC_BRAND_GREEN:
                    runicBrandGUIDconteiner[go->GetEntry()] = go->GetGUID();
                    break;
                default:
                    break;
            }
        }

        void RestoreRewardChest(GameObject* chest, uint32 encounter)
        {
            if (!chest || instance->GetDifficultyID() == DIFFICULTY_MYTHIC_KEYSTONE ||
                GetBossState(encounter) != DONE || chest->isSpawned())
                return;

            // Negative DB spawn times hide these chests until explicitly activated.
            // Restore availability without replaying boss credit or completion events.
            chest->SetRespawnTime(86400);
        }

        void RestoreLoadedRewardChests()
        {
            RestoreRewardChest(instance->GetGameObject(HymdallChestGUID), DATA_HYMDALL);
            RestoreRewardChest(instance->GetGameObject(OdynChestGUID), DATA_ODYN);
        }

        void OnGameObjectRemove(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_HYMDALL_ENTER_DOOR:
                case GO_HYMDALL_EXIT_DOOR:
                case GO_HYRJA_DOOR:
                case GO_GATES_OF_GLORY_DOOR:
                case GO_ODYN_AND_SKOVALD_DOOR:
                    AddDoor(go, false);
                    break;
                default:
                    break;
            }
            auto itr = runicBrandGUIDconteiner.find(go->GetEntry());
            if (itr != runicBrandGUIDconteiner.end() && itr->second == go->GetGUID())
                runicBrandGUIDconteiner.erase(itr);
        }

        void SetRuneState(uint32 colour, bool active)
        {
            if (colour >= 5)
                return;
            auto itr = runicBrandGUIDconteiner.find(GoRunicColour[colour]);
            if (itr != runicBrandGUIDconteiner.end())
                if (GameObject* rune = instance->GetGameObject(itr->second))
                    rune->SetGoState(active ? GO_STATE_ACTIVE : GO_STATE_READY);
        }

        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                return false;

            switch (type)
            {
                case DATA_HYMDALL:
                    if (state == DONE)
                    {
                        if (instance->GetDifficultyID() != DIFFICULTY_MYTHIC_KEYSTONE)
                            if (GameObject* chest = instance->GetGameObject(HymdallChestGUID))
                                chest->SetRespawnTime(86400);

                        if (Creature* hymdall = instance->GetCreature(HymdallGUID))
                            if (uint16 encounterId = sObjectMgr->GetDungeonEncounterByCreature(hymdall->GetEntry()))
                                instance->SendToPlayers(WorldPackets::Instance::BossKillCredit(encounterId).Write());

                        instance->ApplyOnEveryPlayer([&](Player* player)
                        {
                            if (auto hymdall = instance->GetCreature(HymdallGUID))
                            {
                                instance->UpdateEncounterState(ENCOUNTER_CREDIT_KILL_CREATURE, NPC_HYMDALL, hymdall, player);
                            }

                            instance->ToInstanceMap()->PermBindAllPlayers(player);
                            player->getHostileRefManager().deleteReferences();
                        });
                    }
                    break;
                case DATA_SKOVALD:
                {
                    if (state != IN_PROGRESS)
                    {
                        DoRemoveAurasDueToSpellOnPlayers(193783); // Aegis extra action
                        DoRemoveAurasDueToSpellOnPlayers(193743); // Aegis protection
                        DoRemoveAurasDueToSpellOnPlayers(193708); // Flame fixate
                        if (Creature* aegis = instance->GetCreature(aegisGUID))
                            aegis->DespawnOrUnsummon();
                    }
                    if (state == DONE)
                        if (Creature* odyn = instance->GetCreature(OdynGUID))
                            odyn->AI()->DoAction(true);
                    break;
                }
                case DATA_ODYN:
                {
                    if (state != IN_PROGRESS)
                    {
                        for (uint8 i = 0; i < 5; ++i)
                        {
                            SetRuneState(i, false);
                            DoRemoveAurasDueToSpellOnPlayers(197963 + i);
                        }
                        DoRemoveAurasDueToSpellOnPlayers(200988);
                    }
                    if (state == DONE)
                    {
                        if (instance->GetDifficultyID() != DIFFICULTY_MYTHIC_KEYSTONE)
                            if (GameObject* chest = instance->GetGameObject(OdynChestGUID))
                                chest->SetRespawnTime(86400);

                        instance->SummonCreature(NPC_SPOILS_CHEST_VISUAL, spoilsPos);
                        if (Creature* odyn = instance->GetCreature(OdynGUID))
                            if (uint16 encounterId = sObjectMgr->GetDungeonEncounterByCreature(odyn->GetEntry()))
                                instance->SendToPlayers(WorldPackets::Instance::BossKillCredit(encounterId).Write());

                        instance->ApplyOnEveryPlayer([&](Player* player)
                        {
                            if (auto odyn = instance->GetCreature(OdynGUID))
                            {
                                instance->UpdateEncounterState(ENCOUNTER_CREDIT_KILL_CREATURE, NPC_ODYN, odyn, player);
                            }

                            instance->ToInstanceMap()->PermBindAllPlayers(player);
                            player->getHostileRefManager().deleteReferences();
                        });
                    }
                    break;
                }
                default:
                    break;
            }

            if (type == DATA_HYRJA || type == DATA_FENRYR)
                if (GetBossState(DATA_HYRJA) == DONE && GetBossState(DATA_FENRYR) == DONE)
                    DoCastSpellOnPlayers(202160); //Odyn's Blessing - Speed buff

            return true;
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case DATA_FENRYR_EVENT:
                {
                    fenryrEventDone = data;
                    if (data == DONE)
                    {
                        if (Creature* fen = instance->GetCreature(feryrGUID))
                        {
                            fen->SetVisible(true);
                            fen->SetReactState(REACT_AGGRESSIVE);
                        }
                        SaveToDB();
                    }
                    break;
                }
                case DATA_SKOVALD_EVENT:
                    skovaldEventDone = data;
                    if (data == DONE)
                        SaveToDB();
                    break;
                case DATA_RUNES_ACTIVATED:
                    SetRuneState(data, true);
                    break;
                case DATA_RUNES_DEACTIVATED:
                    if (data < 5)
                    {
                        SetRuneState(data, false);
                        if (Creature* odyn = instance->GetCreature(OdynGUID))
                            odyn->AI()->SetData(1, data);
                    }
                    break;
                default:
                    break;
            }
        }

        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_HYRJA:
                    return hyrjaGUID;
                case DATA_SKOVALD:
                    return skovaldGUID;
                case DATA_ODYN:   
                    return OdynGUID;
            }
            return ObjectGuid::Empty;
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_FENRYR_EVENT:
                    return fenryrEventDone;
                case DATA_SKOVALD_EVENT:
                    return skovaldEventDone;
            }
            return 0;
        }

        void OnPlayerEnter(Player* player) override
        {
            if (GetBossState(DATA_HYRJA) == DONE && GetBossState(DATA_FENRYR) == DONE &&
                GetBossState(DATA_SKOVALD) != IN_PROGRESS && GetBossState(DATA_ODYN) != IN_PROGRESS)
                player->CastSpell(player, 202160, true);

            // Include the entering player exactly once, regardless of map-link timing.
            std::set<ObjectGuid> presentPlayers;
            for (auto const& itr : instance->GetPlayers())
                if (Player* present = itr.getSource())
                    if (!present->isGameMaster())
                        presentPlayers.insert(present->GetGUID());
            if (!player->isGameMaster())
                presentPlayers.insert(player->GetGUID());

            if (!StartEvent && presentPlayers.size() >= 5)
                if (Group* group = player->GetGroup())
                    if (Player* leader = ObjectAccessor::GetPlayer(*player, group->GetLeaderGUID()))
                    {
                        StartEvent = true;
                        leader->CastSpell(leader, 202036);
                    }
        }

        void Update(uint32 diff) override
        {
            if (checkTimer <= diff)
            {
                checkTimer = 1000;

                instance->ApplyOnEveryPlayer([&](Player* player)
                {
                    if (player->GetCurrentAreaID() == 7672)
                    {
                        if (player->isAlive() && player->GetPositionZ() <= 600.00f)
                        {
                            player->Kill(player);
                            player->RepopAtGraveyard();
                        }
                    }
                });
            }
            else
                checkTimer -= diff;

            if (checkTimerAura <= diff)
            {
                checkTimerAura = 1000;

                instance->ApplyOnEveryPlayer([&](Player* player)
                {
                    if (player->GetCurrentAreaID() == 7672 && player->HasAura(192635))
                        if (player->GetPositionX() <= 2590.00f || player->GetPositionX() >= 3175.61f)
                            player->RemoveAura(192635);
                });
            }
            else
                checkTimerAura -= diff;
        }

        std::string GetSaveData() override
        {
            OUT_SAVE_INST_DATA;

            std::ostringstream saveStream;
            saveStream << "H V " << GetBossSaveData() << fenryrEventDone << " " << skovaldEventDone << " ";

            OUT_SAVE_INST_DATA_COMPLETE;
            return saveStream.str();
        }

        void Load(const char* in) override
        {
            if (!in)
            {
                OUT_LOAD_INST_DATA_FAIL;
                return;
            }

            OUT_LOAD_INST_DATA(in);

            char dataHead1 = 0, dataHead2 = 0;
            uint32 states[MAX_ENCOUNTER] = {};
            uint32 fenryrState = 0, skovaldState = 0;
            std::istringstream loadStream(in);
            if (!(loadStream >> dataHead1 >> dataHead2) || dataHead1 != 'H' || dataHead2 != 'V')
            {
                OUT_LOAD_INST_DATA_FAIL;
                return;
            }
            for (uint8 i = 0; i < MAX_ENCOUNTER; ++i)
                if (!(loadStream >> states[i]))
                {
                    OUT_LOAD_INST_DATA_FAIL;
                    return;
                }
            if (!(loadStream >> fenryrState >> skovaldState))
            {
                OUT_LOAD_INST_DATA_FAIL;
                return;
            }

            // Parse the whole record before modifying instance state.
            fenryrEventDone = fenryrState == DONE ? DONE : NOT_STARTED;
            skovaldEventDone = skovaldState == DONE ? DONE : NOT_STARTED;
            for (uint8 i = 0; i < MAX_ENCOUNTER; ++i)
            {
                if (states[i] == IN_PROGRESS || states[i] > SPECIAL)
                    states[i] = NOT_STARTED;
                SetBossState(i, EncounterState(states[i]));
            }

            RestoreLoadedRewardChests();
            OUT_LOAD_INST_DATA_COMPLETE;
        }

        WorldLocation* GetClosestGraveYard(float x, float y, float z) override
        {
            loc_res_pla.Relocate(x, y, z);
            loc_res_pla.SetMapId(1477);

            uint32 graveyardId = 5098;

            if (instance->GetDifficultyID() != DIFFICULTY_MYTHIC_KEYSTONE)
            {
                if (GetBossState(DATA_HYRJA) == DONE && GetBossState(DATA_FENRYR) == DONE)
                    graveyardId = 5289;
                else if (GetBossState(DATA_FENRYR) != DONE && GetData(DATA_FENRYR_EVENT) == DONE)
                    graveyardId = 5226;
                else if (GetBossState(DATA_HYRJA) == DONE)
                    graveyardId = 5225;
            }

            if (graveyardId)
            {
                if (WorldSafeLocsEntry const* gy = sWorldSafeLocsStore.LookupEntry(graveyardId))
                {
                    loc_res_pla.Relocate(gy->Loc.X, gy->Loc.Y, gy->Loc.Z);
                    loc_res_pla.SetMapId(gy->MapID);
                }
            }
            return &loc_res_pla;
        }

        /* void Update(uint32 diff) 
        {
            InstanceScript::Update(diff);
        } */
        
    };
};

void AddSC_instance_halls_of_valor()
{
    new instance_halls_of_valor();
}
