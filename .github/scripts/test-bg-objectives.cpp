// Compile and execute the actual objective commanders with a controlled engine boundary.
#include "../../src/server/game/Battlegrounds/CommandBG/CommandObjectiveBG.cpp"
#include <iostream>
#include <stdexcept>

// Existing commanders are only constructed by the extracted production dispatcher.
struct CommandWS : CommandBG { using CommandBG::CommandBG; };
struct CommandAB : CommandBG { using CommandBG::CommandBG; };
struct CommandAV : CommandBG { using CommandBG::CommandBG; };
struct CommandEY : CommandBG { using CommandBG::CommandBG; };
struct CommandIC : CommandBG { using CommandBG::CommandBG; };
struct CommandBFG : CommandBG { using CommandBG::CommandBG; };

static unsigned checks = 0;
void Check(bool value, char const* description)
{
    ++checks;
    if (!value) throw std::runtime_error(description);
}

template<class BG, class Commander> struct Fixture
{
    BG bg;
    Commander commander;
    std::array<Player,8> players;
    std::array<BotBGAI,8> ais;
    std::array<GameObject,8> objects;
    Fixture(TeamId team) : commander(&bg,team)
    {
        ObjectAccessor::players.clear(); botAI.clear();
        for (unsigned i=0;i<8;++i)
        {
            players[i].id=i+1;players[i].team=team;players[i].bg=&bg;players[i].map=&bg.map;
            ObjectAccessor::players[i+1]=&players[i];botAI[i+1]=&ais[i];
            bg.roster[players[i].GetGUID()]=0;
            commander.AddPlayerBot(&players[i],&bg);
            objects[i].map=&bg.map;objects[i].Relocate(float(i)*100,0,0);bg.objects[i]=&objects[i];
        }
    }
};

int main()
{
    try
    {
        using namespace FelsoriaObjectives;
        for (TeamId team : {TEAM_ALLIANCE,TEAM_HORDE})
        {
            Fixture<BattlegroundTwinPeaks,CommandTP> f(team);
            auto& p=f.players[0];auto& ai=f.ais[0];TeamId enemy=team==TEAM_ALLIANCE?TEAM_HORDE:TEAM_ALLIANCE;
            Check(CommandObjectiveBG::Supports(&f.bg),"Normal Twin Peaks rejected.");
            f.bg.keepers[enemy]=p.GetGUID();f.bg.states[enemy]=BG_TP_FLAG_STATE_ON_PLAYER;
            Check(!f.commander.CanUpMount(&p),"Flag carrier may mount.");
            Check(f.commander.UpdateBot(&p),"Carrier did not return home.");
            Check(ai.movement.goal.x==f.objects[BG_TP_OBJECT_A_FLAG+team].x,"Carrier targeted the wrong base.");
            Check(ai.dismounts>0,"Carrier was not dismounted.");
            GameObject dropped;dropped.map=&f.bg.map;dropped.Relocate(300,0,0);
            f.bg.map.objects[100]=&dropped;f.bg.dropped[team]={100};f.bg.states[team]=BG_TP_FLAG_STATE_ON_GROUND;
            Check(f.commander.UpdateBot(&p),"Carrier did not recover the home flag.");
            Check(ai.movement.goal.x==300,"Dropped home flag lost priority.");
            p.Relocate(300,0,0);
            dropped.onUse=[&](Player*){f.bg.states[team]=BG_TP_FLAG_STATE_ON_BASE;dropped.spawned=false;};
            Check(f.commander.UpdateBot(&p)&&dropped.uses==1,"Native home-flag interaction was not used.");
            Check(f.commander.UpdateBot(&p),"Carrier did not resume returning home.");
            Check(ai.movement.goal.x==f.objects[BG_TP_OBJECT_A_FLAG+team].x,"Carrier abandoned capture after a return.");
            f.bg.keepers[enemy]=ObjectGuid::Empty;f.bg.states[enemy]=BG_TP_FLAG_STATE_ON_BASE;
            p.Relocate(f.objects[BG_TP_OBJECT_A_FLAG+enemy].x,0,0);
            Check(f.commander.UpdateBot(&p)&&f.objects[BG_TP_OBJECT_A_FLAG+enemy].uses==1,
                "Enemy flag pickup bypassed or missed native interaction.");
            p.Relocate(0,0,0);f.players[1].team=enemy;f.players[1].Relocate(10,0,0);f.bg.keepers[team]=f.players[1].GetGUID();
            f.bg.states[team]=BG_TP_FLAG_STATE_ON_PLAYER;
            Check(!f.commander.UpdateBot(&p)&&p.selected==f.players[1].GetGUID(),"Bot did not select the enemy carrier.");
            Check(f.commander.GetMatchGraveyardNPC(&p)==&f.bg.guides[team],"Wrong Twin Peaks graveyard team.");
            f.bg.guides[team].Relocate(500,0,0);
            Check(f.commander.GetMatchGraveyardNPC(&p)==&f.bg.guides[TP_SPIRIT_MIDDLE_ALLIANCE+team],
                "Bot was queued at a remote graveyard instead of its nearby friendly guide.");
            p.alive=false;Check(!f.commander.UpdateBot(&p),"Dead bot commanded.");p.alive=true;
            p.bot=false;Check(!f.commander.UpdateBot(&p),"Human player commanded.");p.bot=true;
            p.world=false;Check(!f.commander.UpdateBot(&p),"Unloaded player commanded.");p.world=true;
            Map other;p.map=&other;Check(!f.commander.UpdateBot(&p),"Another instance was commanded.");p.map=&f.bg.map;
            f.bg.status=STATUS_WAIT_JOIN;Check(!f.commander.UpdateBot(&p),"Objective used before gates opened.");
            f.bg.status=STATUS_WAIT_LEAVE;Check(!f.commander.UpdateBot(&p),"Objective used after match ended.");
        }
        for (TeamId team : {TEAM_ALLIANCE,TEAM_HORDE})
        {
            Fixture<BattlegroundKotmoguTemplate,CommandKT> f(team);
            auto& p=f.players[0];auto& ai=f.ais[0];
            for(unsigned i=0;i<MAX_ORBS;++i)
                f.objects[BG_KT_OBJECT_ORB_GREEN+i].Relocate(BG_KT_OrbPositions[i][0],BG_KT_OrbPositions[i][1],BG_KT_OrbPositions[i][2]);
            f.bg.keepers[2]=p.GetGUID();f.objects[BG_KT_OBJECT_ORB_GREEN+2].spawned=false;
            Check(!f.commander.CanUpMount(&p),"Orb carrier may mount.");
            Check(f.commander.UpdateBot(&p),"Orb carrier did not move to the scoring area.");
            Check(ai.movement.goal.x>1750.27f&&ai.movement.goal.x<1816.04f&&
                ai.movement.goal.y>1287.34f&&ai.movement.goal.y<1379.34f&&ai.movement.goal.z<10,
                "Carrier goal is outside the native best-scoring bounds.");
            f.bg.map.floor=INVALID_HEIGHT;Check(!f.commander.UpdateBot(&p),"Missing map ground accepted.");f.bg.map.floor=4;
            f.bg.keepers[2]=ObjectGuid::Empty;f.objects[BG_KT_OBJECT_ORB_GREEN+2].spawned=true;
            for(unsigned i=0;i<8;++i)f.players[i].Relocate(1700+float(i),1200,13.57f);
            f.commander.Update(1000);
            std::array<unsigned,4> assigned{};
            for(unsigned i=0;i<8;++i)
            {
                f.commander.UpdateBot(&f.players[i]);
                for(unsigned orb=0;orb<MAX_ORBS;++orb)
                    if(f.ais[i].movement.goal.x==BG_KT_OrbPositions[orb][0]&&
                       f.ais[i].movement.goal.y==BG_KT_OrbPositions[orb][1])++assigned[orb];
            }
            for(unsigned count:assigned)Check(count==1,"Bots piled onto one orb or left a free orb unassigned.");
            Check(f.commander.GetMatchGraveyardNPC(&p)==&f.bg.guides[BG_KT_CREATURE_SPIRIT_1+team],"Wrong Kotmogu graveyard team.");
            f.bg.rated=true;Check(!CommandObjectiveBG::Supports(&f.bg),"Rated match changed.");f.bg.rated=false;
            f.bg.rbg=true;Check(!CommandObjectiveBG::Supports(&f.bg),"Rated BG changed.");f.bg.rbg=false;
            f.bg.brawl=true;Check(!CommandObjectiveBG::Supports(&f.bg),"Brawl changed.");f.bg.brawl=false;
            f.bg.wargame=true;Check(!CommandObjectiveBG::Supports(&f.bg),"Wargame changed.");f.bg.wargame=false;
            f.bg.arena=true;Check(!CommandObjectiveBG::Supports(&f.bg),"Arena changed.");
        }
        Check(!CommandObjectiveBG::Supports(nullptr),"Null BG accepted.");
        for (uint16 type : {uint16(108),uint16(699)})
            for (bool random : {false,true})
            {
                Battleground bg;bg.type=type;bg.map.m_bg=&bg;
                uint16 queue=random?uint16(MS::Battlegrounds::BattlegroundTypeId::BattlegroundRandom):type;
                bg.map.InsureCommander(queue);
                auto* alliance=dynamic_cast<CommandObjectiveBG*>(bg.map.m_pAllianceCommander);
                auto* horde=dynamic_cast<CommandObjectiveBG*>(bg.map.m_pHordeCommander);
                Check(alliance&&horde,"Direct or random dispatch missed a team.");
                Check(type==108 ? dynamic_cast<CommandTP*>(alliance)!=nullptr : dynamic_cast<CommandKT*>(alliance)!=nullptr,
                    "The wrong commander was dispatched.");
                bg.map.InsureCommander(queue);
                Check(bg.map.m_pAllianceCommander==alliance&&bg.map.m_pHordeCommander==horde,"Repeated dispatch replaced a commander.");
                delete alliance;delete horde;
                for(unsigned mode=0;mode<5;++mode)
                {
                    Battleground excluded;excluded.type=type;excluded.map.m_bg=&excluded;
                    excluded.arena=mode==0;excluded.rated=mode==1;excluded.rbg=mode==2;
                    excluded.wargame=mode==3;excluded.brawl=mode==4;
                    excluded.map.InsureCommander(queue);
                    Check(!excluded.map.m_pAllianceCommander&&!excluded.map.m_pHordeCommander,"Excluded mode received an objective commander.");
                }
            }
        for (TeamId team : {TEAM_ALLIANCE,TEAM_HORDE})
        {
            Fixture<BattlegroundTwinPeaks,CommandTP> f(team);
            TeamId enemy=team==TEAM_ALLIANCE?TEAM_HORDE:TEAM_ALLIANCE;
            f.bg.states[enemy]=BG_TP_FLAG_STATE_ON_PLAYER;f.bg.keepers[enemy]=f.players[1].GetGUID();
            for (unsigned i : {0u,1u})
                (team==TEAM_ALLIANCE ? f.players[i].allianceTrigger : f.players[i].hordeTrigger)=true;
            f.bg._CheckPositions(1000);
            Check(f.bg.captures.size()==1&&f.bg.captures[0]==f.players[1].id,
                "A non-carrier at the flag stand prevented the real carrier from scoring.");
            f.bg.captures.clear();f.bg.states[team]=BG_TP_FLAG_STATE_ON_PLAYER;
            f.bg._CheckPositions(1000);
            Check(f.bg.captures.empty(),"Capture allowed without the home flag on its base.");
        }
        Check(PlanTwin(true,false,true,true,false,0,10)==TwinTask::ReturnHome,"Carrier was sent to attack.");
        Check(PlanTwin(true,true,false,true,true,0,10)==TwinTask::ReturnFlag,"Carrier ignored its dropped home flag.");
        for(unsigned mask=0;mask<16;++mask)
            for(unsigned count=0;count<13;++count)
            {
                std::array<bool,4> free{};for(unsigned i=0;i<4;++i)free[i]=(mask&(1u<<i))!=0;
                std::vector<std::array<float,4>> costs(count);
                for(unsigned b=0;b<count;++b)for(unsigned o=0;o<4;++o)costs[b][o]=float((b+o)%7);
                auto result=AssignOrbs(costs,free);std::array<unsigned,4> uses{};
                for(int orb:result)if(orb>=0){Check(free[unsigned(orb)],"An occupied orb was assigned.");++uses[unsigned(orb)];}
                for(unsigned used:uses)Check(used<=1,"Duplicate orb assignment.");
            }
        std::cout<<checks<<" objective commander and assignment checks passed.\n";
    }
    catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
