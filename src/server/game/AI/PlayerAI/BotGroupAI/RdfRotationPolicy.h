#ifndef RDF_ROTATION_POLICY_H
#define RDF_ROTATION_POLICY_H
#include <cstdint>
namespace RdfRotationPolicy {
inline bool healer(unsigned s) { return s==65||s==256||s==257||s==264||s==270||s==105; }
inline bool tank(unsigned s) { return s==73||s==66||s==250||s==268||s==104||s==581; }
inline bool melee(unsigned s) { return tank(s)||s==71||s==72||s==70||s==255||s==259||s==260||s==261||s==251||s==252||s==263||s==269||s==103||s==577; }
inline bool supported(unsigned s) {
 switch(s) {
 case 71:case 72:case 73:case 65:case 66:case 70:case 253:case 254:case 255:
 case 259:case 260:case 261:case 256:case 257:case 258:case 250:case 251:case 252:
 case 262:case 263:case 264:case 62:case 63:case 64:case 265:case 266:case 267:
 case 268:case 269:case 270:case 102:case 103:case 104:case 105:case 577:case 581:return true;
 default:return false;
 }
}
// A dying DPS outranks a moderately injured tank. Tank bias applies within a tier.
inline float triage(float hp, bool isTank) { return hp - (isTank && hp>30 ? 3.0f : 0.0f); }
inline bool groupEmergency(unsigned injured, unsigned critical) { return injured>=3 || critical>=2; }
inline bool mayDamage(bool isHealer, float lowestHP) { return !isHealer || lowestHP>=90.0f; }
inline bool elapsed(std::uint32_t now, std::uint32_t then, std::uint32_t delay) { return std::uint32_t(now-then)>=delay; }
struct FormationOffset { float forward; float side; };
inline FormationOffset formation(unsigned spec, unsigned slot) {
 if(tank(spec)) return {5.0f+1.5f*slot, slot%2 ? -1.5f : 0.0f};
 if(healer(spec)) return {-6.0f-1.5f*slot, slot%2 ? 2.0f : 0.0f};
 switch(slot%4) {
  case 0:return {2.8f, 2.8f+float(slot/4)*1.5f};
  case 1:return {2.8f,-2.8f-float(slot/4)*1.5f};
  case 2:return {-1.5f,4.0f+float(slot/4)*1.5f};
  default:return {-1.5f,-4.0f-float(slot/4)*1.5f};
 }
}
}
#endif
