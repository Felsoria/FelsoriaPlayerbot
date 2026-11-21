#ifndef FELSORIA_AV_POLICY_H
#define FELSORIA_AV_POLICY_H
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <functional>
#include <utility>
#include <queue>
#include <vector>
namespace FelsoriaAV {
struct Point {float x,y,z;};
struct Edge {unsigned to;float cost;};
inline float Gap(Point a,Point b){float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return std::sqrt(x*x+y*y+z*z);}
inline bool Capturable(unsigned owner,unsigned state,unsigned team){return state!=2&&owner!=team;}
// Final assault depends on four native enemy-tower destructions and captain
// death, never on a graveyard owner or its capture timer.
inline bool GeneralReady(bool,unsigned towersDestroyed,bool captainAlive){return towersDestroyed==4&&!captainAlive;}
// Keep a stable minority on defence, including at small BG population sizes.
enum TacticalRole { Push, Raid, Guard, Reserve };
inline unsigned GuardBudget(unsigned army){return army<8?0:std::max(1u,(army+5)/10);}
inline unsigned ReserveBudget(unsigned army){return army<10?0:std::max(1u,(army+5)/10);}
inline unsigned DefenderBudget(unsigned army){return GuardBudget(army)+ReserveBudget(army);}
inline unsigned RaiderBudget(unsigned army){return army<8?0:std::max(2u,(army*3+5)/10);}

// Canonical lane order is mirrored by faction, independent of native node IDs.
inline unsigned EnemyTowerOrder(unsigned team,unsigned ordinal){
 static unsigned const towers[2][4]={{11,12,13,14},{10,9,7,8}};
 return team<2&&ordinal<4?towers[team][ordinal]:21;
}
inline unsigned DefenceOrder(unsigned team,unsigned node){
 if(node<7)return team==0?6-node:node;
 for(unsigned i=0;i<4;++i)if(EnemyTowerOrder(1-team,i)==node)return i;
 return 21;
}
inline unsigned MainHealerBudget(unsigned healers,unsigned wingSlots,bool reserve){
 if(!healers)return 0;unsigned wings=std::min(wingSlots,healers-1);
 unsigned held=reserve&&healers>wings+2?1u:0u;
 return std::min((healers*3+4)/5,healers-wings-held);
}
inline bool CanRescue(float yards,float speed,unsigned remaining){
 return remaining>0&&yards/std::max(3.0f,speed)*1000.0f+11000.0f<float(remaining);
}
inline unsigned GarrisonSize(unsigned observedEnemies){return std::min(4u,std::max(2u,observedEnemies+1));}
inline bool PursuitExpired(unsigned elapsed,float displacement){return elapsed>=8000||displacement>35.0f;}
inline bool MinesEnabled(){return false;}
inline bool EnemyTower(unsigned objective,unsigned team){return team==0?(objective>=11&&objective<=14):(objective>=7&&objective<=10);}
inline bool BaseTower(unsigned objective,unsigned team){return team==0?(objective==13||objective==14):(objective==7||objective==8);}
inline unsigned OtherBaseTower(unsigned objective,unsigned team){return team==0?(objective==13?14:13):(objective==7?8:7);}
inline bool DefenceNode(unsigned owner,unsigned totalOwner,unsigned state,unsigned team){
 return state!=2&&(owner==team||(state==1&&totalOwner==team));
}
inline bool AttackNode(unsigned owner,unsigned state,unsigned team){return state!=2&&owner!=team;}
inline float NodeScore(bool own,bool assaulted,bool rescue,unsigned assigned,unsigned army,float travel,bool previous){
 float value=own?(assaulted?175.0f:25.0f):(rescue?330.0f:210.0f);
 unsigned capacity=own&&!assaulted?1u:std::max(2u,std::min(7u,army/5));
 value-=assigned*(own&&!assaulted?80.0f:24.0f);
 if(assigned>=capacity)value-=180.0f;
 value-=travel/9.0f;if(previous)value+=35.0f;return value;
}
inline bool ChannelValid(bool available,bool sameFlag,bool threatened,bool damaged,float moved,unsigned tickGap){
 return available&&sameFlag&&!threatened&&!damaged&&moved<=0.8f&&tickGap<=10000;
}
// Shared implementation is compiled by the isolated tests and the server.
template<class Blocked>
bool FindRoute(unsigned start,unsigned goal,unsigned count,unsigned const* offsets,Edge const* edges,
               Blocked blocked,std::vector<unsigned>& result){
 result.clear();if(start>=count||goal>=count)return false;
 std::vector<float>cost(count,std::numeric_limits<float>::max());
 std::vector<unsigned>parent(count,count);
 using Item=std::pair<float,unsigned>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>>queue;
 cost[start]=0;queue.push({0,start});
 while(!queue.empty()){
  auto item=queue.top();queue.pop();unsigned v=item.second;
  if(item.first!=cost[v])continue;if(v==goal)break;
  for(unsigned e=offsets[v];e<offsets[v+1];++e){auto const&link=edges[e];
   if(link.to>=count||blocked(v,link.to))continue;
   float next=item.first+link.cost;
   if(next<cost[link.to]){cost[link.to]=next;parent[link.to]=v;queue.push({next,link.to});}
  }
 }
 if(cost[goal]==std::numeric_limits<float>::max())return false;
 for(unsigned v=goal;;v=parent[v]){result.push_back(v);if(v==start)break;if(parent[v]>=count||result.size()>count){result.clear();return false;}}
 std::reverse(result.begin(),result.end());return true;
}
}
#endif
