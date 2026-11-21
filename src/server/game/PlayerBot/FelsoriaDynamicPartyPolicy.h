#ifndef FELSORIA_DYNAMIC_PARTY_POLICY_H
#define FELSORIA_DYNAMIC_PARTY_POLICY_H

#include <algorithm>
#include <cstdint>

namespace FelsoriaDynamicPartyPolicy
{
struct Targets
{
    std::uint32_t alliance;
    std::uint32_t horde;
};

// These counts describe ambient leases only. Protected party members are
// outside both counts, regardless of their faction, level or current map.
inline Targets TargetsFor(std::uint32_t total, bool allianceTerritory,
    bool hordeTerritory, int forcedTeam = -1)
{
    if (forcedTeam == 0) return {total, 0};
    if (forcedTeam == 1) return {0, total};
    std::uint32_t const minority = std::min(total / 2,
        std::max(std::uint32_t(2), total / 20));
    if (allianceTerritory) return {total - minority, minority};
    if (hordeTerritory) return {minority, total - minority};
    return {total - total / 2, total / 2};
}

// Allocate missing enemies first; a failed minority login must not turn into
// another majority lease. A point inside a native settlement may be restricted
// to one faction; wait for another legal point once that faction is full.
inline int PickTeam(Targets target, std::uint32_t alliance, std::uint32_t horde,
    bool allianceTerritory, bool hordeTerritory, int forcedTeam = -1)
{
    if (forcedTeam == 0) return alliance < target.alliance ? 0 : -1;
    if (forcedTeam == 1) return horde < target.horde ? 1 : -1;
    if (allianceTerritory && horde < target.horde) return 1;
    if (hordeTerritory && alliance < target.alliance) return 0;
    if (alliance < target.alliance && horde < target.horde) return alliance <= horde ? 0 : 1;
    if (alliance < target.alliance) return 0;
    if (horde < target.horde) return 1;
    return -1;
}

inline bool NeedsFollow(std::uint32_t botMap, std::uint32_t botInstance,
    float botX, float botY, float botZ, std::uint32_t humanMap,
    std::uint32_t humanInstance, float humanX, float humanY, float humanZ)
{
    if (botMap != humanMap || botInstance != humanInstance) return true;
    float const dx = humanX - botX, dy = humanY - botY, dz = humanZ - botZ;
    return dx * dx + dy * dy + dz * dz > 120.0f * 120.0f;
}
}

#endif
