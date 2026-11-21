#ifndef FELSORIA_PVP_ROTATION_POLICY_H
#define FELSORIA_PVP_ROTATION_POLICY_H
#include <cstdint>
namespace PvpRotationPolicy {
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
inline bool elapsed(std::uint32_t now, std::uint32_t then, std::uint32_t delay) { return std::uint32_t(now-then)>=delay; }
inline bool groupEmergency(unsigned injured, unsigned critical) { return injured>=3 || critical>=2; }
inline bool mayDamage(bool isHealer, float lowestHP) { return !isHealer || lowestHP>=90.0f; }
inline bool potionAllowed(bool arena, bool combat, bool alive, bool casting, bool mounted,
 bool used, bool attempted, std::uint32_t now, std::uint32_t attempt) {
 return !arena && combat && alive && !casting && !mounted && !used &&
  (!attempted || elapsed(now,attempt,15000));
}
inline bool burstWindow(bool started, std::uint32_t now, std::uint32_t start) {
 return started && std::uint32_t(now-start)<10000;
}
}
#endif
