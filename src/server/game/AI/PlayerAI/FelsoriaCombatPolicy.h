#ifndef FELSORIA_COMBAT_POLICY_H
#define FELSORIA_COMBAT_POLICY_H

// Shared decision state only. Native Spell::prepare remains authoritative.
// No game-object pointers are stored here; all clocks use wrap-safe elapsed time.
#include <cstdint>
#include <cstddef>
#include <map>

namespace FelsoriaCombatPolicy
{
inline bool Elapsed(std::uint32_t now, std::uint32_t then, std::uint32_t delay)
{
    return std::uint32_t(now - then) >= delay;
}

class RetryBook
{
    struct Attempt { std::uint32_t at; std::uint32_t delay; };
    std::map<std::uint32_t, Attempt> attempts;
public:
    void Clear() { attempts.clear(); }
    bool Ready(std::uint32_t spell, std::uint32_t now) const
    {
        auto it = attempts.find(spell);
        return it == attempts.end() || Elapsed(now, it->second.at, it->second.delay);
    }
    void Record(std::uint32_t spell, std::uint32_t now, std::uint32_t delay)
    {
        attempts[spell] = Attempt{now, delay};
    }
};

class CooldownGate
{
    bool used = false;
    std::uint32_t at = 0;
    float health = 100.0f;
public:
    void Clear() { used = false; at = 0; health = 100.0f; }
    bool Ready(std::uint32_t now, float currentHealth, bool urgent = false) const
    {
        // Ordinary cooldowns stay spaced; a new crisis may require a second one.
        return !used || Elapsed(now, at, 8000) || urgent || currentHealth <= 20.0f ||
            currentHealth <= health - 15.0f;
    }
    void Record(std::uint32_t now, float currentHealth)
    {
        used = true; at = now; health = currentHealth;
    }
};

// Callers serialize access. Entries identify individual casts, not spell names.
template<class Key, class Owner>
class CastClaims
{
    struct Claim { Owner owner; std::uint32_t at; std::uint32_t duration; };
    std::map<Key, Claim> claims;
public:
    bool Acquire(Key const& key, Owner const& owner, std::uint32_t now, std::uint32_t duration)
    {
        for (auto it = claims.begin(); it != claims.end();)
            if (Elapsed(now, it->second.at, it->second.duration)) it = claims.erase(it);
            else ++it;
        if (claims.find(key) != claims.end()) return false;
        // Bounded even under synthetic cast spam; expiry/failure normally removes entries.
        if (claims.size() >= 4096) return false;
        claims.emplace(key, Claim{owner, now, duration});
        return true;
    }
    void Release(Key const& key, Owner const& owner)
    {
        auto it = claims.find(key);
        if (it != claims.end() && it->second.owner == owner) claims.erase(it);
    }
    std::size_t Size() const { return claims.size(); }
};
}
#endif
