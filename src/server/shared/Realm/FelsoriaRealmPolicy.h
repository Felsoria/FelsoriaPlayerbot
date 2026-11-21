#ifndef FELSORIA_REALM_POLICY_H
#define FELSORIA_REALM_POLICY_H
#include <cstdint>
namespace FelsoriaRealmPolicy
{
// Stable AUTH catalog categories. Portuguese Aggra shares the English pool.
inline std::uint8_t LanguageGroup(std::uint32_t category)
{
    switch (category)
    {
        case 8: case 11: case 50: return 1;
        case 9: return 2;
        case 10: return 3;
        case 12: return 4;
        default: return 0;
    }
}
// Cyrillic/Latin boundary always requires the player to choose a new name.
inline bool TransferRequiresRename(std::uint32_t sourceCategory, std::uint32_t destinationCategory)
{
    return (sourceCategory == 12) != (destinationCategory == 12);
}
inline bool Decoration(std::uint32_t id) { return id == 907; }
inline bool NormalBG(bool battleground, bool arena, bool rated, bool wargame, bool brawl)
{
    return battleground && !arena && !rated && !wargame && !brawl;
}
// One draw per match, four equally sized intervals.
inline std::uint8_t EnemyGroup(std::uint32_t draw) { return std::uint8_t(1 + draw % 4); }
}
#endif
