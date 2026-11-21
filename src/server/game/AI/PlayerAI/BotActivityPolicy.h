#ifndef FELSORIA_BOT_ACTIVITY_POLICY_H
#define FELSORIA_BOT_ACTIVITY_POLICY_H
#include <cstdint>
#include <map>
#include <set>
namespace BotActivityPolicy
{
inline bool preparationWindow(bool waiting, std::int64_t delay) { return waiting && delay > 0 && delay <= 20000; }
inline bool bandageAllowed(bool alive, bool combat, bool casting, float hp) { return alive && !combat && !casting && hp <= 75.0f; }
inline bool joinTimedOut(std::uint32_t now, std::uint32_t started) { return std::uint32_t(now-started) >= 45000; }
inline bool grounded(float z, float ground) { return z >= ground-1.0f && z <= ground+2.0f; }
class Attempts
{
    std::map<std::uint64_t,std::uint32_t> tried;
    std::set<std::uint64_t> completed;
public:
    void Reset() { tried.clear(); completed.clear(); }
    bool Begin(std::uint64_t key, std::uint32_t now, std::uint32_t delay, bool once=false)
    {
        if (once && completed.count(key)) return false;
        auto it=tried.find(key);
        if (it!=tried.end() && std::uint32_t(now-it->second)<delay) return false;
        tried[key]=now; return true;
    }
    void Complete(std::uint64_t key) { completed.insert(key); }
};
}
#endif
