#ifndef FELSORIA_COMMAND_AB_H
#define FELSORIA_COMMAND_AB_H
#include "CommandBG.h"
#include "BattlegroundArathiBasin.h"
#include <map>
#include <vector>
// FELSORIA_AB_MANUAL_V2: one instance and BG team own this state.
class CommandAB : public CommandBG {
public:
    CommandAB(Battleground* bg,TeamId team);
    ~CommandAB();
    void Initialize() override;
    void Update(uint32 diff) override;
    void OnPlayerDead(uint64 guid) override;
    void OnPlayerRevive(Player* player) override;
    AIWaypoint* GetReadyPosition() override {return nullptr;}
    Creature const* GetMatchGraveyardNPC(Player const* player) override;
    bool UpdateBot(Player* player);
private:
    struct Task {
        uint8 node=0;
        uint32 assigned=0,patrolAt=0,castAt=0,castTick=0,retryAt=0,entryAt=0,opportunityUntil=0;
        uint64 castHealth=0;
        int from=-1,to=-1,exitRoute=-1;
        unsigned legEnd=0,exitCursor=0,exitLegEnd=0;
        bool attached=false,leg=false,combat=false,exitChecked=false;
        Position patrol,castOrigin;
        std::vector<G3D::Vector3> legPath;
    };
    struct Report {unsigned count=0;uint32 time=0;};
    struct Progress {Position position;uint32 movedAt=0,loggedAt=0,tickAt=0;};
    std::map<uint64,Task> m_tasks;
    std::map<uint64,Progress> m_progress;
    Report m_reports[5];
    uint64 m_cappers[5]={};
    uint32 m_capUntil[5]={};
    uint32 m_planTimer=0;
    bool Enabled() const;
    void Observe(Player* player);
    void Plan();
    bool Capture(Player* player,Task& task);
    void CancelCapture(Player* player,Task& task,char const* reason);
    void ConsiderNearbyBase(Player* player,Task& task);
    void TrackProgress(Player* player,Task const& task);
    void Travel(Player* player,Task& task,unsigned destination);
    unsigned NearestAnchor(Player* player) const;
};
#endif
