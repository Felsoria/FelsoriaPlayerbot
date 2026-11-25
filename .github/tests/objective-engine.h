// Minimal engine boundary for compiling the production objective commanders.
#ifndef FELSORIA_OBJECTIVE_TEST_ENGINE_H
#define FELSORIA_OBJECTIVE_TEST_ENGINE_H
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
enum TeamId { TEAM_ALLIANCE, TEAM_HORDE, TEAM_NEUTRAL };
unsigned const MAX_TEAMS = 2, MAX_ORBS = 4;
float const BOTAI_SEARCH_RANGE = 30.0f, INVALID_HEIGHT = -100000.0f;
enum { STATUS_WAIT_JOIN, STATUS_IN_PROGRESS, STATUS_WAIT_LEAVE };
namespace MS { namespace Battlegrounds { namespace BattlegroundTypeId {
enum : uint16 { BattlegroundTwinPeaks = 108, BattlegroundKotmoguTemplate = 699,
    BattlegroundRandom = 32, BattlegroundBattleForGilneas = 120, BattlegroundArathiBasin = 3,
    BattlegroundWarsongGulch = 2, BattlegroundEyeOfTheStorm = 7, BattlegroundAlteracValley = 1,
    BattlegroundIsleOfConquest = 30 };
} } }
struct ObjectGuid
{
    uint64 id = 0;
    bool IsEmpty() const { return !id; }
    bool operator==(ObjectGuid rhs) const { return id == rhs.id; }
    bool operator!=(ObjectGuid rhs) const { return id != rhs.id; }
    bool operator<(ObjectGuid rhs) const { return id < rhs.id; }
    static ObjectGuid const Empty;
};
ObjectGuid const ObjectGuid::Empty = {};
struct Position
{
    float x = 0, y = 0, z = 0;
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
    void Relocate(float a, float b, float c) { x = a; y = b; z = c; }
    float GetExactDist(float a, float b, float c) const
    { return std::sqrt((x-a)*(x-a) + (y-b)*(y-b) + (z-c)*(z-c)); }
};
struct Player;
struct GameObject;
struct Battleground;
struct CommandBG;
struct Map
{
    std::map<uint64, GameObject*> objects;
    float floor = 4;
    GameObject* GetGameObject(ObjectGuid guid)
    { auto it=objects.find(guid.id); return it==objects.end() ? nullptr : it->second; }
    float GetHeight(unsigned, float, float, float, bool, float) { return floor; }
};
struct BattlegroundMap : Map
{
    struct Guard { ~Guard() { } };
    Guard LockBattlegroundLifetime() { return {}; }
    Battleground* m_bg = nullptr;
    CommandBG* m_pAllianceCommander = nullptr;
    CommandBG* m_pHordeCommander = nullptr;
    void InsureCommander(uint16 type);
};
struct Creature : Position { };
struct GameObject : Position
{
    Map* map = nullptr;
    bool spawned = true;
    unsigned uses = 0;
    std::function<void(Player*)> onUse;
    bool isSpawned() const { return spawned; }
    Map* GetMap() const { return map; }
    Position const& GetPosition() const { return *this; }
    void Use(Player* player) { ++uses; if (onUse) onUse(player); }
};
struct Player : Position
{
    uint64 id = 1;
    TeamId team = TEAM_ALLIANCE;
    Map* map = nullptr;
    Battleground* bg = nullptr;
    bool bot = true, alive = true, world = true, combat = false, los = true, attackable = true;
    bool allianceTrigger = false, hordeTrigger = false;
    ObjectGuid selected;
    ObjectGuid GetGUID() const { return {id}; }
    uint64 GetGUIDLow() const { return id; }
    TeamId GetBGTeamId() const { return team; }
    Map* GetMap() const { return map; }
    Battleground* GetBattleground() const { return bg; }
    bool IsPlayerBot() const { return bot; }
    bool isAlive() const { return alive; }
    bool IsInWorld() const { return world; }
    bool isInCombat() const { return combat; }
    unsigned GetPhases() const { return 0; }
    Position const& GetPosition() const { return *this; }
    template<class T> float GetDistance(T const* object) const
    { return GetExactDist(object->x, object->y, object->z); }
    template<class T> bool IsWithinLOSInMap(T const*) const { return los; }
    bool IsValidAttackTarget(Player const* target) const { return target->attackable && team != target->team; }
    bool canSeeOrDetect(Player const*, bool, bool) const { return true; }
    bool IsInAreaTriggerRadius(unsigned trigger) const { return trigger == 5904 ? allianceTrigger : hordeTrigger; }
    void AttackStop() { }
    void SetSelection(ObjectGuid guid) { selected = guid; }
};
struct Battleground
{
    virtual ~Battleground() = default;
    uint16 type = MS::Battlegrounds::BattlegroundTypeId::BattlegroundTwinPeaks;
    bool arena = false, rated = false, rbg = false, wargame = false, brawl = false;
    unsigned status = STATUS_IN_PROGRESS;
    BattlegroundMap map;
    std::array<GameObject*, 8> objects{};
    std::array<Creature, 6> guides{};
    std::map<ObjectGuid,int> roster;
    bool IsArena() const { return arena; }
    bool IsRated() const { return rated; }
    bool IsRBG() const { return rbg; }
    bool IsWargame() const { return wargame; }
    bool IsBrawl() const { return brawl; }
    uint16 GetTypeID(bool) const { return type; }
    unsigned GetMapId() const { return type == 108 ? 726 : type == 699 ? 998 : 0; }
    unsigned GetStatus() const { return status; }
    BattlegroundMap* GetBgMap() { return &map; }
    GameObject* GetBGObject(unsigned index) { return objects.at(index); }
    Creature* GetBGCreature(unsigned index) { return &guides.at(index); }
    std::map<ObjectGuid,int> const& GetPlayers() const { return roster; }
    virtual void _CheckPositions(uint32) { }
};
enum { BG_TP_FLAG_STATE_ON_BASE, BG_TP_FLAG_STATE_WAIT_RESPAWN, BG_TP_FLAG_STATE_ON_PLAYER, BG_TP_FLAG_STATE_ON_GROUND };
enum { BG_TP_OBJECT_A_FLAG = 6, BG_TP_OBJECT_H_FLAG = 7, TP_SPIRIT_ALLIANCE = 0, TP_SPIRIT_MIDDLE_ALLIANCE = 2 };
struct BattlegroundTwinPeaks : Battleground
{
    std::array<ObjectGuid,2> keepers{}, dropped{};
    std::array<uint8,2> states{};
    std::array<uint8,2>& _flagState = states;
    std::array<ObjectGuid,2>& _flagKeepers = keepers;
    std::vector<uint64> captures;
    void _CheckPositions(uint32 diff) override;
    void EventPlayerCapturedFlag(Player* player) { captures.push_back(player->id); }
    ObjectGuid GetFlagPickerGUID(int team) const { return keepers.at(team); }
    uint8 GetFlagState(TeamId team) const { return states.at(team); }
    ObjectGuid GetDroppedFlagGUID(TeamId team) const { return dropped.at(team); }
};
enum { BG_KT_OBJECT_ORB_GREEN = 2, BG_KT_CREATURE_SPIRIT_1 = 4 };
float const BG_KT_OrbPositions[MAX_ORBS][4] = {
    {1716.78f,1416.64f,13.5709f,0}, {1850.26f,1416.77f,13.5709f,0},
    {1850.29f,1250.31f,13.5708f,0}, {1716.83f,1249.93f,13.5706f,0}};
struct BattlegroundKotmoguTemplate : Battleground
{
    BattlegroundKotmoguTemplate() { type=MS::Battlegrounds::BattlegroundTypeId::BattlegroundKotmoguTemplate; }
    std::array<ObjectGuid,4> keepers{};
    ObjectGuid GetFlagPickerGUID(int orb) const { return keepers.at(orb); }
};
struct BotBGAIMovement
{
    Position goal;
    bool allowed = true;
    unsigned moves = 0;
    bool MoveObjectiveTo(Position const& point, float)
    { ++moves; goal=point; return allowed; }
};
struct BotBGAI
{
    BotBGAIMovement movement;
    unsigned dismounts = 0;
    bool interaction = true;
    BotBGAIMovement* GetAIMovement() { return &movement; }
    bool TryUpMount() { return false; }
    bool CanUseBGObject() { return interaction; }
    void Dismount() { ++dismounts; }
};
namespace ObjectAccessor {
std::map<uint64,Player*> players;
inline Player* FindPlayer(ObjectGuid guid)
{ auto it=players.find(guid.id); return it==players.end() ? nullptr : it->second; }
}
std::map<uint64,BotBGAI*> botAI;
struct CommandBG
{
    CommandBG(Battleground* bg,TeamId team) : m_pBattleground(bg),m_TeamID(team) { }
    virtual void Update(uint32) { }
    virtual bool CanDireFlee() { return true; }
    virtual bool CanUpMount(Player*) { return true; }
    virtual Creature const* GetMatchGraveyardNPC(Player const*) { return nullptr; }
    bool AddPlayerBot(Player* player,Battleground*) { m_PlayerGUIDs[player->id]=false; return true; }
    Player* GetBGPlayer(uint64 guid)
    { return m_PlayerGUIDs.count(guid) ? ObjectAccessor::FindPlayer({guid}) : nullptr; }
    BotBGAI* GetBotBGAI(uint64 guid)
    { auto it=botAI.find(guid); return it==botAI.end() ? nullptr : it->second; }
protected:
    Battleground* m_pBattleground;
    TeamId m_TeamID;
    std::map<uint64,bool> m_PlayerGUIDs;
};
#endif
