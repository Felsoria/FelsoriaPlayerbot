#ifndef FELSORIA_MECHANICS_GEOMETRY_H
#define FELSORIA_MECHANICS_GEOMETRY_H
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdint>
namespace FelsoriaMechanics {
struct Vec {float x=0,y=0,z=0;Vec()=default;Vec(float X,float Y,float Z=0):x(X),y(Y),z(Z){} };
inline Vec lerp(Vec a,Vec b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
inline float length(Vec v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
inline float distance(Vec a,Vec b){return length({a.x-b.x,a.y-b.y,a.z-b.z});}
inline float angleDelta(float a,float b){return std::atan2(std::sin(b-a),std::cos(b-a));}
inline float segmentDistance2(Vec p,Vec a,Vec b){
 float x=b.x-a.x,y=b.y-a.y,l=x*x+y*y;
 float t=l>0.000001f?std::max(0.0f,std::min(1.0f,((p.x-a.x)*x+(p.y-a.y)*y)/l)):0;
 float dx=p.x-a.x-t*x,dy=p.y-a.y-t*y;return dx*dx+dy*dy;
}
inline float cross(Vec a,Vec b,Vec c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
inline bool segmentsCross(Vec a,Vec b,Vec c,Vec d){
 float p=cross(a,b,c),q=cross(a,b,d),r=cross(c,d,a),s=cross(c,d,b);
 if(((p>0 && q<0)||(p<0 && q>0))&&((r>0 && s<0)||(r<0 && s>0)))return true;
 return segmentDistance2(a,c,d)<0.000001f||segmentDistance2(b,c,d)<0.000001f||
        segmentDistance2(c,a,b)<0.000001f||segmentDistance2(d,a,b)<0.000001f;
}
// FELSORIA_DODGE_NAVIGATION_V7: check the whole walkable route, not just its end.
// Floor and sight callbacks use the bot's current map and phases.
template<class Floor,class Sight>
inline bool walkableEscape(std::vector<Vec> const& route,Floor floor,Sight sight,
                           float maxLength=45.0f){
 if(route.size()<2)return false;
 float total=0;
 for(auto const& p:route)
  if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;
 for(size_t i=1;i<route.size();++i){
  Vec a=route[i-1],b=route[i];float len=distance(a,b);total+=len;
  if(total>maxLength || !sight(a,b))return false;
  unsigned steps=static_cast<unsigned>(std::ceil(len/0.75f));
  steps=std::max(1u,steps);
  for(unsigned n=0;n<=steps;++n){
   Vec p=lerp(a,b,float(n)/float(steps));float z=floor(p);
   if(!std::isfinite(z)||std::fabs(z-p.z)>1.25f)return false;
  }
 }
 // Feet/body clearance at the landing: reject narrow ledges and wall corners.
 Vec end=route.back();
 for(unsigned i=0;i<8;++i){
  float angle=float(i)*0.78539816339f;
  Vec side{end.x+0.65f*std::cos(angle),end.y+0.65f*std::sin(angle),end.z};
  float z=floor(side);
  if(!std::isfinite(z)||std::fabs(z-end.z)>1.0f||!sight(end,side))return false;
 }
 // Every segment is grounded and visible in both directions: the same route
 // remains a return route when the temporary hazard has ended.
 return true;
}
struct Shape {
 float radius=0,height=6;std::vector<Vec> polygon;
 bool contains(Vec p,float margin=0)const{
  if(std::fabs(p.z)>height+margin)return false;
  if(polygon.empty())return radius+margin>=0 && p.x*p.x+p.y*p.y<=(radius+margin)*(radius+margin);
  bool inside=false;
  for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){
   Vec a=polygon[j],b=polygon[i];
   if(segmentDistance2(p,a,b)<=margin*margin)return margin>=0;
   if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside;
  }
  return inside;
 }
 bool intersects(Vec a,Vec b,float margin=0)const{
  if(std::min(a.z,b.z)>height+margin || std::max(a.z,b.z)<-height-margin)return false;
  if(contains(a,margin)||contains(b,margin))return true;
  if(polygon.empty())return segmentDistance2({},a,b)<=(radius+margin)*(radius+margin);
  for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){
   Vec c=polygon[j],d=polygon[i];
   if(segmentsCross(a,b,c,d) || segmentDistance2(a,c,d)<=margin*margin ||
      segmentDistance2(b,c,d)<=margin*margin || segmentDistance2(c,a,b)<=margin*margin ||
      segmentDistance2(d,a,b)<=margin*margin)return true;
  }
  return false;
 }
};
// FELSORIA_DODGE_LEASH_V10: independent, wrap-safe separation timing.
enum class RegroupReason { None, Distant, Stalled };
struct RegroupClock {
 bool initialized=false,separated=false;
 std::uint32_t progressTime=0,separatedSince=0;
 Vec progress;
 void reset(){initialized=false;separated=false;}
 RegroupReason observe(Vec position,float separation,bool stalled,std::uint32_t now){
  if(!initialized || distance(position,progress)>0.75f){
   initialized=true;progress=position;progressTime=now;
  }
  if(separation<=25.0f || !stalled){progress=position;progressTime=now;}
  if(separation>60.0f){
   if(!separated){separated=true;separatedSince=now;}
   if(std::uint32_t(now-separatedSince)>=3000)return RegroupReason::Distant;
  }else separated=false;
  if(separation>25.0f && stalled && std::uint32_t(now-progressTime)>=8000)
   return RegroupReason::Stalled;
  return RegroupReason::None;
 }
};
// FELSORIA_VAULT_BRIDGES_V11: timers are independent of movement-generator retries.
struct BridgeClock {
 bool active=false,reverse=false,pending=false,attempted=false;
 std::uint32_t started=0,progressTime=0,lastAttempt=0;
 float best=0;
 void reset(){active=false;pending=false;attempted=false;}
 void begin(bool returning,float progress,std::uint32_t now){
  active=true;reverse=returning;pending=false;attempted=false;
  started=progressTime=now;best=reverse?-progress:progress;
 }
 void observe(float progress,std::uint32_t now){
  float p=reverse?-progress:progress;
  if(p>best+0.03f){best=p;progressTime=now;}
 }
 bool due(std::uint32_t now)const{return active &&
  (std::uint32_t(now-progressTime)>=4000 || std::uint32_t(now-started)>=12000);}
 bool canAttempt(std::uint32_t now)const{return !pending && (!attempted || std::uint32_t(now-lastAttempt)>=3000);}
 void attempt(std::uint32_t now){attempted=true;lastAttempt=now;}
};
inline bool bridgeHeightMissing(float height){
 return !std::isfinite(height) || height<=-50000.0f || std::fabs(height)<0.01f;
}
inline float avoidanceMargin(Shape const& shape){
 float extent=shape.radius;
 for(auto const& p:shape.polygon)extent=std::max(extent,std::sqrt(p.x*p.x+p.y*p.y));
 return std::max(2.5f,std::min(4.0f,extent*0.15f)); // V12: clearance without room-wide inflation
}
// V12: position requests stay local and cannot move an already distant bot farther away.
inline bool withinPartyLeash(Vec here,Vec end,Vec leader,float limit){
 float current=distance(here,leader),next=distance(end,leader);
 return next<=limit || (current>limit && next<current-0.5f);
}
inline float combatRadius(float current,float minimum,float maximum,bool closeCombat){
 if(closeCombat)return std::min(3.0f,maximum);
 float low=std::min(maximum,std::max(1.0f,minimum+0.5f));
 return std::max(low,std::min(std::max(low,maximum-1.0f),current));
}
struct Pose {Vec pos;float yaw=0;};
inline Vec local(Vec p,Pose q){float x=p.x-q.pos.x,y=p.y-q.pos.y;float c=std::cos(q.yaw),s=std::sin(q.yaw);return {c*x+s*y,-s*x+c*y,p.z-q.pos.z};}
inline bool swept(Shape const& shape,Pose a,Pose b,Vec p,Vec q,float margin){
 Vec u=local(p,a),v=local(q,b);
 // Bound the curved relative path when a non-circular shape rotates in this slice.
 if(!shape.polygon.empty())margin+=std::max(length(u),length(v))*std::fabs(angleDelta(a.yaw,b.yaw));
 return shape.intersects(u,v,margin);
}
struct Hazard {
 Shape shape;std::vector<Pose> frames;float active=0,expires=100000;bool moving=false;
 Pose pose(float t)const{
  if(frames.empty())return {};
  float idx=std::max(0.0f,t)*10.0f;size_t i=static_cast<size_t>(idx);
  if(i+1>=frames.size())return frames.back();
  float k=idx-float(i);return {lerp(frames[i].pos,frames[i+1].pos,k),frames[i].yaw+angleDelta(frames[i].yaw,frames[i+1].yaw)*k};
 }
 // Inflate horizontal danger clearance, not adjacent floors or safe shelters.
 bool contains(Vec p,float t,float margin=-1.0f)const{
  Vec q=local(p,pose(t));
  return std::fabs(q.z)<=shape.height+1.0f &&
         shape.contains(q,margin<0?avoidanceMargin(shape):margin);
 }
 bool hit(Vec p,float t,float margin=-1.0f)const{return t>=active && t<=expires && contains(p,t,margin);}
 bool crossing(Vec a,Vec b,float t0,float t1,float margin=-1.0f)const{
  if(t1<active || t0>expires)return false;
  float begin=std::max(t0,active),end=std::min(t1,expires);
  float span=t1-t0;
  Vec p=span>0?lerp(a,b,(begin-t0)/span):a,q=span>0?lerp(a,b,(end-t0)/span):b;
  Vec u=local(p,pose(begin)),v=local(q,pose(end));
  if(std::min(u.z,v.z)>shape.height+1.0f || std::max(u.z,v.z)<-shape.height-1.0f)return false;
  return swept(shape,pose(begin),pose(end),p,q,margin<0?avoidanceMargin(shape):margin);
 }
};
inline Vec positionOnPath(std::vector<Vec> const& path,float travel){
 if(path.empty())return {};
 for(size_t i=1;i<path.size();++i){float l=distance(path[i-1],path[i]);if(travel<=l)return lerp(path[i-1],path[i],l>0?travel/l:0);travel-=l;}
 return path.back();
}
// Moving threats are guaranteed only within the rolling 1.5 second snapshot horizon.
inline bool pathSafe(std::vector<Vec> const& path,float speed,std::vector<Hazard> const& hazards,bool allowInitialStay=false){
 if(path.size()<2||speed<=0)return false;
 float total=0;for(size_t i=1;i<path.size();++i)total+=distance(path[i-1],path[i]);
 float arrival=total/speed;
 for(auto const& h:hazards){
  // FELSORIA_AOE_ESCAPE_V8: an upcoming marker already surrounds the bot.
  // Even if damage starts before it can leave, running out is better than
  // rejecting every escape and standing still. Never allow re-entry/new hazards.
  bool initial=h.contains(path.front(),0),exited=!initial;
  float until=h.moving?std::min(arrival+0.5f,1.5f):arrival+0.5f;
  // Include activation/deactivation boundaries, not just fixed 100ms samples.
  for(float t=0;t<until;){
   float next=std::min(until,t+0.1f);
   if(h.active>t && h.active<next)next=h.active;
   if(h.expires>t && h.expires<next)next=h.expires;
   Vec a=positionOnPath(path,t*speed),b=positionOnPath(path,next*speed);
   if(h.crossing(a,b,t,next) && exited)return false;
   if(!h.contains(b,next))exited=true;
   t=next;
  }
  if(!exited && h.expires>=until && !allowInitialStay)return false;
  if(!h.moving && h.hit(path.back(),std::max(arrival,h.active)) &&
     !(allowInitialStay && initial && !exited))return false;
 }
 return true;
}
}
#endif
