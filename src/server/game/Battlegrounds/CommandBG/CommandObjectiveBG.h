#ifndef FELSORIA_COMMAND_OBJECTIVE_BG_H
#define FELSORIA_COMMAND_OBJECTIVE_BG_H

#include "CommandBG.h"
#include <map>
#include <vector>

// These commanders use live BG objects and the native navigation mesh. They
// do not borrow WSG waypoints, invent database routes or relocate a stuck bot.
class CommandObjectiveBG : public CommandBG
{
public:
    CommandObjectiveBG(Battleground* bg, TeamId team) : CommandBG(bg, team) { }
    virtual ~CommandObjectiveBG() = default;
    static bool Supports(Battleground const* bg);
    virtual bool UpdateBot(Player* player) = 0;
    bool CanDireFlee() override { return false; }

protected:
    bool Eligible(Player* player) const;
    std::vector<Player*> LivingBots();
    Player* Carrier(ObjectGuid guid) const;
    bool Walk(Player* player, Position const& goal, float stop = 1.0f);
    bool Follow(Player* player, Player* target, bool attack);
    bool Use(Player* player, GameObject* object);
};

class CommandTP final : public CommandObjectiveBG
{
public:
    CommandTP(Battleground* bg, TeamId team) : CommandObjectiveBG(bg, team) { }
    bool UpdateBot(Player* player) override;
    bool CanUpMount(Player* player) override;
    Creature const* GetMatchGraveyardNPC(Player const* player) override;
};

class CommandKT final : public CommandObjectiveBG
{
public:
    CommandKT(Battleground* bg, TeamId team) : CommandObjectiveBG(bg, team) { }
    void Update(uint32 diff) override;
    bool UpdateBot(Player* player) override;
    bool CanUpMount(Player* player) override;
    Creature const* GetMatchGraveyardNPC(Player const* player) override;

private:
    void AssignOrbs();
    bool ScoringPosition(Player* player, Position& result, unsigned slot);
    std::map<uint64, unsigned> _orbAssignments;
    uint32 _assignmentTimer = 0;
};
#endif
