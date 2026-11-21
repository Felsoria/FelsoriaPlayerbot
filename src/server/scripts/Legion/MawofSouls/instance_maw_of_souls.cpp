/*
    Dungeon : Maw of Souls 100-110
*/

#include "maw_of_souls.h"
#include "PrecompiledHeaders/ScriptPCH.h"
#include "WorldPacket.h"
#include "InstancePackets.h"

DoorData const doorData[] =
{
    {GO_HARBARON_DOOR,      DATA_HARBARON,       DOOR_TYPE_PASSAGE,       BOUNDARY_NONE},
    {GO_HARBARON_DOOR_2,    DATA_HARBARON,       DOOR_TYPE_ROOM,          BOUNDARY_NONE},
    {GO_SKJAL_INVIS_DOOR,   DATA_SKJAL,          DOOR_TYPE_PASSAGE,       BOUNDARY_NONE},
    {GO_SKJAL_DOOR_1,       DATA_SKJAL,          DOOR_TYPE_PASSAGE,       BOUNDARY_NONE},
    {GO_SKJAL_DOOR_2,       DATA_SKJAL,          DOOR_TYPE_PASSAGE,       BOUNDARY_NONE},
    {0, 0, DOOR_TYPE_ROOM, BOUNDARY_NONE} // LoadDoorData requires a terminator.
};

class instance_maw_of_souls : public InstanceMapScript
{
public:
    instance_maw_of_souls() : InstanceMapScript("instance_maw_of_souls", 1492) {}

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_maw_of_souls_InstanceMapScript(map);
    }

    struct instance_maw_of_souls_InstanceMapScript : public InstanceScript
    {
        instance_maw_of_souls_InstanceMapScript(Map* map) : InstanceScript(map) 
        {
            SetBossNumber(MAX_ENCOUNTER);
        }

        WorldLocation loc_res_pla;

        ObjectGuid YmironGUID;
        ObjectGuid YmironGornGUID;
        ObjectGuid helyaGUID;
        ObjectGuid shipGUID;
        ObjectGuid helyaChestGUID;
        ObjectGuid SkjalGUID;

        void Initialize() override
        {
            LoadDoorData(doorData);
        }

        void Load(char const* data) override
        {
            if (!data)
                return;

            // Keep the existing four-integer save format. Do not restore active fights.
            std::istringstream loadStream(data);
            for (uint32 bossId = 0; bossId < MAX_ENCOUNTER; ++bossId)
            {
                uint32 state = NOT_STARTED;
                if (!(loadStream >> state))
                    state = NOT_STARTED;
                if (state != DONE)
                    state = NOT_STARTED;
                SetBossState(bossId, static_cast<EncounterState>(state));
            }
        }

        void OnPlayerEnter(Player* player) override
        {
            if (GetBossState(DATA_YMIRON) == DONE && player->GetDistance(7188.54f, 7317.84f, 23.66f) < 25.0f)
                player->AddDelayedEvent(500, [player] () -> void { if (player) player->RepopAtGraveyard(); });
        }

        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_YMIRON:
                    YmironGUID = creature->GetGUID(); 
                    break;
                case NPC_SKJAL:
                    SkjalGUID = creature->GetGUID(); 
                    break;
                case NPC_HELYA:    
                    helyaGUID = creature->GetGUID(); 
                    break;
            }
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_YMIRON_GORN:
                    YmironGornGUID = go->GetGUID();
                    if (GetBossState(DATA_YMIRON) == DONE)
                        go->RemoveFlag(GAMEOBJECT_FIELD_FLAGS, GO_FLAG_NOT_SELECTABLE);
                    else
                        go->SetFlag(GAMEOBJECT_FIELD_FLAGS, GO_FLAG_NOT_SELECTABLE);
                    break;
                case GO_SHIP:
                    shipGUID = go->GetGUID();
                    break;
                case GO_HELYA_CHEST:
                    helyaChestGUID = go->GetGUID();
                    break;
                case GO_HARBARON_DOOR:
                case GO_HARBARON_DOOR_2:
                case GO_SKJAL_INVIS_DOOR:
                case GO_SKJAL_DOOR_1:
                case GO_SKJAL_DOOR_2:
                    AddDoor(go, true);
                    break;
                default:
                    break;
            }
        }

        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                return false;

            switch (type)
            {
                case DATA_YMIRON:
                    if (GameObject* horn = instance->GetGameObject(YmironGornGUID))
                    {
                        if (state == DONE)
                            horn->RemoveFlag(GAMEOBJECT_FIELD_FLAGS, GO_FLAG_NOT_SELECTABLE);
                        else
                            horn->SetFlag(GAMEOBJECT_FIELD_FLAGS, GO_FLAG_NOT_SELECTABLE);
                    }
                    break;
                case DATA_HELYA:
                {
                    if (state == DONE && instance->GetDifficultyID() != DIFFICULTY_MYTHIC_KEYSTONE)
                    {
                        if (GameObject* chest = instance->GetGameObject(helyaChestGUID))
                            chest->SetRespawnTime(86400);

                        if (auto helya = instance->GetCreature(helyaGUID))
                            if (uint16 encounterId = sObjectMgr->GetDungeonEncounterByCreature(helya->GetEntry()))
                                instance->SendToPlayers(WorldPackets::Instance::BossKillCredit(encounterId).Write());

                        instance->ApplyOnEveryPlayer([&](Player* player)
                        {
                            if (auto helya = instance->GetCreature(helyaGUID))
                            {
                                instance->UpdateEncounterState(ENCOUNTER_CREDIT_KILL_CREATURE, NPC_HELYA, helya, player);
                            }
                        });
                    }
                    break;
                }
                default:
                    break;
            }
            return true;
        }

        void SetData(uint32 type, uint32 data) override
        {
            /* switch (type)
            {
                case DATA_:
                    break;
                default:
                    break;
            } */
        }

        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_YMIRON:
                    return YmironGUID;
                case DATA_YMIRON_GORN:   
                    return YmironGornGUID;
                case DATA_SKJAL:
                    return SkjalGUID;
                case DATA_HELYA:
                    return helyaGUID;
                case DATA_SHIP:
                    return shipGUID;
            }
            return ObjectGuid::Empty;
        }

        uint32 GetData(uint32 type) const override
        {
            return 0;
        }

        WorldLocation* GetClosestGraveYard(float x, float y, float z) override
        {
            loc_res_pla.Relocate(x, y, z);
            loc_res_pla.SetMapId(1492);

            uint32 graveyardId = 5102;

            if (GetBossState(DATA_HARBARON) == DONE)
                graveyardId = 5508;
            else if (GetBossState(DATA_YMIRON) == DONE)
                graveyardId = 5101;

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

        void Update(uint32 diff) override {}
    };
};

void AddSC_instance_maw_of_souls()
{
    new instance_maw_of_souls();
}
