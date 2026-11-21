#ifndef FELSORIA_ARATHI_POLICY_H
#define FELSORIA_ARATHI_POLICY_H
#include <algorithm>
namespace FelsoriaArathiPolicy {
inline float Score(bool friendly,bool contested,bool neutral,unsigned owned,
    unsigned threats,unsigned assigned,unsigned teamSize,float travel,
    bool previous,bool recent,bool healer) {
    float value=neutral?150.0f:friendly?(contested?155.0f:65.0f):(owned<3?125.0f:50.0f);
    unsigned desired=friendly&&!contested?std::max(1u,teamSize/12u):std::max(2u,teamSize/4u);
    if(threats){value+=90.0f+25.0f*std::min(6u,threats);desired=std::max(desired,std::min(6u,threats+1));}
    if(owned>=3&&friendly)value+=25.0f;
    value-=42.0f*float(assigned)/float(desired);
    if(assigned>=desired)value-=65.0f*float(assigned-desired+1);
    value-=travel*0.12f;
    if(previous)value+=recent?60.0f:16.0f;
    if(healer&&!friendly&&assigned==0)value-=40.0f;
    return value;
}
}
#endif
