#ifndef FELSORIA_GILNEAS_STRATEGY_POLICY_H
#define FELSORIA_GILNEAS_STRATEGY_POLICY_H
#include <algorithm>
#include <limits>
namespace FelsoriaGilneasStrategy {
struct Node {
    bool ours=false,captured=false,neutral=true;
    unsigned enemies=0,humans=0;
    float travel=0;
};
struct State {
    Node nodes[3];unsigned home=0,score=0,enemyScore=0,maxScore=1500;
    // Supplied from this core's native BgBFGTickIntervals/BgBFGTickPoints.
    float secondsPerPoint[4]={0,0.9f,0.3f,1.0f/30.0f};
};
struct Allocation {
    unsigned quota[3]={},primary=3,owned=0,enemyOwned=0;
    bool urgent=false;
};
inline float FinishTime(unsigned score,unsigned bases,State const&s){
    if(score>=s.maxScore)return 0;
    return bases?(s.maxScore-score)*s.secondsPerPoint[std::min(bases,3u)]:std::numeric_limits<float>::infinity();
}
inline Allocation Allocate(State const&s,unsigned bots,unsigned previous=3){
    Allocation result;
    for(auto const&node:s.nodes)if(node.captured){if(node.ours)++result.owned;else ++result.enemyOwned;}
    result.urgent=result.owned<2||(result.enemyOwned&&FinishTime(s.enemyScore,result.enemyOwned,s)<=FinishTime(s.score,result.owned,s)+12);
    if(!bots)return result;
    unsigned desired[3]={},reserved=0;
    for(unsigned n=0;n<3;++n)if(s.nodes[n].ours){
        auto const&node=s.nodes[n];unsigned need=node.enemies?(result.urgent?2u:node.enemies+1):(result.urgent?1u:2u);
        if(!node.captured&&!node.enemies)need=1;
        desired[n]=need>node.humans?need-node.humans:0;
        if(desired[n]&&reserved<bots){result.quota[n]=1;++reserved;}
    }
    bool more=true;
    while(reserved<bots&&more){more=false;
        for(unsigned n=0;n<3&&reserved<bots;++n)if(result.quota[n]<desired[n]){++result.quota[n];++reserved;more=true;}
    }
    bool opening=s.nodes[0].neutral&&s.nodes[1].neutral&&s.nodes[2].neutral;
    if(opening&&s.home<3&&reserved<bots){++result.quota[s.home];++reserved;}
    float best=-1e20f;
    for(unsigned n=0;n<3;++n)if(!s.nodes[n].ours&&!(opening&&n==s.home)){
        auto const&node=s.nodes[n];
        float value=(node.neutral?175.0f:node.captured?145.0f:180.0f)+(opening&&n==1?35.0f:0.0f)-node.travel*.12f-float(node.enemies)*14.0f;
        if(n==previous)value+=14;
        if(value>best){best=value;result.primary=n;}
    }
    unsigned left=bots-reserved;
    if(result.primary<3){
        unsigned secondary=3;float secondBest=-1e20f;
        for(unsigned n=0;n<3;++n)if(n!=result.primary&&!s.nodes[n].ours&&!(opening&&n==s.home)){
            float value=-s.nodes[n].travel*.12f-float(s.nodes[n].enemies)*14;
            if(value>secondBest){secondBest=value;secondary=n;}
        }
        unsigned flank=secondary<3?(left>=8?2u:left>=6?1u:0u):0;
        if(secondary<3)result.quota[secondary]+=flank;
        result.quota[result.primary]+=left-flank;
    }else {
        // All flags belong to us. There is no useful hostile base to attack.
        while(left){--left;unsigned n=0;for(unsigned k=1;k<3;++k)if(result.quota[k]<result.quota[n])n=k;++result.quota[n];}
    }
    return result;
}
} // namespace FelsoriaGilneasStrategy
#endif
