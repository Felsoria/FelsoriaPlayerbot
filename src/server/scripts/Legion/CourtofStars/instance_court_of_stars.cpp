/*
    Dungeon : Court of Stars 110
*/

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Creature.h"
#include "GameObject.h"
#include "court_of_stars.h"
#include <algorithm>
#include <list>
#include <sstream>
#include <vector>

DoorData const doorData[] =
{
    {GO_GERDO_DOOR,           DATA_CAPTAIN_GERDO,       DOOR_TYPE_PASSAGE,     BOUNDARY_NONE},
    {GO_MELANDRUS_DOOR_1,     DATA_TALIXAE,             DOOR_TYPE_PASSAGE,     BOUNDARY_NONE},
    {GO_MELANDRUS_DOOR_2,     DATA_MELANDRUS_EVENT,     DOOR_TYPE_PASSAGE,     BOUNDARY_NONE},
    {GO_MELANDRUS_DOOR_2,     DATA_MELANDRUS,           DOOR_TYPE_ROOM,        BOUNDARY_NONE},
    {0,                      0,                        DOOR_TYPE_ROOM,        BOUNDARY_NONE}
};

class instance_court_of_stars : public InstanceMapScript
{
public:
    instance_court_of_stars() : InstanceMapScript("instance_court_of_stars", 1571) {}

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_court_of_stars_InstanceMapScript(map);
    }

    struct instance_court_of_stars_InstanceMapScript : public InstanceScript
    {
        instance_court_of_stars_InstanceMapScript(Map* map) : InstanceScript(map) 
        {
            SetBossNumber(MAX_ENCOUNTER);
        }

        WorldLocation loc_res_pla;

        mutable ObjectGuid guidEvent;
        std::vector<ObjectGuid> noblesGUID;
        ObjectGuid GerdoGUID;
        ObjectGuid TalixaeGUID;
        bool flaskPoisoned = false;

        std::list<ObjectGuid> beaconsGUID;
        
        void Initialize() override
        {
            LoadDoorData(doorData);
            SetBossState(DATA_MELANDRUS_EVENT, NOT_STARTED);
        }

        void Load(char const* data) override
        {
            if (!data)
                return;
            std::istringstream input(data);
            uint32 states[MAX_ENCOUNTER];
            for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
                if (!(input >> states[i]))
                    return;
            for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
            {
                if (states[i] == IN_PROGRESS || states[i] > SPECIAL)
                    states[i] = NOT_STARTED;
                SetBossState(i, EncounterState(states[i]));
            }
        }

        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_PATROL_CAPTAIN_GERDO:
                    GerdoGUID = creature->GetGUID();
                    break;
                case NPC_TALIXAE_FLAMEWREATH:
                    TalixaeGUID = creature->GetGUID();
                    break;
                case NPC_GERDO_ARCANE_BEACON:
                    if (std::find(beaconsGUID.begin(), beaconsGUID.end(), creature->GetGUID()) == beaconsGUID.end())
                        beaconsGUID.push_back(creature->GetGUID());
                    break;
                case NPC_SUSPICIOUS_NOBLE:
                    if (std::find(noblesGUID.begin(), noblesGUID.end(), creature->GetGUID()) == noblesGUID.end())
                        noblesGUID.push_back(creature->GetGUID());
                    break;
            } 
        }

        void CreatureDies(Creature* creature, Unit* /*killer*/) override
        {
            // Includes the DB-spawned Enforcer, which has no summoner callback.
            if (creature->GetEntry() == NPC_FELBOUND_ENFORCER)
                if (Creature* talixae = instance->GetCreature(TalixaeGUID))
                    if (talixae->IsAIEnabled && talixae->isAlive())
                        talixae->AI()->SetGUID(creature->GetGUID(), DATA_ENFORCER_DIED);
        }

        void OnCreatureRemove(Creature* creature) override
        {
            if (creature->GetEntry() == NPC_GERDO_ARCANE_BEACON)
                beaconsGUID.remove(creature->GetGUID());
        }

        void OnGameObjectRemove(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_GERDO_DOOR:
                case GO_MELANDRUS_DOOR_1:
                case GO_MELANDRUS_DOOR_2:
                    AddDoor(go, false);
                    break;
                default:
                    break;
            }
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_GERDO_DOOR:
                case GO_MELANDRUS_DOOR_1:
                case GO_MELANDRUS_DOOR_2:
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
            
            return true;
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case DATA_FLASK_POISONED:
                    // One-way per instance: boss Reset must not unpoison the flask.
                    if (data && GetBossState(DATA_CAPTAIN_GERDO) != IN_PROGRESS &&
                        GetBossState(DATA_CAPTAIN_GERDO) != DONE)
                        flaskPoisoned = true;
                    break;
                case DATA_BEACON_ACTIVATE:
                    if (Creature* gerdo = instance->GetCreature(GerdoGUID))
                    {
                        for (std::list<ObjectGuid>::iterator itr = beaconsGUID.begin(); itr != beaconsGUID.end(); ++itr)
                            if (Creature* beacon = instance->GetCreature(*itr))
                                if (beacon && !beacon->HasAura(210257)) //SPELL_BEACON_DISABLED
                                    beacon->CastSpell(beacon, 210435, true, 0, 0, gerdo->GetGUID()); //SPELL_SUM_VIGILANT_NIGHTWATCH
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
                case NPC_SUSPICIOUS_NOBLE:
                    // Select once, when clues/gossip first request an identity.
                    // Never change it while players are solving the mystery.
                    if (!guidEvent && !noblesGUID.empty())
                        guidEvent = noblesGUID[urand(0, uint32(noblesGUID.size() - 1))];
                    return guidEvent;
            }
            return ObjectGuid::Empty;
        } 

        uint32 GetData(uint32 type) const override
        {
            if (type == DATA_FLASK_POISONED)
                return flaskPoisoned ? 1 : 0;
            return 0;
        }

        WorldLocation* GetClosestGraveYard(float x, float y, float z) override
        {
            loc_res_pla.Relocate(x, y, z);
            loc_res_pla.SetMapId(1571);

            uint32 graveyardId = 5432;

            if (GetBossState(DATA_TALIXAE) == DONE)
                graveyardId = 5483;
            else if (GetBossState(DATA_CAPTAIN_GERDO) == DONE)
                graveyardId = 5502;

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
            // Challenge
            InstanceScript::Update(diff);
        } */
    };
};

void AddSC_instance_court_of_stars()
{
    new instance_court_of_stars();
}